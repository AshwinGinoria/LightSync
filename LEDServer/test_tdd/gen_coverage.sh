#!/bin/bash
# Generate gcov coverage report by computing union of covered lines across all test binaries
BASE="/home/ashwin/workspace/LightSync/LightSync/LEDServer/test_tdd"
SRC="$BASE/../src"
COV="$BASE/cov_bin"

# List of source files to report on
SOURCES=(
    "boot_flow.c"
    "captive_dns.c"
    "config_storage.c"
    "effects_engine.c"
    "flash_abstraction.c"
    "httpd.c"
    "led_engine.c"
    "mdns_service.c"
    "music_sync.c"
    "protocol_ddp.c"
    "provisioning_http.c"
    "settings_http.c"
)

echo "=========================================="
echo "  LEDServer Code Coverage Report"
echo "  Union across all test binaries"
echo "=========================================="
echo ""

# Temp file for collecting all covered lines across all binaries
> /tmp/all_covered_$$

for srcfile in "${SOURCES[@]}"; do
    srcpath="$SRC/$srcfile"
    if [ ! -f "$srcpath" ]; then
        srcpath="$BASE/$srcfile"
        if [ ! -f "$srcpath" ]; then
            continue
        fi
    fi

    # For each binary dir, run gcov and extract covered line numbers from the .gcov output
    > /tmp/covered_$$

    for bindir in "$COV"/*/; do
        # Run gcov, it creates a .gcov file
        gcov -o "$bindir" -l "$srcpath" >/dev/null 2>&1

        # Find the .gcov output file (gcov may append extra suffixes)
        gcov_file=$(ls "$bindir"/*.gcov 2>/dev/null | head -1)
        if [ -n "$gcov_file" ] && [ -f "$gcov_file" ]; then
            # Parse gcov output: lines like "        1:    42:    if (cond) {"
            # Executed lines start with a number (execution count), possibly preceded by spaces
            grep -E '^[[:space:]]+[0-9]+:' "$gcov_file" | \
                awk '{print $1}' | \
                sort -un >> /tmp/covered_$$
        fi

        # Clean up gcov output files to avoid clutter
        rm -f "$bindir"/*.gcov
    done

    # Compute union of covered lines
    covered_unique=$(sort -un /tmp/covered_$$ | grep -v '^$' | wc -l)

    # Count total executable lines
    total_exec=$(awk '
        /^[[:space:]]*$/ { next }
        /^[[:space:]]*\/\// { next }
        /^[[:space:]]*\/\*/ { next }
        /^[[:space:]]*\*/ { next }
        /^[[:space:]]*\}/ { next }
        /^[[:space:]]*\{/ { next }
        /^[[:space:]]*else/ { next }
        /^[[:space:]]*#/{ next }
        /^[[:space:]]*typedef/ { next }
        /^[[:space:]]*struct[[:space:]]/ { next }
        /^[[:space:]]*enum[[:space:]]/ { next }
        { count++ }
        END { print count }
    ' "$srcpath")

    if [ "$total_exec" -gt 0 ]; then
        pct=$(awk "BEGIN {printf \"%.1f\", ($covered_unique/$total_exec)*100}")
    else
        pct="0.0"
    fi

    printf "%-25s %4d/%-4d lines (%5s%%)\n" "$srcfile" "$covered_unique" "$total_exec" "$pct"

    # Detailed output
    echo ""
    echo "--- $srcfile ---"

    # Find uncovered executable lines
    total=$(wc -l < "$srcpath")
    uncovered=""
    for ((i=1; i<=total; i++)); do
        line_content=$(sed -n "${i}p" "$srcpath")
        # Skip non-executable lines
        if echo "$line_content" | grep -qE '^[[:space:]]*(//|/\*|\*|#|\}|else|\{|typedef|struct |enum |#endif|#else|#ifndef|#if |#define |#include |$)'; then
            continue
        fi
        if ! grep -qx "$i" /tmp/covered_$$ 2>/dev/null; then
            if [ -z "$uncovered" ]; then
                uncovered="$i"
            else
                uncovered="$uncovered,$i"
            fi
        fi
    done

    if [ -n "$uncovered" ]; then
        echo "  Uncovered lines: $uncovered"
        echo "$uncovered" | tr ',' '\n' | while read line; do
            content=$(sed -n "${line}p" "$srcpath" | sed 's/^[[:space:]]*//')
            if [ -n "$content" ]; then
                echo "    L${line}: $content"
            fi
        done
    else
        echo "  All lines covered!"
    fi
    echo ""
done

rm -f /tmp/covered_$$ /tmp/all_covered_$$
