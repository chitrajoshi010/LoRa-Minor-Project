# Flashing Guide — forest_monitor

Status as of this writing: **all three roles compile and produce a flashable
`.bin`** via real `idf.py build` runs (not just static review). This file
documents what was fixed to get there, how to build/flash each role, and what
is still *not* verified (compiles ≠ tested on real hardware).

## TL;DR

```sh
. "$HOME/esp/esp-idf/export.sh"
cd forest_monitor

# Gateway — ESP32-WROOM-32, role 0
idf.py set-target esp32 && idf.py -p /dev/ttyUSB0 flash monitor

# Relay — ESP32-S3, role 1
idf.py fullclean && idf.py set-target esp32s3 && idf.py -D CONFIG_LDSE_ROLE=1 -p /dev/ttyUSB0 flash monitor

# Node — ESP32-S3, role 2 (default for esp32s3 target)
idf.py fullclean && idf.py set-target esp32s3 && idf.py -p /dev/ttyUSB0 flash monitor
```

`idf.py fullclean` is required every time you switch `--target` (esp32 <->
esp32s3), because `sdkconfig` is regenerated from `sdkconfig.defaults.*` and a
stale `build/` directory from the other target will not reconfigure cleanly.

## What was broken, and what was fixed

The firmware did **not** build before this pass. Six real, confirmed defects
were found and fixed (verified by actually running `idf.py build`, not just
reading the code):

1. **`chmorgan/esp-dht` doesn't exist.** `main/idf_component.yml` declared a
   dependency on a GitHub repo that returns 404 and isn't in the ESP
   Component Registry either. Because component-manager dependency
   resolution runs for the *entire* manifest before `CONFIG_LDSE_ROLE`
   branching even starts, this one bad line broke **every** role's build,
   including the gateway (which doesn't even use DHT22).
   __Fix:__ removed the dependency; `main/sensors/dht22.cpp` was rewritten
   from scratch as a self-contained, bit-banged AM2302/DHT22 driver using
   only stock ESP-IDF APIs (`driver/gpio.h`, `esp_rom_delay_us`, FreeRTOS
   critical sections). Public API (`dht22_init`/`dht22_read`) is unchanged,
   so no caller code needed changes.

2. **Wrong managed-component name for RadioLib.** `main/CMakeLists.txt`
   listed `RadioLib` in `REQUIRES`, but the actual component-manager
   directory/target name is `jgromes__radiolib`. Fixed.

3. **`EspHal` doesn't actually exist in RadioLib.** Both `ARCHITECTURE.md`
   and the old `AGENTS.md`/`CLAUDE.md` docs claim "RadioLib's built-in
   ESP-IDF EspHal (SPI2_HOST)" — this is **factually wrong**. `EspHal` only
   exists as *example* glue code under
   `managed_components/jgromes__radiolib/examples/NonArduino/ESP-IDF/`, and
   that example is **ESP32-only** (raw DPORT/SPI register pokes, hard
   `#error` on any other target) — copying it verbatim would have broken
   the ESP32-S3 relay/node builds entirely.
   __Fix:__ wrote a new, portable `main/ldse/EspHal.{h,cpp}` implementing
   RadioLib's `RadioLibHal` interface using the standard ESP-IDF
   `driver/spi_master.h` (SPI2_HOST) and `driver/gpio.h` APIs, which work
   identically on ESP32 and ESP32-S3. NSS/CS is left to RadioLib itself
   (`spics_io_num = -1`), matching how `Module.cpp` toggles chip-select via
   `digitalWrite`. GPIO interrupts are implemented properly but are not
   exercised by this codebase (`LdseRadio::Receive` polls the IRQ-flags
   register directly instead of using DIO0 interrupts).

4. **Role-conditional `REQUIRES` silently dropped `esp_wifi`.** ESP-IDF
   resolves component `REQUIRES` during an "early expansion" CMake pass that
   runs *before* Kconfig options are available — and `CONFIG_LDSE_ROLE` is
   defined by this same component's own `Kconfig.projbuild`, so during that
   early pass it reads as empty and any `if(CONFIG_LDSE_ROLE EQUAL 0)` guard
   around `REQUIRES esp_wifi ...` silently fell through to the node/relay
   branch — permanently excluding `esp_wifi` from the gateway's dependency
   graph even though `SRCS` correctly included `net/wifi_manager.cpp`.
   __Fix:__ `main/CMakeLists.txt` now lists the **union** of every role's
   `REQUIRES` unconditionally (only `SRCS`/`INCLUDE_DIRS` stay
   role-conditional) — the standard ESP-IDF pattern for this exact gotcha.

5. **Tensor arena overflowed DRAM.** `main/acoustic/classifier.cpp` statically
   allocated a 270 KB tensor arena (comment: "270 KB verified"), but this
   overflowed the ESP32-S3's internal DRAM segment by ~3.2 KB once linked
   against the rest of the LDSE stack. The project's own docs
   (`AGENTS.md`/`CLAUDE.md`) say 260 KB. Reduced `TENSOR_ARENA_SIZE` to
   260 KB to match, which links cleanly with headroom.

6. **Format-string / `-Werror=format=` build errors on ESP32-S3.** `uint32_t`
   is a different underlying type on the esp32s3 toolchain than on esp32
   (`long unsigned int` vs `unsigned int`), so `printf("...%u...", someU32)`
   calls that were fine for the gateway (esp32) failed to compile for
   relay/node (esp32s3). Fixed the three offending calls in `node.cpp` /
   `relay.cpp` to use `%lu` with an explicit `(unsigned long)`/`(long)` cast.

None of these required touching the LDSE protocol logic, packet format, pin
maps, or the acoustic-alert-threshold logic added earlier — those were
untouched and are unaffected.

## Build verification performed

Ran clean, from-scratch builds (`idf.py fullclean` + `set-target` +
`build`) for all three roles and confirmed a `.bin` was produced with no
errors:

| Role    | Target   | `CONFIG_LDSE_ROLE` | Result | Flash headroom |
|---------|----------|--------------------|--------|-----------------|
| Gateway | esp32    | 0 (default)        | ✅ builds | 23% free of 1 MB app partition |
| Relay   | esp32s3  | 1                  | ✅ builds | 46% free |
| Node    | esp32s3  | 2 (default)        | ✅ builds | 46% free |

## What is still NOT verified

Compiling is necessary but not sufficient. The following have **not** been
tested on real hardware and should be treated as open risk until the bench
test (gateway ↔ relay ↔ node) is actually run:

- **Rewritten `dht22.cpp`** — logic-reviewed against the AM2302/DHT22 timing
  spec, but never verified against a real sensor. Watch the first boot's
  serial log for checksum/timeout errors.
- **New `EspHal`** — implements the RadioLib HAL contract correctly by
  inspection, but SPI timing/radio behavior (SX1278 register reads/writes,
  actual LoRa TX/RX) has not been verified on hardware.
- **Gateway Wi-Fi manager** (`net/wifi_manager.cpp`) — compiles and links;
  the actual STA connect/backoff/reconnect behavior needs a real Wi-Fi AP to
  verify. Set `CONFIG_GATEWAY_WIFI_SSID`/`CONFIG_GATEWAY_WIFI_PASSWORD` via
  `idf.py menuconfig` (or `sdkconfig.defaults.esp32`) before flashing the
  gateway, otherwise it will try to connect to an empty SSID. **Note:** this
  only brings up connectivity — no Firebase/HTTP upload path exists yet; see
  the "Gateway Wi-Fi" section above for what's missing.
- **Fire-score calibration constants** (`CAL_*` in `sensors/fire_scoring.cpp`)
  are still placeholders — recalibrate per site before trusting fire alerts.
- **Acoustic alert threshold logic** (`classifier_is_threat`,
  `ACOUSTIC_ALERT_THRESHOLD = 0.70f`) compiles as part of relay/node, but
  the actual on-device TFLite inference accuracy against real audio hasn't
  been re-verified after the arena-size change (260 KB is still large enough
  for the model + working buffers per `interpreter.arena_used_bytes()`
  logging in `classifier.cpp` — check that log line on first boot to confirm
  headroom).

## Gateway Wi-Fi — what's actually implemented

The gateway (`CONFIG_LDSE_ROLE=0`) brings up a Wi-Fi **station connectivity
layer** (`main/net/wifi_manager.{h,cpp}`), but as of this build it is
**connectivity only — no data upload path exists yet**. Read this section
before assuming the gateway pushes data to Firebase/the dashboard over Wi-Fi;
it currently does not.

### What is done

- `wifi_manager_init()` is called once, as the very first line of
  `ldse_gateway_main()` in `main/roles/gateway.cpp`, **before** the LDSE
  sync/radio loop starts.
- It is fully **non-blocking**: it kicks off `esp_wifi_connect()` from the
  `WIFI_EVENT_STA_START` handler and returns immediately — the gateway's
  10 s LDSE epoch (SYNC beacon every 250 ms) is never delayed waiting on an
  AP handshake.
- **Event-driven reconnect with capped exponential backoff**: on
  `WIFI_EVENT_STA_DISCONNECTED`, a retry is scheduled via a one-shot
  `esp_timer`, starting at 1 s and doubling up to a 10 s cap on each
  subsequent failure (reset back to 1 s the moment `IP_EVENT_STA_GOT_IP`
  fires). This avoids a tight reconnect loop hammering a hotspot that's
  briefly out of range.
- `esp_wifi_set_ps(WIFI_PS_NONE)` is set immediately after `esp_wifi_start()`
  — Wi-Fi modem-sleep power-save is disabled specifically because it's the
  main source of timing jitter risk against the LDSE SYNC beacon cadence
  (this was an explicit requirement from the project's Wi-Fi skill).
- Credentials are **not hardcoded** — they come from Kconfig
  (`CONFIG_GATEWAY_WIFI_SSID` / `CONFIG_GATEWAY_WIFI_PASSWORD`, added to
  `main/Kconfig.projbuild` under a menu scoped `depends on LDSE_ROLE = 0`),
  settable via `idf.py menuconfig` → "Gateway Wi-Fi" or directly in
  `sdkconfig.defaults.esp32`.
- `wifi_manager_is_connected()` is a public getter (backed by a FreeRTOS
  event group bit set on `IP_EVENT_STA_GOT_IP` / cleared on disconnect) meant
  for any future network code to gate on before attempting I/O.
- Compiles and links cleanly as part of the gateway build (see verification
  table above); the STA connect/backoff/reconnect state machine has **not**
  been tested against a real access point yet.

### What is NOT done (do not assume this exists)

- **No Firebase/HTTP/MQTT upload code has been written.** `wifi_manager_init()`
  brings the radio up and gets an IP, but nothing in `gateway.cpp` currently
  calls `wifi_manager_is_connected()` or sends any request anywhere. The
  gateway's only "output" today is `LogPacket()`'s `printf(...)` CSV line
  over the serial console (`DATA|FIRE,epoch_ms,origin,src,hops,seq,rssi,layer,
  class,temp,hum,gas,fire,count`) — the same format documented in
  `ARCHITECTURE.md`/`CLAUDE.md`, but it goes to serial, not to Wi-Fi.
- To actually get gateway data onto the Firebase-backed dashboard
  (`Lora-based-forest-monitor-dashboard/`), you still need to add, inside
  `LogPacket()` (or a new function called from the same place):
  1. A check on `wifi_manager_is_connected()` before attempting any network
     call (skip/buffer if not connected — don't block the LDSE loop).
  2. An HTTP client (`esp_http_client`, already available as an ESP-IDF
     component — not yet added to `main/CMakeLists.txt`'s `REQUIRES`) that
     PUTs/POSTs the packet's fields into the `/nodes/{nodeId}/latest` and
     `/nodes/{nodeId}/history` paths of the Firebase Realtime Database
     schema described in the dashboard repo's `CLAUDE.md`.
  3. Firebase REST auth (email/password sign-in, matching the existing
     `serial_to_firebase.py`/`ESP-IDF-main` bridge pattern in the dashboard
     repo) or an ID-token refresh flow — none of this exists in
     `forest_monitor/` today.
- This is a real gap, not a stylistic choice — until it's implemented, the
  mesh firmware's data never reaches the dashboard; only serial CSV output
  (or `scripts/seed_random_data.py`'s synthetic seeding) populates Firebase
  today.

## Flashing checklist

1. `. "$HOME/esp/esp-idf/export.sh"` in every new shell.
2. Set radio pins / Wi-Fi credentials via `idf.py menuconfig` if your wiring
   differs from `sdkconfig.defaults.esp32`/`sdkconfig.defaults.esp32s3`
   (see `main/Kconfig.projbuild`).
3. `idf.py fullclean` before switching `--target`.
4. `idf.py set-target <esp32|esp32s3>`.
5. For relay specifically, remember `-D CONFIG_LDSE_ROLE=1` (node is the
   esp32s3 default, i.e. role 2, if you don't pass this flag).
6. `idf.py -p <PORT> flash monitor` and watch the serial log:
   - Gateway: Wi-Fi connect messages, then CSV log lines
     (`DATA|FIRE,epoch_ms,...`).
   - Relay/Node: `interpreter.arena_used_bytes()` log, DHT22 read results,
     LDSE sync offset logs.
7. Bench-test all three boards together (gateway + relay + node powered on
   simultaneously) to confirm the LDSE mesh actually forms and forwards
   packets — this has not been done as part of this fix pass.
