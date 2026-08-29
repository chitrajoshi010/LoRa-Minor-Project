# Scalable Multi-hop LoRa Networks for Intelligent Forest Monitoring

A low-cost, energy-efficient wireless sensor network for remote forest monitoring. Two ESP32-S3 field devices run **on-device TinyML acoustic classification** (chainsaw, axe, handsaw, gunshot, background) and **multi-sensor fire-risk scoring** (MQ-135 + DHT22), then forward alerts over a **multi-hop LoRa mesh** using the **LDSE** protocol to a gateway, which logs CSV over serial and can forward packets over Wi-Fi to a Firebase-backed dashboard.

**Authors:** Chitra Raj Joshi · Keshar Singh Sunar · Prabesh Parajulee · Santosh Gadtaula
**Supervisor:** Er. Saroj Shakya, Department of Electronics and Computer Engineering, Thapathali Campus, Tribhuvan University
**Report:** [`Minor_project_Mid_term_diffence.md`](./Minor_project_Mid_term_diffence.md) (mid-term report, July 2026)

---

## Repository layout

| Path | Purpose |
|---|---|
| `forest_monitor/` | **Unified ESP-IDF firmware** — single codebase, role selected at build time (`CONFIG_LDSE_ROLE`: 0=gateway, 1=relay, 2=node). Combines the acoustic classifier, sensor stack, and LDSE multi-hop LoRa. |
| `models/model_int8.tflite` | Standalone copy of the acoustic model artifact; the firmware embeds the deployed INT8 model separately in `forest_monitor/main/acoustic/model_data.h`. |
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

For the build/flash commands, wiring tables, node payload layout, and
fire-score calibration notes, see **[`forest_monitor/README.md`](./forest_monitor/README.md)**
(the single source of truth for those details — not duplicated here).

## Where to find what

| Need | Read |
|---|---|
| Build/flash instructions, wiring, payload format | [`forest_monitor/README.md`](./forest_monitor/README.md) |
| Plain-language walkthrough (for examiners) | [`forest_monitor/explain.md`](./forest_monitor/explain.md) |
| Full file-by-file architecture map | [`ARCHITECTURE.md`](./ARCHITECTURE.md) |
| AI coding agent instructions (Copilot, Claude Code) | [`AGENTS.md`](./AGENTS.md) (`CLAUDE.md` just points here) |
| Build-fix log / what's verified vs. not yet tested on hardware | [`FLASHING.md`](./FLASHING.md) |
| Academic report | [`Minor_project_Mid_term_diffence.md`](./Minor_project_Mid_term_diffence.md) |
| Archived one-off code review notes | [`docs/CODE_REVIEW_GRAPH.md`](./docs/CODE_REVIEW_GRAPH.md) |

## Tools

- **Framework:** ESP-IDF v5.x project files; the latest documented Windows builds were verified with a local ESP-IDF v5.5.5 install via `forest_monitor/build_win.bat`
- **ML:** TensorFlow Lite Micro + Edge Impulse-style INT8 quantization
- **Radio:** SX1278 LoRa, RadioLib
- **Sensors:** INMP441 (I2S), MQ-135 (ADC), DHT22 (GPIO)
- **DHT22 driver:** project-authored bit-banged driver in `forest_monitor/main/sensors/dht22.cpp`
