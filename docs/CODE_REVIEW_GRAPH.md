# Forest Monitor — Code Review Graph

> **Archived point-in-time report** (moved from repo root to `docs/` — not
> living documentation; findings may be stale/already fixed, see `FLASHING.md`
> for the current verified-build status). Generated code review for
> `forest_monitor/` (the canonical unified firmware).
> Scope: correctness of the LDSE protocol role code, the acoustic pipeline, and
> the fire-scoring path. No host unit tests exist; "verified" = all 3 configs
> compile and the 3-board bench boots (gateway → relay → node).

---

## 1. What the project is

A **low-power wireless sensor network for forest fire / illegal-logging
detection**. Three physical roles are compiled from **one image** selected by
`CONFIG_LDSE_ROLE` (0=gateway, 1=relay, 2=node):

| Role | Board | Inputs | Outputs |
|---|---|---|---|
| Gateway | ESP32‑WROOM‑32 | LoRa (DATA/FIRE) | CSV over serial (current code also includes a best-effort Wi‑Fi/Firebase upload path) |
| Relay | ESP32‑S3 | LoRa + MQ‑135 + DHT22 | Forwarded LoRa + local FIRE alert |
| Node | ESP32‑S3 | LoRa + INMP441 mic + MQ‑135 + DHT22 | LoRa DATA/FIRE |

Pipeline: mic → spectrogram → TinyML inference (Axe/Chainsaw/Gunshot/Handsaw/
Background) **and** MQ‑135/DHT22 → `fire_score` → `NodePayload` (37 B) →
LDSE multi‑hop LoRa → gateway CSV.

---

## 2. Dependency / data-flow graph

```
                    ┌────────────────── main.cpp ──────────────────┐
                    │          app_main() → one ldse_{role}_main() │
                    └───────┬───────────┬───────────┬──────────────┘
                        role 0         role 1        role 2
                            │              │             │
                    gateway.cpp      relay.cpp       node.cpp
                            │              │             └──┬──────────┐
                            │              │                │    classifier.cpp
                            │              │                │        │
                            │         ┌────┴─────┐      ┌───┴───┐  audio_capture.cpp
                            │         │ sensors/ │      │ sensors/│  spectrogram.cpp
                            │         │  relay   │      │  node   │  model_data.h
                            └────┬─────┴────┬─────┘      └───────┘  mel_filterbank.h
                                 │          │
                                 │   LdseForwarder ── CAD + backoff ──► LdseRadio
                                 │   LdseRouting    ── IRE parent ────► (RadioLib SX1278)
                                 │   LdseSync       ── FTSP ──────────► EspHal(SPI2)
                                 │   LdseEnergy     ── battery model
                                 └── LdsePacket / LdseEpoch / LdseConfig
                                                       ▲
                                    payload.h ← shared, dependency-free
```

Cross-cutting rule enforced by `main/CMakeLists.txt`: the gateway image compiles
**only** `ldse/*` + `roles/gateway.cpp + payload.h` — never sensors or
classifier — keeping the WROOM‑32 image lean and `payload.h` dependency-free.

---

## 3. Review findings by risk

### HIGH

**H1. Relay fires its own alert on the DATA channel collision window — relays
and nodes both sample sensors at the *same* phase, but the relay's ACK and the
node bypass can collide on Puc.**
`relay.cpp:117` sends `MSG_ACK` immediately after `Enqueue`, while `node.cpp`
sits in `WaitForAck` on Prc1. The relay ACK is sent on whatever channel the
radio was tuned to (Prc1 during phase A) — good — but `DrainQueue()` then
tunes to Puc. There is no per-transmit frequency isolate between the ACK and
the subsequent drain. Low impact for a 1‑relay demo, but unscalable and
unprotected by CAD.

**H2. No validation of the `Encode/Decode` frame beyond magic+version.**
`LdsePacket.h:90` trusts `pkt.type` and `pkt.payloadLen` on RX. Nothing checks
that `payloadLen ≤ sizeof(NodePayload)` before the gateway `memcpy`s it into a
stack struct (`gateway.cpp:77`). A malformed sender (or corruption that
survives LoRa CRC) can over-read `payload`. Functionally bounded by
`LDSE_PAYLOAD_MAX=48` ≥ 37, so no overflow today — hardening recommended.

### MEDIUM

**M1. Fire-score baselines are hardcoded placeholders** (`fire_scoring.cpp`).
All tuned for a *different* site (`GAS_MEAN=800 mV`, 25 °C, 60 %RH). Until
field-calibrated, `fireScore ≥ 3.0` may never trigger (false negatives) or
trigger spuriously. The code itself flags this; it's a documented deploy risk,
not a codeg bug — but the alert decision is the project's safety path.

**M2. `NodePayload` uses `float confidence[5]` (20 B) per packet.**
Only the argmax `classIdx` is consumed on the gateway. Carrying all 5 floats
doubles payload size and airtime (energy = TX-dominant). Consider sending only
`classIdx` + top-1 confidence to save ~16 B/packet. **Constraint:** `payload.h`
must stay dependency-free and `<48 B` — this is compatible and recommended.

**M3. Energy percentage truncation.**
`node.cpp:129` / `relay.cpp:171`: `GetBatteryMouth()/CAPACITY*100` cast to
`uint8_t`. `LDSE_BATTERY_CAPACITY_MAH` is a constant 2000 mAh, so `energyPct`
can read 100% forever unless the model actually decays — the IRE parent score
(battery-aware) never meaningfully diverges in the demo. Fine as a model, but
it's a *display* value, not a measured one.

**M4. `GetWindow` uses `(int64_t)nowMs + phaseOffsetMs` then truncates to
`uint32_t`.**
`LdseEpoch.h:31`. Safe while `nowMs` is small and offsets < 2^31, but a large
`phaseOffsetMs` from a mis-synced child can wrap oddly. For a 32‑bit uptime
(49.7 days) system this is the classic millis-wraparound risk; the epoch math
is correct modulo, but the offset sign handling (`-offset/1000` in node.cpp:58)
has no documented clamp.

### LOW

**L1. Duplicated epoch/sync logic** between `relay.cpp` and `node.cpp` (the
`!g_synced` pre-loop and the SYNC-window poll are near-identical). Extract a
shared helper to reduce drift risk.
**L2. `printf` in ISR-adjacent hot loops** (gateway CSVs on Puc receive path) —
can stall the loop if the USB monitor backpressures; the WDT yield
(`vTaskDelay(1)`) mitigates.
**L3. Magic model labels** duplicated as `"Axe",...` in `classifier.cpp:33`
must stay in sync with `model_data.h` training order — add a static_assert-free
comment (already present) and keep it.
**L4. `LDSE_BROADCAST = 0xFF`** vs `energyPct` stored in the same byte region —
no collision in practice since broadcast packets carry no energy payload.

---

## 4. Test-coverage status

No host test suite (`AGENTS.md`). Coverage is implicitly:
- **Build matrix** = integration smoke test (3 targets × role). This is the
  strongest guard for the `CMakeLists.txt` role matrix (`payload.h` size).
- **No unit tests** for: `LdseEpoch::GetWindow` boundary math, `LdsePacket`
  round-trip (`Encode`↔`Decode`), `LdseForwarder` ring-buffer wrap, CAD/backoff,
  `fire_score_compute`, spectrogram INT8 quantization, classifier confidence
  argmax.

### Suggested tests (highest value first)
1. `LdsePacket` round-trip: `Encode`→`Decode` for every message type; assert
   fields and that `payloadLen` is clamped at 48 and 37.
2. `LdseEpoch::GetWindow`/`RemainingInWindow` across epoch boundaries and
   negative/positive `phaseOffsetMs`.
3. `LdseForwarder` enqueue-to-capacity, dequeue wrap, congestion flag toggle.
4. `fire_score_compute` at and around `FIRE_ALERT_THRESHOLD` with calibrated
   baselines (currently placeholder constants).
5. Spectrogram: known sine tone → expected mel-band energy / dominant class.

---

## 5. Merge recommendation

**The code is well-scoped, clearly structured, and the build-time role split is
elegant.** Safe to merge as a demo. Before field deployment, address:

1. Field-calibrate `fire_scoring.cpp` baselines (M1) — the safety path.
2. Add decode-length validation in gateway/relay RX (H2).
3. Shrink payload to classIdx + top-1 confidence (M2) to reduce airtime.

No blocker for the mid-term/academic demo. Recommend merging after H1/H2 review
and at least a `LdsePacket` round-trip test.