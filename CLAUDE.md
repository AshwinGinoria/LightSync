# CLAUDE.md — LightSync

## Build Commands

- **LEDServer native tests:** `LEDServer/.claude/skills/run-ledserver/build.sh native`
- **LEDServer firmware build:** `LEDServer/.claude/skills/run-ledserver/build.sh build LEDServer` (Docker, cross-compiles)
- **LEDServer clean:** `LEDServer/.claude/skills/run-ledserver/build.sh clean` — never `rm -rf build/` on host (Docker owns files as root)
- **SyncClient:** `cd SyncClient && cargo build` (Windows only)

## Hardware Operations

**ALWAYS USE THE run-ledserver SKILL FOR PICO-RELATED COMMANDS. NEVER run Pico-related shell commands directly.**

- All Pico W hardware work (flash, serial capture, USB diagnostics, AP testing, reboot, bootsel) goes through `driver.sh`
- All Pico W builds go through `build.sh`
- All WiFi operations go through `wifi.sh`
- Direct shell commands like `picotool`, `lsusb`, `dmesg`, `cp` to RPI-RP2, `python3 serial_capture.py` are forbidden — use the skill equivalents instead
- The only exception is when the Pico is completely dead (USB stack killed by HardFault) and the skill cannot communicate with it — in that case, physically hold BOOTSEL and replug, then use `driver.sh flash` to recover

```
LEDServer/.claude/skills/run-ledserver/
  driver.sh, build.sh, wifi.sh, serial_capture.py, usb_state_poller.sh
```

Full reference: `LEDServer/.claude/skills/run-ledserver/SKILL.md`

## Code Structure

- `SyncClient/` — Rust Windows desktop app (eframe/egui, UDP client, effects)
- `LEDServer/` — C++ Pico W firmware (WiFi STA, UDP server, PicoLed WS2812B)
- `LEDServer/test_tdd/` — native test suite (gcc, mocks Pico SDK/lwIP)

Key constants: LED_PIN=2, LED_LENGTH=288, SERVER_PORT=5005, client IP=192.168.0.244

## Development Process

- **Use Available Tools:** Use the `clang LSP`, `run-ledserver` skill, claude code `Monitor`, claude code `agent` and other available skills throughout the development cycle. Attempt to discover the features and toolset each provide you with and use them when they meet the use-case. Given a choice always prioritize using these tools.
- **Verify before committing:** After any change, build and test the affected target. LEDServer native: `LEDServer/.claude/skills/run-led-server/build.sh native`. SyncClient: `cd SyncClient && cargo build`.
- **Use the skill for LEDServer builds and hardware:** All LEDServer firmware work goes through `.claude/skills/run-ledserver/` — `build.sh` for firmware builds (Docker cross-compilation), `driver.sh` for hardware ops (Docker ownership, USB state, serial capture), and `driver.sh clean` for cleanup. Never run cmake or `rm -rf build/` on host inside `LEDServer/`.
- **Small, testable increments:** One feature or fix per commit. Don't batch unrelated changes.
- **Work Backwords:** When debugging work backwards from the error log, do NOT assume. logs are the source of truth and must always take priority. if visibility is missing then add visibility and then debug.

## Constraints

- SyncClient is **Windows-only** (fxc, windows-capture, D3D11)
- WiFi/cyw43 testing requires real Pico W hardware

## Debugging

- LOGS are the first source of truth
- If the logs are not enough to determine the failure. Retry with More logs and visibility
- RCA based on incomplete imformation is hallucination
- Make use of PicoSDK Documentaion and read before write 

