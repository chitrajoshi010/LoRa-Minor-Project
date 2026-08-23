---
name: gateway-wifi-integration
description: Integrate, extend, or debug Wi-Fi station connectivity on the forest_monitor gateway role (ESP32-WROOM-32, CONFIG_LDSE_ROLE=0). Use this skill whenever the task touches wifi_manager.h/.cpp, the gateway's connection to a phone hotspot or router, Wi-Fi Kconfig options (CONFIG_GATEWAY_WIFI_SSID / CONFIG_GATEWAY_WIFI_PASSWORD), or wiring the gateway's CSV/DATA|FIRE stream onward to Firebase or any other HTTP backend. Also use it when adding networking to a *new* role (e.g. if relay or node ever needs Wi-Fi) — the same non-blocking-vs-LDSE-timing constraints apply.
---

# Gateway Wi-Fi Integration

Scope: `forest_monitor/` (ESP-IDF, C++17). This skill governs how network
connectivity is added to the **gateway** role only (`CONFIG_LDSE_ROLE=0`,
ESP32-WROOM-32). Relay and node roles do not carry Wi-Fi — keep it that way
unless the user explicitly asks otherwise, since it costs power budget that
`main/sensors/fire_scoring.h` and the LDSE energy model (`LdseEnergy.h/.cpp`)
assume those roles don't have.

## Why this is its own skill

The gateway has a hard real-time constraint: it must broadcast an LDSE SYNC
beacon every 250 ms during the SYNC window (see `ARCHITECTURE.md` §5.3), and
the whole mesh's time sync depends on that cadence staying tight. Wi-Fi
connect/reconnect on ESP32 is not free — DHCP, TLS handshakes, and modem
power-save all introduce CPU/radio jitter that can visibly disturb SYNC
timing if done carelessly. Every step below exists to keep Wi-Fi fully
decoupled from the LDSE loop.

## Non-negotiable constraints

1. **Wi-Fi init must be non-blocking.** `wifi_manager_init()` starts the
   connection and returns immediately. Never call it in a way that blocks
   on `WIFI_CONNECTED_BIT` before entering `ldse_gateway_main()`'s main loop
   — the gateway must start broadcasting SYNC on schedule with or without
   an IP yet.
2. **Always call `esp_wifi_set_ps(WIFI_PS_NONE)`** right after
   `esp_wifi_start()`. Modem sleep is the main source of jitter risk.
3. **Reconnect with backoff, not a tight retry loop.** Cap backoff (10 s is
   the existing default in `wifi_manager.cpp`) — a hotspot that's briefly
   out of range shouldn't spin the radio.
4. **Credentials via Kconfig, never hardcoded in source.** Mirrors the
   existing `CONFIG_LDSE_ROLE` pattern in `Kconfig.projbuild` — keeps
   secrets out of git and lets the SSID/password be set per-deployment with
   `idf.py menuconfig` without touching code.
5. **Gate any network I/O (HTTP/Firebase push, NTP, etc.) on
   `wifi_manager_is_connected()`.** Never assume connectivity — the gateway
   must keep functioning (CSV logging over serial, LDSE forwarding) even if
   the hotspot is down or out of range.

## Files this touches

| File | Change |
|---|---|
| `main/net/wifi_manager.h` | New. Public API: `wifi_manager_init()`, `wifi_manager_is_connected()`. |
| `main/net/wifi_manager.cpp` | New. STA mode, event-driven connect/reconnect, backoff. |
| `main/Kconfig.projbuild` | Add `CONFIG_GATEWAY_WIFI_SSID` / `CONFIG_GATEWAY_WIFI_PASSWORD`, scoped `depends on LDSE_ROLE = 0`. |
| `main/CMakeLists.txt` | Add `net/wifi_manager.cpp` to gateway's `SRCS`; add `esp_wifi`, `esp_netif`, `esp_event` to gateway's `REQUIRES` (role 0 row in the role→module matrix). |
| `main/roles/gateway.cpp` | Call `wifi_manager_init()` once, at the top of `ldse_gateway_main()`, before the LDSE init/loop. |

Do not add these deps to the relay or node `REQUIRES` — role-based source
selection in `CMakeLists.txt` (`ARCHITECTURE.md` §5.1) exists specifically
to keep non-gateway images lean; don't undo that.

## Integration steps

1. Add `main/net/wifi_manager.{h,cpp}` (event-driven STA connect, backoff
   capped at 10 s, `esp_wifi_set_ps(WIFI_PS_NONE)`).
2. Add the two `CONFIG_GATEWAY_WIFI_*` Kconfig options, scoped to
   `LDSE_ROLE = 0`.
3. Wire `net/wifi_manager.cpp` into the gateway's `SRCS`/`REQUIRES` block in
   `main/CMakeLists.txt`.
4. In `roles/gateway.cpp`, call `wifi_manager_init()` before entering the
   LDSE loop. Do not wait on it.
5. `idf.py menuconfig` → set SSID/password under "Gateway Wi-Fi" → save.
6. `idf.py build flash monitor` — confirm in logs:
   - `wifi_mgr: connecting to SSID:<name>` shortly after boot
   - `wifi_mgr: got IP, connected` once the hotspot accepts it
   - SYNC beacons (`ARCHITECTURE.md` §5.3 cadence) keep firing on schedule
     throughout — this is the check that matters most, since a working Wi-Fi
     connection that stalls SYNC timing is a regression, not a success.
7. Pull the hotspot out of range mid-run and confirm reconnect-with-backoff
   in the logs, and that CSV/DATA|FIRE logging over serial never stops.

## Known follow-on work (not yet implemented)

Pushing the gateway's decoded `DATA|FIRE` CSV rows to Firebase Realtime
Database requires a REST client over `esp_http_client` — the Arduino
`Firebase_ESP_Client` library used in the legacy `ESP32/` sketch does not
build under ESP-IDF/CMake, so it cannot be reused here. That upload leg
(auth token handling, `/nodes/{id}/latest` + `/nodes/{id}/history` writes
per the schema in `README.md`) is a separate task — gate it on
`wifi_manager_is_connected()` and keep it out of the LDSE-timing-critical
path the same way this skill keeps Wi-Fi connect itself out of it.

## Gotchas carried over from `AGENTS.md` / `ARCHITECTURE.md` §6

These aren't Wi-Fi-specific but apply to any gateway change: role loops
must end each iteration with `vTaskDelay(pdMS_TO_TICKS(1))` to feed the
watchdog; don't force `CONFIG_SPIRAM=y` on boards without responding
PSRAM; the gateway's WROOM-32 image must stay dependency-free of
sensors/TFLite (per `payload.h`'s design) — Wi-Fi additions don't change
that, but double-check a Wi-Fi-driven refactor hasn't accidentally pulled
in sensor or acoustic headers via a shared include.