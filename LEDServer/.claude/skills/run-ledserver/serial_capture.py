#!/usr/bin/env python3
"""
LightSync — serial capture daemon using pySerial.

Monitors /dev/ttyACM0 (Pico CDC-ACM) at 115200 baud, appends all output
to a log file. Automatically restarts on device disconnect (Pico reboot).

Usage:
    serial_capture.py start   — start daemon in background
    serial_capture.py stop    — stop daemon
    serial_capture.py status  — show running state
    serial_capture.py log --last N | --since M  — read log
    serial_capture.py live    — tail -f the log

PID file: serial-log/serial.pid
Log file: serial-log/ledserver.log
"""

import sys
import os
import time
import signal
import subprocess
import serial
import serial.tools.list_ports

LOG_DIR = os.path.expanduser(
    "~/.claude/projects/-home-ashwin-workspace-LightSync/serial-log"
)
LOG_FILE = os.path.join(LOG_DIR, "ledserver.log")
PID_FILE = os.path.join(LOG_DIR, "serial.pid")


def find_pico_cdc(timeout=15):
    """Find Pico CDC-ACM device, wait up to timeout seconds."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        for port in serial.tools.list_ports.comports():
            if "raspberry" in port.description.lower() or "rp2040" in port.description.lower():
                return port.device
        for port in serial.tools.list_ports.comports():
            if port.device.startswith("/dev/ttyACM"):
                return port.device
        time.sleep(0.5)
    return None


def get_pid():
    """Read PID from file."""
    try:
        with open(PID_FILE) as f:
            return int(f.read().strip())
    except (FileNotFoundError, ValueError):
        return None


def is_running(pid):
    """Check if PID is alive."""
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False


def read_log(last=None, since=None):
    """Read log file. Requires --last N or --since M."""
    if not last and not since:
        print("Usage: serial_capture.py log --last N | --since M")
        sys.exit(1)
    try:
        with open(LOG_FILE) as f:
            lines = f.readlines()
    except FileNotFoundError:
        print("No log file yet.")
        return
    if since:
        lines = [l for l in lines if since in l]
    if last:
        lines = lines[-last:]
    sys.stdout.writelines(lines)


def stop_daemon():
    """Stop the running daemon."""
    pid = get_pid()
    if pid is None:
        print("No serial capture running.")
        return
    if is_running(pid):
        print(f"Stopping serial capture (PID {pid})...")
        os.kill(pid, signal.SIGTERM)
        time.sleep(0.5)
        if is_running(pid):
            os.kill(pid, signal.SIGKILL)
        print("Serial capture stopped.")
    else:
        print("Serial capture already stopped.")
    os.unlink(PID_FILE)


def daemon_loop(dev):
    """Main capture loop. Runs in the daemon process."""
    while True:
        # Wait for device to appear
        dev = find_pico_cdc(timeout=30)
        if not dev:
            print(f"ERROR: No Pico device found after 30s")
            time.sleep(5)
            continue

        print(f"Opening {dev} at 115200...")
        try:
            ser = serial.Serial(dev, 115200, timeout=1)
            with open(LOG_FILE, "a") as log:
                while True:
                    try:
                        line = ser.readline()
                        if line:
                            text = line.decode("utf-8", errors="replace").strip()
                            if text:
                                ts = time.strftime("%Y-%m-%d %H:%M:%S")
                                log.write(f"[{ts}] {text}\n")
                                log.flush()
                    except (serial.SerialException, OSError):
                        ser.close()
                        break
            ser.close()
        except (serial.SerialException, OSError) as e:
            print(f"Error opening {dev}: {e}")
        print(f"Device {dev} disconnected — restarting in 3s...")
        time.sleep(3)


def start_daemon():
    """Start the serial capture daemon in background."""
    pid = get_pid()
    if pid and is_running(pid):
        print(f"Serial capture already running (PID {pid}).")
        return

    os.makedirs(LOG_DIR, exist_ok=True)

    print("Starting serial capture...")

    # Fork into background
    pid = os.fork()
    if pid > 0:
        # Parent — write PID file and exit
        with open(PID_FILE, "w") as f:
            f.write(str(pid))
        print(f"Serial capture started (PID {pid}).")
        return

    # Child — become daemon
    os.setsid()
    signal.signal(signal.SIGTERM, lambda s, f: sys.exit(0))

    # Redirect stdio to devnull
    sys.stdout.flush()
    sys.stderr.flush()
    devnull = os.open(os.devnull, os.O_WRONLY)
    os.dup2(devnull, 0)
    os.dup2(devnull, 1)
    os.dup2(devnull, 2)
    os.close(devnull)

    # Find initial device (wait up to 30s)
    dev = find_pico_cdc(timeout=30)
    if not dev:
        print(f"ERROR: No Pico device found after 30s")
        sys.exit(1)

    daemon_loop(dev)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    cmd = sys.argv[1]

    if cmd == "start":
        start_daemon()
    elif cmd == "stop":
        stop_daemon()
    elif cmd == "status":
        pid = get_pid()
        if pid and is_running(pid):
            print(f"Serial capture running (PID {pid}).")
            if os.path.exists(LOG_FILE):
                size = os.path.getsize(LOG_FILE)
                lines = sum(1 for _ in open(LOG_FILE))
                print(f"Log: {LOG_FILE} ({size} bytes, {lines} lines)")
        else:
            print("No serial capture running.")
            print("Start it with: serial_capture.py start")
    elif cmd == "log":
        last = None
        since = None
        i = 2
        while i < len(sys.argv):
            if sys.argv[i] == "--last" and i + 1 < len(sys.argv):
                last = int(sys.argv[i + 1])
                i += 2
            elif sys.argv[i] == "--since" and i + 1 < len(sys.argv):
                since = sys.argv[i + 1]
                i += 2
            else:
                i += 1
        read_log(last=last, since=since)
    elif cmd == "live":
        subprocess.run(["tail", "-f", LOG_FILE])
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
