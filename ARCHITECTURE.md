# Forest Monitor — Architecture Reference

A complete map of the codebase: what each file does, how the layers fit together, and the data/control flow at runtime.

> **Project:** Scalable Multi-hop LoRa Networks for Intelligent Forest Monitoring
> **Stack:** ESP-IDF v5.2.x · C++17 · FreeRTOS · TensorFlow Lite Micro · RadioLib (SX1278)
> **Targets:** ESP32-WROOM-32 (gateway) · ESP32-S3 (relay + node)

---

## 1. Repository layout (top-down)

```
Minor_Project/
├── README.md                              # top-level overview, quick build, wiring
├── ARCHITECTURE.md                        # this file
├── Minor_project_Mid_term_diffence.md     # mid-term academic report (deliverable)
│
├── Minor_Project/
│   └── esp-idf/                           # vendored ESP-IDF v5.2.x source clone (toolchain)
│
├── forest_monitor/                        # canonical unified firmware (build with ESP-IDF)
│   ├── CMakeLists.txt
│   ├── sdkconfig.defaults                 # common
│   ├── sdkconfig.defaults.esp32           # gateway pin overrides, default role 0
│   ├── sdkconfig.defaults.esp32s3         # S3 pin overrides (mic-collision remap), role 2
│   ├── .gitignore
│   ├── README.md                          # build/flash, wiring, payload format
│   ├── AGENTS.md                          # conventions for AI coding agents
│   ├── explain.md                         # plain-language walkthrough for examiners
│   ├── build/                             # generated ESP-IDF build output
│   ├── build_win.bat                      # convenience wrapper for Windows users
│   └── main/
│       ├── CMakeLists.txt                 # role-aware src/REQUIRES selection
│       ├── Kconfig.projbuild              # CONFIG_LDSE_ROLE + radio pin Kconfigs
│       ├── idf_component.yml              # declares RadioLib, esp-tflite-micro, esp-dsp, esp-dht
│       ├── main.cpp                       # 26-line role dispatcher
│       ├── payload.h                      # shared NodePayload struct (37 B ≤ 48 B LDSE limit)
│       ├── ldse/                          # ported LDSE multi-hop LoRa library (ESP-IDF)
│       ├── acoustic/                      # on-device TinyML pipeline (relay + node)
│       ├── sensors/                       # fire-risk stack (relay + node)
│       └── roles/                         # gateway.cpp / relay.cpp / node.cpp entry points
```

The canonical firmware is `forest_monitor/`. The pre-merge code was carried into `forest_monitor/main/{acoustic,ldse,roles}/` and the standalone trees removed.

---

## 2. `forest_monitor/` — file-by-file

### 2.1 Project root

| File | Purpose |
|---|---|
| `CMakeLists.txt` | ESP-IDF project entry — sets the project name `forest_monitor`. |
| `sdkconfig.defaults` | Common defaults: FreeRTOS 1 kHz tick, main task stack size. |
| `sdkconfig.defaults.esp32` | Gateway (WROOM-32) pin overrides (MOSI=23, MISO=19, DIO1=26), default `CONFIG_LDSE_ROLE=0`. |
| `sdkconfig.defaults.esp32s3` | S3 pin overrides (MOSI=8, DIO1=9 to avoid colliding with mic on GPIO 17/16), default `CONFIG_LDSE_ROLE=2`. |
| `.gitignore` | Excludes `build/`, `managed_components/`, `sdkconfig`. |
| `README.md` | Build/flash instructions, wiring tables, payload format. |
| `AGENTS.md` | Conventions for AI coding agents (build cmds, role→module matrix, watchdog rule, etc.). |
| `explain.md` | Plain-language walkthrough for examiners. |
| `build/` | Generated ESP-IDF build output (CMakeCache, .elf, .bin). |
| `build_win.bat` | Convenience wrapper for Windows users. |

### 2.2 `main/` — application

| File | Purpose |
|---|---|
| `CMakeLists.txt` | **The role dispatcher at build time.** Selects `SRCS` + `REQUIRES` per `CONFIG_LDSE_ROLE`. Gateway pulls only LDSE; relay adds sensors; node adds sensors + acoustic. |
| `Kconfig.projbuild` | Exposes `CONFIG_LDSE_ROLE` (0=gateway, 1=relay, 2=node) and per-board radio pin Kconfigs. |
| `idf_component.yml` | Declares managed-component dependencies: RadioLib, esp-tflite-micro, esp-dsp, esp-dht. |
| `main.cpp` | 26-line `app_main()` that calls `ldse_gateway_main()` / `ldse_relay_main()` / `ldse_node_main()`. |
| `payload.h` | Shared `NodePayload` struct (37 B, must stay ≤ 48 B LDSE limit). Gateway decodes this **without compiling sensors or classifier**. |

### 2.3 `main/ldse/` — ported LDSE library (ESP-IDF)

The full LDSE (Layered Dynamic Synchronization Energy-saving) protocol stack, ported from the Arduino/PlatformIO project. Each file is small and single-purpose.

| File | Lines | Purpose |
|---|---|---|
| `LdseCompat.h` | 49 | Shims for `millis`/`micros`/`delay` on esp_timer + FreeRTOS. Custom `ldse_min_u8` / `ldse_clampf` avoid clashing with RadioLib's `std::min`. |
| `LdseConfig.h` | 96 | All protocol constants: pin overrides from Kconfig, ISM-band frequencies (433.3/433.5/433.7 MHz — Nepal band), SF/BW/coding rate, epoch timings, FTSP drift, congestion threshold, energy model. |
| `LdsePacket.h` | 112 | Wire format for every LDSE message type (`MSG_DATA`, `MSG_FIRE_ALERT`, `MSG_SYNC_BEACON`, `MSG_ACK`, …) — POD struct + serializers. |
| `LdseEpoch.h` | 74 | 10-second cycle struct: `epoch_start`, `now_ms`, `window` enum (`SYNC`/`DATA`/`SLEEP`), helpers to compute current phase. |
| `LdseRadio.h/.cpp` | 60 + 141 | Thin RadioLib wrapper. Owns the `SX1278` module (heap-allocated because RadioLib keeps the pointer) via `EspHal(SPI2_HOST)`. Provides `tx()`, `rxAsync()`, `setChannel()`. |
| `LdseSync.h/.cpp` | 63 + 89 | FTSP-style time sync. Gateway broadcasts `LAYER_INIT` + `SYNC` every 250 ms during SYNC window; relays re-broadcast at one-epoch intervals; nodes track clock drift (45 ppm) and adjust. |
| `LdseRouting.h/.cpp` | 73 + 146 | Layered topology + Implicit Route Exploration (IRE). `Score = α·RSSI + β·(1 − E_consumed/E_initial)` for parent selection. `Layer = beacon_layer + 1`. |
| `LdseForwarder.h/.cpp` | 85 + 166 | The relay. Listens during DATA window, queues packets (cap 10), forwards on Puc when channel clear. Uses Channel Activity Detection (CAD, 2-symbol listen) + randomized exponential backoff. |
| `LdseEnergy.h/.cpp` | 56 + 96 | Energy model: `E_total = E_sleep + E_sense + E_proc + E_sync + E_tx + E_rx`. Tracks per-node remaining energy (mAh) for IRE parent scoring. |

### 2.4 `main/acoustic/` — on-device TinyML pipeline (relay + node)

Implements: **I2S capture → framing → Hann window → FFT → mel filterbank → log → z-score → INT8 quantize → TFLite Micro inference**.

| File | Lines | Purpose |
|---|---|---|
| `audio_capture.h/.cpp` | 21 + 83 | INMP441 I2S mic init + DMA-driven sample reads. GPIO 17/15/16 (BCK/WS/DIN), 16 kHz, 16-bit, mono. |
| `spectrogram_params.h` | 12 | All magic numbers: 16 kHz, 512-pt FFT, hop 160, 64 mel bands, 251 frames, INT8 scale `0.053753` / zp `17`. |
| `hann_window.h` | 71 | 512-sample precomputed Hann window (lookup table for FFT). |
| `mel_filterbank.h` | 2064 | 64 × 257 mel filterbank matrix (64 psychoacoustic bands × 257 FFT bins). Generated by `gen_constants.py`. |
| `model_data.h` | 1644 | INT8 quantized TFLite model embedded as `const unsigned char model_data[]`. **The "brain."** 5 classes: Axe, Chainsaw, Gunshot, Handsaw, Background. |
| `spectrogram.h/.cpp` | 25 + 258 | Full DSP pipeline. Fills the INT8 input tensor. |
| `classifier.h/.cpp` | 39 + 166 | FreeRTOS task. Loads model, sets up TFLite Micro interpreter with 260 KB tensor arena, runs the capture→spectrogram→invoke loop, publishes the latest result for the LDSE node. |

### 2.5 `main/sensors/` — fire-risk stack (relay + node)

| File | Lines | Purpose |
|---|---|---|
| `mq135.h/.cpp` | 25 + 88 | MQ-135 gas/CO₂ sensor on GPIO2 via `esp_adc_oneshot` (ADC1_CH1). Returns mV. |
| `dht22.h/.cpp` | 28 + 37 | DHT22 on GPIO12 via `chmorgan/esp-dht` component. Returns °C and %RH. |
| `fire_scoring.h/.cpp` | 29 + 36 | Composite fire-risk score per report Eq. 3.4: `0.51·ΔCO₂ + 0.37·ΔT + 0.12·ΔH`, threshold `3.0`. `CAL_*` baselines are placeholder calibration constants. |

### 2.6 `main/roles/` — three role entry points

Each file defines a single `ldse_{gateway,relay,node}_main()` function. The role-specific logic only; the cross-role code (LDSE, sensors, acoustic) lives in its own module.

| File | Lines | Role | What it does |
|---|---|---|---|
| `gateway.cpp` | 152 | 0 (ESP32-WROOM-32) | Holds the LDSE root. Broadcasts SYNC every 250 ms during the SYNC window. Logs every received DATA/FIRE packet as CSV: `DATA\|FIRE,epoch_ms,origin,src,hops,seq,rssi,layer,class,temp,hum,gas,fire,count`. |
| `relay.cpp` | 292 | 1 (ESP32-S3) | Picks parent via IRE; sleeps most of the epoch. Listens on Prc1 for 2.5 s during DATA window, then forwards on Puc. Reads its own MQ-135 + DHT22 each epoch and reports fire score. |
| `node.cpp` | 272 | 2 (ESP32-S3) | Same as relay **plus** the classifier task. Sends `MSG_FIRE_ALERT` if `fireScore ≥ 3.0`; sends `MSG_DATA` on acoustic threat (confidence ≥ 0.85); bypasses directly to gateway on Puc if relay is congested (`ShouldBypassToGateway()`). |

---

## 3. The pre-merge standalone trees

The original standalone `forest_acoustic_classifier/` (ESP-IDF) and `ldse-esp32/` (Arduino/PlatformIO) trees were consolidated into `forest_monitor/` and removed from the repository. Their code lives on as:

- **Acoustic classifier** → `forest_monitor/main/acoustic/`
- **LDSE protocol** → `forest_monitor/main/ldse/`
- **Role entry points (gateway/relay/node)** → `forest_monitor/main/roles/`

---

## 4. Vendored toolchain

- **`Minor_Project/esp-idf/`** — full ESP-IDF v5.2.x source clone at the repo root (`~/.esp/esp-idf`-style layout). `idf.py set-target … && idf.py build` runs against this checkout. The unified firmware documents the path; the standard install path `~/esp/esp-idf` is also accepted.

---

## 5. Cross-layer data flow

### 5.1 Build-time role selection

```
main/main.cpp (app_main)
    │
    ├── #if CONFIG_LDSE_ROLE == 0  → ldse_gateway_main()  (roles/gateway.cpp)
    ├── #elif CONFIG_LDSE_ROLE == 1 → ldse_relay_main()    (roles/relay.cpp)
    └── #else                      → ldse_node_main()     (roles/node.cpp)
```

The role choice also drives `main/CMakeLists.txt`:

| Role | Sources added | Required components |
|---|---|---|
| 0 (gateway) | `ldse/*`, `roles/gateway.cpp` | `RadioLib`, `esp_timer`, `driver` |
| 1 (relay) | + `roles/relay.cpp`, `sensors/*` | + `esp_adc`, `chmorgan__esp-dht` |
| 2 (node) | + `roles/node.cpp`, `acoustic/*` | + `esp_driver_i2s`, `esp-tflite-micro`, `espressif__esp-dsp` |

### 5.2 Runtime data flow (node)

```
┌─────────────────────────┐
│ I2S INMP441 microphone  │   GPIO 17/15/16, 16 kHz mono
└────────────┬────────────┘
             │ int16_t samples (DMA)
             ▼
┌─────────────────────────┐
│ audio_capture.cpp       │   512-sample DMA descriptors
└────────────┬────────────┘
             │ 40 000 samples / 2.5 s
             ▼
┌─────────────────────────┐
│ spectrogram.cpp         │   frame → Hann → FFT (dsps_fft2r_fc32)
│                         │   magnitude → mel → log10 → z-score
│                         │   → INT8 quantize (scale 0.053753, zp 17)
└────────────┬────────────┘
             │ 64 × 251 × 1 INT8 tensor
             ▼
┌─────────────────────────┐
│ classifier.cpp          │   TFLite Micro interpreter
│ (FreeRTOS task)         │   260 KB tensor arena
└────────────┬────────────┘
             │ classIdx (0..4) + 5 confidences
             ▼
┌─────────────────────────────────────────────┐
│ node.cpp loop                               │
│                                             │
│   MQ-135 + DHT22 → fire_scoring → fireScore │
│                                             │
│   decision:                                 │
│     fireScore ≥ 3.0     → MSG_FIRE_ALERT    │
│     conf ≥ 0.85         → MSG_DATA          │
│     else                → sleep until epoch │
└────────────┬────────────────────────────────┘
             │ NodePayload (37 B)
             ▼
┌─────────────────────────┐
│ LdseForwarder           │   Prc1 (433.5 MHz) → Puc (433.3 MHz)
│ (queue, CAD, backoff)   │   if congested → bypass to gateway on Puc
└────────────┬────────────┘
             │ LoRa SX1278 (RadioLib, EspHal)
             ▼
        ┌────────┐
        │ relay  │   layer 1
        └───┬────┘
            ▼
        ┌─────────┐
        │ gateway │   layer 0, ESP32-WROOM-32
        └────┬────┘
             │ CSV log: DATA|FIRE,epoch_ms,origin,src,hops,seq,rssi,layer,class,temp,hum,gas,fire,count
             ▼
       web dashboard (out of scope)
```

### 5.3 LDSE epoch timeline (10 s cycle)

```
0 s         2 s         8 s              10 s
├───────────┼───────────┼─────────────────┤
│  SYNC     │   DATA    │     SLEEP       │
│  window   │  window   │   window        │
│  (2 s)    │  (6 s)    │    (2 s)        │
└───────────┴───────────┴─────────────────┘
   ▲           ▲             ▲
   │           │             │
gateway    node TX +        everyone
broadcasts relay listen     in deep sleep
SYNC every  forwards       (~7 µA)
250 ms
```

### 5.4 Channel plan (Nepal ISM 433.05–434.79 MHz)

| Channel | Frequency | Purpose |
|---|---|---|
| **Puc**  | 433.3 MHz | Primary uplink / control — route discovery, sync beacons, forwarded traffic. |
| **Prc1** | 433.5 MHz | Data channel relay ↔ node. |
| **Prc2** | 433.7 MHz | Reserved data channel — used for the congestion-bypass path. |

### 5.5 Node payload (`payload.h`, 37 bytes)

```c
struct NodePayload {
    uint8_t classIdx;          // acoustic argmax (0..4)
    float   confidence[5];     // per-class probabilities
    float   temperature;       // °C (DHT22)
    float   humidity;          // % RH (DHT22)
    float   gas;               // mV (MQ-135)
    float   fireScore;         // weighted fire-risk
};                             // total = 1 + 20 + 4 + 4 + 4 + 4 = 37 bytes
```

The gateway decodes this struct **without compiling sensors or classifier**, keeping the gateway image lean.

---

## 6. Key design choices and gotchas

1. **Single image, build-time role.** Avoids three separate firmware trees; `CONFIG_LDSE_ROLE` and per-target `sdkconfig.defaults.*` choose everything.
2. **Payload stays dependency-free.** The gateway image must decode `NodePayload` without pulling in sensors or TFLite Micro — keeps the WROOM-32 image small.
3. **GPIO collision avoidance.** S3 mic uses GPIO 17 (BCK) + 16 (DIN). Classic LDSE SPI defaults (`MOSI=17`, `DIO1=16`) would collide — S3 remaps to `MOSI=8`, `DIO1=9`. Gateway (WROOM-32) uses the classic 23/19/26 instead.
4. **Watchdog.** Role loops busy-poll the radio, so each loop iteration ends with `vTaskDelay(pdMS_TO_TICKS(1))` to feed the idle task.
5. **S3 flash layout.** The 16 MB layout (`default_16MB.csv`) crashes the bootloader on the dev board — use the 8 MB layout (`default_8MB.csv`). See `AGENTS.md` for the full gotchas list.
6. **No PSRAM force.** Don't set `CONFIG_SPIRAM=y` on boards whose PSRAM doesn't respond; emits a harmless but noisy warning.
7. **Fire-score baselines are placeholders.** `sensors/fire_scoring.cpp` has `CAL_*` constants that need field calibration against your site's stable readings and standard deviations.

---

## 7. Tooling and dependencies

| Layer | Component | Source |
|---|---|---|
| Framework | ESP-IDF v5.2.x | vendored at `Minor_Project/esp-idf/` |
| Build | CMake + Ninja | bundled with ESP-IDF |
| ML | `espressif/esp-tflite-micro` | managed component |
| DSP | `espressif/esp-dsp` | managed component (FFT) |
| Radio | `RadioLib` (jgromes) | managed component (SX1278 driver) |
| DHT | `chmorgan/esp-dht` | managed component |
| Build wrapper | `build_win.bat` | Windows shortcut |

---

## 8. Source line counts (forest_monitor only)

| Module | Total lines |
|---|---|
| `main/ldse/` | ~1 350 |
| `main/acoustic/` (incl. `model_data.h` + `mel_filterbank.h`) | ~4 400 |
| `main/sensors/` | ~245 |
| `main/roles/` | ~715 |
| `main/` (top) | ~80 |
| **Total** | **~6 700** |

Source-of-truth reference: this document and `forest_monitor/README.md`. Any contradiction between the two — this file wins.
