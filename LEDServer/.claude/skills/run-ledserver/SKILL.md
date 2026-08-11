# LEDServer — Run & Flash Skill

## Hardware (Pico W)

```bash
.claude/skills/run-ledserver/driver.sh flash build/LEDServer.uf2   # Flash uf2 (auto-detects BOOTSEL/running)
.claude/skills/run-ledserver/driver.sh bootsel                      # Enter BOOTSEL mode (1200 baud trick)
.claude/skills/run-ledserver/driver.sh reboot                       # Reboot firmware (picotool)
.claude/skills/run-ledserver/driver.sh serial log --last N          # Read last N lines of serial log
.claude/skills/run-ledserver/driver.sh serial log --since M         # Read lines matching timestamp
.claude/skills/run-ledserver/driver.sh serial live                  # Tail -f on serial log
.claude/skills/run-ledserver/driver.sh serial start                 # Start serial capture daemon
.claude/skills/run-ledserver/driver.sh serial stop                  # Stop serial capture daemon
.claude/skills/run-ledserver/driver.sh serial status                # Show capture daemon status
.claude/skills/run-ledserver/driver.sh serial clear                 # Truncate serial log
.claude/skills/run-ledserver/driver.sh usb-status                   # Show USB enumeration state
.claude/skills/run-ledserver/driver.sh dmesg                        # Kernel USB logs
.claude/skills/run-ledserver/driver.sh setup                        # One-time: serial log dir + poller
```

## Build + Test (Docker)

```bash
.claude/skills/run-ledserver/build.sh                    # Run all: native tests + build firmware
.claude/skills/run-ledserver/build.sh native             # Native x86 unit tests (10 test binaries)
.claude/skills/run-ledserver/build.sh build <target>     # Build any cmake target (e.g. LEDServer)
.claude/skills/run-ledserver/build.sh clean              # Remove all build artifacts
```

## WiFi AP Testing

```bash
.claude/skills/run-ledserver/wifi.sh connect              # Connect host to Pico AP
.claude/skills/run-ledserver/wifi.sh disconnect           # Disconnect from Pico AP
.claude/skills/run-ledserver/wifi.sh status               # Show WiFi connection state
.claude/skills/run-ledserver/wifi.sh http-get <url>       # HTTP GET to Pico AP (192.168.4.1)
.claude/skills/run-ledserver/wifi.sh http-post <url> <data>  # HTTP POST to Pico AP
.claude/skills/run-ledserver/wifi.sh http-post-form <url> <field=value>...  # URL-encoded POST (provisioning form)
.claude/skills/run-ledserver/wifi.sh ping                 # Ping Pico AP gateway
.claude/skills/run-ledserver/wifi.sh scan                 # Scan for Pico AP
```

## Provisioning Test (submit /connect wifi form + monitor)

```bash
.claude/skills/run-ledserver/test_pico.sh provision --ssid "MyWifi" [--pass "secret"]
```

Flow: pre-flight USB check → clear log + reboot (expect AP/captive-portal boot) →
connect host to AP → GET / (verify provisioning page serves) →
POST ssid/password to /connect (expect HTTP 302) → verify `config_save completed` in log →
reboot → watch for `config valid, trying STA mode` → outcome:
`Server running at` = SUCCESS (connected to WiFi), `entering AP mode path` = FAILED (AP fallback).

## Claude Monitor — USB State Watcher

```bash
Monitor({
  description: "Pico W USB state (BOOTSEL/connect/disconnect)",
  command: ".claude/skills/run-ledserver/usb_state_poller.sh",
  persistent: true
})
```

Emits one line per state change: `bootsel`, `running`, `connected`, `absent`.

## Serial Capture & Querying

Continuous serial logging is managed by a background daemon, ensuring all telemetry is captured even across reboots.

- **Continuous Logging:** A `pySerial` daemon (`serial_capture.py`) automatically manages the capture and restarts on Pico reboot (handling USB enumeration/disconnection).
- **Persistent Storage:** Logs are stored on disk at:
  `~/.claude/projects/-home-ashwin-workspace-LightSync/serial-log/ledserver.log`
- **Querying Infrastructure:** Use `driver.sh` to inspect the logs:
  - `driver.sh serial log --last N` — Get the last `N` lines.
  - `driver.sh serial log --since <timestamp>` — Query logs from a specific time.
  - `driver.sh serial live` — Stream live log output.
  - `driver.sh serial clear` — Wipe the current log.
  - `driver.sh serial start` / `stop` — Manually control the daemon.

## Gotchas

| Problem | Cause | Fix |
|---------|-------|-----|
| Pico stays in BOOTSEL after flash | Firmware crashes before USB init | Check serial log, fix, reflash |
| Pico disappears after `eject` | SCSI command destroys USB MSC | Physically replug USB |
| `cyw43_arch_init()` crash | Missing `MEM_LIBC_MALLOC` in lwipopts.h | Add `#define MEM_LIBC_MALLOC 1` |
| WiFi hangs after 96+ requests | Stale `recv_len` not reset | Reset `recv_len = 0` in TCP recv |
| Flash fails — wrong path | Dangling `/dev/disk/by-label/RPI-RP2` symlink | `sudo rm /dev/disk/by-label/RPI-RP2` |
| Build fails — no error output | Check docker logs, `build.sh` now prints full output | |

## Pico USB IDs

| State | VID:PID | Device |
|-------|---------|--------|
| BOOTSEL | `2e8a:0003` | Mass storage (RPI-RP2) |
| Running | `2e8a:000a` | CDC-ACM serial |

## When to Use Which

| Task | Script | Command |
|------|--------|---------|
| Quick test | `build.sh` | `.claude/skills/run-ledserver/build.sh native` |
| Build any target | `build.sh` | `.claude/skills/run-ledserver/build.sh build LEDServer` |
| Deploy to Pico W | `driver.sh` | `.claude/skills/run-ledserver/driver.sh flash build/LEDServer.uf2` |
| Debug crash | `driver.sh` | `.claude/skills/run-ledserver/driver.sh serial log --last 50` |
| Check USB state | `driver.sh` | `.claude/skills/run-ledserver/driver.sh usb-status` |
| Enter BOOTSEL | `driver.sh` | `.claude/skills/run-ledserver/driver.sh bootsel` |
| Connect to AP | `wifi.sh` | `.claude/skills/run-ledserver/wifi.sh connect` |
| Test HTTP | `wifi.sh` | `.claude/skills/run-ledserver/wifi.sh http-get /` |
| Monitor state | `usb_state_poller.sh` | Claude Monitor |
