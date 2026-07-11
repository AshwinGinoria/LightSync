#ifndef MUSIC_SYNC_H
#define MUSIC_SYNC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ── Music sync protocol ──────────────────────────────────────────────
 *
 * The Pico W does NO FFT.  A desktop client captures audio, computes the
 * frequency spectrum (FFT), extracts per-band amplitudes (0-255), and
 * sends a compact UDP packet to port 5006.
 *
 * Packet format (2 + N bytes, where N = num_bands):
 *   Byte   0:  effect_id   — maps to effects_engine effect selection (0-5)
 *   Byte   1:  num_bands   — number of frequency bands (typically 8-32)
 *   Bytes 2+: band values  — uint8 each, 0 = silent, 255 = max amplitude
 *
 * Band-to-LED mapping:
 *   Each band controls LED_LENGTH / num_bands consecutive LEDs.
 *   With 16 bands and 288 LEDs: 18 LEDs per band.
 *   Low bands → start of strip, high bands → end of strip.
 *
 * Effect integration:
 *   The effect_id byte selects which effects_engine effect to use.
 *   Band amplitudes are written directly to led_buffer[] with the
 *   selected effect's colour palette applied as an intensity mask.
 *   Currently the bands write raw intensity (R=G=B=band_value) into
 *   each band's LED zone, creating a simple spectrum visualiser. */

#define MUSIC_SYNC_PORT   5006
#define MUSIC_MAX_BANDS   64

/* ── Public API ─────────────────────────────────────────────────────── */

/* Initialise the music-sync UDP listener on port 5006.
 * Returns a pointer to the udp_pcb on success, NULL on failure. */
void *music_sync_init(void);

#ifdef __cplusplus
}
#endif

#endif /* MUSIC_SYNC_H */
