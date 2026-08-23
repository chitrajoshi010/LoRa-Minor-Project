# Scalable Multi-hop LoRa Networks for Intelligent Forest Monitoring

A low-cost, energy-efficient wireless sensor network for remote forest monitoring. Two ESP32-S3 sensor nodes run **on-device TinyML acoustic classification** (chainsaw, axe, handsaw, gunshot, background) and **multi-sensor fire-risk scoring** (MQ-135 + DHT22), then forward alerts over a **multi-hop LoRa mesh** using the **LDSE** protocol to a gateway, which publishes them to a web dashboard.

**Authors:** Chitra Raj Joshi · Keshar Singh Sunar · Prabesh Parajulee · Santosh Gadtaula
**Supervisor:** Er. Saroj Shakya, Department of Electronics and Computer Engineering, Thapathali Campus, Tribhuvan University
**Report:** [`Minor_project_Mid_term_diffence.md`](./Minor_project_Mid_term_diffence.md) (mid-term report, July 2026)

---

## Repository layout

| Path | Purpose |
|---|---|
| `forest_monitor/` | **Unified ESP-IDF firmware** — single codebase, role selected at build time (`CONFIG_LDSE_ROLE`: 0=gateway, 1=relay, 2=node). Combines the acoustic classifier, sensor stack, and LDSE multi-hop LoRa. |
| `Minor_Project/esp-idf/` | Vendored Espressif ESP-IDF source clone used to build `forest_monitor/`. `idf.py` runs against this checkout. |
| `Minor_project_Mid_term_diffence.md` | Mid-term academic report (full deliverable). |
| `README.md` | This file. |

## `forest_monitor/` — the canonical firmware

A single ESP-IDF project with a build-time **role** switch:

| Role | `CONFIG_LDSE_ROLE` | Target | Board | What it runs |
|---|---|---|---|---|
| Gateway | `0` | `esp32` | ESP32-WROOM-32 | LDSE sink, sync source, CSV logger |
| Relay | `1` | `esp32s3` | ESP32-S3 | LDSE forwarder + MQ-135/DHT22 fire scoring |
| Node | `2` | `esp32s3` | ESP32-S3 | Acoustic classifier + sensors + LDSE uplink |

It combines:

- **LDSE** multi-hop LoRa protocol (SX1278 @ 433 MHz via RadioLib), in `main/ldse/`.
- **Acoustic classifier** (I2S INMP441 → log-mel spectrogram → TFLite Micro, 5 classes: Axe, Chainsaw, Gunshot, Handsaw, Background), in `main/acoustic/`.
- **Fire scoring** from MQ-135 + DHT22 (relay and node), in `main/sensors/`.

### Quick build

```sh
. "$HOME/Minor_Project/esp-idf/export.sh"

PROJECT_DIR="$HOME/Minor_Project/forest_monitor"

# Gateway (ESP32-WROOM-32)
cd "$PROJECT_DIR" && idf.py set-target esp32 && idf.py build

# Node (ESP32-S3, default role = 2)
cd "$PROJECT_DIR" && idf.py set-target esp32s3 && idf.py build

# Relay (ESP32-S3, override role to 1)
cd "$PROJECT_DIR" && idf.py set-target esp32s3 && idf.py -D CONFIG_LDSE_ROLE=1 build
```

> **Adjust the toolchain path** to wherever you keep ESP-IDF. If you prefer the standard `~/esp/esp-idf`, change the first line.

### Wiring (SX1278 LoRa, per board)

| Signal | Gateway (WROOM-32) | Relay / Node (S3) |
|---|---|---|
| NSS  | GPIO18 | GPIO18 |
| SCK  | GPIO13 | GPIO13 |
| MOSI | GPIO23 | **GPIO8** |
| MISO | GPIO19 | GPIO21 |
| DIO0 | GPIO4  | GPIO4  |
| RST  | GPIO14 | GPIO14 |
| DIO1 | GPIO26 | **GPIO9** |

> **GPIO conflict resolution (relay & node):** the INMP441 mic uses GPIO17 (BCK) and GPIO16 (DIN), which would collide with the classic LDSE MOSI=17 / DIO1=16. The S3 radio is remapped to **MOSI=8, DIO1=9**.

### Sensors & microphone (relay & node, S3)

| Peripheral | GPIO | Notes |
|---|---|---|
| INMP441 mic BCK | 17 | I2S (relay & node) |
| INMP441 mic WS  | 15 | I2S (relay & node) |
| INMP441 mic DIN | 16 | I2S (relay & node) |
| MQ-135 AOUT | 2 | ADC1_CH1, esp_adc oneshot |
| DHT22 DATA  | 12 | chmorgan/esp-dht |

See `forest_monitor/README.md` for payload layout, fire-score calibration, and node-data flow.

## Tools

- **Framework:** ESP-IDF v5.2.x (vendored at `Minor_Project/esp-idf/`)
- **ML:** TensorFlow Lite Micro + Edge Impulse-style INT8 quantization
- **Radio:** SX1278 LoRa, RadioLib
- **Sensors:** INMP441 (I2S), MQ-135 (ADC), DHT22 (GPIO)
- **Sensors library:** `chmorgan/esp-dht`
