#!/usr/bin/env bash
# LightSync LEDServer — Docker build + test pipeline.
#
# Usage:
#   ./build.sh                    # run all: native tests + build firmware
#   ./build.sh native             # native x86 tests (10 test binaries)
#   ./build.sh build <target>     # build any cmake target (e.g. LEDServer, heartbeat)
#   ./build.sh clean              # remove all build artifacts
#
# Requires: Docker with lightsync-dev image.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "${SCRIPT_DIR}/../../.." && pwd)"   # LEDServer/
DOCKER_IMAGE="lightsync-dev"

RED='\033[0;31m'
GREEN='\033[0;32m'
NC='\033[0m'

die() { echo -e "${RED}FAIL:${NC} $*" >&2; exit 1; }
info() { echo -e "${GREEN}==>${NC} $*"; }

require_docker() {
    docker ps &>/dev/null || die "Docker not running."
    docker images --format '{{.Repository}}' | grep -qx "${DOCKER_IMAGE}" || \
        die "Image '${DOCKER_IMAGE}' not found. Build it: docker build -t ${DOCKER_IMAGE} ."
}

# ── helpers ────────────────────────────────────────────────────────────────

_clone_picoled() {
    docker run --rm --memory=4g --cpus=2 \
        -v "$(cd "${PROJECT_DIR}/../.." && pwd):/workspace" \
        "${DOCKER_IMAGE}" bash -c "
            cd /workspace/LightSync/LEDServer
            rm -rf PicoLED
            git clone --depth 1 https://github.com/usedbytes/picoled.git PicoLED
        " || die "Failed to clone PicoLED"
}

_build_target() {
    local target="$1" uf2_name="$2"
    require_docker

    # PicoLED is always needed — clone if missing
    if [ ! -f "${PROJECT_DIR}/PicoLED/PicoLed.cmake" ]; then
        info "PicoLED not found — cloning..."
        _clone_picoled
    fi

    local build_output
    if build_output=$(docker run --rm --memory=4g --cpus=2 \
        -v "$(cd "${PROJECT_DIR}/../.." && pwd):/workspace" \
        "${DOCKER_IMAGE}" bash -c "
            set -eo pipefail
            cd /opt/pico-sdk
            git submodule update --init --recursive 2>/dev/null
            rm -rf /root/.pico-sdk
            ln -s /opt/pico-sdk /root/.pico-sdk
            cd /workspace/LightSync/LEDServer
            rm -rf build && mkdir build && cd build
            cmake .. -G 'Unix Makefiles' 2>&1
            cmake --build . --parallel --target ${target} 2>&1
            ls -lah ${uf2_name}.elf ${uf2_name}.uf2 ${uf2_name}.hex 2>/dev/null || echo 'Some outputs missing'
        "); then
        echo "$build_output"
    else
        echo "$build_output" >&2
        die "Build failed — see errors above"
    fi
}

# ── commands ───────────────────────────────────────────────────────────────

cmd_clean() {
    info "Cleaning build artifacts..."
    docker run --rm --memory=4g --cpus=2 \
        -v "$(cd "${PROJECT_DIR}/../.." && pwd):/workspace" \
        "${DOCKER_IMAGE}" bash -c "
            rm -rf /workspace/LightSync/LEDServer/test_tdd/build-native
            rm -rf /workspace/LightSync/LEDServer/build
        " 2>/dev/null
    echo "Done."
}

cmd_native() {
    info "Native x86 tests (10 test binaries)"
    require_docker
    docker run --rm --memory=4g --cpus=2 \
        -v "$(cd "${PROJECT_DIR}/../.." && pwd):/workspace" \
        "${DOCKER_IMAGE}" bash -c "
            set -e
            cd /workspace/LightSync/LEDServer/test_tdd
            rm -rf build-native && mkdir build-native && cd build-native
            cmake .. -G 'Unix Makefiles' >/dev/null
            cmake --build . --parallel >/dev/null
            echo '=== Running all test binaries ==='
            for bin in test_* ledserver_tdd; do
                if [ -x \"\$bin\" ]; then
                    echo \"--- Running \$bin ---\"
                    ./\$bin || { echo \"FAILED: \$bin\"; exit 1; }
                fi
            done
        " || die "Native tests failed"
}

cmd_build() {
    local target="${1:?Usage: build.sh build <target_name>}"
    _build_target "$target" "$target" "yes"
}

# ── main ───────────────────────────────────────────────────────────────────

case "${1:-all}" in
    all)
        cmd_native
        info "All checks passed."
        ;;
    native)     cmd_native ;;
    build)      cmd_build "${2:-}" ;;
    clean)      cmd_clean ;;
    *)
        echo "Usage: $0 {all|native|build <target>|clean}"
        exit 1
        ;;
esac
