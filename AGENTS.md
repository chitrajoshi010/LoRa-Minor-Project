# AGENTS.md — Forest Monitor (unified ESP-IDF firmware)

Guidance for AI coding agents (Claude Code, Copilot, etc.) working in
`LoRa-Minor-Project/forest_monitor/`. This is the merged firmware combining the
LDSE LoRa protocol and the acoustic classifier into one ESP-IDF project with a
build-time role (`CONFIG_LDSE_ROLE`: 0=gateway, 1=relay, 2=node).

## Build / test commands

All work happens from `forest_monitor/`. ESP-IDF must be sourced in every shell
(`build_win.bat` handles this on Windows).

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

There is no host unit-test suite; "verified" means all three configs compile
and the three-board bench boots (gateway -> relay -> node).

## Architecture rules

- **One image, role chosen at build time.** `main/main.cpp` dispatches to
  `ldse_{gateway,relay,node}_main()` via `#if CONFIG_LDSE_ROLE`. Only the
  selected role's `.cpp` and its modules are compiled (see `main/CMakeLists.txt`).
  Never duplicate sources per role — change `main/CMakeLists.txt` only.
- **Role → module matrix (do not break this):**
  - gateway (0): LDSE only. No sensors, no classifier.
  - relay (1): LDSE + `sensors/` (fire scoring) + `acoustic/` (classifier task).
  - node (2): LDSE + `sensors/` + `acoustic/` (classifier task).
- **`payload.h` must stay dependency-free** — the gateway decodes it without
  compiling sensors/classifier. Keep `sizeof(NodePayload) ≤ 48` (currently 37 B;
  `static_assert` enforces).

## LDSE protocol (main/ldse/)

- Ported from Arduino; `LdseCompat.h` supplies `millis`/`micros`/`delay` on
  `esp_timer`/FreeRTOS. Do NOT reintroduce a global `min`/`max`/`constrain` macro —
  it breaks RadioLib's `std::min`. Use `ldse_min_u8` / `ldse_clampf`.
- Keep the packet format (`LdsePacket.h`), message types, epoch timing, FTSP
  sync, IRE routing and congestion logic **byte-identical** to the original.
- Radio uses RadioLib's built-in ESP-IDF `EspHal` (SPI2_HOST):
  `new EspHal(SCK, MISO, MOSI)` then `new Module(hal, NSS, DIO0, RST, DIO1)`.
  The `Module` must be heap-allocated (RadioLib keeps the pointer).
- LDSE epoch (10 s): `SYNC` 0–2 s (gateway broadcasts `MSG_SYNC` every 250 ms) ·
  `DATA` 2–8 s · `SLEEP` 8–10 s (~7 µA). Nepal ISM channels: `Puc` 433.3,
  `Prc1` 433.5, `Prc2` 433.7 MHz. FTSP drift = 45 ppm. Relay queue cap = 10
  packets. Congestion threshold = 0.8. CAD listen = 2 symbols; backoff slot = 2 ms.

## Pins (Kconfig)

- Radio pins come from `CONFIG_LDSE_PIN_*` (see `main/Kconfig.projbuild`),
  defaulted per target in `sdkconfig.defaults.esp32{,s3}`.
- **Never** set S3 `MOSI=17` or `DIO1=16` — they collide with the mic
  (BCK=17, DIN=16). S3 uses MOSI=8, DIO1=9. Gateway (WROOM-32) uses 23/19/26.

## Sensors (main/sensors/)

- MQ-135 on GPIO2 via esp_adc oneshot (ADC1_CH1); DHT22 on GPIO12 via the
  chmorgan/esp-dht component. `dht22.cpp` is the single adapter point if the
  component API differs.
- Fire score follows report Eq. 3.4 (weights 0.51/0.37/0.12); `CAL_*` baselines
  in `fire_scoring.cpp` are placeholders needing field calibration.

## Acoustic (main/acoustic/)

- INMP441 I2S on GPIO17 (BCK) / GPIO15 (WS) / GPIO16 (DIN), 16 kHz mono.
- Pipeline: Hann → dsps_fft2r_fc32 → 64-mel → log → z-score → INT8 quantize
  (scale 0.053753, zp 17) → TFLite Micro (260 KB arena, 5 classes:
  Axe/Chainsaw/Gunshot/Handsaw/Background). The model is embedded in
  `model_data.h`; `mel_filterbank.h` and `hann_window.h` are precomputed.

## Watchdog

- Role loops busy-poll the radio, so each loop iteration ends with
  `vTaskDelay(pdMS_TO_TICKS(1))` to feed the idle task. Keep that yield.

## Gotchas

- Node S3-N16R8: do not force 16 MB flash layout (bootloader SHA panic) — use
  the 8 MB layout. Avoid forcing `CONFIG_SPIRAM=y` on boards whose PSRAM does
  not respond.
- The ESP-IDF CMake project name is `forest_monitor` (binary `forest_monitor.*`).
- Serial console is 115200 baud (`sdkconfig.defaults`).
- Fire-score calibration constants (`CAL_GAS_MEAN_MV`, `CAL_GAS_STD_MV`,
  `CAL_TEMP_MEAN_C`, `CAL_TEMP_STD_C`, `CAL_HUM_MEAN`, `CAL_HUM_STD`) are
  placeholders — recalibrate per site (record stable readings + spread).
- Acoustic model: 5 classes (tree_falling was dropped vs. the 7-class mid-term
  report design) — reconcile in the report or retrain.
- ESP-IDF version: vendored clone is v5.2.x. The Windows wrapper
  (`build_win.bat`) points at `C:\Espressif\frameworks\esp-idf-v5.5.5` — adjust
  if your install path differs.

## MCP Tools: code-review-graph

**IMPORTANT: This project has a knowledge graph at `.code-review-graph/graph.db`.
ALWAYS use the code-review-graph MCP tools BEFORE using Grep/Glob/Read to
explore the codebase.** The graph is faster, cheaper (fewer tokens), and gives
you structural context (callers, dependents, test coverage) that file scanning
cannot.

### When to use graph tools FIRST

- **Exploring code**: `semantic_search_nodes_tool` or `query_graph_tool` instead of Grep
- **Understanding impact**: `get_impact_radius_tool` instead of manually tracing imports
- **Code review**: `detect_changes_tool` + `get_review_context_tool` instead of reading entire files
- **Finding relationships**: `query_graph_tool` with `callers_of`/`callees_of`/`imports_of`/`tests_for`
- **Architecture questions**: `get_architecture_overview_tool` + `list_communities_tool`

Fall back to Grep/Glob/Read **only** when the graph doesn't cover what you need.

### Key Tools

| Tool | Use when |
| ------ | ---------- |
| `detect_changes_tool` | Reviewing code changes — gives risk-scored analysis |
| `get_review_context_tool` | Need source snippets for review — token-efficient |
| `get_impact_radius_tool` | Understanding blast radius of a change |
| `get_affected_flows_tool` | Finding which execution paths are impacted |
| `query_graph_tool` | Tracing callers, callees, imports, tests, dependencies |
| `semantic_search_nodes_tool` | Finding functions/classes by name or keyword |
| `get_architecture_overview_tool` | Understanding high-level codebase structure |
| `refactor_tool` | Planning renames, finding dead code |

### Workflow

1. The graph auto-updates on file changes (via hooks).
2. Use `detect_changes_tool` for code review.
3. Use `get_affected_flows_tool` to understand impact.
4. Use `query_graph_tool` pattern="tests_for" to check coverage.

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
