# AGENTS.md — Forest Monitor (unified ESP-IDF firmware)

Guidance for AI coding agents (Claude Code, Copilot, etc.) working in
`LoRa-Minor-Project/forest_monitor/`. This is the merged firmware combining the LDSE LoRa
protocol and the acoustic classifier into one ESP-IDF project with a build-time
role (`CONFIG_LDSE_ROLE`: 0=gateway, 1=relay, 2=node).

## Build / test commands

```sh
# from esp/forest_monitor/
idf.py set-target esp32   && idf.py build                       # gateway (role 0)
idf.py set-target esp32s3 && idf.py build                       # node (role 2, S3 default)
idf.py set-target esp32s3 && idf.py -D CONFIG_LDSE_ROLE=1 build # relay (role 1, sensors + acoustic)
idf.py fullclean   # required when switching target
```

There is no host unit-test suite; "verified" means all three configs compile
and the three-board bench boots (gateway -> relay -> node).

## Architecture rules

- __One image, role chosen at build time.__ `main/main.cpp` dispatches to
   `ldse_{gateway,relay,node}_main()` via `#if CONFIG_LDSE_ROLE`. Only the
   selected role's `.cpp` and its modules are compiled (see `main/CMakeLists.txt`).
- **Role -> module matrix (do not break this):**
   - gateway (0): LDSE only. No sensors, no classifier.
   - relay (1): LDSE + `sensors/` (fire scoring) + `acoustic/` (classifier task).
   - node (2): LDSE + `sensors/` + `acoustic/` (classifier task).

- **`payload.h` must stay dependency-free** - the gateway decodes it without
   compiling sensors/classifier. Keep `sizeof(NodePayload) <= 48`.

## LDSE protocol (main/ldse/)

- Ported from Arduino; `LdseCompat.h` supplies millis/micros/delay on
   esp_timer/FreeRTOS. Do NOT reintroduce a global min/max/constrain macro - it
   breaks RadioLib's std::min. Use `ldse_min_u8` / `ldse_clampf`.
- Keep the packet format (`LdsePacket.h`), message types, epoch timing, FTSP
   sync, IRE routing and congestion logic **byte-identical** to the original.
- Radio uses this repo's ESP-IDF `EspHal` implementation
   (`main/ldse/EspHal.{h,cpp}`) on top of SPI2_HOST:
   `new EspHal(SCK, MISO, MOSI)` then `new Module(hal, NSS, DIO0, RST, DIO1)`.
   The Module must be heap-allocated (RadioLib keeps the pointer).

## Pins (Kconfig)

- Radio pins come from `CONFIG_LDSE_PIN_*` (see `main/Kconfig.projbuild`),
   defaulted per target in `sdkconfig.defaults.esp32{,s3}`.
- **Never** set S3 MOSI=17 or DIO1=16 - they collide with the mic
   (BCK=17, DIN=16). S3 uses MOSI=8, DIO1=9. Gateway (WROOM-32) uses 23/19/26.
- Relay/node only: `CONFIG_LDSE_PIN_SLEEP_GATE` (default GPIO10) drives a
   peripheral-power MOSFET gate/base via `LdseSleepGate` - HIGH during
   SYNC/DATA, LOW during SLEEP, on the parent-synced epoch schedule.

## Sensors (main/sensors/)

- MQ-135 on GPIO2 via esp_adc oneshot (ADC1_CH1); DHT22 on GPIO12 via the
   project-authored bit-banged driver in `main/sensors/dht22.cpp`.
- Fire score follows report Eq. 3.4 (weights 0.51/0.37/0.12); CAL_* baselines
   in `fire_scoring.cpp` are placeholders needing field calibration.

## Watchdog

- Role loops busy-poll the radio, so each loop iteration ends with
   `vTaskDelay(pdMS_TO_TICKS(1))` to feed the idle task. Keep that yield.

## Gotchas

- Node S3-N16R8: do not force 16 MB flash layout (bootloader SHA panic) - use
   the 8 MB layout. Avoid forcing CONFIG_SPIRAM=y on boards whose PSRAM does
   not respond.
- The ESP-IDF CMake project name is `forest_monitor` (binary forest_monitor.*).
