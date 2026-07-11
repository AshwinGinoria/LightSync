#!/usr/bin/env bash
# USB state poller for Claude Monitor.
# Outputs state change lines: bootsel, running, connected, absent
# Each line becomes a Monitor event → Claude decides whether to PushNotification.
#
# Detects via device filesystem (no sudo, no lsusb AppArmor issues).

set -euo pipefail

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
