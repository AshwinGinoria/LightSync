FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

# Build dependencies + CA certs (needed for HTTPS curl later)
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    git \
    cmake \
    make \
    g++ \
    python3 \
    ninja-build \
    && rm -rf /var/lib/apt/lists/*

# ARM GCC cross-compiler + newlib (C stdlib for bare-metal)
RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib \
    && rm -rf /var/lib/apt/lists/*

# Node.js for rp2040js
RUN curl -fsSL https://deb.nodesource.com/setup_20.x | bash - \
    && apt-get install -y --no-install-recommends nodejs \
    && rm -rf /var/lib/apt/lists/*

# Pico SDK
ENV PICO_SDK_PATH="/opt/pico-sdk"
RUN git clone --depth 1 --branch 2.1.0 https://github.com/raspberrypi/pico-sdk.git ${PICO_SDK_PATH} \
    && cd ${PICO_SDK_PATH} \
    && git submodule update --init --recursive \
    && cd tools \
    && cmake -B build -S . \
    && cmake --build build --parallel \
    && rm -rf build

# rp2040js (Wokwi RP2040 emulator — not on npm, must clone)
RUN git clone --depth 1 https://github.com/wokwi/rp2040js.git /opt/rp2040js \
    && cd /opt/rp2040js \
    && npm install \
    && npm run build

# CMakeLists.txt hardcodes $HOME/.pico-sdk — bridge to /opt/pico-sdk
RUN ln -sf /opt/pico-sdk /root/.pico-sdk

WORKDIR /workspace

# Drop into interactive shell
CMD ["bash"]
