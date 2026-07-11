# CLAUDE.md — LightSync

## Build Commands

- **LEDServer native tests:** `cd LEDServer/test_tdd && cmake -B build && cmake --build build && ctest --test-dir build` (gcc, mocks Pico SDK)
- **LEDServer ARM simulation:** `cd LEDServer/test_cmake` + Docker `lightsync-dev` + `rp2040_test.js`
- **LEDServer firmware build:** `.claude/skills/run-ledserver/build.sh build LEDServer` (Docker, cross-compiles)
- **LEDServer clean:** `.claude/skills/run-ledserver/driver.sh clean` — never `rm -rf build/` on host (Docker owns files as root)
- **SyncClient:** `cd SyncClient && cargo build` (Windows only)

## LEDServer Critical Gotchas

- **`LEDServer/lwipopts.h` — `MEM_LIBC_MALLOC=1` is mandatory.** Without it, `cyw43_arch_init()` crashes silently. Pico appears dead (no USB, no WiFi).
- **AP mode TCP checksums:** AP-mode packets have zero checksums; Linux rejects them. Set `CHECKSUM_GEN_TCP=1`, `CHECKSUM_GEN_UDP=1`, `CHECKSUM_GEN_IP=1` in lwipopts.h.
- **Never `sudo eject` the Pico** — SCSI command tears down USB MSC. Physically replug.
- **`picotool reboot` flags:** use `-a -f`, NOT `-r`.
- **Docker `bash -c` not `sh -c`** — container's `sh` is `dash`, doesn't support `pipefail`.

## Hardware Operations

All Pico W hardware work (flash, serial capture, USB diagnostics, AP testing) is in the run-ledserver skill:

```
LEDServer/.claude/skills/run-ledserver/
  driver.sh, build.sh, wifi.sh, serial_capture.py, usb_state_poller.sh
```

Full reference: `LEDServer/.claude/skills/run-ledserver/SKILL.md`

## Code Structure

- `SyncClient/` — Rust Windows desktop app (eframe/egui, UDP client, effects)
- `LEDServer/` — C++ Pico W firmware (WiFi STA, UDP server, PicoLed WS2812B)
- `LEDServer/test_tdd/` — native test suite (gcc, mocks Pico SDK/lwIP)
- `LEDServer/test_cmake/` — ARM rp2040js simulation (Docker)

Key constants: LED_PIN=2, LED_LENGTH=288, SERVER_PORT=5005, client IP=192.168.0.244

## Development Process

- **Verify before committing:** After any change, build and test the affected target. LEDServer native: `cd LEDServer/test_tdd && cmake --build build && ctest --test-dir build`. SyncClient: `cd SyncClient && cargo build`.
- **Use the skill for LEDServer builds and hardware:** All LEDServer firmware work goes through `.claude/skills/run-ledserver/` — `build.sh` for firmware builds (Docker cross-compilation), `driver.sh` for hardware ops (Docker ownership, USB state, serial capture), and `driver.sh clean` for cleanup. Never run cmake or `rm -rf build/` on host inside `LEDServer/`.
- **Small, testable increments:** One feature or fix per commit. Don't batch unrelated changes.

## Constraints

- SyncClient is **Windows-only** (fxc, windows-capture, D3D11)
- ReplicateEffect spiral mapping assumes 16:9 display
- WiFi/cyw43 testing requires real Pico W hardware
