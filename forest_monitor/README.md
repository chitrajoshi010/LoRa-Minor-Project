# Forest Monitor — Unified ESP-IDF Firmware

Single ESP-IDF project that merges the two previously separate firmwares into
one codebase with a build-time **role**:

| Role | `CONFIG_LDSE_ROLE` | Target | Board | What it runs |
| ---- | ------------------ | ------ | ----- | ------------ |
| Gateway | `0` | `esp32` | ESP32-WROOM-32 | LDSE sink, sync source, CSV logger + Wi-Fi/Firebase uploader |
| Relay | `1` | `esp32s3` | ESP32-S3 | LDSE forwarder + MQ-135/DHT22 fire scoring + acoustic classifier |
| Node | `2` | `esp32s3` | ESP32-S3 | Acoustic classifier + sensors + LDSE uplink |

It combines:

- **LDSE** multi-hop LoRa protocol (SX1278 @ 433 MHz via RadioLib), ported from
   the Arduino/PlatformIO project to ESP-IDF (`main/ldse/`).
- **Acoustic classifier** (I2S INMP441 -> mel-spectrogram -> TFLite Micro, 5
   classes: Axe, Chainsaw, Gunshot, Handsaw, Background) running as a FreeRTOS
   task on the relay and node (`main/acoustic/`).
- **Fire scoring** from MQ-135 (gas/CO2 proxy) + DHT22 (temp/humidity) on relay
   and node (`main/sensors/`).
- **Gateway networking** (`main/net/`) for non-blocking Wi-Fi station mode and
   best-effort Firebase RTDB upload from the gateway role.

## Layout

```ini
forest_monitor/
├── CMakeLists.txt
├── sdkconfig.defaults            # common (FreeRTOS 1 kHz, main stack)
├── sdkconfig.defaults.esp32      # gateway pins (MOSI23/MISO19/DIO1 26), role 0
├── sdkconfig.defaults.esp32s3    # S3 pins (MOSI8/DIO1 9), role 2 default
└── main/
    ├── CMakeLists.txt            # per-role sources + REQUIRES
    ├── Kconfig.projbuild         # CONFIG_LDSE_ROLE + radio pin configs
    ├── idf_component.yml         # radiolib, esp-tflite-micro, esp-dsp
    ├── main.cpp                  # role dispatcher (app_main)
    ├── payload.h                 # shared node DATA/FIRE payload (dependency-free)
    ├── ldse/                     # ported LDSE library (+ LdseCompat.h shim)
    ├── net/                      # gateway Wi-Fi manager + Firebase uploader
    ├── roles/                    # gateway.cpp / relay.cpp / node.cpp
    ├── sensors/                  # mq135, dht22, fire_scoring (roles 1 & 2)
    └── acoustic/                 # audio_capture, spectrogram, classifier (roles 1 & 2)
```

## Wiring

### SX1278 LoRa (per board)

| Signal | Gateway (WROOM-32) | Relay / Node (S3) |
| ------ | ------------------ | ----------------- |
| NSS  | GPIO18 | GPIO18 |
| SCK  | GPIO13 | GPIO13 |
| MOSI | GPIO23 | **GPIO8** |
| MISO | GPIO19 | GPIO21 |
| DIO0 | GPIO4  | GPIO4  |
| RST  | GPIO14 | GPIO14 |
| DIO1 | GPIO26 | **GPIO9** |

> **GPIO conflict resolution (relay & node):** the INMP441 mic uses GPIO17 (BCK) and
> GPIO16 (DIN), which collided with the classic LDSE MOSI=17 / DIO1=16. The S3
> radio is remapped to **MOSI=8, DIO1=9** (via Kconfig / sdkconfig.defaults.esp32s3).

### Sensors (relay & node, S3)

| Peripheral | GPIO | Notes |
| ---------- | ---- | ----- |
| INMP441 mic BCK | 17 | I2S (relay & node) |
| INMP441 mic WS  | 15 | I2S (relay & node) |
| INMP441 mic DIN | 16 | I2S (relay & node) |
| MQ-135 AOUT | 2 | ADC1_CH1, esp_adc oneshot |
| DHT22 DATA | 12 | project-authored bit-banged driver in `main/sensors/dht22.cpp` |
| Sleep MOSFET gate/base | 10 | `CONFIG_LDSE_PIN_SLEEP_GATE`; HIGH during SYNC/DATA, LOW during SLEEP (`LdseSleepGate`), cuts power to the peripheral rail on the parent-synced schedule |

## Build & flash

```sh
# Load ESP-IDF tools in each new terminal session
. "$HOME/esp/esp-idf/export.sh"

PROJECT_DIR="/path/to/LoRa-Minor-Project/forest_monitor"

# Gateway (ESP32-WROOM-32)
cd "$PROJECT_DIR" && idf.py fullclean && idf.py set-target esp32
cd "$PROJECT_DIR" && idf.py build flash monitor            # role 0 comes from sdkconfig.defaults.esp32

# Node (ESP32-S3) - role 2 is the S3 default
cd "$PROJECT_DIR" && idf.py fullclean && idf.py set-target esp32s3
cd "$PROJECT_DIR" && idf.py build flash monitor

# Relay (ESP32-S3) - override the role to 1
cd "$PROJECT_DIR" && idf.py fullclean && idf.py set-target esp32s3
cd "$PROJECT_DIR" && idf.py -D CONFIG_LDSE_ROLE=1 build flash monitor
# (or: idf.py menuconfig -> "LDSE forest-monitor configuration" -> role = 1)
```

On Windows, use `build_win.bat gateway`, `build_win.bat relay`, or
`build_win.bat node`; the script targets a local ESP-IDF install and handles
the environment setup for you.

Switching target regenerates `sdkconfig` from the matching `sdkconfig.defaults.*`.
Do a clean build (`idf.py fullclean`) when changing target.

## Node DATA payload

`main/payload.h`, 37 bytes (<= 48 B LDSE payload):

```c
struct NodePayload {
    uint8_t classIdx;          // acoustic argmax (0..4)
    float   confidence[5];     // per-class probabilities
    float   temperature;       // deg C  (DHT22)
    float   humidity;          // % RH   (DHT22)
    float   gas;               // mV     (MQ-135)
    float   fireScore;         // weighted fire-risk
};
```

Sent as `MSG_DATA`; the node sends `MSG_FIRE_ALERT` instead when ANY of these
independent triggers fire (see `fire_score_evaluate()` in `fire_scoring.cpp`):
the combined weighted score reaches 3.0, the DHT22-only (temp+humidity)
sub-score reaches 3.0 on its own, or the MQ-135-only (gas) sub-score reaches
3.0 on its own. `fireScore` in the payload is whichever of the three scores
is currently highest. Normal path is node -> relay (Prc1). If the relay is
congested (`ShouldBypassToGateway`) or unreachable, the node bypasses directly
to the gateway on Puc.

The gateway logs every received packet as CSV:

```csv
DATA|FIRE,epoch_ms,origin,src,hops,seq,rssi,layer,class,temp,hum,gas,fire,count
```

## Notes / calibration

- __Fire-score baselines__ in `sensors/fire_scoring.cpp` are placeholders -
   recalibrate `CAL_*` constants to your site's stable readings and spread.
- **DHT22 implementation:** `sensors/dht22.cpp` is a self-contained
   bit-banged AM2302/DHT22 driver; there is no external DHT managed component
   to install.
- __Acoustic model__ is the 5-class model (tree_falling removed vs. the
   7-class design in the mid-term report) - reconcile in the report or retrain.
