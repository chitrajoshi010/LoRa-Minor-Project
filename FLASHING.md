# Flashing Guide — forest_monitor

Status as of this writing: **all three roles compile and produce a flashable
`.bin`** via real `idf.py build` runs (not just static review). This file
documents what was fixed to get there, how to build/flash each role, and what
is still *not* verified (compiles ≠ tested on real hardware).

> Last verified end-to-end on **Windows 11** with **ESP-IDF v5.5.5** installed
> at `C:\Espressif\frameworks\esp-idf-v5.5.5`. `build_win.bat` was fixed (see
> fix #7 below) and re-verified: `build_win.bat gateway`, `build_win.bat
> node`, and `build_win.bat relay` all build cleanly and produce a flashable
> `forest_monitor.bin`. Flashing itself (writing to a real board) has not yet
> been re-verified after that fix — see "What is still NOT verified" for the
> earlier `Wrong boot mode detected (0x13)` note on COM9, which is a
> board/driver DTR-RTS issue, unrelated to the build fix.

## TL;DR — build + flash, per role

`idf.py fullclean` is required every time you switch `--target` (esp32 <->
esp32s3), because `sdkconfig` is regenerated from `sdkconfig.defaults.*` and a
stale `build/` directory from the other target will not reconfigure cleanly.
Replace `/dev/ttyUSB0` / `COM9` with your board's actual port.

### Linux / WSL

```sh
. "$HOME/esp/esp-idf/export.sh"
cd forest_monitor

# --- Gateway — ESP32-WROOM-32, role 0 (default) ---
idf.py fullclean
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor

# --- Relay — ESP32-S3, role 1 ---
idf.py fullclean
idf.py set-target esp32s3
idf.py -D CONFIG_LDSE_ROLE=1 build
idf.py -p /dev/ttyUSB0 flash monitor

# --- Node — ESP32-S3, role 2 (default for esp32s3 target) ---
idf.py fullclean
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Find the port with `ls /dev/ttyUSB*` (or `ls /dev/ttyACM*`); on WSL you must
first attach the USB device with `usbipd attach --wsl` (from Windows) before
`/dev/ttyUSB0` appears inside WSL.

### Windows (via the project's `build_win.bat` wrapper — recommended)

`build_win.bat` sources ESP-IDF (`export.bat`) and runs `set-target` +
`build` for you. **Caveat:** it's a batch file that internally calls
`export.bat`, so its `PATH`/env changes only last inside that process — if
you invoke it from PowerShell as `.\build_win.bat gateway`, PowerShell spawns
a child `cmd.exe` for it, and `idf.py` will **not** be on `PATH` in your
PowerShell session afterward. Chain the build and flash into the *same*
`cmd.exe` invocation instead:

```powershell
cd C:\Users\Chitra\Minor_Project_Windows\forest_monitor

# --- Gateway — esp32, role 0 (default) ---
cmd /c "build_win.bat gateway && idf.py -p COM9 flash monitor"

# --- Relay — esp32s3, role 1 ---
cmd /c "build_win.bat relay && idf.py -p COM9 flash monitor"

# --- Node — esp32s3, role 2 (default for esp32s3 target) ---
cmd /c "build_win.bat node && idf.py -p COM9 flash monitor"
```

(If you're already working from a `cmd.exe` window rather than PowerShell,
`build_win.bat gateway` followed by `idf.py -p COM9 flash monitor` as two
separate commands works fine — env vars persist within the same `cmd.exe`
process, just not across a PowerShell→child-cmd→PowerShell boundary.)

Find the port with:
```powershell
Get-PnpDevice -Class Ports -Status OK | Format-Table FriendlyName, InstanceId -AutoSize
```
(replug the board to see which `COM#` appears/disappears). If flashing fails
with `Wrong boot mode detected (0x13)`, hold BOOT, tap RST, release BOOT
(manual download mode) before retrying `flash`.

---

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

7. **`build_win.bat` could invoke `idf.py` under the wrong Python
   interpreter.** The script hardcoded
   `set PATH=%IDF_ROOT%\tools\idf-python\3.11.2;...;%PATH%` *before* calling
   `export.bat`. That path point to the bare base interpreter (no packages
   installed), not either of the venvs `export.bat` actually manages
   (`idf5.5_py3.11_env` / `idf5.5_py3.14_env`, both with `click` etc.
   installed). Depending on PATH-resolution order this could shadow the
   venv `export.bat` selected, so `idf.py` occasionally ran under the
   packageless interpreter and failed with `No module named 'click'` —
   intermittently, and only reproduced for the esp32s3 (node/relay) builds
   in this pass, not the esp32 (gateway) build.
   __Fix:__ removed the manual PATH prepend; after `call export.bat`, the
   script now reads `%IDF_PYTHON_ENV_PATH%` (set by `export.bat` itself to
   whichever venv it activated) and invokes `idf.py` via
   `%IDF_PYTHON_ENV_PATH%\Scripts\python.exe` explicitly, so the correct
   venv is used deterministically regardless of PATH order or which of the
   two installed venvs `export.bat` picks that run.

None of these required touching the LDSE protocol logic, packet format, pin
maps, or the acoustic-alert-threshold logic added earlier — those were
untouched and are unaffected.

---

## Fixing the Python venv mismatch (Windows)

The ESP-IDF v5.5 Windows installer expects the venv to be named
`idf5.5_py3.14_env` (matching the system Python 3.14), but on this machine
the existing venv is `idf5.5_py3.11_env` — left over from an earlier install
that used a different Python. This causes two symptoms:

- `.\export.ps1` fails with `ESP-IDF Python virtual environment "C:\Espressif\python_env\idf5.5_py3.14_env\Scripts\python.exe" not found. Please run the install script to set it up before proceeding.`
- `idf.py` (when invoked via `export.bat` or `build_win.bat`) fails with
  `No module named 'click'. This usually means that "idf.py" was not spawned within an ESP-IDF shell environment or the python virtual environment used by "idf.py" is corrupted.`

### Quick fix — symlink the expected name to the real env

```powershell
New-Item -ItemType Junction `
  -Path 'C:\Espressif\python_env\idf5.5_py3.14_env' `
  -Target 'C:\Espressif\python_env\idf5.5_py3.11_env'
```

After that, `.\export.ps1` and `build_win.bat gateway` both work normally,
and the `No module named 'click'` error goes away (because the venv that
`activate.py` finds on disk now actually has the right pip packages).

### Clean fix — rerun the installer

```powershell
cd C:\Espressif\frameworks\esp-idf-v5.5.5
.\install.bat
```

This recreates `idf5.5_py3.14_env` from scratch using whichever Python is
currently on PATH (3.14 in your case). Take the symlink route first; only
fall back to this if `idf.py` later complains about missing pip packages.

---

## Build verification performed

Ran clean, from-scratch builds (`idf.py fullclean` + `set-target` +
`build`) for all three roles and confirmed a `.bin` was produced with no
errors:

| Role    | Target  | `CONFIG_LDSE_ROLE` | Result    | Flash headroom                                                                  |
| ------- | ------- | -------------------- | --------- | ------------------------------------------------------------------------------- |
| Gateway | esp32   | 0 (default)          | ✅ builds | 9% free of 1 MB app partition (was 23% before the Firebase/TLS additions below) |
| Relay   | esp32s3 | 1                    | ✅ builds | 46% free                                                                        |
| Node    | esp32s3 | 2 (default)          | ✅ builds | 46% free                                                                        |

Windows re-verification after the `build_win.bat` interpreter fix (fix #7
above), run via `build_win.bat {gateway,node,relay}` from a fresh PowerShell
(no manually-sourced ESP-IDF environment):

| Command                  | Target  | `CONFIG_LDSE_ROLE` | Result    |
| ------------------------- | ------- | --------------------- | --------- |
| `build_win.bat gateway`   | esp32   | 0                     | ✅ builds |
| `build_win.bat node`      | esp32s3 | 2                     | ✅ builds |
| `build_win.bat relay`     | esp32s3 | 1                     | ✅ builds |

All three produced `forest_monitor.bin` with no `idf.py`/Python errors.

The gateway build was re-verified end-to-end on **2026-08-24** during the
real flashing session: `set-target esp32` → `build` →
`Project build complete. To flash, run: idf.py flash`. Final image size
`0xeb1d0 bytes`, 8% free in the 1 MB app partition. The flash itself failed
at runtime on COM9 with `Wrong boot mode detected (0x13)` — see "Manual
download mode" below.

---

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
  gateway, otherwise it will try to connect to an empty SSID.
- **Firebase uploader** (`net/firebase_uploader.cpp`, new) — compiles/links
  and follows the same REST auth flow already proven by
  `serial_to_firebase.py`, but the actual HTTP round trips (auth sign-in,
  TLS handshake against `identitytoolkit.googleapis.com`/
  `*.firebaseio.com`, RTDB writes) have **not** been exercised against a
  real Firebase project from this firmware yet. See the "Gateway Wi-Fi +
  Firebase upload" section below for what to set and what to check on
  first boot.
- **Fire-score calibration constants** (`CAL_*` in `sensors/fire_scoring.cpp`)
  are still placeholders — recalibrate per site before trusting fire alerts.
- **Acoustic alert threshold logic** (`classifier_is_threat`,
  `ACOUSTIC_ALERT_THRESHOLD = 0.70f`) compiles as part of relay/node, but
  the actual on-device TFLite inference accuracy against real audio hasn't
  been re-verified after the arena-size change (260 KB is still large enough
  for the model + working buffers per `interpreter.arena_used_bytes()`
  logging in `classifier.cpp` — check that log line on first boot to confirm
  headroom).
- **Actual flash on hardware** — the gateway build succeeded but the flash
  step on COM9 ended in `Wrong boot mode detected (0x13)`. See "Manual
  download mode" below; until that's resolved, none of the roles have
  actually been written to a real ESP32.

---

## Gateway Wi-Fi + Firebase upload — what's actually implemented

The gateway (`CONFIG_LDSE_ROLE=0`) brings up a Wi-Fi **station connectivity
layer** (`main/net/wifi_manager.{h,cpp}`) and, as of this pass, a **Firebase
RTDB upload path** (`main/net/firebase_uploader.{h,cpp}`) built on top of it.
Both compile/link cleanly as part of the gateway image; neither has been
exercised against a real AP / real Firebase project from this firmware yet
(see "What is still NOT verified" above).

### Wi-Fi connectivity layer (`net/wifi_manager`)

- `wifi_manager_init()` is called once, as the first line of
  `ldse_gateway_main()` in `main/roles/gateway.cpp`, **before** the LDSE
  sync/radio loop starts, and is fully **non-blocking**: it kicks off
  `esp_wifi_connect()` from the `WIFI_EVENT_STA_START` handler and returns
  immediately — the gateway's 10 s LDSE epoch (SYNC beacon every 250 ms) is
  never delayed waiting on an AP handshake.
- **Event-driven reconnect with capped exponential backoff**: on
  `WIFI_EVENT_STA_DISCONNECTED`, a retry is scheduled via a one-shot
  `esp_timer`, starting at 1 s and doubling up to a 10 s cap (reset to 1 s
  the moment `IP_EVENT_STA_GOT_IP` fires).
- `esp_wifi_set_ps(WIFI_PS_NONE)` immediately after `esp_wifi_start()` — Wi-Fi
  modem-sleep power-save is disabled because it's the main source of timing
  jitter risk against the LDSE SYNC beacon cadence.
- Credentials via Kconfig (`CONFIG_GATEWAY_WIFI_SSID` /
  `CONFIG_GATEWAY_WIFI_PASSWORD`, menu scoped `depends on LDSE_ROLE = 0`),
  settable via `idf.py menuconfig` → "Gateway Wi-Fi" or in
  `sdkconfig.defaults.esp32`.
- `wifi_manager_is_connected()` is a public getter (FreeRTOS event group bit
  set on `IP_EVENT_STA_GOT_IP`, cleared on disconnect) — now actually
  consumed by `firebase_uploader.cpp` (see below).
- Also starts SNTP (`esp_netif_sntp_init`, `pool.ntp.org`) so uploaded
  records carry a real Unix timestamp once the clock syncs; opportunistic
  and non-blocking, same as the Wi-Fi connect itself.

### Firebase uploader (`net/firebase_uploader`, new)

- **Schema**: pushes to the mesh schema from `CLAUDE.md` —
  `/nodes/{nodeId}/latest` (PUT, overwritten every cycle) and
  `/nodes/{nodeId}/history` (POST, append-only) — not the legacy
  `/predictions` + `/latest_prediction` paths.
- **Node keys**: this firmware still uses fixed role IDs (`LDSE_NODE_ID=2`,
  `LDSE_RELAY_ID=1`), not per-device unique IDs, so the Firebase node key is
  looked up from a packet's `originId`: `CONFIG_FIREBASE_NODE_KEY_NODE`
  (default `"ForestNode-01"`) for `LDSE_NODE_ID`, `CONFIG_FIREBASE_NODE_KEY_RELAY`
  (default `"ForestNode-02"`) for `LDSE_RELAY_ID` (the relay also carries its
  own sensors/classifier and can originate `MSG_DATA`/`MSG_FIRE_ALERT`). Gateway
  never originates data and is never uploaded as a "node".
- **Auth**: Firebase Identity Toolkit email/password sign-in
  (`identitytoolkit.googleapis.com/v1/accounts:signInWithPassword`), same
  account/flow as `serial_to_firebase.py` in the dashboard repo. The idToken
  is cached and refreshed ~60 s before its reported expiry; a 401 on upload
  forces one immediate re-auth + retry.
- **Transport**: `esp_http_client` + TLS via ESP-IDF's built-in certificate
  bundle (`esp_crt_bundle_attach`) — no pinned/custom cert needed for
  `*.googleapis.com` / `*.firebaseio.com`. `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_CMN`
  (the smaller "common" bundle, ~99% coverage) is set in
  `sdkconfig.defaults.esp32` to save flash on the already-tight 1 MB gateway
  partition.
  - **JSON**: hand-built with `snprintf` for the outgoing record (fixed,
    small shape) and hand-parsed with `strstr`/`strchr` for the two auth
    response fields needed (`idToken`, `expiresIn`) — avoids pulling a general
    JSON parser into a path that only ever talks to two known Google endpoints.
- **Never blocks the LDSE loop**: `firebase_uploader_enqueue()` (called from
  `gateway.cpp`'s `LogPacket()`, right after the existing CSV `printf`) only
  copies fields into a 4-deep FreeRTOS queue and returns; a dedicated
  background task (`FirebaseUploadTask`, its own 8 KB stack) drains the queue
  and does the actual HTTP calls. If the queue is full, the oldest queued
  item is dropped to make room — CSV/serial logging remains the source of
  truth, Firebase is best-effort onward delivery. Every upload attempt is
  also gated on `wifi_manager_is_connected()` inside the task; if Wi-Fi is
  down the item is silently dropped rather than retried.
- **Credentials via Kconfig**: `CONFIG_FIREBASE_API_KEY`, `CONFIG_FIREBASE_DB_URL`,
  `CONFIG_FIREBASE_AUTH_EMAIL`, `CONFIG_FIREBASE_AUTH_PASSWORD` — same pattern
  as the Wi-Fi SSID/password, under `idf.py menuconfig` → "Firebase upload
  (mesh schema)" (menu scoped `depends on LDSE_ROLE = 0`). Set these to match
  `Lora-based-forest-monitor-dashboard/Dashboard/env.js`'s `apiKey`/`databaseURL`
  and whatever email/password account `serial_to_firebase.py` already uses,
  so both paths write into the same Firebase project/account.
- **Kill switch**: `CONFIG_FIREBASE_UPLOAD_ENABLE` (default `y`) — set to `n`
  in menuconfig to compile the gateway with Wi-Fi connectivity but no upload
  code at all (falls back to the previous serial-only behavior), e.g. if you
  only want to use `serial_to_firebase.py` as the upload path instead.

### What is still NOT done / NOT verified here

- **No real Firebase project has been hit from this firmware.** The auth
  flow, JSON shape, and RTDB writes are logic-reviewed against the RTDB REST
  API and against `serial_to_firebase.py`'s already-working equivalent, but
  need a first real boot against your actual `CONFIG_FIREBASE_DB_URL` to
  confirm end-to-end (watch for `wifi_mgr: got IP` → `fb_upload: authenticated`
  → no `latest push failed` / `history push failed` warnings in the serial
  log).
- **Clock**: until SNTP syncs, `timestamp` is sent as `0` rather than a wrong
  small number (see the `nowEpoch > 1577836800` guard in `gateway.cpp`) — if
  you see `timestamp: 0` in Firebase, SNTP hasn't synced yet (check Wi-Fi is
  actually connected; SNTP needs an IP first).
- **Relay's own Firebase key** (`CONFIG_FIREBASE_NODE_KEY_RELAY`, default
  `"ForestNode-02"`) is a placeholder pending a real second-node naming
  decision — rename it to whatever key your dashboard/team actually wants
  for the relay's own sensor+acoustic stream.
- Flash headroom on the gateway dropped from 23% to 9% free (1 MB app
  partition) after adding `esp_http_client`/`esp-tls`/`mbedtls` cert bundle —
  still builds, but there's less room left for future gateway-side features
  before a partition table change becomes necessary.

---

## Identifying the gateway's COM port (Windows)

Before flashing, confirm which COM port maps to your gateway. The USB-UART
bridge on the dev board will show up as a `Ports (COM & LPT)` entry in
Device Manager. Typical mappings for ESP32-WROOM-32 dev boards:

| USB-UART bridge              | Likely COM | Notes                                 |
| ---------------------------- | ---------- | ------------------------------------- |
| Silicon Labs**CP210x** | varies     | most common on WROOM-32               |
| **CH340**              | varies     | common on cheap clones                |
| **CH9102**             | varies     | more typical on ESP32-S3 (relay/node) |

Replug the gateway USB cable and run:

```powershell
Get-PnpDevice -Class Ports -Status OK | Format-Table FriendlyName, InstanceId -AutoSize
```

The port that **newly appears** when you plug in the gateway is the one to
use in the `-p COMx` flag. On this machine the candidates are COM8 (CH340),
COM9 (CH9102), COM10/COM11 (CH343), COM14 (CP210x) — match by the FriendlyName
that corresponds to your gateway's actual bridge chip.

---

## Manual download mode (fix for `Wrong boot mode detected (0x13)`)

When the flash step ends with:

```
esptool.py v4.12.0
Serial port COM9
Connecting......................................

A fatal error occurred: Failed to connect to ESP32: Wrong boot mode detected (0x13)! The chip needs to be in download mode.
```

esptool **did connect to the chip on that COM port** — the chip just answered
sync with `0x13`, which means it's in **normal boot mode** (running existing
firmware), not **download mode** (the mode esptool needs to write flash). GPIO0
needs to be LOW at reset to enter download mode. Most ESP32 dev boards handle
this automatically by toggling DTR/RTS through the USB-UART bridge, but some
**CH9102 / CP210x** boards do not route those lines correctly. The fix is to
manually pulse the BOOT and EN buttons.

### Procedure

1. **Hold the BOOT button** (sometimes labeled `IO0` or `FLASH` — usually the
   button closest to the USB connector on the opposite side from EN).
2. **Press and release the EN (reset) button** while still holding BOOT.
3. **Wait about 1 second.**
4. **Release the BOOT button.**
5. Within ~3 seconds, run the flash command. The chip is in download mode
   for that brief window.

```powershell
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' `
   'C:\Espressif\frameworks\esp-idf-v5.5.5\tools\idf.py' -p COM9 flash monitor
```

### If your board has no BOOT button

Hold **GPIO0 to GND** with a jumper wire, momentarily short the **EN pin to
GND** (or pull it low then high), then release GPIO0 before flashing.

### Lower the baud rate if the manual sequence is flaky

The CH9102 in particular can have handshake issues at 460800:

```powershell
& 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe' `
   'C:\Espressif\frameworks\esp-idf-v5.5.5\tools\idf.py' -p COM9 -b 115200 flash monitor
```

Successful flash output looks like:

```
Connecting....
Chip is ESP32-D0WD-V3 (revision v3.1)
...
Writing at 0x00001000... (100 %)
Wrote 26128 bytes at 0x00001000 in 0.7 seconds
...
Hash of data verified.
Leaving...
Hard resetting via RTS pin...
```

The monitor then opens and prints boot logs at 115200 baud. Press `Ctrl+]`
to exit.

### After the first successful flash

Once the bootloader on the chip handles DTR/RTS correctly (which the
ESP-IDF-built bootloader does), subsequent auto-flash attempts usually work
without the manual BOOT dance. So this is mostly a one-time pain per board.

---

## Flashing checklist

1. **Every new shell**: source ESP-IDF (Linux/WSL) or use the wrapper / venv-python
   invocation (Windows).
2. Set radio pins / Wi-Fi credentials via `idf.py menuconfig` if your wiring
   differs from `sdkconfig.defaults.esp32`/`sdkconfig.defaults.esp32s3`
   (see `main/Kconfig.projbuild`).
   2a. **Gateway only**: also set `CONFIG_GATEWAY_WIFI_SSID`/`_PASSWORD` and, if
   you want Firebase upload, `CONFIG_FIREBASE_API_KEY`/`_DB_URL`/
   `_AUTH_EMAIL`/`_AUTH_PASSWORD` (menuconfig → "Gateway Wi-Fi" /
   "Firebase upload (mesh schema)") to match your Firebase project and
   `Lora-based-forest-monitor-dashboard/Dashboard/env.js`. Set
   `CONFIG_FIREBASE_UPLOAD_ENABLE=n` to skip Firebase entirely and keep
   serial-CSV-only behavior.
3. `idf.py fullclean` before switching `--target`.
4. `idf.py set-target <esp32|esp32s3>`.
5. For relay specifically, remember `-D CONFIG_LDSE_ROLE=1` (node is the
   esp32s3 default, i.e. role 2, if you don't pass this flag).
6. Confirm the right COM port with `Get-PnpDevice -Class Ports -Status OK` (Windows)
   or `ls /dev/ttyUSB*` (Linux).
7. `idf.py -p <PORT> flash monitor` and watch the serial log:
   - Gateway: `wifi_mgr: connecting...` → `wifi_mgr: got IP, connected` →
     (if Firebase enabled) `fb_upload: authenticated` → CSV log lines
     (`DATA|FIRE,epoch_ms,...`) with no `latest push failed`/`history push failed` warnings.
   - Relay/Node: `interpreter.arena_used_bytes()` log, DHT22 read results,
     LDSE sync offset logs.
8. If the flash step fails with `Wrong boot mode detected (0x13)`, follow
   the manual BOOT+EN sequence in "Manual download mode" above, then retry.
9. Bench-test all three boards together (gateway + relay + node powered on
   simultaneously) to confirm the LDSE mesh actually forms and forwards
   packets, and that data lands in Firebase/the dashboard — this has not
   been done as part of this fix pass.
