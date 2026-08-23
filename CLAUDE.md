# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project summary

ESP-IDF v5.2 firmware for **Scalable Multi-hop LoRa Networks for Intelligent Forest Monitoring**. One unified codebase compiles into one of three roles (`CONFIG_LDSE_ROLE`): **gateway** (ESP32-WROOM-32, mains sink + CSV log), **relay** (ESP32-S3, LoRa forwarder + fire scoring + acoustic classifier), **node** (ESP32-S3, on-device TinyML acoustic classifier + sensors). LDSE (Layered Dynamic Synchronization Energy-saving) is a custom multi-hop LoRa protocol on 433 MHz SX1278 (RadioLib).

Canonical firmware lives in `forest_monitor/`. Mid-term academic report is `Minor_project_Mid_term_diffence.md`. Architecture deep-dive is `ARCHITECTURE.md`.

## Build / test commands

All work happens from `forest_monitor/`. ESP-IDF must be sourced in every shell (`build_win.bat` handles this on Windows).

Linux / WSL:
```sh
. "$HOME/esp/esp-idf/export.sh"
cd "$HOME/Minor_Project/forest_monitor"

# Gateway (ESP32-WROOM-32, role 0 — default in sdkconfig.defaults.esp32)
idf.py set-target esp32   && idf.py build

# Node (ESP32-S3, role 2 — default in sdkconfig.defaults.esp32s3)
idf.py set-target esp32s3 && idf.py build

# Relay (ESP32-S3, role 1 — override at command line)
idf.py set-target esp32s3 && idf.py -D CONFIG_LDSE_ROLE=1 build

# Switching target regenerates sdkconfig from sdkconfig.defaults.* — run fullclean first
idf.py fullclean
```

Windows (convenience wrapper):
```bat
:: from forest_monitor\
build_win.bat gateway   :: esp32,  role 0 (default if no arg)
build_win.bat node      :: esp32s3, role 2
build_win.bat relay     :: esp32s3, role 1
```

Flash + monitor:
```sh
idf.py -p /dev/ttyUSB0 flash monitor     # adjust port per board
```

**There is no host unit-test suite.** Verification = all three role/target configs compile cleanly + the three-board bench (gateway ↔ relay ↔ node) boots.

## High-level architecture

`forest_monitor/main/` is split into four role-aware modules:

| Module | Compiled for | Purpose |
|---|---|---|
| `main/ldse/` | all roles | LDSE multi-hop LoRa stack: `LdseRadio` (SX1278/RadioLib), `LdseSync` (FTSP drift sync), `LdseRouting` (IRE parent scoring), `LdseForwarder` (relay queue/CAD/backoff), `LdseEnergy` (per-node mAh model), plus `LdseCompat.h` (Arduino `millis/micros/delay` shims), `LdseConfig.h` (constants), `LdsePacket.h` (wire format), `LdseEpoch.h` (10 s cycle). |
| `main/sensors/` | relay + node | `mq135` (ADC1_CH1 on GPIO2), `dht22` (chmorgan/esp-dht on GPIO12), `fire_scoring` (report Eq. 3.4: `0.51·ΔCO₂ + 0.37·ΔT + 0.12·ΔH`, threshold 3.0). |
| `main/acoustic/` | relay + node | `audio_capture` (INMP441 I2S on GPIO17/15/16, 16 kHz mono), `spectrogram` (Hann → dsps_fft2r_fc32 → 64-mel → log → z-score → INT8 quantize, scale 0.053753 zp 17), `classifier` (TFLite Micro task, 260 KB arena, 5 classes: Axe/Chainsaw/Gunshot/Handsaw/Background). The model is embedded in `model_data.h`; `mel_filterbank.h` and `hann_window.h` are precomputed lookup tables. |
| `main/roles/` | one file per role | `gateway.cpp` (LAYER_INIT + SYNC every 250 ms + CSV log), `relay.cpp` (parent pick, listen-then-forward, own fire score), `node.cpp` (fire-alert + acoustic-threshold + congestion-bypass). |

Top of `main/`: `main.cpp` (26-line `#if CONFIG_LDSE_ROLE` dispatcher), `payload.h` (shared `NodePayload`, **dependency-free**, ≤ 48 B), `Kconfig.projbuild` (`CONFIG_LDSE_ROLE` + radio pin Kconfigs), `idf_component.yml` (managed deps), `CMakeLists.txt` (role-aware `SRCS` + `REQUIRES`).

### Build-time role dispatch (do not break this)

```
main/main.cpp (app_main)
  ├─ #if CONFIG_LDSE_ROLE == 0  → ldse_gateway_main()    (LDSE only)
  ├─ #elif CONFIG_LDSE_ROLE == 1 → ldse_relay_main()      (LDSE + sensors + acoustic)
  └─ #else                       → ldse_node_main()       (LDSE + sensors + acoustic)
```

`main/CMakeLists.txt` adds sources + `REQUIRES` per role. Role 0 image never compiles the classifier or sensors, keeping the WROOM-32 image lean.

### Runtime data flow (node)

INMP441 I2S → `audio_capture` (DMA) → `spectrogram` (FFT/mel/log/quant) → `classifier` (TFLite Micro) → `node.cpp` decision (`fireScore ≥ 3.0` ⇒ `MSG_FIRE_ALERT`, `conf ≥ 0.85` ⇒ `MSG_DATA`) → `LdseForwarder` (Prc1 433.5 MHz → Puc 433.3 MHz; bypasses to gateway if relay is congested) → relay → gateway → CSV log.

### LDSE epoch (10 s)

`SYNC` 0–2 s (gateway broadcasts `MSG_SYNC` every 250 ms) · `DATA` 2–8 s (node TX, relay listens then forwards on Puc) · `SLEEP` 8–10 s (~7 µA). Nepal ISM channels: `Puc` 433.3, `Prc1` 433.5, `Prc2` 433.7 MHz. FTSP drift = 45 ppm. Relay queue cap = 10 packets. Congestion threshold = 0.8. CAD listen = 2 symbols; backoff slot = 2 ms.

## Codebase conventions & gotchas

- **One image, role at build time.** Never duplicate sources per role — change `main/CMakeLists.txt` only.
- **`payload.h` must stay dependency-free.** The gateway image decodes `NodePayload` without compiling sensors or TFLite. Keep `sizeof(NodePayload) ≤ 48` (currently 37 B; `static_assert` enforces).
- **No global `min`/`max`/`constrain` macros.** RadioLib uses `std::min`. Use `ldse_min_u8` / `ldse_clampf` from `LdseCompat.h`.
- **Radio pin collision (S3 mic vs. SPI).** INMP441 uses GPIO17 (BCK) and GPIO16 (DIN); classic LDSE `MOSI=17` / `DIO1=16` collides. S3 defaults in `sdkconfig.defaults.esp32s3` remap to `MOSI=8`, `DIO1=9` — this now applies to both relay and node (both build the acoustic classifier). **Never** set S3 `MOSI=17` or `DIO1=16`. Gateway (WROOM-32) uses 23/19/26. All pin defaults live in `main/Kconfig.projbuild` and are overridable per build.
- **Heap-allocate RadioLib objects.** `LdseRadio::Begin` does `new EspHal(...)`, `new Module(hal,...)`, `new SX1278(module)` — `Module` keeps the `EspHal*` and `SX1278` keeps the `Module*`, so they cannot be stack temporaries.
- **Watchdog.** Role loops busy-poll the radio; every loop iteration ends with `vTaskDelay(pdMS_TO_TICKS(1))` to feed the idle task. Keep the yield.
- **Flash layout / PSRAM.** On the node S3-N16R8 board, do **not** force the 16 MB flash layout (bootloader SHA panic) — use 8 MB. Do not force `CONFIG_SPIRAM=y` on boards whose PSRAM does not respond.
- **Fire-score calibration.** `CAL_GAS_MEAN_MV`, `CAL_GAS_STD_MV`, `CAL_TEMP_MEAN_C`, `CAL_TEMP_STD_C`, `CAL_HUM_MEAN`, `CAL_HUM_STD` in `sensors/fire_scoring.cpp` are **placeholders** — recalibrate per site (record stable readings + spread). Weights 0.51/0.37/0.12 are the report's coefficients.
- **Acoustic model note.** 5 classes (tree_falling was dropped vs. the 7-class mid-term report design) — reconcile in the report or retrain.
- **Serial console** is 115200 baud (`sdkconfig.defaults`).
- **Project / binary name** is `forest_monitor` (set in `CMakeLists.txt`); output binaries are `forest_monitor.elf` / `forest_monitor.bin`.
- **ESP-IDF version.** Vendored clone is v5.2.x. The Windows wrapper (`build_win.bat`) points at `C:\Espressif\frameworks\esp-idf-v5.5.5` — adjust if your install path differs.

## Code-review-graph MCP

The project has a knowledge graph at `.code-review-graph/graph.db`. Prefer its tools over Grep/Glob/Read when exploring (semantic search, impact radius, callers/callees, change detection, architecture overview). See `AGENTS.md` at the repo root for the full tool list and workflow.

## Key files

- `forest_monitor/main/main.cpp` — role dispatcher
- `forest_monitor/main/CMakeLists.txt` — role-aware sources + deps
- `forest_monitor/main/Kconfig.projbuild` — `CONFIG_LDSE_ROLE` + radio pin Kconfigs
- `forest_monitor/main/payload.h` — shared wire format (dependency-free)
- `forest_monitor/main/ldse/LdseConfig.h` — radio/band/epoch/timing constants
- `forest_monitor/main/ldse/LdsePacket.h` — wire format for `MSG_DATA`, `MSG_FIRE_ALERT`, `MSG_SYNC_BEACON`, `MSG_ACK`, `MSG_LAYER_INIT`, etc.
- `forest_monitor/main/ldse/LdseRadio.{h,cpp}` — RadioLib + EspHal(SPI2_HOST) wiring
- `forest_monitor/main/sensors/fire_scoring.cpp` — fire score + calibration placeholders
- `forest_monitor/main/roles/{gateway,relay,node}.cpp` — role entry points
- `forest_monitor/sdkconfig.defaults{,esp32,esp32s3}` — per-target defaults
- `forest_monitor/build_win.bat` — Windows convenience wrapper
- `ARCHITECTURE.md` — full file-by-file reference (source of truth when this file conflicts)
- `forest_monitor/README.md` — wiring tables + payload format
