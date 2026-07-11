# Music Sync Client

This document describes how to build a client that captures audio, computes an FFT spectrum, and sends it to the LightSync LED Server for music-reactive LED animations.

## Protocol

The LightSync Pico W listens on **UDP port 5006** for music sync packets. The server does NO audio processing — it receives pre-computed frequency band amplitudes.

### Packet Format

```
Byte 0:     effect_id   — selects the colour palette (0-5)
Byte 1:     num_bands   — number of frequency bands (8-32 recommended)
Byte 2..N:  band_values — uint8 per band, 0 = silent, 255 = max amplitude
```

Total size: `2 + num_bands` bytes (e.g., 18 bytes for 16 bands).

### Band-to-LED Mapping

Each frequency band controls a contiguous zone of LEDs:

- Zone size = `288 / num_bands` LEDs per band
- Bar height = `value * zone_size / 255` LEDs lit
- Low-frequency bands → start of strip, high bands → end

With 16 bands: 18 LEDs per band, creating a 16-bar spectrum visualiser across the strip.

### Effect IDs

| ID | Effect | Colour mapping |
|----|--------|---------------|
| 0 | Solid | All white, bands control brightness |
| 1 | Rainbow | Each band gets a hue from the colour wheel |

## Client Implementation (Rust)

### Dependencies

```toml
[dependencies]
cpal = "0.15"       # Cross-platform audio capture
rustfft = "6.2"     # FFT implementation
```

### Audio Pipeline

1. **Capture** — `cpal` opens the default input device at 44100 Hz (or 48000 Hz), mono
2. **Window** — Apply a Hann window to the input buffer to reduce spectral leakage
3. **FFT** — `rustfft` computes a real-to-complex forward FFT
4. **Magnitude** — Compute `sqrt(re² + im²)` for each FFT bin
5. **Bin to band** — Group FFT bins into logarithmic frequency bands (16-32 bands)
6. **Normalise** — Scale each band to 0-255, optionally with a decay/smoothing envelope
7. **Send** — Pack into the 2-byte header + band_values format, send via UDP

### Example: Logarithmic Band Binning

```rust
// 16 bands, 44100 Hz sample rate, 1024-point FFT → 512 usable bins
// Band edges (Hz): 20, 40, 80, 160, 320, 640, 1280, 2560, 5120, ...
fn bin_to_band(bin: usize, fft_size: usize, sample_rate: u32) -> usize {
    let freq = bin as f32 * sample_rate as f32 / fft_size as f32;
    // Map to band index via log2
    let band = (freq / 20.0).log2().clamp(0.0, 15.0) as usize;
    band.min(15)
}
```

### Python Test Script

A simple Python script for testing music sync without a full audio client:

```python
import socket
import time
import random

UDP_IP = "192.168.0.244"  # or lightsync-XXXX.local
UDP_PORT = 5006

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

while True:
    effect_id = 1  # rainbow
    num_bands = 16
    bands = bytes([random.randint(0, 255) for _ in range(num_bands)])
    packet = bytes([effect_id, num_bands]) + bands
    sock.sendto(packet, (UDP_IP, UDP_PORT))
    time.sleep(0.033)  # ~30 FPS
```

## Testing with Real Hardware

1. Flash the LEDServer firmware to a Pico W
2. Discover the IP via mDNS: `avahi-browse -r _lightsync._udp` (Linux) or `dns-sd -B _lightsync._udp .` (macOS)
3. Run the Python test script — LEDs should animate as a spectrum visualiser
4. Stop the script — after 5 seconds, the autonomous rainbow effect resumes

## Discovery

The music sync port (`5006`) is advertised in the mDNS TXT record `music_port=5006`. Clients can discover the device and all its ports by querying `_lightsync._udp.local`.
