#!/usr/bin/env python3
"""LightSync — MCP server for the LEDServer Pico W dev workflow.

Pure-Python reimplementation of the run-ledserver shell scripts (driver.sh /
build.sh / wifi.sh / test_pico.sh used only as reference). No bash delegation:
every operation is built here with subprocess/os/requests to the underlying
tools (picotool, docker, wpa_supplicant, curl) directly.

Design for weaker models:
  - ~12 flat tools, scalar args only, defaults provided.
  - Every description is an imperative recipe with a concrete example and the
    state prerequisites stated explicitly.
  - Uniform result shape: {ok: bool, output: str}.

Run:  python3 server.py   (stdio transport, spawned per-session via .mcp.json)
Deps: fastmcp + pyserial in mcp_deps/ (pip --target).
"""

from __future__ import annotations

import glob
import os
import re
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

# Ensure the project-local dependency dir (fastmcp, pyserial) is importable
# whether spawned via .mcp.json (PYTHONPATH) or run standalone.
_HERE = Path(__file__).resolve().parent
_DEPS = _HERE / "mcp_deps"
if str(_DEPS) not in sys.path:
    sys.path.insert(0, str(_DEPS))

from fastmcp import FastMCP

# ── paths ───────────────────────────────────────────────────────────────────

SERVER_DIR = Path(__file__).resolve().parent
LEDSERVER_DIR = SERVER_DIR.parent.parent.parent          # LEDServer/
PROJECT_ROOT = LEDSERVER_DIR.parent                       # LightSync repo
WORKSPACE = PROJECT_ROOT.parent                           # parent of repo (docker mount)

DOCKER_IMAGE = "lightsync-dev"
SERIAL_LOG_DIR = Path.home() / ".claude/projects/-home-ashwin-workspace-LightSync/serial-log"
SERIAL_LOG = SERIAL_LOG_DIR / "ledserver.log"

AP_IP = "192.168.4.1"
AP_SSID = "LightSync"
AP_PASS = "lightsync"

ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")

# ── result helpers ──────────────────────────────────────────────────────────

def _ok(output: str) -> dict[str, Any]:
    return {"ok": True, "output": output}


def _fail(output: str) -> dict[str, Any]:
    return {"ok": False, "output": output}


def _run(cmd: list[str], timeout: int = 120, cwd: Path | None = None) -> tuple[int, str]:
    """Run a host command, return (exit_code, stripped stdout+stderr)."""
    try:
        proc = subprocess.run(
            cmd, capture_output=True, text=True, timeout=timeout, cwd=cwd
        )
    except subprocess.TimeoutExpired:
        return -1, f"TIMEOUT after {timeout}s"
    except FileNotFoundError as exc:
        return -1, f"Not found: {exc}"
    text = ANSI_RE.sub("", f"{proc.stdout}\n{proc.stderr}").strip()
    return proc.returncode, text


def _sudo(cmd: list[str], timeout: int = 120) -> tuple[int, str]:
    return _run(["sudo", *cmd], timeout=timeout)


# ── Pico device detection ───────────────────────────────────────────────────

def _serial_by_id_matches() -> bool:
    byid = Path("/dev/serial/by-id")
    if not byid.is_dir():
        return False
    return any(
        "raspberry" in p.name.lower() or "rp2040" in p.name.lower()
        for p in byid.iterdir()
    )


def get_usb_state() -> str:
    """Return one of: bootsel, running, connected, absent."""
    if os.path.exists("/dev/disk/by-label/RPI-RP2"):
        return "bootsel"
    if glob.glob("/dev/ttyACM*"):
        return "running"
    if _serial_by_id_matches():
        return "connected"
    return "absent"


def find_pico_cdc() -> str | None:
    try:
        import serial.tools.list_ports
    except ImportError:
        return None
    for port in serial.tools.list_ports.comports():
        desc = port.description.lower()
        if "raspberry" in desc or "rp2040" in desc:
            return port.device
    for port in serial.tools.list_ports.comports():
        if port.device.startswith("/dev/ttyACM"):
            return port.device
    return None


def _pico_bootdev() -> str | None:
    link = Path("/dev/disk/by-label/RPI-RP2")
    if not link.exists():  # -e semantics: fails on dangling symlink
        return None
    try:
        dev = link.resolve()
    except OSError:
        return None
    return str(dev) if dev.is_block_device() else None


def _bootsel_mountpoint(dev: str) -> str | None:
    """Find where the RPI-RP2 block device is currently mounted."""
    rc, out = _run(["findmnt", "-rno", "TARGET", dev])
    if rc == 0 and out.strip():
        return out.strip().splitlines()[-1]
    return None


# ── serial log helpers ──────────────────────────────────────────────────────

def _ensure_log_dir() -> None:
    SERIAL_LOG_DIR.mkdir(parents=True, exist_ok=True)


def _daemon_cmd(subcmd: str) -> tuple[int, str]:
    return _run([os.sys.executable, str(SERVER_DIR / "serial_daemon.py"), subcmd], timeout=60)


def _read_log_lines(last_n: int) -> list[str]:
    if not SERIAL_LOG.exists():
        return []
    return SERIAL_LOG.read_text(errors="replace").splitlines()[-last_n:]


def _wait_for_marker(pattern: str, timeout_s: int, label: str) -> bool:
    """Poll the serial log for a regex marker, up to timeout_s."""
    rx = re.compile(pattern)
    elapsed = 0
    while elapsed < timeout_s:
        if any(rx.search(line) for line in _read_log_lines(1000)):
            return True
        time.sleep(2)
        elapsed += 2
    return False


# ── docker helper ───────────────────────────────────────────────────────────

def _require_docker() -> str | None:
    rc, out = _run(["docker", "ps"])
    if rc != 0:
        return "Docker not running. Start Docker Desktop / dockerd and retry."
    rc, out = _run(["docker", "images", "--format", "{{.Repository}}"])
    if DOCKER_IMAGE not in out.splitlines():
        return f"Docker image '{DOCKER_IMAGE}' not found. Build it: docker build -t {DOCKER_IMAGE} ."
    return None


def _docker_run(script: str, timeout: int = 600) -> tuple[int, str]:
    return _run(
        [
            "docker", "run", "--rm", "--memory=4g", "--cpus=2",
            "-v", f"{WORKSPACE}:/workspace",
            DOCKER_IMAGE, "bash", "-c", script,
        ],
        timeout=timeout,
    )


_NATIVE_SCRIPT = """\
set -e
cd /workspace/LightSync/LEDServer/test_tdd
rm -rf build-native && mkdir build-native && cd build-native
cmake .. -G 'Unix Makefiles' >/dev/null
cmake --build . --parallel >/dev/null
echo '=== Running all test binaries ==='
for bin in test_* ledserver_tdd; do
    if [ -x "$bin" ]; then
        echo "--- Running $bin ---"
        ./$bin || { echo "FAILED: $bin"; exit 1; }
    fi
done
"""

_BUILD_SCRIPT = """\
set -eo pipefail
cd /opt/pico-sdk
git submodule update --init --recursive 2>/dev/null
cd /opt/pico-sdk/lib/cyw43-driver
if grep -q 'cyw43_await_background_or_timeout_us(10000)' src/cyw43_ll.c 2>/dev/null; then
    echo 'Pacing patch already applied'
else
    git apply -p0 /workspace/LightSync/LEDServer/patches/cyw43_ll_ioctl_pacing.patch
    echo 'Pacing patch applied'
fi
cd /opt/pico-sdk
if grep -q 'dma_wait_timeout_ms' src/rp2_common/pico_cyw43_driver/cyw43_bus_pio_spi.c 2>/dev/null; then
    echo 'SPI timeout patch already applied'
else
    git apply -p0 /workspace/LightSync/LEDServer/patches/cyw43_bus_pio_spi_timeout.patch
    echo 'SPI timeout patch applied'
fi
rm -rf /root/.pico-sdk
ln -s /opt/pico-sdk /root/.pico-sdk
cd /workspace/LightSync/LEDServer
rm -rf build && mkdir build && cd build
cmake .. -G 'Unix Makefiles' 2>&1
cmake --build . --parallel --target {target} 2>&1
ls -lah {target}.elf {target}.uf2 {target}.hex 2>/dev/null || echo 'Some outputs missing'
"""

_CLONE_PICOLED = """\
cd /workspace/LightSync/LEDServer
rm -rf PicoLED
git clone --depth 1 https://github.com/usedbytes/picoled.git PicoLED
"""


# ── wifi helpers ────────────────────────────────────────────────────────────

def _detect_wifi_iface() -> str | None:
    rc, out = _run(["ip", "-o", "link", "show", "type", "wifi"])
    if rc == 0 and out:
        m = re.search(r"^\d+:\s+([^:]+):", out, re.M)
        if m:
            return m.group(1)
    rc, out = _run(["ip", "-o", "link", "show"])
    for line in out.splitlines():
        m = re.search(r"^\d+:\s+(\w+):", line)
        if m and re.search(r"\bwlan|\bwl", m.group(1)):
            return m.group(1)
    return None


def _curl_on_iface(args: list[str], timeout: int = 60) -> tuple[int, str]:
    iface = _detect_wifi_iface()
    if iface is None:
        return 1, "No WiFi interface found. Is the host WiFi up?"
    return _run(["curl", "--interface", iface, *args], timeout=timeout)


def _http_url(url: str) -> str:
    if url.startswith("http://") or url.startswith("https://"):
        return url
    return f"http://{AP_IP}/{url.lstrip('/')}"


# ── MCP server ──────────────────────────────────────────────────────────────

mcp = FastMCP("lightsync")


# ── hardware ────────────────────────────────────────────────────────────────

@mcp.tool(
    description=(
        "Read the Pico W's USB state: bootsel (BOOTSEL mode, ready to flash), "
        "running (firmware up, serial available), connected (USB present but "
        "unrecognized), absent (no Pico detected). Call this first to decide "
        "what to do. Returns {ok, output} with the state and a hint. "
        "Example: no args needed."
    )
)
def usb_status() -> dict[str, Any]:
    state = get_usb_state()
    dev = find_pico_cdc() if state in ("running", "connected") else None
    devinfo = f"\nserial device: {dev}" if dev else ""
    hint = {
        "bootsel": "in BOOTSEL mode, ready to flash .uf2",
        "running": "firmware running, serial available",
        "connected": "USB present but unrecognized",
        "absent": "no Pico detected — check cable / USB port",
    }[state]
    return _ok(f"State: {state} — {hint}{devinfo}")


@mcp.tool(
    description=(
        "Flash a .uf2 to the Pico W. Path is relative to LEDServer/ (e.g. "
        "'build/LEDServer.uf2') or absolute. Works in bootsel state (copies "
        "to the RPI-RP2 volume) or running state (picotool load + reboot); "
        "fails if the Pico is 'connected' or 'absent'. Starts serial capture "
        "afterwards. Returns {ok, output}: ok=false on any failure. "
        "Example: flash('build/LEDServer.uf2')."
    )
)
def flash(uf2_path: str) -> dict[str, Any]:
    p = Path(uf2_path)
    if not p.is_absolute():
        p = LEDSERVER_DIR / p
    if not p.is_file():
        return _fail(f"File not found: {p}")

    state = get_usb_state()
    if state == "absent":
        return _fail(
            "Pico not found. Put it in BOOTSEL mode (hold BOOTSEL, plug in) and retry."
        )

    if state == "bootsel":
        bootdev = _pico_bootdev()
        if bootdev is None:
            return _fail("RPI-RP2 label present but no block device. Replug the Pico.")
        mp = _bootsel_mountpoint(bootdev)
        mounted = False
        if mp is None:
            mp = "/tmp/pico-boot"
            _sudo(["mkdir", "-p", mp])
            if _sudo(["mount", bootdev, mp])[0] != 0:
                return _fail(f"Could not mount BOOTSEL device {bootdev}.")
            mounted = True
        rc, out = _sudo(["cp", str(p), mp])
        _sudo(["sync"])
        if mounted:
            _sudo(["umount", mp])
        if rc != 0:
            return _fail(f"Copy to BOOTSEL device failed: {out}")
        _daemon_cmd("start")
        return _ok("Firmware copied to RPI-RP2. Pico reboots into firmware automatically.")

    if state == "connected":
        return _fail(
            "Pico state is 'connected' (USB present but unrecognized) — cannot flash. "
            "Put it in BOOTSEL mode (hold BOOTSEL, replug) or run firmware first."
        )

    # running → picotool
    rc, _ = _run(["command", "-v", "picotool"])
    if rc != 0:
        return _fail("picotool not found. Put the Pico in BOOTSEL mode and retry.")
    rc, out = _sudo(["picotool", "load", "-f", str(p)])
    if rc != 0:
        return _fail(f"picotool load failed: {out}")
    _sudo(["picotool", "reboot", "-a", "-f"])
    _daemon_cmd("start")
    return _ok("Firmware flashed via picotool and Pico rebooted.")


@mcp.tool(
    description=(
        "Reboot the Pico W's running firmware via picotool. Requires: Pico "
        "running (usb_status shows 'running'). Returns {ok, output}. "
        "Example: no args needed."
    )
)
def reboot() -> dict[str, Any]:
    if find_pico_cdc() is None:
        return _fail("No Pico CDC device. Pico must be running firmware.")
    rc, out = _sudo(["picotool", "reboot", "-a", "-f"])
    if rc != 0:
        return _fail(f"picotool reboot failed: {out}")
    return _ok("Pico rebooting.")


# ── serial logging ──────────────────────────────────────────────────────────

@mcp.tool(
    description=(
        "Start the background serial-capture daemon: reads the Pico's USB "
        "serial and writes timestamped lines to the log. Call once; it "
        "survives Pico reboots. The Pico may be connected before or within "
        "~60 seconds of starting. Returns {ok, output}. Example: no args needed."
    )
)
def log_start() -> dict[str, Any]:
    _ensure_log_dir()
    rc, out = _daemon_cmd("start")
    return _ok(out) if rc == 0 else _fail(out)


@mcp.tool(
    description=(
        "Stop the background serial-capture daemon. Returns {ok, output}. "
        "Example: no args needed."
    )
)
def log_stop() -> dict[str, Any]:
    rc, out = _daemon_cmd("stop")
    return _ok(out) if rc == 0 else _fail(out)


@mcp.tool(
    description=(
        "Read the last N lines of the Pico serial log (default 50). If the "
        "log is empty, call log_start first. Returns {ok, output}: ok=false "
        "when the log is empty or missing. Example: read_log(100)."
    )
)
def read_log(last_n: int = 50) -> dict[str, Any]:
    if last_n < 1:
        last_n = 1
    lines = _read_log_lines(last_n)
    if not lines:
        return _fail(
            "Serial log is empty or missing. Call log_start() to begin capture."
        )
    return _ok("\n".join(lines))


# ── build & test ────────────────────────────────────────────────────────────

@mcp.tool(
    description=(
        "Run the native x86 unit test suite inside Docker (clean rebuild; "
        "takes a few minutes). Requires: Docker running + 'lightsync-dev' "
        "image. Returns {ok, output} with per-binary pass/fail. "
        "Example: no args needed."
    )
)
def build_native() -> dict[str, Any]:
    err = _require_docker()
    if err:
        return _fail(err)
    rc, out = _docker_run(_NATIVE_SCRIPT)
    return _ok(out) if rc == 0 else _fail(f"Native tests failed:\n{out}")


@mcp.tool(
    description=(
        "Cross-compile a Pico W firmware target inside Docker (clean rebuild, "
        "takes a few minutes; auto-applies cyw43 SDK patches and clones "
        "PicoLED if missing). Pass the CMake target, e.g. 'LEDServer'. The "
        ".uf2 lands in LEDServer/build/. Requires: Docker running + "
        "'lightsync-dev' image. Returns {ok, output}. "
        "Example: build_target('LEDServer')."
    )
)
def build_target(target: str) -> dict[str, Any]:
    err = _require_docker()
    if err:
        return _fail(err)
    if not (LEDSERVER_DIR / "PicoLED" / "PicoLed.cmake").exists():
        if _docker_run(_CLONE_PICOLED)[0] != 0:
            return _fail("Failed to clone PicoLED.")
    rc, out = _docker_run(_BUILD_SCRIPT.format(target=target))
    return _ok(out) if rc == 0 else _fail(f"Build failed:\n{out}")


# ── wifi / AP ───────────────────────────────────────────────────────────────

@mcp.tool(
    description=(
        "Connect the host's WiFi interface to the Pico W access point "
        f"(SSID {AP_SSID}, {AP_IP}). Kills any running wpa_supplicant on the "
        "host's WiFi interface and joins the Pico's AP; waits up to 15s for "
        "association and requests an IP. Side effect: ONLY the WiFi interface "
        "is affected — if the host's internet is via ethernet/LAN it is "
        "untouched. If the host was using WiFi, that connection is dropped "
        "and not auto-restored (wifi_disconnect leaves the WiFi interface "
        "down). Requires: Pico running and in AP mode (captive portal), AND "
        "a WiFi interface on the host (fails with 'No WiFi interface found' "
        "if the host has none — e.g. an ethernet-only box). "
        "Returns {ok, output}. Example: no args needed."
    )
)
def wifi_connect() -> dict[str, Any]:
    iface = _detect_wifi_iface()
    if iface is None:
        return _fail("No WiFi interface found on the host.")
    _sudo(["pkill", "-x", "wpa_supplicant"])
    time.sleep(1)
    conf = "/tmp/.wpa_light_sync.conf"
    Path(conf).write_text(
        f'ctrl_interface=/run/wpa_supplicant\n'
        f'update_config=1\n'
        f'network={{\n    ssid="{AP_SSID}"\n    psk="{AP_PASS}"\n    key_mgmt=WPA-PSK\n}}\n'
    )
    _sudo(["mkdir", "-p", "/run/wpa_supplicant"])
    _sudo(["ip", "link", "set", iface, "up"])
    time.sleep(1)
    rc, out = _sudo(["wpa_supplicant", "-B", "-i", iface, "-c", conf])
    if rc != 0:
        return _fail(f"wpa_supplicant failed: {out}")

    for _ in range(15):
        rc, out = _sudo(["wpa_cli", "-i", iface, "status"])
        if "wpa_state=COMPLETED" in out:
            break
        time.sleep(1)

    rc, out = _sudo(["wpa_cli", "-i", iface, "status"])
    if "wpa_state=COMPLETED" in out:
        rc, _ = _run(["command", "-v", "dhclient"])
        if rc == 0:
            _sudo(["dhclient", iface])
        else:
            _sudo(["ip", "addr", "add", "192.168.4.2/24", "dev", iface])
        return _ok(f"Connected to {AP_SSID} ({AP_IP}/24). Test: http://{AP_IP}/")
    return _fail("Association did not complete within 15s. Check with usb_status/wifi status.")


@mcp.tool(
    description=(
        "Disconnect the host's WiFi interface from the Pico W access point "
        "and bring the interface down. ONLY the WiFi interface is affected; "
        "ethernet/LAN connectivity is untouched. The WiFi interface is left "
        "down with no wpa_supplicant running — restart the network manager "
        "or wpa_supplicant if WiFi is needed later. Returns {ok, output}. "
        "Example: no args needed."
    )
)
def wifi_disconnect() -> dict[str, Any]:
    iface = _detect_wifi_iface()
    if iface is None:
        return _ok("No WiFi interface found; nothing to disconnect.")
    _sudo(["ip", "link", "set", iface, "down"])
    _sudo(["pkill", "-x", "wpa_supplicant"])
    Path("/tmp/.wpa_light_sync.conf").unlink(missing_ok=True)
    return _ok("Disconnected.")


@mcp.tool(
    description=(
        "HTTP GET a page from the Pico W AP. Pass a path like '/' — it "
        f"resolves to http://{AP_IP}/. Requires: host connected to the Pico "
        "AP (wifi_connect) — which itself needs a host WiFi interface; on "
        "an ethernet-only host this fails with 'No WiFi interface found'. "
        "Returns {ok, output}: ok=false on transport error; HTTP error "
        "bodies (4xx/5xx) are returned as output. Example: http_get('/')."
    )
)
def http_get(url: str = "/") -> dict[str, Any]:
    target = _http_url(url)
    rc, out = _curl_on_iface(["-sSL", target])
    return _ok(out) if rc == 0 else _fail(f"GET {target} failed: {out}")


@mcp.tool(
    description=(
        "End-to-end provisioning test: submit ssid/password to the Pico's "
        "captive-portal /connect form, reboot, and verify it boots into STA "
        "mode connected to your WiFi. Takes up to ~5 minutes. Requires: Pico "
        "running in AP mode with NO saved WiFi config (fails if already in "
        "STA mode), AND a WiFi interface on the host to join the Pico's AP "
        "(fails at step 3/7 on ethernet-only hosts). Disconnects the host "
        "from the AP when done. Returns {ok, output} with a step-by-step "
        "trace. Example: provision('MyWifi', 'myPass123')."
    )
)
def provision(ssid: str, password: str = "") -> dict[str, Any]:
    steps: list[str] = []

    # [1/7] pre-flight
    state = get_usb_state()
    if state == "absent":
        return _fail("Pico W not detected. Connect it and retry.")
    if state == "bootsel":
        return _fail("Pico W is in BOOTSEL mode. Flash firmware first (flash tool).")
    steps.append(f"[1/7] Pre-flight: Pico {state}")

    # [2/7] clear log + reboot, expect AP boot
    SERIAL_LOG.unlink(missing_ok=True)
    _ensure_log_dir()
    if not reboot()["ok"]:
        return _fail("[2/7] Reboot failed — check usb_status. Aborting provision.")
    if not _wait_for_marker(
        r"BOOT_COMPLETE|entering main loop|Server running at", 30, "boot-complete"
    ):
        return _fail("Pico did not complete boot within 30s. Check serial log.")
    log = _read_log_lines(1000)
    if any("Server running at" in line for line in log):
        return _fail(
            "Pico is ALREADY in STA mode (valid WiFi config present). "
            "Provisioning needs AP mode — erase the config or use a fresh device."
        )
    steps.append("[2/7] Booted in AP mode (captive portal up)")

    # [3/7] connect host to AP
    res = wifi_connect()
    if not res["ok"]:
        return _fail(f"[3/7] {res['output']}")
    steps.append("[3/7] Host connected to Pico AP")

    # [4/7] confirm provisioning page serves
    rc, out = _curl_on_iface(["-sSL", f"http://{AP_IP}/"])
    if "LEDServer WiFi Setup" not in out:
        wifi_disconnect()
        return _fail(
            f"[4/7] Provisioning page not served correctly. Got: {out[:300]}"
        )
    steps.append("[4/7] Provisioning page served correctly")

    # [5/7] submit ssid/password form
    rc, out = _curl_on_iface(
        [
            "-sS", "-X", "POST",
            "--data-urlencode", f"ssid={ssid}",
            "--data-urlencode", f"password={password}",
            "-w", "\nHTTP %{http_code}\n",
            f"http://{AP_IP}/connect",
        ],
        timeout=90,
    )
    if "HTTP 302" not in out:
        return _fail(f"[5/7] POST /connect did not return 302. Got: {out[-200:]}")
    if not _wait_for_marker(r"config_save completed", 10, "config-save"):
        return _fail("[5/7] config_save marker not seen — form may not have reached flash.")
    steps.append("[5/7] Config saved to flash (HTTP 302 + config_save completed)")

    # [6/7] reboot → watch for STA attempt
    SERIAL_LOG.unlink(missing_ok=True)
    if not reboot()["ok"]:
        steps.append("[6/7] WARNING: reboot failed — continuing to watch the log")
    if _wait_for_marker(r"config valid, trying STA mode", 30, "STA-attempt"):
        steps.append("[6/7] Detected saved config, attempting STA connection")
    else:
        steps.append("[6/7] WARNING: 'config valid, trying STA mode' not seen")

    # [7/7] watch for STA result
    if _wait_for_marker(r"Server running at", 50, "STA-connected"):
        steps.append(f"[7/7] SUCCESS — Pico connected to WiFi '{ssid}'.")
        wifi_disconnect()
        return _ok("\n".join(steps))
    if _wait_for_marker(
        r"entering AP mode path|AP mode: entering main loop", 50, "AP-fallback"
    ):
        steps.append(
            f"[7/7] FAILED — Pico fell back to AP mode (could not connect to '{ssid}'). "
            "Check SSID, password, 2.4GHz band, WPA2/WPA3."
        )
        wifi_disconnect()
        return _fail("\n".join(steps))
    steps.append("[7/7] No STA-success or AP-fallback within 50s — possible hang.")
    wifi_disconnect()
    return _fail("\n".join(steps))


if __name__ == "__main__":
    mcp.run()
