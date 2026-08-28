# Explain.md — How the Forest Monitor Project Works

This file explains the project in plain, simple language, and walks through
exactly what happens **from the moment you flash a board to it running in the
forest**. It is meant for anyone (student, teacher, examiner) who wants to
understand what the system does without reading all the code.

> Every fact below was checked directly against the source code in
> `main/` (not just against the other docs) as of this revision, so it
> should match what your hardware actually does.

## 1. What is the project about?

It is a **forest fire and illegal-logging detection system** made of three
small ESP32 computers that talk to each other wirelessly (using radio / LoRa).

Each computer has a "job" (role):

| Role | What it does |
| ---- | ------------ |
| **Gateway** (ESP32-WROOM-32) | The base station. Always powered. Collects all data, prints it to a serial screen as CSV, **and forwards it over Wi-Fi to a Firebase cloud database** for a web dashboard. |
| **Relay** (ESP32-S3) | In the middle. Passes messages between the node and the gateway. Also reads its own fire sensors and runs the acoustic classifier. |
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
  (`LDSE_EPOCH_MS = 10000`): a SYNC window (2 s, `LDSE_SYNC_MS`), then a DATA
  window (6 s, `LDSE_DATA_MS`), then a SLEEP window (2 s,
  `LDSE_SLEEP_MS`). Everyone sleeps together, so battery lasts longer.
- **Time sync (FTSP).** Clocks drift, so the gateway sends a "sync beacon"
  with its time. The relay and node read it and correct their own clocks, so
  all devices agree on when SYNC/DATA/SLEEP happen (`LdseSync`, drift model
  `LDSE_DRIFT_PPM = 45.0`).
- **Routing (IRE).** Each node keeps a small list of possible "parents"
  (devices closer to the gateway). It picks the one with the highest score,
  where **`Score = α × (remaining energy %) + β × (link quality from RSSI)`**,
  with `α = β = 0.5` by default (`LdseRouting::Score()`). Link quality maps
  −30 dBm (best) .. −120 dBm (worst) RSSI onto a 0–1 scale. A lower-layer
  candidate always wins outright; ties within the same layer are broken by
  this score.
- **Congestion control.** The relay's forward queue holds up to
  `LDSE_RELAY_QUEUE_CAPACITY = 10` packets. Once it's ≥ 80%
  (`LDSE_CONGESTION_THRESHOLD = 0.8`) full, it's "congested": it switches to
  a more robust spreading factor and tells the node to send directly to the
  gateway instead (bypass), skipping the relay hop (`LdseForwarder`).
- **Channel access.** Before transmitting, a device listens for the channel
  to clear and backs off exponentially if busy:
  `delay = LDSE_BACKOFF_SLOT_MS (2 ms) × 2^attempt`, up to
  `LDSE_MAX_ACK_RETRIES = 3` attempts, then it gives up and treats it as a
  parent failure (triggers a route refresh).
- **Radio.** The actual radio is an SX1278 LoRa chip. The code in
  `LdseRadio` (via a hand-written `EspHal` on top of ESP-IDF's
  `driver/spi_master.h`) sends/receives packets and checks if the channel is
  busy before transmitting.

### B. Acoustic classifier — "listening" for illegal activity (`main/acoustic/`)

The **relay and the node** both run this (not just the node — the relay has
its own microphone too). It uses a small microphone (INMP441) to listen and
a tiny AI model (TFLite Micro) to recognise **5 sounds**:

- Axe
- Chainsaw
- Gunshot
- Handsaw
- Background (normal forest noise)

The sound is processed in steps, inside a background FreeRTOS task:

1. **Capture** — microphone samples are collected (`audio_capture`).
2. **Spectrogram** — the sound is converted into a picture of frequencies
   over time (`spectrogram`).
3. **Model** — the AI model looks at that picture and outputs a
   "probability" for each sound (`classifier`, 260 KB tensor arena).
4. The most likely sound (highest probability) becomes the current class.

The result is stored so the node/relay can send it along with the sensor
data. Crucially, the classifier task is **paused whenever its board is
asleep** — see §4 below (`classifier_set_mic_powered`) — so it never reports
a stale or garbage detection right after waking up.

### C. Fire scoring — the sensor part (`main/sensors/`)

The node and relay read two cheap sensors:

- **MQ-135** — a gas sensor (higher voltage ≈ more gas / smoke), GPIO2.
- **DHT22** — temperature and humidity, GPIO12.

The three readings are combined into a single **fire-risk score**
(`fire_scoring.cpp`) using weights from the project report:

```
score = 0.51 × gas(change)  +  0.37 × temperature(change)  +  0.12 × humidity(change)
```

During a fire, gas and temperature rise, and humidity drops — so the score
goes up. If the score is high enough (threshold 3.0), the device sends a
**FIRE_ALERT** message instead of a normal DATA message.

> **Sensor gotcha, already handled in code:** when the peripheral power rail
> is switched off during SLEEP (see §4), the MQ-135's output pin floats up
> toward the 3.3 V rail instead of reading 0 V. `mq135_read()` rejects any
> reading pinned near the rail (`MQ135_RAIL_FLOAT_MV = 3200 mV`) and returns
> "invalid" instead of a fake high-gas value; `fire_score_compute()` then
> drops that term instead of scoring garbage as a real fire signal.

## 3. Flashing each board, step by step

All three roles come from the **same firmware project** — you just build it
three times with a different role setting each time. From
`forest_monitor/` on Windows (the project's own convenience wrapper):

```powershell
# Gateway — ESP32-WROOM-32, role 0
cmd /c "build_win.bat gateway && idf.py -p COM9 flash monitor"

# Relay — ESP32-S3, role 1
cmd /c "build_win.bat relay && idf.py -p COM9 flash monitor"

# Node — ESP32-S3, role 2
cmd /c "build_win.bat node && idf.py -p COM9 flash monitor"
```

(Chain `build_win.bat` and `idf.py ... flash monitor` in the **same**
`cmd.exe` invocation — otherwise a PowerShell → child-`cmd` → PowerShell
boundary loses the ESP-IDF environment `build_win.bat` just set up, and a
plain `idf.py` afterwards fails with "command not found".)

Step by step, for each board:

1. **Plug in the correct board** for the role: the WROOM-32 dev board for
   gateway, an ESP32-S3 board for relay/node.
2. **Find its COM port**:
   ```powershell
   Get-PnpDevice -Class Ports -Status OK | Format-Table FriendlyName, InstanceId -AutoSize
   ```
   (unplug/replug to see which `COM#` appears/disappears).
3. **Before flashing the gateway**, set your Wi-Fi credentials once via
   `idf.py menuconfig` → "LDSE forest-monitor configuration" (or edit
   `sdkconfig.defaults.esp32`): `CONFIG_GATEWAY_WIFI_SSID` and
   `CONFIG_GATEWAY_WIFI_PASSWORD`. If you leave the SSID empty, the gateway
   will try to connect to nothing and never get an IP (it still works
   offline — see §7 — but you lose the Firebase dashboard).
4. **Run the build+flash command** above for that role. `build_win.bat`
   internally does `idf.py set-target` (esp32 for gateway, esp32s3 for
   relay/node) + `idf.py build`, then the chained `idf.py -p COMx flash
   monitor` writes the `.bin` and opens the serial monitor at 115200 baud.
5. **If flashing fails with `Wrong boot mode detected (0x13)`**: hold the
   BOOT button, tap RST, then release BOOT — this forces the board into
   manual download mode — and retry.
6. Repeat for the other two boards on their own COM ports. `idf.py fullclean`
   is required whenever you switch `--target` between esp32 and esp32s3 on
   the *same* checkout (stale `build/` won't reconfigure cleanly), which
   `build_win.bat`'s per-role wrapper already does for you.

## 4. What happens right after power-on (per role)

### Power rail topology (relay & node only)

```
battery → 5V regulator → 3.3V regulator ──┬── ESP32-S3  (always on)
                                           ├── SX1278 LoRa radio (always on)
                                           │
                              [MOSFET gate, GPIO10] ──┬── MQ-135 gas sensor
                                                       ├── DHT22 temp/humidity
                                                       └── INMP441 mic
```

The ESP32-S3 and the LoRa radio are wired **directly** to power and are
always on (they have to be, to keep the epoch timing and radio awake). A
single MOSFET (`CONFIG_LDSE_PIN_SLEEP_GATE`, default GPIO10) gates the power
feed to the **sensor/mic group only** — `LdseSleepGate` drives it HIGH during
SYNC/DATA and LOW during SLEEP, on the parent-synced epoch schedule. In
lockstep with that, `classifier_set_mic_powered(true/false)` pauses/resumes
the acoustic task so it never classifies floating garbage samples while
unpowered, and clears any cached (possibly stale) detection on wake. The
gateway has none of this — it has no MOSFET, no sensors, no mic; it's
mains-powered and always fully on.

### Gateway boot

1. Prints `[LDSE] Gateway (layer 0)`.
2. Calls `wifi_manager_init()` — starts connecting to your configured Wi-Fi
   **non-blocking** (returns immediately; SYNC beacons must never wait on a
   Wi-Fi handshake). Watch for `wifi_mgr: connecting to SSID:...` then
   `wifi_mgr: got IP, connected - ...` in the log once it succeeds.
3. Calls `firebase_uploader_init()` — starts a background upload task/queue
   (only compiled in if `CONFIG_FIREBASE_UPLOAD_ENABLE` is set).
4. Sets itself as the FTSP reference clock (`g_sync.Begin(0.0f)`, hop count 0).
5. Brings up the SX1278 radio on the Puc channel (433.3 MHz): prints
   `[LDSE] Radio on Puc ready` (or halts with a `Radio init FAILED` log if
   the SPI wiriLoRa-Minor-Projectng is wrong).
6. Enters its main loop: every epoch's SYNC window it broadcasts
   `LAYER_INIT` + `SYNC` beacons every 250 ms
   (`LDSE_SYNC_BEACON_PERIOD_MS`); during DATA/SLEEP windows it just listens
   for incoming packets.

### Relay / node boot

1. Prints `[LDSE] Relay` or `[LDSE] End device (node)`.
2. Starts the acoustic classifier task (`classifier_start()` — prints
   `classifier_start() failed` if the model/tensor-arena setup fails).
3. Brings up the SX1278 radio (prints `Radio init FAILED` on error), then
   `[LDSE] Relay listening on Puc` / `[LDSE] Node listening on Prc1`.
4. Has **no parent yet** — it repeatedly prints
   `[NODE] No parent yet: waiting for LAYER_INIT/SYNC` (or the relay
   equivalent) until it hears the gateway's (or a relay's) beacon.
5. On first beacon: `[REL]/[NODE] SYNC offset=... us, hops=...` (clock now
   corrected), then `Layer = N, parent = <role>(<id>)` once IRE picks a
   parent. From here on it's synced and joins the epoch schedule.

## 5. Sync & network join, in detail

1. The **gateway** broadcasts `LAYER_INIT` (advertising "I am layer 1 for
   whoever hears me") and `SYNC` (its current timestamp) every 250 ms during
   the whole 2-second SYNC window — a burst, not a single beacon, so a
   freshly-booted relay/node joins within one epoch no matter when it powers
   on.
2. The **relay** hears it, records the offset between its own clock and the
   gateway's timestamp (`LdseSync::OnReceiveSync`), becomes layer 1, and
   re-broadcasts its own `LAYER_INIT`/`SYNC` so the node can hear a beacon
   too (relay's beacon says "I am layer 2 for whoever hears me").
3. The **node** hears the relay's (or, if in range, directly the gateway's)
   beacon, corrects its own clock, and runs `LdseRouting` to pick the best
   parent by the α/β score described in §2A — always preferring a lower
   layer number, then the higher score.
4. Every epoch this repeats — clocks are periodically re-corrected because
   crystal drift (~45 ppm) means a few tens of µs of error accumulate every
   epoch.

## 6. Runtime data flow, step by step

### Epoch timeline (10 s cycle)

```
0 s         2 s                          8 s              10 s
├───────────┼────────────────────────────┼─────────────────┤
│  SYNC     │           DATA             │     SLEEP       │
│  window   │          window            │   window        │
│  (2 s)    │          (6 s)             │    (2 s)        │
└───────────┴────────────────────────────┴─────────────────┘
   ▲              ▲          ▲                  ▲
   │              │          │                  │
gateway      relay listens  node transmits    everyone's
broadcasts   on Prc1 first  ~200 ms into      sensor/mic
SYNC every   (LDSE_RELAY_    the DATA window   rail (not the
250 ms       LISTEN_MS =    (LDSE_NODE_TX_     radio/MCU) is
             2.5 s), then   OFFSET_MS),        powered down
             forwards on    then relay
             Puc            forwards
```

### Message path

1. During the DATA window, the **node** gathers its latest values: acoustic
   class + 5 confidences (from the AI), temperature, humidity (DHT22), gas
   (MQ-135), and the fire score. All of this fits into one 37-byte packet
   (`NodePayload` in `payload.h`).
2. The node transmits the packet to the relay on the data channel (Prc1,
   433.5 MHz), timed ~200 ms into the DATA window.
3. The **relay** (listening on Prc1 for 2.5 s) receives it, forwards it up to
   the **gateway** on the uplink channel (Puc, 433.3 MHz) — printed as
   `[REL] Forwarded seq=... sf=... bypass=0`.
4. The **gateway** acknowledges it (`MSG_ACK`), dumps the full decoded packet
   to serial (`---- RX PACKET ----` block), then prints one CSV line and
   enqueues it for Firebase.
5. **If the relay is congested** (queue ≥ 80% full) **or unreachable**, the
   node bypasses it and sends directly to the gateway on Puc instead —
   logged as `[NODE] Relay unreachable: bypassing to gateway`, and the
   gateway's log shows `bypass=1` for that packet's forward record. If the
   node gets no ACK back within `LDSE_ACK_TIMEOUT_MS` (1.5 s), it prints
   `[NODE] No ACK seq=...: route refresh` and re-runs parent selection.

### Gateway CSV output

For every packet the gateway prints one CSV line:

```
DATA|FIRE,epoch_ms,origin,src,hops,seq,rssi,layer,class,temp,hum,gas,fire,count
```

| Column | Meaning |
| ------ | ------- |
| `DATA`/`FIRE` | normal reading vs. fire-alert packet |
| `epoch_ms` | time within the current 10 s epoch when received |
| `origin` | the device that produced the reading (node=2, relay=1) |
| `src` | the device that physically transmitted this hop to the gateway |
| `hops` | how many LDSE hops it took to arrive |
| `seq` | per-packet sequence number |
| `rssi` | signal strength of the last hop, in dBm |
| `layer` | LDSE layer of the sender |
| `class`, `temp`, `hum`, `gas`, `fire` | decoded `NodePayload` fields |
| `count` | running count of data packets received so far |

This CSV can be opened in Excel or a script to graph temperature, gas level,
fire score, and to see which sound was detected.

## 7. Gateway → Firebase upload (cloud dashboard path)

Beyond the serial CSV log, the gateway also has a **Wi-Fi + Firebase
Realtime Database uploader** (`main/net/wifi_manager.cpp` +
`main/net/firebase_uploader.cpp`), so a web dashboard can show live data
without anyone watching the serial monitor. This only runs on the gateway —
relay and node never touch Wi-Fi.

- **Wi-Fi**: event-driven station mode. Connects on boot
  (`WIFI_EVENT_STA_START`), auto-retries with exponential backoff (1 s → 10 s
  cap) on disconnect, and disables Wi-Fi modem sleep
  (`esp_wifi_set_ps(WIFI_PS_NONE)`) so Wi-Fi housekeeping never causes jitter
  in the LDSE SYNC beacon cadence. SNTP (`pool.ntp.org`) syncs the real
  wall-clock time opportunistically once connected, so uploaded records get
  a real Unix timestamp instead of "time since boot".
- **Firebase**: every decoded `NodePayload` (DATA or FIRE) is handed to
  `firebase_uploader_enqueue()` from the gateway's receive loop — this call
  is **non-blocking**: it drops into a small 4-item queue and returns
  immediately (dropping the oldest queued item if full), so a slow/stalled
  HTTP request can never delay LDSE's SYNC/DATA timing.
- A separate background FreeRTOS task drains that queue: it signs in to
  Firebase (email/password, cached auth token refreshed ~1 min before its
  ~1 h expiry), writes a one-time `/nodes/{key}/meta` record (label/role/
  layer) per source, then `PUT`s `/nodes/{key}/latest` (overwritten every
  cycle) and `POST`s `/nodes/{key}/history` (append-only) with the full
  reading (confidences, temp/humidity/gas/fireScore, RSSI, hops, layer, seq,
  timestamp).
- If Wi-Fi is down when an item is dequeued, it's simply dropped
  (`Wi-Fi down, dropping queued upload`) — the serial CSV log is unaffected
  either way, since it's written before the Firebase handoff.
- All of this is gated behind `CONFIG_FIREBASE_UPLOAD_ENABLE` at build time;
  if it's off, `firebase_uploader_init()`/`_enqueue()` are no-ops and the
  gateway behaves exactly as it did before this feature existed (Wi-Fi
  station mode still comes up either way).

## 8. Energy saving (why nodes last long on battery)

The relay and node sleep the peripheral rail (sensors, mic) most of the
time via the MOSFET described in §4 — the ESP32-S3 and radio itself stay
awake to keep the epoch schedule and listen for the network, but the power-
hungry mic/sensor group is only on during SYNC+DATA. The code also
**estimates** battery usage (`LdseEnergy`) using a simple power model
(active ≈ 0.396 W, sleep ≈ 0.9 µA @ 3.7 V, 2000 mAh assumed capacity), so it
can report approximate battery remaining each sleep cycle
(`[NODE]/[REL] Sleep: energy=... J battery=... mAh ...`). The gateway stays
awake the whole time because it is mains-powered.

## 9. Wiring at a glance

- SX1278 LoRa radio (433 MHz) on SPI — pins differ per board (see
  `forest_monitor/README.md`).
- INMP441 microphone (relay & node): BCK=17, WS=15, DIN=16.
- MQ-135 gas sensor (relay & node): analog pin GPIO2.
- DHT22 temp/humidity (relay & node): GPIO12.
- Sleep MOSFET gate/base (relay & node): GPIO10.

> Note: the mic pins on the S3 collided with the radio pins, so the S3 uses
> MOSI=8 and DIO1=9 instead of the classic 17/16 (see `LdseConfig.h`).
> The gateway (WROOM-32) uses the classic MOSI=23/MISO=19/DIO1=26 instead,
> since it has no mic to collide with.

## 10. Troubleshooting

**Flashing / build problems**

- `idf.py: command not found` in PowerShell right after `build_win.bat`
  finished: you ran the build and the flash as two separate PowerShell
  commands. Chain them in the same `cmd.exe` invocation (see §3).
- `Wrong boot mode detected (0x13)` when flashing: hold BOOT, tap RST,
  release BOOT, then retry — this is a board/driver DTR-RTS quirk, not a
  firmware bug.
- `No module named 'click'`: the ESP-IDF Python venv is missing/mismatched.
  Re-run `export.bat`/`install.bat`, or symlink the expected venv name to
  the one that actually exists (see `FLASHING.md` for the exact commands).
- Board not appearing as a COM port: replug it and re-run
  `Get-PnpDevice -Class Ports -Status OK`.

**Relay/node never sync**

- Stuck printing `No parent yet: waiting for LAYER_INIT/SYNC` forever: check
  the gateway is actually powered and its radio init succeeded
  (`[LDSE] Radio on Puc ready`, not `Radio init FAILED` — that's usually a
  wrong SPI pin in Kconfig/sdkconfig for that board). Also check both boards
  are within LoRa range of each other.
- `Radio init FAILED` on any board: double-check the SX1278 wiring against
  the pin table in `forest_monitor/README.md`, especially that the S3
  boards use MOSI=8/DIO1=9 (not the classic 17/16, which collide with the
  mic).

**Sensor readings look wrong**

- Gas reading pinned near the ADC rail / suspiciously constant right after
  waking: expected if you catch it mid-power-up — `mq135_read()` should
  reject it automatically (§2C); if it's still happening once fully awake,
  check the MOSFET wiring on `CONFIG_LDSE_PIN_SLEEP_GATE` (GPIO10).
- A garbage/impossible acoustic classification right after a sleep→wake
  transition: shouldn't happen (`classifier_set_mic_powered` clears cached
  results on wake) — if you see it, check that `LdseSleepGate::Wake()` and
  `classifier_set_mic_powered(true)` are both actually being called
  together in your `roles/{relay,node}.cpp` build.

**Gateway not reaching Firebase**

- No `wifi_mgr: got IP, connected` line ever appears: `CONFIG_GATEWAY_WIFI_SSID`
  is probably empty or wrong — set it via `idf.py menuconfig` before
  flashing the gateway (§3).
- Wi-Fi connects but nothing shows up on the dashboard: check
  `CONFIG_FIREBASE_UPLOAD_ENABLE` is actually turned on, and that
  `CONFIG_FIREBASE_DB_URL`/`CONFIG_FIREBASE_API_KEY`/auth email-password are
  set. Look for `auth failed` or `latest push failed` warnings in the serial
  log — the underlying HTTP status/body is printed alongside them.
- The serial CSV log keeps working fine even when Wi-Fi/Firebase is fully
  broken — that log doesn't depend on the network path at all.

## 11. One-paragraph summary

> This project places battery-powered sensor nodes in a forest. Each node
> (and the relay) listens for the sounds of illegal logging (axe, chainsaw,
> gunshot) using a microphone and a tiny AI model, and reads gas/temperature/
> humidity sensors to estimate fire risk. The nodes are too far apart to
> shout directly to the base station, so they form a simple relay chain: the
> node sends its data to a relay, which forwards it to the gateway (or
> bypasses the relay if it's congested/unreachable). Everyone keeps the same
> clock via sync beacons, sleeps their sensor/mic power rail most of the time
> to save power, and the gateway both prints everything as CSV over serial
> **and** pushes it to a Firebase cloud database over Wi-Fi for a live web
> dashboard.
