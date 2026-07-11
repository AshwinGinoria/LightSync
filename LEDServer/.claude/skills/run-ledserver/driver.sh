#!/usr/bin/env bash
# LightSync LEDServer — hardware operations + serial capture.
#
# Usage:
#   ./driver.sh flash <path>     # flash uf2 to Pico W hardware
#   ./driver.sh bootsel          # trigger BOOTSEL mode via 1200 baud trick
#   ./driver.sh reboot           # reboot running firmware (picotool)
#   ./driver.sh serial log --last N                # last N lines
#   ./driver.sh serial log --since HH:MM:SS        # lines since time today
#   ./driver.sh serial live      # tail -f on serial log
#   ./driver.sh serial stop      # stop serial capture daemon
#   ./driver.sh usb-status       # show USB enumeration state
#   ./driver.sh dmesg            # show kernel USB logs
#   ./driver.sh setup            # one-time: create serial log dir + poller script
#
# Build + test commands are in build.sh:
#   ./build.sh native     # native x86 tests
#   ./build.sh simulate   # ARM rp2040js simulation
#   ./build.sh build <target>   # build any cmake target
#   ./build.sh clean      # remove build artifacts
#   ./build.sh            # run all: native + simulate
#
# Requires: physical Pico W (hardware). Docker with lightsync-dev image (build.sh).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SKILL_DIR="${SCRIPT_DIR}"
PROJECT_DIR="$(cd "${SKILL_DIR}/../../.." && pwd)"   # LEDServer/

# Serial log directory (outside project, shared across sessions)
SERIAL_LOG_DIR="${HOME}/.claude/projects/-home-ashwin-workspace-LightSync/serial-log"
SERIAL_LOG="${SERIAL_LOG_DIR}/ledserver.log"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

# ── helpers ────────────────────────────────────────────────────────────────

die() { echo -e "${RED}FAIL:${NC} $*" >&2; exit 1; }
info() { echo -e "${GREEN}==>${NC} $*"; }
warn() { echo -e "${YELLOW}WARN:${NC} $*" >&2; }
detail() { echo -e "${CYAN}    ${NC} $*"; }

# Detect Pico device path — try /dev/serial/by-id/ first, then ttyACM*
find_pico_cdc() {
    local by_id
    by_id=$(find /dev/serial/by-id/ -maxdepth 1 \( -name '*raspberrypi*' -o -name '*rp2040*' \) 2>/dev/null | head -1) || true
    if [ -n "$by_id" ]; then
        echo "$by_id"
        return 0
    fi
    local tty
    tty=$(ls /dev/ttyACM* 2>/dev/null | head -1) || true
    if [ -n "$tty" ]; then
        echo "$tty"
        return 0
    fi
    return 1
}

find_pico_bootdev() {
    local link="/dev/disk/by-label/RPI-RP2"
    [ -e "$link" ] || return 1          # -e follows symlinks; fails on dangling
    local dev
    dev=$(readlink -f "$link" 2>/dev/null) || true
    if [ -n "$dev" ] && [ -b "$dev" ]; then
        echo "$dev"
        return 0
    fi
    return 1
}

get_usb_state() {
    # Returns: connected, bootsel, running, crashed, absent
    # Detect via device filesystem (no sudo, no lsusb AppArmor issues)
    if [ -e /dev/disk/by-label/RPI-RP2 ]; then
        echo "bootsel"
        return 0
    fi
    if ls /dev/ttyACM* &>/dev/null; then
        echo "running"
        return 0
    fi
    if ls /dev/serial/by-id/*raspberry* 2>/dev/null | grep -q . || \
       ls /dev/serial/by-id/*rp2040* 2>/dev/null | grep -q .; then
        echo "connected"
        return 0
    fi
    echo "absent"
}

# ── serial capture daemon ──────────────────────────────────────────────────

ensure_serial_log_dir() {
    mkdir -p "${SERIAL_LOG_DIR}"
}

start_serial_capture() {
    ensure_serial_log_dir
    "${SCRIPT_DIR}/serial_capture.py" start
}

stop_serial_capture() {
    "${SCRIPT_DIR}/serial_capture.py" stop
}

# ── flash ──────────────────────────────────────────────────────────────────

cmd_flash() {
    local uf2_path="${1:?Usage: driver.sh flash <path/to/firmware.uf2>}"

    # Resolve path
    if [[ "$uf2_path" != /* ]]; then
        uf2_path="$(cd "${PROJECT_DIR}" && realpath "$uf2_path")"
    fi

    [ -f "$uf2_path" ] || die "File not found: ${uf2_path}"

    info "Flashing ${uf2_path##*/} to Pico W..."

    # Detect Pico state to choose the right flash method
    if find_pico_bootdev &>/dev/null; then
        # ── Pico is in BOOTSEL mode ──────────────────────────────────
        info "Pico is in BOOTSEL mode — copying .uf2 to RPI-RP2 drive..."

        local bootdev mount_point
        bootdev=$(find_pico_bootdev) || die "BOOTSEL device disappeared."

        # Unmount ALL stale RPI-RP2 mounts to avoid copying to the wrong device
        local _stale_mps
        _stale_mps=$(mount | grep " on /media/" | grep RPI-RP2 | awk '{print $3}' | sort -u || true)
        if [ -n "$_stale_mps" ]; then
            echo "$_stale_mps" | while read _mp; do
                sudo umount -l "$_mp" 2>/dev/null || true
            done
            sleep 1
        fi

        # Re-read mount table after cleanup
        local _mp
        _mp=$(mount | grep "$bootdev" | awk '{print $3}' | head -1 || true)
        if [ -n "$_mp" ]; then
            mount_point="$_mp"
        else
            # Not mounted — try common locations
            mount_point="/media/ashwin/RPI-RP2"
            if [ ! -d "$mount_point" ]; then
                mount_point="/media/${USER}/RPI-RP2"
            fi
            if [ ! -d "$mount_point" ]; then
                mount_point="/tmp/pico-boot"
                sudo mkdir -p "$mount_point"
            fi
            sudo mount "$bootdev" "$mount_point" || die "Failed to mount BOOTSEL device at ${bootdev}"
        fi

        info "Mount point: ${mount_point}"
        sudo cp "$uf2_path" "$mount_point/"
        sudo sync

        if [[ "$mount_point" != "/media/"* ]]; then
            sudo umount "$mount_point"
            sudo rmdir "$mount_point" 2>/dev/null || true
        fi

        info "Firmware copied. Pico will reboot into firmware automatically."
        warn "If it does not, physically unplug and replug the USB cable."

    elif find_pico_cdc &>/dev/null; then
        # ── Pico is running ──────────────────────────────────────────
        info "Pico is running — using picotool..."

        if ! command -v picotool &>/dev/null; then
            die "picotool not found. Install it or put Pico in BOOTSEL mode first."
        fi

        if sudo picotool load -f "$uf2_path" 2>/dev/null; then
            info "Firmware flashed via picotool."
            info "Rebooting Pico into firmware..."
            sudo picotool reboot -a -f 2>/dev/null || warn "picotool reboot failed — Pico may need manual replug."
        else
            die "picotool load failed. Is the firmware connected? Try bootsel mode."
        fi

    else
        die "Pico not found. Put it in BOOTSEL mode (hold BOOTSEL, plug in) and retry."
    fi

    start_serial_capture
}

# ── bootsel ────────────────────────────────────────────────────────────────

cmd_bootsel() {
    info "Triggering BOOTSEL mode via 1200 baud trick..."

    local dev
    dev=$(find_pico_cdc) || die "No Pico CDC device found. Firmware must be running to use 1200 baud trick."

    info "Using device: ${dev}"

    # Method A: stty (preferred)
    if sudo stty -F "$dev" 1200 2>/dev/null; then
        info "1200 baud trick sent. Pico is entering BOOTSEL mode..."
        info "Wait ~3s, then check with: driver.sh usb-status"
        return 0
    fi

    # Method B: python3 fallback
    if command -v python3 &>/dev/null && python3 -c "import serial" &>/dev/null; then
        warn "stty failed — trying python3 serial fallback..."
        sudo python3 -c "
import serial
s = serial.Serial('${dev}', 1200)
s.close()
print('1200 baud trick sent via python3')
" 2>/dev/null || die "All methods failed."
        info "Pico is entering BOOTSEL mode..."
        info "Wait ~3s, then check with: driver.sh usb-status"
        return 0
    fi

    die "Neither stty nor python3-serial available. Use the physical BOOTSEL button."
}

# ── reboot ─────────────────────────────────────────────────────────────────

cmd_reboot() {
    info "Rebooting Pico firmware..."

    # Reboot requires the Pico to be running (CDC-ACM serial available)
    local serial_dev
    serial_dev=$(find_pico_cdc) || die "No Pico CDC-ACM device found. Pico must be running firmware."

    info "Using serial device: ${serial_dev}"
    sudo picotool reboot -a -f || die "picotool reboot failed."

    info "Pico is rebooting..."
}

# ── serial ─────────────────────────────────────────────────────────────────

cmd_serial() {
    local subcmd="${1:-log}"
    shift || true

    ensure_serial_log_dir

    case "$subcmd" in
        log)
            if [ ! -f "${SERIAL_LOG}" ]; then
                die "Serial log not found at ${SERIAL_LOG}. Run 'driver.sh flash' to start capture, or 'driver.sh serial start'."
            fi

            local lines=""
            local since_min=""

            # Parse arguments
            while [[ $# -gt 0 ]]; do
                case "$1" in
                    --last)
                        lines="${2:?Usage: driver.sh serial log --last N}"
                        shift 2
                        ;;
                    --since)
                        since_min="${2:?Usage: driver.sh serial log --since M}"
                        shift 2
                        ;;
                    *)
                        die "Unknown option: $1. Use --last N or --since M."
                        ;;
                esac
            done

            if [ -z "$lines" ] && [ -z "$since_min" ]; then
                die "Usage: driver.sh serial log --last N | --since M"
            fi

            if [ -n "$since_min" ]; then
                echo "=== Serial log (since ${since_min}) ==="
                grep "$since_min" "${SERIAL_LOG}" || echo "(no matching entries)"
            else
                echo "=== Serial log (last ${lines} lines) ==="
                tail -n "$lines" "${SERIAL_LOG}" || echo "(empty log)"
            fi
            ;;

        live)
            if [ ! -f "${SERIAL_LOG}" ]; then
                die "Serial log not found at ${SERIAL_LOG}. Run 'driver.sh flash' to start capture."
            fi
            echo "=== Serial log live (Ctrl+C to stop) ==="
            tail -f "${SERIAL_LOG}"
            ;;

        stop)
            stop_serial_capture
            ;;

        status)
            "${SCRIPT_DIR}/serial_capture.py" status
            ;;

        start)
            start_serial_capture
            ;;

        clear)
            if [ -f "${SERIAL_LOG}" ]; then
                truncate -s 0 "${SERIAL_LOG}"
                info "Serial log cleared."
            else
                touch "${SERIAL_LOG}"
                info "Serial log created (was empty)."
            fi
            ;;

        *)
            die "Unknown serial subcommand: $subcmd. Use: log, live, stop, status, start, clear"
            ;;
    esac
}

# ── usb-status ─────────────────────────────────────────────────────────────

cmd_usb_status() {
    local state
    state=$(get_usb_state)

    echo "=== Pico W USB State ==="
    echo "State: ${state}"
    echo ""

    case "$state" in
        bootsel)
            echo "BOOTSEL mode — ready to flash .uf2"
            local bootdev
            bootdev=$(find_pico_bootdev) || bootdev="(not mounted)"
            echo "  Device: ${bootdev}"
            ;;
        running)
            echo "Firmware running — USB CDC-ACM serial available"
            local dev
            dev=$(find_pico_cdc) || dev="(not found)"
            echo "  Serial: ${dev}"
            ;;
        connected)
            echo "Pico connected but unrecognized USB ID"
            ;;
        crashed)
            echo "Pico connected but firmware crashed before USB init"
            ;;
        absent)
            echo "No Pico W detected"
            ;;
    esac

    echo ""
    echo "--- USB devices ---"
    lsusb 2>/dev/null | grep -i "raspberrypi\|rp2040\|pico" 2>/dev/null || echo "(none or use sudo)"

    echo ""
    echo "--- Serial devices ---"
    ls /dev/ttyACM* 2>/dev/null || echo "(none)"

    if [ -d /dev/serial/by-id ]; then
        echo ""
        echo "--- Serial by-id ---"
        ls -la /dev/serial/by-id/ 2>/dev/null | grep -i "raspberrypi\|rp2040" || echo "(none)"
    fi
}

# ── dmesg ──────────────────────────────────────────────────────────────────

cmd_dmesg() {
    echo "=== Kernel USB logs (last 20 lines) ==="
    sudo dmesg 2>/dev/null | grep -E "usb [0-9]|ttyACM|RP2|pico|cdc_acm|New USB" | tail -20 || \
        sudo dmesg 2>/dev/null | tail -20
}

# ── setup (one-time) ───────────────────────────────────────────────────────

cmd_setup() {
    info "Setting up serial capture environment..."

    ensure_serial_log_dir

    # Create usb_state_poller.sh
    local poller="${SKILL_DIR}/usb_state_poller.sh"
    if [ -f "$poller" ]; then
        info "usb_state_poller.sh already exists — skipping."
    else
        cat > "$poller" << 'POLLER_EOF'
#!/usr/bin/env bash
# USB state poller for Claude Monitor.
# Outputs state change lines: bootsel, running, connected, absent
# Each line becomes a Monitor event → Claude decides whether to PushNotification.
#
# Detects via device filesystem (no sudo, no lsusb AppArmor issues).

prev="absent"

while true; do
    state="absent"

    # BOOTSEL mode: mass storage volume mounted
    if [ -e /dev/disk/by-label/RPI-RP2 ]; then
        state="bootsel"
    # Running mode: CDC-ACM serial available
    elif ls /dev/ttyACM* &>/dev/null; then
        state="running"
    # Connected but unrecognized
    elif ls /dev/serial/by-id/*raspberry* &>/dev/null || \
         ls /dev/serial/by-id/*rp2040* &>/dev/null; then
        state="connected"
    fi

    if [ "$state" != "$prev" ]; then
        echo "$state"
        prev="$state"
    fi

    sleep 2
done
POLLER_EOF
        chmod +x "$poller"
        info "Created usb_state_poller.sh"
    fi

    info "Setup complete."
    info "  Log dir: ${SERIAL_LOG_DIR}"
    info "  Poller:  ${SKILL_DIR}/usb_state_poller.sh"
    info ""
    info "Next steps:"
    info "  1. Flash firmware: driver.sh flash build/LEDServer.uf2"
    info "  2. Ask Claude to start USB monitor: 'Start the Pico USB state monitor'"
}

# ── main ───────────────────────────────────────────────────────────────────

case "${1:-help}" in
    # Hardware commands
    flash)      cmd_flash "${2:-}" ;;
    bootsel)    cmd_bootsel ;;
    reboot)     cmd_reboot ;;
    serial)     cmd_serial "${2:-log}" "${@:3}" ;;
    usb-status) cmd_usb_status ;;
    dmesg)      cmd_dmesg ;;
    setup)      cmd_setup ;;

    help|*)
        echo "Usage: $0 <command> [args...]"
        echo ""
        echo "Hardware (requires Pico W):"
        echo "  flash <path>     Flash uf2 to Pico W"
        echo "  bootsel          Enter BOOTSEL mode (1200 baud trick)"
        echo "  reboot           Reboot running firmware (picotool)"
        echo "  serial <cmd>     Serial operations (log/live/stop/status/start/clear)"
        echo "  usb-status       Show USB enumeration state"
        echo "  dmesg            Kernel USB logs"
        echo ""
        echo "Setup:"
        echo "  setup            One-time: create serial log dir + poller script"
        echo ""
        echo "Build + Test (use build.sh):"
        echo "  ./build.sh native     Native x86 tests"
        echo "  ./build.sh simulate   ARM rp2040js simulation"
        echo "  ./build.sh build <target>  Build any cmake target"
        echo "  ./build.sh clean      Remove build artifacts"
        exit 1
        ;;
esac
