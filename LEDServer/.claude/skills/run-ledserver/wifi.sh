#!/usr/bin/env bash
# LightSync LEDServer — WiFi AP testing.
#
# Usage:
#   ./wifi.sh connect              # Connect host to Pico AP (LightSync / lightsync)
#   ./wifi.sh disconnect           # Disconnect from Pico AP
#   ./wifi.sh status               # Show WiFi connection state
#   ./wifi.sh http-get <url>       # HTTP GET to Pico AP (192.168.4.1)
#   ./wifi.sh http-post <url> <data>  # HTTP POST to Pico AP
#   ./wifi.sh ping                 # Ping Pico AP gateway
#   ./wifi.sh scan                 # Scan for Pico AP (LightSync SSID)

set -euo pipefail

# Pico AP credentials (hardcoded in src/boot_flow.c)
AP_SSID="LightSync"
AP_PASS="lightsync"
AP_IP="192.168.4.1"

# Host WiFi interface (auto-detect)
WIFI_IFACE=""

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

die() { echo -e "${RED}FAIL:${NC} $*" >&2; exit 1; }
info() { echo -e "${GREEN}==>${NC} $*"; }
warn() { echo -e "${YELLOW}WARN:${NC} $*" >&2; }
detail() { echo -e "${CYAN}    ${NC} $*"; }

# Auto-detect WiFi interface (first non-loopback wireless interface)
detect_wifi_iface() {
    if [ -n "$WIFI_IFACE" ]; then
        echo "$WIFI_IFACE"
        return 0
    fi

    local iface
    iface=$(ip -o link show type wifi 2>/dev/null | awk -F': ' '{print $2}' | head -1) || true
    if [ -z "$iface" ]; then
        # Fallback: look for common names
        iface=$(ip -o link show 2>/dev/null | grep -i 'wlan\|wl' | awk -F': ' '{print $2}' | head -1) || true
    fi

    if [ -n "$iface" ]; then
        WIFI_IFACE="$iface"
        echo "$iface"
        return 0
    fi

    return 1
}

# ── connect ────────────────────────────────────────────────────────────────

cmd_connect() {
    local iface
    iface=$(detect_wifi_iface) || die "No WiFi interface found. Use --iface <name>."

    info "Connecting to Pico AP '${AP_SSID}' on interface ${iface}..."

    # Kill existing wpa_supplicant for this interface
    sudo pkill -x wpa_supplicant 2>/dev/null || true
    sleep 1

    # Create wpa_supplicant config (fixed path, cleaned up on disconnect)
    local wpa_conf="/tmp/.wpa_light_sync.conf"
    sudo mkdir -p /run/wpa_supplicant
    cat > "$wpa_conf" << EOF
ctrl_interface=/run/wpa_supplicant
update_config=1
network={
    ssid="${AP_SSID}"
    psk="${AP_PASS}"
    key_mgmt=WPA-PSK
}
EOF

    # Bring interface up and connect
    info "Bringing up ${iface}..."
    sudo ip link set "$iface" up
    sleep 1

    info "Running wpa_supplicant..."
    sudo wpa_supplicant -B -i "$iface" -c "$wpa_conf" || die "wpa_supplicant failed."

    info "Waiting for association (up to 15s)..."
    local tries=0
    while [ $tries -lt 15 ]; do
        if sudo wpa_cli -i "$iface" status 2>/dev/null | grep -q "wpa_state=COMPLETED"; then
            info "Connected to ${AP_SSID}."
            break
        fi
        sleep 1
        tries=$((tries + 1))
    done

    if [ $tries -ge 15 ]; then
        warn "Association may not have completed. Check with: ./wifi.sh status"
    fi

    # Get an IP — try DHCP first, then static
    info "Obtaining IP address..."
    if command -v dhclient &>/dev/null; then
        sudo dhclient "$iface" 2>/dev/null || true
    else
        info "dhclient not found — assigning static IP..."
        sudo ip addr add 192.168.4.2/24 dev "$iface" 2>/dev/null || true
    fi

    info "Connected to ${AP_SSID} (${AP_IP}/24)."
    info "Test: curl http://${AP_IP}/"
}

# ── disconnect ─────────────────────────────────────────────────────────────

cmd_disconnect() {
    local iface
    iface=$(detect_wifi_iface) || die "No WiFi interface found."

    info "Disconnecting from Pico AP on ${iface}..."
    sudo ip link set "$iface" down 2>/dev/null || true
    sudo pkill -x wpa_supplicant 2>/dev/null || true
    rm -f /tmp/.wpa_light_sync.conf
    info "Disconnected."
}

# ── status ─────────────────────────────────────────────────────────────────

cmd_status() {
    local iface
    iface=$(detect_wifi_iface) || { warn "No WiFi interface found."; return 0; }

    echo "=== WiFi Status ==="
    echo "Interface: ${iface}"

    # Check wpa_supplicant status
    local wpa_status
    wpa_status=$(sudo wpa_cli -i "$iface" status 2>/dev/null) || {
        warn "wpa_supplicant not running on ${iface}."
        return 0
    }

    local ssid bssid ip_mode ip_addr
    ssid=$(echo "$wpa_status" | grep "^ssid=" | cut -d= -f2-) || true
    bssid=$(echo "$wpa_status" | grep "^bssid=" | cut -d= -f2-) || true
    ip_mode=$(echo "$wpa_status" | grep "^ip_address=" | cut -d= -f2-) || true
    ip_addr=$(echo "$wpa_status" | grep "^addr_mode=" | cut -d= -f2-) || true

    echo "SSID: ${ssid:-<not connected>}"
    echo "BSSID: ${bssid:-<unknown>}"
    echo "IP: ${ip_addr:-<none>}"
    echo "Mode: ${ip_mode:-<unknown>}"

    # Check connectivity to Pico
    if ping -c 1 -W 2 "$AP_IP" &>/dev/null; then
        echo "Connectivity: OK (ping ${AP_IP})"
    else
        warn "Connectivity: FAIL (cannot ping ${AP_IP})"
    fi
}

# ── http-get ───────────────────────────────────────────────────────────────

cmd_http_get() {
    local url="${1:?Usage: wifi.sh http-get <url>}"
    local iface
    iface=$(detect_wifi_iface) || die "No WiFi interface found."

    # Ensure URL has http:// prefix
    [[ "$url" == http://* ]] || url="http://${AP_IP}/${url#/}"

    info "GET ${url} (interface: ${iface})"
    curl --interface "$iface" -sSL "$url" 2>/dev/null || \
        curl --interface "$iface" "$url"
}

# ── http-post ──────────────────────────────────────────────────────────────

cmd_http_post() {
    local url="${1:?Usage: wifi.sh http-post <url> <data>}"
    local data="${2:-}"
    local iface
    iface=$(detect_wifi_iface) || die "No WiFi interface found."

    [[ "$url" == http://* ]] || url="http://${AP_IP}/${url#/}"

    info "POST ${url} (interface: ${iface})"
    curl --interface "$iface" -sSL -X POST -d "$data" "$url" 2>/dev/null || \
        curl --interface "$iface" -X POST -d "$data" "$url"
}

# ── http-post-form ─────────────────────────────────────────────────────────
# POST a URL-encoded x-www-form-urlencoded form with named fields.
# Unlike http-post (raw -d), this survives '&', spaces, '+' and '%'
# in values — required for the provisioning ssid/password form.

cmd_http_post_form() {
    local url="${1:?Usage: wifi.sh http-post-form <url> <field=value>...}"
    shift
    local iface
    iface=$(detect_wifi_iface) || die "No WiFi interface found."

    [[ "$url" == http://* ]] || url="http://${AP_IP}/${url#/}"

    local args=()
    local field
    for field in "$@"; do
        args+=(--data-urlencode "$field")
    done

    info "POST form ${url} (interface: ${iface}) fields: $*"
    # -w reports the HTTP status as a trailing line so callers can detect
    # e.g. 302 (redirect = form accepted) vs 200 (error page).
    curl --interface "$iface" -sS -X POST -w '\nHTTP %{http_code}\n' "${args[@]}" "$url" 2>/dev/null || \
        curl --interface "$iface" -X POST -w '\nHTTP %{http_code}\n' "${args[@]}" "$url"
}

# ── ping ───────────────────────────────────────────────────────────────────

cmd_ping() {
    local iface
    iface=$(detect_wifi_iface) || die "No WiFi interface found."

    info "Pinging Pico AP (${AP_IP})..."
    ping -c 3 -W 2 "$AP_IP"
}

# ── scan ───────────────────────────────────────────────────────────────────

cmd_scan() {
    local iface
    iface=$(detect_wifi_iface) || die "No WiFi interface found."

    info "Scanning for Pico AP (SSID: ${AP_SSID}) on ${iface}..."

    # Use wpa_supplicant scan results
    sudo wpa_supplicant -i "$iface" -D wext -c /dev/null 2>/dev/null &
    local scan_pid=$!
    sleep 2
    sudo kill "$scan_pid" 2>/dev/null || true

    # Alternative: use iw scan
    if command -v iw &>/dev/null; then
        echo "=== Scan Results ==="
        sudo iw dev "$iface" scan 2>/dev/null | grep -A 2 "SSID: ${AP_SSID}" || \
            echo "No Pico AP found. Is it broadcasting?"
    else
        warn "iw not available — try: sudo apt install iw"
        warn "Or check manually: sudo iw dev ${iface} scan"
    fi
}

# ── main ───────────────────────────────────────────────────────────────────

case "${1:-help}" in
    connect)      cmd_connect ;;
    disconnect)   cmd_disconnect ;;
    status)       cmd_status ;;
    http-get)     cmd_http_get "${2:-}" ;;
    http-post)    cmd_http_post "${2:-}" "${3:-}" ;;
    http-post-form) cmd_http_post_form "${@:2}" ;;
    ping)         cmd_ping ;;
    scan)         cmd_scan ;;
    help|*)
        echo "Usage: $0 <command> [args...]"
        echo ""
        echo "WiFi AP testing (Pico W creates AP: ${AP_SSID} / ${AP_PASS}):"
        echo "  connect              Connect host to Pico AP"
        echo "  disconnect           Disconnect from Pico AP"
        echo "  status               Show WiFi connection state"
        echo "  http-get <url>       HTTP GET to Pico AP"
        echo "  http-post <url> <data>  HTTP POST to Pico AP"
        echo "  http-post-form <url> <field=value>...  URL-encoded POST (provisioning form)"
        echo "  ping                 Ping Pico AP gateway"
        echo "  scan                 Scan for Pico AP"
        exit 1
        ;;
esac
