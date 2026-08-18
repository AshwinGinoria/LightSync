#!/usr/bin/env python3
"""LightSync — serial capture daemon (pure Python, no bash).

Reimplementation of serial_capture.py's logic: monitors the Pico W CDC-ACM
device at 115200 baud and appends timestamped lines to a log file, surviving
Pico reboots. Spawned by the MCP server's log_start/log_stop tools via
subprocess (no os.fork inside the server, which would be unsafe in fastmcp's
threaded runtime).

Usage:
    serial_daemon.py start   — start daemon in background
    serial_daemon.py stop    — stop daemon
    serial_daemon.py status  — show running state

PID file: <serial-log>/serial.pid
Log file: <serial-log>/ledserver.log
"""

import os
import signal
import sys
import time
from pathlib import Path

# Ensure the project-local dependency dir is importable when the daemon is
# spawned from .mcp.json (which sets PYTHONPATH for the server; inherit it,
# and fall back to a relative lookup for robustness).
_HERE = Path(__file__).resolve().parent
_DEPS = _HERE / "mcp_deps"
if str(_DEPS) not in sys.path:
    sys.path.insert(0, str(_DEPS))

import serial
import serial.tools.list_ports  # noqa: E402

LOG_DIR = Path.home() / ".claude/projects/-home-ashwin-workspace-LightSync/serial-log"
LOG_FILE = LOG_DIR / "ledserver.log"
PID_FILE = LOG_DIR / "serial.pid"


def find_pico_cdc(timeout: float = 5.0) -> str | None:
    """Find the Pico CDC-ACM device, polling up to `timeout` seconds."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        for port in serial.tools.list_ports.comports():
            desc = port.description.lower()
            if "raspberry" in desc or "rp2040" in desc:
                return port.device
        for port in serial.tools.list_ports.comports():
            if port.device.startswith("/dev/ttyACM"):
                return port.device
        time.sleep(0.2)
    return None


def get_pid() -> int | None:
    try:
        return int(PID_FILE.read_text().strip())
    except (FileNotFoundError, ValueError):
        return None


def is_running(pid: int | None) -> bool:
    if pid is None:
        return False
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def start_daemon() -> None:
    pid = get_pid()
    if is_running(pid):
        print(f"Serial capture already running (PID {pid}).")
        return

    LOG_DIR.mkdir(parents=True, exist_ok=True)

    # Fork into the background; child detaches from the MCP server session so
    # capture survives the session ending.
    child = os.fork()
    if child > 0:
        PID_FILE.write_text(str(child))
        print(f"Serial capture started (PID {child}).")
        return

    os.setsid()
    signal.signal(signal.SIGTERM, lambda _s, _f: sys.exit(0))

    devnull = os.open(os.devnull, os.O_WRONLY)
    for fd in (0, 1, 2):
        os.dup2(devnull, fd)
    os.close(devnull)

    # Initial device: wait up to 60s for first boot with no device present.
    dev = find_pico_cdc(timeout=60)
    if dev is None:
        print("ERROR: No Pico device found after 60s")
        sys.exit(1)
    daemon_loop()


def daemon_loop() -> None:
    while True:
        dev = find_pico_cdc(timeout=5)
        if dev is None:
            time.sleep(2)
            continue

        try:
            ser = serial.Serial(dev, 115200, timeout=1)
        except (serial.SerialException, OSError) as exc:
            time.sleep(1)
            continue

        with LOG_FILE.open("a") as log:
            while True:
                try:
                    line = ser.readline()
                    if not line:
                        continue
                    text = line.decode("utf-8", errors="replace").strip()
                    if text:
                        ts = time.strftime("%Y-%m-%d %H:%M:%S")
                        log.write(f"[{ts}] {text}\n")
                        log.flush()
                except (serial.SerialException, OSError):
                    break
        ser.close()
        time.sleep(1)  # device disconnected — retry loop


def stop_daemon() -> None:
    pid = get_pid()
    if not is_running(pid):
        print("No serial capture running.")
        try:
            PID_FILE.unlink()
        except FileNotFoundError:
            pass
        return

    print(f"Stopping serial capture (PID {pid})...")
    os.kill(pid, signal.SIGTERM)
    time.sleep(0.5)
    if is_running(pid):
        os.kill(pid, signal.SIGKILL)
    print("Serial capture stopped.")
    try:
        PID_FILE.unlink()
    except FileNotFoundError:
        pass


def status() -> None:
    pid = get_pid()
    if is_running(pid):
        print(f"Serial capture running (PID {pid}).")
        if LOG_FILE.exists():
            size = LOG_FILE.stat().st_size
            lines = sum(1 for _ in LOG_FILE.open())
            print(f"Log: {LOG_FILE} ({size} bytes, {lines} lines)")
    else:
        print("No serial capture running.")


def main() -> None:
    if len(sys.argv) < 2:
        print("Usage: serial_daemon.py {start|stop|status}")
        sys.exit(1)
    cmd = sys.argv[1]
    if cmd == "start":
        start_daemon()
    elif cmd == "stop":
        stop_daemon()
    elif cmd == "status":
        status()
    else:
        print(f"Unknown command: {cmd}")


if __name__ == "__main__":
    main()
