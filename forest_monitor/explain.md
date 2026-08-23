# Explain.md — How the Forest Monitor Project Works

This file explains the project in plain, simple language. It is meant for
anyone (student, teacher, examiner) who wants to understand what the system
does without reading all the code.

## 1. What is the project about?

It is a **forest fire and illegal-logging detection system** made of three
small ESP32 computers that talk to each other wirelessly (using radio / LoRa).

Each computer has a "job" (role):

| Role | What it does |
| ---- | ------------ |
| **Gateway** (ESP32-WROOM-32) | The base station. Always powered. Collects all data and prints it to a computer screen as CSV. |
| **Relay** (ESP32-S3) | In the middle. Passes messages between the node and the gateway. Also reads its own fire sensors. |
| **Node** (ESP32-S3) | Out in the forest. Listens for sounds (axe, chainsaw, gunshot) with a microphone and reads fire sensors. |

One single firmware (one program) is written for all three; the *job* is
chosen at build time with a setting called `CONFIG_LDSE_ROLE`
(0 = gateway, 1 = relay, 2 = node). See `main/main.cpp`.

## 2. The three main parts

### A. LDSE — the wireless network protocol (`main/ldse/`)

LDSE (LoRa-based protocol) lets the nodes talk to the gateway over a long
distance by making messages **hop** through the relay. Instead of a loud
single long-range shout, each device speaks softly to its neighbour, and the
neighbour passes the message on — this saves battery.

Key ideas:

- **Roles and layers.** The gateway is "layer 0", the relay is "layer 1",
  and the node is "layer 2". Data flows up: node → relay → gateway.
- **Epochs (time slots).** Time is divided into repeating 10-second cycles
  (`LDSE_EPOCH_MS`): first a SYNC window (2 s), then a DATA window (6 s),
  then a SLEEP window (2 s). Everyone sleeps together, so battery lasts longer.
- **Time sync (FTSP).** Clocks drift, so the gateway sends a "sync beacon"
  with its time. The relay and node read it and correct their own clocks, so
  all devices agree on when SYNC/DATA/SLEEP happen (`LdseSync`).
- **Routing (IRE).** Each node keeps a small list of possible "parents"
  (devices closer to the gateway). It picks the best one based on signal
  strength and remaining battery (`LdseRouting`).
- **Congestion control.** If too many messages pile up at the relay, it tells
  the node to send directly to the gateway instead (`LdseForwarder`).
- **Radio.** The actual radio is an SX1278 LoRa chip. The code in
  `LdseRadio` sends/receives packets and checks if the channel is busy before
  transmitting.

### B. Acoustic classifier — "listening" for illegal activity (`main/acoustic/`)

Only the **node** runs this. It uses a small microphone (INMP441) to listen
and a tiny AI model (TFLite Micro) to recognise **5 sounds**:

- Axe
- Chainsaw
- Gunshot
- Handsaw
- Background (normal forest noise)

The sound is processed in steps, inside a background task:

1. **Capture** — microphone samples are collected (`audio_capture`).
2. **Spectrogram** — the sound is converted into a picture of frequencies
   over time (`spectrogram`).
3. **Model** — the AI model looks at that picture and outputs a
   "probability" for each sound (`classifier`).
4. The most likely sound (highest probability) becomes the current class.

The result is stored so the node can send it along with the sensor data.

### C. Fire scoring — the sensor part (`main/sensors/`)

The node (and relay) read two cheap sensors:

- **MQ-135** — a gas sensor (higher voltage ≈ more gas / smoke).
- **DHT22** — temperature and humidity.

The three readings are combined into a single **fire-risk score**
(`fire_scoring.cpp`) using weights from the project report:

```
score = 0.51 × gas(change)  +  0.37 × temperature(change)  +  0.12 × humidity(change)
```

During a fire, gas and temperature rise, and humidity drops — so the score
goes up. If the score is high enough (threshold 3.0), the device sends a
**FIRE_ALERT** message instead of a normal DATA message.

## 3. How a message travels (normal day in the forest)

1. The **gateway** broadcasts a "LAYER_INIT" + "SYNC" beacon every few
   hundred ms during the SYNC window.
2. The **relay** hears it, learns it is layer 1, corrects its clock, and
   forwards the sync beacon to the **node**.
3. The **node** hears the beacon, learns it is layer 2, and picks the relay
   as its parent.
4. During the DATA window, the node gathers its latest values:
   - acoustic class + confidence (from the AI)
   - temperature, humidity (DHT22)
   - gas reading (MQ-135)
   - fire score
   All of this fits into one 37-byte packet (`NodePayload` in `payload.h`).
5. The node transmits the packet to the relay on a data channel (Prc1).
6. The **relay** acknowledges it, then forwards it up to the **gateway** on
   the uplink channel (Puc).
7. The **gateway** acknowledges it, prints one line of CSV to the serial
   monitor, and stores counts.

If the relay is unreachable or congested, the node **bypasses** it and sends
directly to the gateway on Puc.

## 4. What the gateway outputs (CSV)

For every packet the gateway prints a line like:

```
DATA,epoch_ms,origin,src,hops,seq,rssi,layer,class,temp,hum,gas,fire,count
```

This CSV can be opened in Excel or a script to graph temperature, gas level,
fire score, and to see which sound was detected.

## 5. Energy saving (why nodes last long on battery)

The node and relay sleep most of the time. Only during the SYNC and DATA
windows do they wake the radio. The code also **estimates** battery usage
(`LdseEnergy`) using a simple power model, so it can report approximate
battery remaining. The gateway stays awake because it is mains-powered.

## 6. Wiring at a glance

- SX1278 LoRa radio (433 MHz) on SPI — pins differ per board (see README).
- INMP441 microphone (node only): BCK=17, WS=15, DIN=16.
- MQ-135 gas sensor: analog pin GPIO2.
- DHT22 temp/humidity: GPIO12.

> Note: the mic pins on the S3 collided with the radio pins, so the S3 uses
> MOSI=8 and DIO1=9 instead of the classic 17/16 (see `LdseConfig.h`).

## 7. Simple "one paragraph" summary

> This project places battery-powered sensor nodes in a forest. Each node
> listens for the sounds of illegal logging (axe, chainsaw, gunshot) using a
> microphone and a tiny AI model, and reads gas/temperature/humidity sensors
> to estimate fire risk. The nodes are too far apart to shout directly to the
> base station, so they form a simple relay chain: the node sends its data to
> a relay, which forwards it to the gateway. Everyone keeps the same clock via
> sync beacons, sleeps most of the time to save power, and the gateway prints
> everything as a table (CSV) for monitoring.
