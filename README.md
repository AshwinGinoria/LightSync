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
