# LightSync

Client and Server for controlling addressable LED strips (WS2812B) over WiFi.

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│  SyncClient (Rust / eframe/egui, Windows)               │
│  • Screen capture → pixel mapping                       │
│  • UDP → LEDServer (raw RGB / DDP)                      │
└──────────────────────┬──────────────────────────────────┘
                       │ UDP
┌──────────────────────▼──────────────────────────────────┐
│  LEDServer (C++ / Raspberry Pi Pico W)                  │
│                                                         │
│  Boot Flow:                                             │
│    1. Load flash config (WiFi + effects)               │
│    2. Valid config → STA mode → connect to WiFi        │
│    3. No config / connect fail → AP mode + captive     │
│       portal                                            │
│                                                         │
│  Protocols:                                             │
│    • Raw UDP  port 5005 — direct RGB pixel frames      │
│    • DDP      port 4048 — Open Pixel Control protocol  │
│    • Music    port 5006 — FFT spectrum visualization   │
│                                                         │
│  Effects Engine (autonomous, 30 FPS):                   │
│    Solid · Rainbow · Pulse · Chase · Sparkle            │
│    Theater Chase                                        │
│                                                         │
│  AP Mode Services:                                      │
│    • Captive DNS (port 53) — redirects all queries      │
│    • DHCP server — assigns IPs to clients               │
│    • HTTP server (port 80) — captive portal             │
│        GET  /       → WiFi provisioning form            │
│        POST /connect → save credentials                 │
│        GET  /settings → effects settings form           │
│        POST /settings → save effect config              │
│                                                         │
│  Discovery: mDNS → _lightsync._udp.local               │
└─────────────────────────────────────────────────────────┘
```

## LEDServer Features

### LED Engine
- 288 WS2812B pixels driven via PIO (PicoLed, GRB format)
- 100 Hz frame refresh (~10 ms cycle)
- Shared `led_buffer[]` across all protocols — last-write-wins

### WiFi & Networking
- **STA mode**: Connects to stored WiFi, advertises via mDNS
- **AP mode**: Broadcasts `LightSync` (192.168.4.1) with captive portal
- **Captive DNS**: UDP port 53 — answers every query with 192.168.4.1
- **DHCP server**: Assigns 192.168.4.2–192.168.4.9 to AP clients

### Protocols
| Protocol | Port | Description |
|----------|------|-------------|
| Raw UDP | 5005 | Direct RGB frames (1024 bytes = 341 pixels) |
| DDP | 4048 | Open Pixel Control DDP (24-bit RGB, frame offset) |
| Music Sync | 5006 | FFT spectrum bands (up to 64 bands) |

### Effects Engine
6 autonomous effects with configurable parameters:

| Effect | Description |
|--------|-------------|
| Solid | Single primary color |
| Rainbow | 256-entry color wheel, per-pixel hue offset |
| Pulse | Quadratic sine wave breathing (inhale/exhale) |
| Chase | Moving foreground blob over background color |
| Sparkle | Random sparkle overlay on background |
| Theater Chase | Alternating on/off stripes, moving |

Parameters: speed (1–255), brightness (0–255), primary color, secondary color.

Modes:
- **CLIENT** — effects pause while a DDP/UDP client is sending data; resume after 5 s silence
- **AUTO** — effects run continuously regardless of client activity

### Config Storage
- Stored in last 4 KB flash sector (Fletcher-16 checksum)
- Persists: SSID, password, effects mode, effect ID, speed, brightness, colors
- Survives reboots; erased when no valid config is present

### HTTP Captive Portal
- **GET /** — WiFi provisioning form (SSID + password)
- **POST /connect** — saves credentials to flash, redirects to `/connected`
- **GET /settings** — effects settings form with live sliders and color pickers
- **POST /settings** — saves effect parameters to flash, applies immediately

### mDNS Service Discovery
- Registers `lightsync-XXXX.local` (based on RP2040 unique board ID)
- Service: `_lightsync._udp.local` — advertises capabilities: raw, ddp, effects, music

## SyncClient (Rust, Windows)

- eframe/egui desktop application
- Screen capture → pixel location mapping → LED strip topology
- UDP client sending raw RGB frames to LEDServer
- Effects: solid, replicate (screen capture), pixel locate

## Build

### LEDServer firmware
```bash
.claude/skills/run-ledserver/build.sh build LEDServer
```

### LEDServer native tests
```bash
cd LEDServer/test_tdd && cmake -B build && cmake --build build && ctest --test-dir build
```

### SyncClient
```bash
cd SyncClient && cargo build
```

## Known Issue: Pico W WiFi Connect Hang ("Wedge") — IN PROGRESS

> **Status: connect-stage wedge fixed and verified on hardware; remaining blocker is a
> STA post-connect crash in mDNS.** Workaround per phased plan: mDNS currently bypassed.

### Symptom
- Firmware boots, then **hangs indefinitely** during `cyw43_arch_init()`'s WiFi-chip
  SDPCM handshake (intermittent — some boots succeed, most hang).
- USB stays enumerated as a CDC device (`2e8a:000a`) but produces **zero serial output**
  and **ignores reboot/bootsel control transfers** (picotool reboot does nothing).
- Root cause: the main thread is stuck in an **unbounded
  `dma_channel_wait_for_finish_blocking()`** inside `cyw43_bus_pio_spi.c`
  ([pico-sdk#2186](https://github.com/raspberrypi/pico-sdk/issues/2186)). With the main
  thread wedged, `tud_task()` never runs, so `stdio_usb` never flushes (its output path
  only writes while the CDC host is connected) and control transfers go unserviced.

### Fixes already built and verified (wedge / connect stage)
1. **`patches/cyw43_bus_pio_spi_timeout.patch`** — replaces the three unbounded DMA
   waits in `cyw43_bus_pio_spi.c` with a bounded 10 ms timeout that aborts the SPI
   transfer (and returns an error) instead of hanging forever. Also bounds the TX-stall
   wait and switches `cyw43_delay_ms` → `sleep_ms`. Generated to match SDK 2.3.0.
2. **`patches/cyw43_ll_ioctl_pacing.patch`** — 10 ms pacing instead of the 1 ms
   `CYW43_DO_IOCTL_WAIT` in the `cyw43_ll.c` ioctl wait loop (another pico-sdk#2186
   workaround, applied on top).
3. **`src/boot_flow.c` double-init fix** — `cyw43_arch_init()` is not idempotent; it is
   now called exactly once at the top of `boot_flow_run()` (STA failure falls through to
   AP reusing the initialized driver). Previously a second init hard-asserted.

Both patches are applied to the SDK at firmware build time by `build.sh` (via `git apply
-p0` with **precise** markers — a loose `grep '10000'` silently skipped the pacing patch,
now fixed), so they survive container restarts.

### Current state
- **Verified on hardware (2026-08-09):** with mDNS bypassed, the Pico boots, joins STA
  (`wifi_connect rc=0`, IP `192.168.0.244`), starts raw UDP 5005 / DDP 4048 / music 5006,
  and the main loop runs at a **100 Hz** cadence (`W%u\n` every 10 ms, `HB:%u\n` every 50).
  `ping` → **0% loss**. Phase-1 "serve on plain IP" is working and ready for the WLED IP test.
- **Verified on hardware (2026-08-11):** Phase 2.5 done — the WS control fix is live. A two-peer
  WS test against `192.168.0.244` confirmed both peers receive the state broadcast after a TEXT
  change and after a `POST /json/state` (7/7 checks). The WLED app's controls should now stay in
  sync instead of reverting.
- **Verified on hardware (2026-08-10):** Phase 2 done — `httpd` now starts in STA mode on
  port 80 and serves the **WLED JSON API** at `192.168.0.244` (`GET/POST /json/state`
  works — POST drove the strip green @ bri 128). Initial `/json/info` build was wrapped
  (`{"info":{...}}`) and hid `brand` from the WLED app's recognition check ("Could not
  connect"); the per-resource routes are now **unwrapped** (see Phase 2 below), so the
  WLED app recognises the device by entering `192.168.0.244` manually.
- **Main-loop hang fixed — root cause confirmed:** the old `main_loop_body()` used
  `dwt_read_cycles()` (DWT `CYCCNT`) for its 10 ms deadline. In this build DWT `CYCCNT`
  reads **0** (every heartbeat logs `total_cycles=0`), so `deadline = 0 + 1330000` and the
  condition `dwt_read_cycles() < deadline` (`0 < 1330000`) was **true forever** — the loop
  never exited, so the post-loop `W%u\n` markers never printed. Fix in `src/LEDServer.cpp`:
  the deadline now comes from `time_us_64()` (the system timer, which always advances), and
  `wfi` was replaced with `sleep_us(deadline - now)` so the core still sleeps between network
  wake-ups but the system-timer alarm guarantees a wake on the deadline (idle effects were
  otherwise starved to <1 FPS). **Follow-up:** `dwt_init()` / `CYCCNT` itself is still broken
  (reads 0; reason not yet established), so `cpu_load` reports 0% — the loop no longer depends
  on it, but the metric is dead.
- **STA post-connect mDNS crash (phase 3):** the log still stops right after
  `[MAIN] STA: mdns init...` when mDNS is enabled. Crash is **inside `mdns_service_init()`**
  (one of: `mdns_resp_init`, the netif walk, `mdns_resp_add_netif`, `mdns_resp_add_service`).
- `mdns_service_init()` is **currently commented out** in `src/LEDServer.cpp` (phase 1) so
  STA serves on a plain IP while the mDNS crash is investigated separately.

### Recovery plan: reconnect STA in phases (documented 2026-08-09)

**Phase 1 — Bypass mDNS, serve on plain IP** ✅ *done 2026-08-09*
- Comment out `mdns_service_init()` in `src/LEDServer.cpp` STA path so the firmware
  reaches the main loop and serves **raw UDP 5005 / DDP 4048** on its DHCP IP.
- Verify on hardware: serial shows `Server running at 192.168.0.244` + `W%u\n` heartbeats
  at 100 Hz + `ping` 0% loss. **Verified** (after the main-loop DWT hang fix).

**Phase 2 — Enable HTTP server in STA mode (WLED JSON API)** ✅ *done 2026-08-10*
- `httpd` now starts in the STA branch of `src/LEDServer.cpp` (previously AP-captive-portal-only),
  bound to `INADDR_ANY`, so it serves the STA netif. `httpd_set_device_ip()` reports the DHCP IP.
- New **WLED-compatible JSON endpoints** in `src/httpd.c`. Real WLED serves the
  object **unwrapped** on the per-resource routes (the WLED app reads
  `"brand":"WLED"` at the **top level** of `/json/info` to recognise the device):
  - `GET /json/info` → `{"ver":...,"brand":"WLED","name":"LightSync","ip":"<ip>",...}`
    — **unwrapped**; `"brand":"WLED"` at the top level is what the app's recognition
    check requires.
  - `GET /json/state` → `{"on":..,"bri":..,"seg":[{"col":[[r,g,b]],...}]}` — **unwrapped**.
  - `GET /json` → combined `{"state":{...},"info":{...}}` (the one route that IS wrapped,
    matching real WLED).
  - `POST /json/state` → accepts `{"on":true,"bri":255,"seg":[{"col":[[r,g,b]]}]}`; applies it to the
    strip (brightness + static colour) and echoes the new state back.
- **Bug fixed 2026-08-10:** the original Phase-2 build served `/json/info` wrapped as
  `{"info":{...}}`, which hid `brand` from the app's top-level recognition check — the
  WLED app kept retrying `GET /json/info` and reported "Could not connect". Unwrapped the
  per-resource routes; app now recognises the device at `192.168.0.244`.
- Verify: `curl http://<pico-ip>/json/info` (expect top-level `"brand":"WLED"`, **no**
  `{"info":` wrapper), `POST /json/state` drives the strip, `GET /settings` still serves.
  **Verified on hardware 2026-08-10** at `192.168.0.244`. The WLED app connects by
  entering the IP manually (no mDNS discovery).

**Phase 2.5 — WebSocket control channel + state broadcast (the WLED app control fix)** ✅ *done 2026-08-11*
- The WLED Android app **connected** to `192.168.0.244` (user confirmed) but its sliders / colour
  wheel **did not control the strip**: the firmware applied the state locally but never broadcast it
  back. Real WLED's `updateWS()` broadcasts the full state JSON to every connected WS client after
  any change; the app sends *optimistic* state over the socket, then waits for that broadcast to
  confirm — without it the UI reverts and the controls look dead.
- **RFC 6455 WebSocket endpoint added** in `src/httpd.c` (`GET /ws` → 101 upgrade; masked client
  frames → unmasked server frames; PING→PONG, TEXT apply, CLOSE echo).
- **State broadcast (`updateWS()` equivalent):** a peer table (`g_ws_peers[8]`) tracks every open
  WS connection. After any state change — a WS TEXT frame **or** a `POST /json/state` — the server
  wraps the full state JSON in a WS TEXT frame and writes it to **every** peer. Peers are removed
  on teardown / peer-close / `tcp_err`. Covered by native test H44
  (`test_ws_broadcast_state_to_peers`, 44/44 green) with a test-only
  `httpd_test_ws_reset_peers()` reset so the shared peer table stays deterministic across tests.
- **Verified on hardware 2026-08-11:** a two-peer WS test against `192.168.0.244` showed both
  peers upgrade (101 + RFC accept), receive the broadcast after a TEXT change on the other peer
  (`{"on":true,"bri":150,...,"seg":[{"id":0,"start":0,"stop":288,...}]}`), and receive it again
  after a `POST /json/state` (bri:99) — **7/7 checks passed**, 0 failures. Ready for the user to
  re-test control in the WLED app.
- **Per-LED / per-segment note:** the WLED JSON path currently drives **solid colour only**
  (`on`, `bri`, first `col` triplet). WLED app *per-LED* (segment `start`/`stop` + per-segment
  colours) is not parsed yet. True per-pixel control already exists via the **DDP protocol**
  (port 4048, what SyncClient uses). WLED-style per-LED from the app would extend
  `parse_wled_state`/`apply_wled_state` to the `seg[]` model.

**Phase 3 — Fix mDNS crash and re-enable**
- Instrument `mdns_service_init()` (markers between `mdns_resp_init` / netif walk /
  `mdns_resp_add_netif` / `mdns_resp_add_service`) and add a HardFault handler that dumps
  PC/LR to serial, to find which call hard-faults and why.
- Fix the crash, re-enable mDNS → `lightsync-XXXX.local` discovery works.
- Optionally also advertise `_http._tcp` so `http://lightsync-XXXX.local/settings` works
  from a browser (mDNS resolves the hostname; the HTTP server itself is phase 2 — done).

### Next steps (when physically at the Pico)
1. **Recover the Pico** — the documented CLAUDE.md exception: physically hold **BOOTSEL**,
   replug USB, then `driver.sh flash build/LEDServer.uf2` (this restores a fresh serial
   attach and loads the new, patched firmware).
2. **Capture the boot** — `driver.sh serial clear` + fresh daemon, reboot, then
   `driver.sh serial log --last N`. Watch for `M\n` → `BF\n` → `HEAP:*` → `W%u\n` markers.
   The 10 ms DMA timeouts in the patch should prevent the hang; a hung boot shows only
   `M\n` then silence.
3. **Determine the STA connect result** — the pass/fail criterion for the pacing
   experiment: `wifi_connect rc=0` (STA join success → proceeds to effects/mDNS) vs
   `rc=-8` (timeout → falls through to AP mode). This continues tasks #3/#4.
4. If STA still fails, `rc=-8` means the 30 s connect timed out — retry with the reboot
   loop to characterize, and confirm which post-connect call (effects / mDNS /
   multi-server) is silent.

## Hardware Constants

| Constant | Value |
|----------|-------|
| LED_PIN | 2 |
| LED_LENGTH | 288 |
| SERVER_PORT | 5005 |
| DDP_PORT | 4048 |
| MUSIC_PORT | 5006 |
| AP_IP | 192.168.4.1 |
| AP_SSID | LightSync |
| AP_PASSWORD | lightsync |
