#!/usr/bin/env bash
# LightSync LEDServer — Reproduce Pico W AP connection issue.
#
# Symptom: Phone connects to Pico AP → can't open settings page →
#          disconnects after a few seconds → can't reconnect ("connection failed").
#
# This script simulates the phone connection pattern:
#   1. DHCP handshake
#   2. IMMEDIATE DNS burst (captive portal detection)
#   3. HTTP requests to captive portal / settings page
#   4. Rapid reconnect attempts (phone retries)
#   5. Poll serial log for errors/panics
#   6. CHECK FOR SILENT HANG (network unresponsiveness)
#
# Usage:
#   ./test_pico.sh              # Run the full repro
#   ./test_pico.sh provision --ssid "MyWifi" [--pass "secret"]   # Submit /connect wifi form, reboot, verify STA boot
#   ./test_pico.sh --dns N      # DNS queries (default 20)
#   ./test_pico.sh --http N     # HTTP connections (default 8)
#   ./test_pico.sh --reconnect N # Reconnect attempts (default 5)
#   ./test_pico.sh --dry-run    # Show what would happen
#
# Prerequisites:
#   - Pico W flashed with LEDServer firmware
#   - Host connected to Pico AP (LightSync / lightsync)
#
# After the issue the Pico may be unresponsive. Reboot with:
#   ./driver.sh reboot

set -euo pipefail

# ── Defaults ────────────────────────────────────────────────────────────────

DNS_QUERIES=20
HTTP_CONNECTIONS=8
RECONNECT_ATTEMPTS=5
MAX_POLL_SECONDS=60
POLL_INTERVAL=2

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DRIVER="${SCRIPT_DIR}/driver.sh"
WIFI="${SCRIPT_DIR}/wifi.sh"

# Pico AP details
AP_IP="192.168.4.1"
AP_SSID="LightSync"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

# Counters
dns_attempted=0
http_attempted=0
reconnect_attempted=0
panic_found=0
hang_detected=0
panic_output=""
log_stopped=0

# ── Helpers ─────────────────────────────────────────────────────────────────

die() { echo -e "${RED}FAIL:${NC} $*" >&2; exit 1; }
info() { echo -e "${GREEN}==>${NC} $*"; }
warn() { echo -e "${YELLOW}WARN:${NC} $*" >&2; }
detail() { echo -e "${CYAN}    ${NC} $*"; }
step() { echo -e "\n${BOLD}${GREEN}--- $* ---${NC}"; }

# Serial log path (shared with driver.sh)
SERIAL_LOG="${HOME}/.claude/projects/-home-ashwin-workspace-LightSync/serial-log/ledserver.log"

# ── Provisioning test ──────────────────────────────────────────────────────
# Submit ssid/password via the captive-portal /connect form, verify the Pico
# saves the config, then reboot and confirm it boots into STA mode
# (connects to the configured WiFi).
#
# Usage:
#   ./test_pico.sh provision --ssid "MyWifi" [--pass "secret"]
#
# Markers watched in the serial log:
#   POST /connect accepted  -> HTTP 302 + "config_save completed"
#   Reboot picks up config  -> "config valid, trying STA mode"
#   STA connect SUCCESS     -> "Server running at"
#   STA connect FAILED      -> fallback to "entering AP mode path"

PROVISION_SSID=""
PROVISION_PASS=""

# Poll serial log up to timeout_s for a grep -E pattern.
wait_for_marker() {
    local pattern="$1" timeout_s="${2:-60}" label="$3"
    local elapsed=0
    while [ "$elapsed" -lt "$timeout_s" ]; do
        if [ -f "$SERIAL_LOG" ] && grep -qE "$pattern" "$SERIAL_LOG"; then
            detail "marker '${label}' seen after ${elapsed}s"
            return 0
        fi
        sleep 2
        elapsed=$((elapsed + 2))
    done
    warn "marker '${label}' NOT seen within ${timeout_s}s"
    return 1
}

run_provision() {
    [ -n "$PROVISION_SSID" ] || die "Usage: ./test_pico.sh provision --ssid <SSID> [--pass <PASSWORD>]"

    step "[1/7] Pre-flight: Pico present & running"
    usb_state=$("${DRIVER}" usb-status 2>&1) || true
    echo "$usb_state"
    if echo "$usb_state" | grep -q "absent"; then die "Pico W not detected. Connect it and retry."; fi
    if echo "$usb_state" | grep -q "bootsel"; then die "Pico W is in BOOTSEL mode. Flash firmware first."; fi

    step "[2/7] Clear serial log + reboot (expect AP / captive-portal boot)"
    "${DRIVER}" serial clear
    "${DRIVER}" reboot
    info "Waiting for boot..."
    if ! wait_for_marker "BOOT_COMPLETE|entering main loop|Server running at" 30 "boot-complete"; then
        die "Pico did not complete boot within 30s"
    fi

    if grep -q "Server running at" "$SERIAL_LOG"; then
        warn "Pico is ALREADY in STA mode (valid WiFi config present)."
        warn "Provisioning test needs AP mode — erase config or use a fresh device."
        return 1
    fi
    if grep -q "AP mode: entering main loop" "$SERIAL_LOG"; then
        info "Pico is in AP mode (captive portal up)."
    fi

    step "[3/7] Connect host to Pico AP"
    "${WIFI}" connect || die "Failed to connect to Pico AP"

    step "[4/7] Confirm provisioning page serves (GET /)"
    page=$("${WIFI}" http-get / 2>&1 || true)
    if echo "$page" | grep -q "LEDServer WiFi Setup"; then
        info "Provisioning page served correctly."
    else
        warn "Provisioning page not recognized. First lines:"
        echo "$page" | head -10
    fi

    step "[5/7] Submit ssid/password form (POST /connect)"
    post_out=$("${WIFI}" http-post-form /connect "ssid=${PROVISION_SSID}" "password=${PROVISION_PASS}" 2>&1 || true)
    echo "$post_out" | tail -5
    if echo "$post_out" | grep -q "HTTP 302"; then
        info "Form accepted (302 → /connected)."
    else
        die "POST /connect did not return 302. Got: $(echo "$post_out" | grep -o 'HTTP [0-9]*' | tail -1 || echo 'no response')"
    fi

    info "Verifying config_save reached flash..."
    wait_for_marker "config_save completed" 10 "config_save"

    step "[6/7] Reboot → monitor for STA-mode boot (wifi connect attempt)"
    "${DRIVER}" serial clear   # scope marker checks to THIS boot only (avoid stale matches)
    "${DRIVER}" reboot
    info "Watching boot flow for STA attempt..."
    if wait_for_marker "config valid, trying STA mode" 30 "STA-attempt"; then
        info "Pico detected saved config and is attempting STA connection."
    else
        warn "Did not see 'config valid, trying STA mode' — config may not have saved."
    fi

    step "[7/7] Watching for STA result"
    if wait_for_marker "Server running at" 50 "STA-connected"; then
        info "RESULT: SUCCESS — Pico connected to WiFi '${PROVISION_SSID}' via provisioning form."
        info "Serial log tail:"
        tail -n 15 "$SERIAL_LOG" | while read -r line; do detail "$line"; done
        return 0
    elif wait_for_marker "entering AP mode path|AP mode: entering main loop" 50 "AP-fallback"; then
        warn "Pico fell back to AP mode — WiFi connect to '${PROVISION_SSID}' FAILED."
        warn "Check: SSID exists? Password correct? 2.4 GHz? WPA2/WPA3?"
        return 1
    else
        warn "No STA-success or AP-fallback within 50s — possible hang. Check serial log."
        return 2
    fi
}

# ── Provisioning subcommand ─────────────────────────────────────────────────

if [ "${1:-}" = "provision" ]; then
    shift
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --ssid)  PROVISION_SSID="${2:?Usage: --ssid <SSID>}"; shift 2 ;;
            --pass|--password) PROVISION_PASS="${2:?Usage: --pass <PASSWORD>}"; shift 2 ;;
            *) die "Unknown provision arg: $1 (expected --ssid <SSID> [--pass <PASSWORD>])" ;;
        esac
    done
    run_provision
    exit $?
fi

# ── Argument parsing ────────────────────────────────────────────────────────

DRY_RUN=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --dns)
            DNS_QUERIES="${2:?Usage: --dns N}"
            shift 2
            ;;
        --http)
            HTTP_CONNECTIONS="${2:?Usage: --http N}"
            shift 2
            ;;
        --reconnect)
            RECONNECT_ATTEMPTS="${2:?Usage: --reconnect N}"
            shift 2
            ;;
        --dry-run)
            DRY_RUN=1
            shift
            ;;
        --help|-h)
            head -25 "$0" | grep '^#' | sed 's/^# \?//'
            exit 0
            ;;
        *)
            die "Unknown argument: $1"
            ;;
    esac
done

# ── Dry-run mode ────────────────────────────────────────────────────────────

if [ "$DRY_RUN" -eq 1 ]; then
    echo "=== Dry Run Mode ==="
    echo "  DNS queries: ${DNS_QUERIES}"
    echo "  HTTP connections: ${HTTP_CONNECTIONS}"
    echo "  Reconnect attempts: ${RECONNECT_ATTEMPTS}"
    echo "  AP IP: ${AP_IP}"
    echo "  AP SSID: ${AP_SSID}"
    echo "  Max poll time: ${MAX_POLL_SECONDS}s"
    echo ""
    echo "Phone-like connection sequence:"
    echo "  1. Clear serial log"
    echo "  2. Reboot Pico"
    echo "  3. Connect to AP"
    echo "  4. DHCP handshake (automatic)"
    echo "  5. IMMEDIATE burst of ${DNS_QUERIES} DNS queries (captive portal detection)"
    echo "  6. ${HTTP_CONNECTIONS} concurrent HTTP connections (settings page)"
    echo "  7. ${RECONNECT_ATTEMPTS} reconnect attempts (phone retry)"
    echo "  8. Poll serial log for errors/panics"
    echo "  9. CHECK FOR SILENT HANG (network unresponsiveness)"
    echo ""
    echo "Run without --dry-run to execute."
    exit 0
fi

# ── Step 1: Pre-flight check ───────────────────────────────────────────────

step "[1/9] Pre-flight: check Pico is connected and running"

if command -v "${DRIVER}" &>/dev/null; then
    usb_state=$("${DRIVER}" usb-status 2>&1) || true
    echo "$usb_state"

    if echo "$usb_state" | grep -q "absent"; then
        die "Pico W not detected. Connect the Pico W via USB and retry."
    fi
    if echo "$usb_state" | grep -q "bootsel"; then
        die "Pico W is in BOOTSEL mode. Flash firmware first, then retry."
    fi
    if echo "$usb_state" | grep -q "running"; then
        info "Pico W is running — proceeding."
    else
        warn "Unexpected USB state: $(echo "$usb_state" | head -1)"
        warn "Proceeding anyway — some checks may fail."
    fi
else
    die "driver.sh not found at ${DRIVER}"
fi

# ── Step 2: Clear old serial log ───────────────────────────────────────────

step "[2/9] Clear old serial log"

if command -v "${DRIVER}" &>/dev/null; then
    "${DRIVER}" serial clear
else
    die "driver.sh not found at ${DRIVER}"
fi

# ── Step 3: Reboot Pico ────────────────────────────────────────────────────

step "[3/9] Reboot Pico (fresh boot on cleared log)"

if command -v "${DRIVER}" &>/dev/null; then
    "${DRIVER}" reboot
else
    die "driver.sh not found at ${DRIVER}"
fi

info "Waiting for Pico to boot and USB serial to come up..."
boot_wait=0
while [ "$boot_wait" -lt 10 ]; do
    sleep 1
    boot_wait=$((boot_wait + 1))
    if [ -e /dev/ttyACM0 ] && [ -f "${HOME}/.claude/projects/-home-ashwin-workspace-LightSync/serial-log/ledserver.log" ]; then
        boot_lines=$(wc -l < "${HOME}/.claude/projects/-home-ashwin-workspace-LightSync/serial-log/ledserver.log" 2>/dev/null || echo "0")
        if [ "$boot_lines" -gt 0 ]; then
            info "Pico is back (serial log has ${boot_lines} lines)."
            break
        fi
    fi
done

if [ "$boot_wait" -ge 10 ]; then
    warn "Pico may not have come back online. Check with: driver.sh usb-status"
fi

# ── Step 4: Connect host to Pico AP ────────────────────────────────────────

step "[4/9] Connect host to Pico AP (${AP_SSID})"

if command -v "${WIFI}" &>/dev/null; then
    "${WIFI}" connect
else
    die "wifi.sh not found at ${WIFI}"
fi

# ── Step 5: Verify AP is reachable ─────────────────────────────────────────

step "[5/9] Verify AP is reachable"

if command -v "${WIFI}" &>/dev/null; then
    "${WIFI}" ping || warn "Ping failed — AP may still be initializing."
else
    ping -c 3 -W 2 "$AP_IP" || warn "Cannot ping ${AP_IP}."
fi

# ── Step 6: DNS burst (phone captive portal detection) ─────────────────────

step "[6/9] Firing ${DNS_QUERIES} DNS queries (captive portal detection)"

DOMAINS=(
    "connectivitycheck.gstatic.com"
    "captive.apple.com"
    "www.google.com"
    "dns.google"
    "www.msftconnecttest.com"
    "example.com"
    "api.apple-cdn.net"
    "lens.l.google.com"
)

for i in $(seq 1 "$DNS_QUERIES"); do
    domain_idx=$(( (i - 1) % ${#DOMAINS[@]} ))
    domain="${DOMAINS[$domain_idx]}"
    dig @${AP_IP} "${domain}" +short +timeout=1 +tries=1 >/dev/null 2>&1 || true
    dns_attempted=$((dns_attempted + 1))
    if [ $((i % 5)) -eq 0 ]; then
        sleep 0.2
    fi
done

info "DNS burst complete: ${dns_attempted} queries fired."

# ── Step 7: HTTP connections (settings page) ───────────────────────────────

step "[7/9] Firing ${HTTP_CONNECTIONS} concurrent HTTP connections (settings page)"

pids=()
for i in $(seq 1 "$HTTP_CONNECTIONS"); do
    # Try both root and /settings paths (what the phone might hit)
    curl -s -m 5 --connect-timeout 3 "http://${AP_IP}/" >/dev/null 2>&1 &
    pids+=($!)
    http_attempted=$((http_attempted + 1))
done

info "Launched ${#pids[@]} background curl processes."
for pid in "${pids[@]}"; do
    wait "$pid" 2>/dev/null || true
done

info "All ${http_attempted} HTTP connections completed."

# ── Step 8: Reconnect attempts (phone retry pattern) ──────────────────────

step "[8/9] Simulating ${RECONNECT_ATTEMPTS} reconnect attempts (phone retry)"

for i in $(seq 1 "$RECONNECT_ATTEMPTS"); do
    info "Reconnect attempt ${i}/${RECONNECT_ATTEMPTS}..."

    # Rapid HTTP burst — phone retries immediately
    curl -s -m 3 --connect-timeout 2 "http://${AP_IP}/" >/dev/null 2>&1 &
    curl -s -m 3 --connect-timeout 2 "http://${AP_IP}/settings" >/dev/null 2>&1 &
    wait 2>/dev/null || true

    # DNS query — phone checks connectivity
    dig @${AP_IP} "connectivitycheck.gstatic.com" +short +timeout=1 +tries=1 >/dev/null 2>&1 || true

    reconnect_attempted=$((reconnect_attempted + 2))
    sleep 1
done

info "Reconnect attempts complete: ${reconnect_attempted} requests fired."

# ── Step 8.5: Connectivity Check (Silent Hang Detection) ──────────────────

step "[8.5/9] Verifying device connectivity (checking for silent hang)"

# Try to ping the AP
if ping -c 3 -W 2 "$AP_IP" >/dev/null 2>&1; then
    info "Device is still responding to ping."
    hang_detected=0
else
    warn "Device is unresponsive to ping! (Possible silent hang)"
    hang_detected=1
fi

# Try a quick HTTP check as well
if ! curl -s -m 3 --connect-timeout 3 "http://${AP_IP}/" >/dev/null 2>&1; then
    warn "Device is unresponsive to HTTP!"
    hang_detected=1
fi

if [ "$hang_detected" -eq 1 ]; then
    info "Hang detected."
else
    # ── Step 9: Poll serial log for errors/panics ──────────────────────────────

    step "[9/9] Polling serial log for errors/panics (up to ${MAX_POLL_SECONDS}s)..."

    elapsed=0
    last_log_size=0

    while [ "$elapsed" -lt "$MAX_POLL_SECONDS" ]; do
        sleep "$POLL_INTERVAL"
        elapsed=$((elapsed + POLL_INTERVAL))

        if [ -f "${HOME}/.claude/projects/-home-ashwin-workspace-LightSync/serial-log/ledserver.log" ]; then
            current_log_size=$(wc -c < "${HOME}/.claude/projects/-home-ashwin-workspace-LightSync/serial-log/ledserver.log" 2>/dev/null || echo "0")
        else
            current_log_size=0
        fi

        # Check if log stopped growing (Pico may be dead)
        if [ "$current_log_size" -eq "$last_log_size" ] && [ "$last_log_size" -gt 0 ]; then
            log_stopped=$((log_stopped + 1))
            if [ "$log_stopped" -ge 3 ]; then
                warn "Serial log stopped growing for ${log_stopped} polls — Pico may be dead."
                break
            fi
        else
            log_stopped=0
        fi
        last_log_size=$current_log_size

        if [ -f "${HOME}/.claude/projects/-home-ashwin-workspace-LightSync/serial-log/ledserver.log" ]; then
            panic_output=$(tail -n 20 "${HOME}/.claude/projects/-home-ashwin-workspace-LightSync/serial-log/ledserver.log" 2>/dev/null) || true

            if echo "$panic_output" | grep -q '\*\*\* PANIC \*\*\*'; then
                panic_found=1
                info "PANIC detected!"
                break
            fi
            if echo "$panic_output" | grep -qi 'Out of memory\|malloc.*failed\|HardFault\|MemManage\|BusFault'; then
                panic_found=1
                info "Error detected!"
                break
            fi
        fi

        info "  [${elapsed}s] No panic yet. Last log lines:"
        if [ -f "${HOME}/.claude/projects/-home-ashwin-workspace-LightSync/serial-log/ledserver.log" ]; then
            tail -n 5 "${HOME}/.claude/projects/-home-ashwin-workspace-LightSync/serial-log/ledserver.log" 2>/dev/null | while read -r line; do
                detail "$line"
            done
        fi
    done
fi

# ── Results ────────────────────────────────────────────────────────────────

step "Results"

if [ "$panic_found" -eq 1 ] || [ "$hang_detected" -eq 1 ]; then
    echo -e "\n${BOLD}${RED}ISSUE DETECTED!${NC}\n"
    if [ "$panic_found" -eq 1 ]; then
        echo "  Reason: Explicit panic/error detected in serial log."
    else
        echo "  Reason: Silent hang detected (no network response after stress test)."
    fi
    echo ""
    echo "  === Serial log (last 20 lines) ==="
    if [ -n "$panic_output" ]; then
        echo "$panic_output" | while read -r line; do
            echo "  $line"
        done
    else
        echo "  (No panic logs captured)"
    fi
    echo ""
    echo -e "${YELLOW}The Pico may be UNRESPONSIVE.${NC}"
    echo -e "  To recover: ${DRIVER} reboot"
else
    echo -e "\n${GREEN}SUCCESS: NO ISSUE DETECTED${NC}\n"
    echo "  DNS queries fired: ${dns_attempted}"
    echo "  HTTP connections fired: ${http_attempted}"
    echo "  Reconnect requests fired: ${reconnect_attempted}"
    echo "  Poll timeout after: ${MAX_POLL_SECONDS}s"
    echo ""
    if [ "$log_stopped" -ge 3 ]; then
        echo -e "  ${YELLOW}The serial log stopped growing — the Pico may be dead${NC}"
        echo -e "  ${YELLOW}but no explicit error was captured in the log.${NC}"
    fi
    echo ""
    echo "  === Last 20 serial log lines ==="
    if [ -f "${HOME}/.claude/projects/-home-ashwin-workspace-LightSync/serial-log/ledserver.log" ]; then
        tail -n 20 "${HOME}/.claude/projects/-home-ashwin-workspace-LightSync/serial-log/ledserver.log" 2>/dev/null | while read -r line; do
            echo "  $line"
        done
    else
        echo "  (serial log not found)"
    fi
fi

# ── Cleanup ────────────────────────────────────────────────────────────────

step "Cleanup — disconnect from AP"

if command -v "${WIFI}" &>/dev/null; then
    "${WIFI}" disconnect 2>/dev/null || warn "Disconnect may have already happened."
    info "Disconnected from ${AP_SSID}."
else
    warn "wifi.sh not available — manual disconnect needed."
fi

echo ""
echo "=== Done ==="
if [ "$panic_found" -eq 1 ] || [ "$hang_detected" -eq 1 ]; then
    echo "Issue was successfully reproduced."
    exit 0
else
    echo "Issue was NOT reproduced."
    exit 1
fi
