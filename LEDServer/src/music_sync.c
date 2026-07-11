#include "music_sync.h"
#include "led_engine.h"
#include "effects_engine.h"

#include "lwip/udp.h"

#include <stdio.h>
#include <string.h>

/* ── Band → LED zone mapping ──────────────────────────────────────────
 *
 * Each frequency band maps to a contiguous block of LEDs.  The bar
 * height for each band is `value * LEDs_per_band / 255` — integer
 * only, no floats.  All LEDs in a band get the same colour,
 * top-to-bottom fill. */

static void music_map_bands(const uint8_t *bands, uint8_t num_bands,
                            effect_id_t effect_id) {
    if (num_bands == 0 || num_bands > MUSIC_MAX_BANDS) return;

    uint16_t leds_per_band = LED_LENGTH / num_bands;
    if (leds_per_band < 1) leds_per_band = 1;

    uint8_t b;
    for (b = 0; b < num_bands; b++) {
        uint8_t  value     = bands[b];
        uint16_t band_start = (uint16_t)b * leds_per_band;
        uint16_t band_end   = band_start + leds_per_band;
        if (band_end > LED_LENGTH) band_end = LED_LENGTH;

        /* Height of the bar in LEDs (0 to leds_per_band) */
        uint16_t bar_height = ((uint16_t)value * leds_per_band) / 255u;
        if (bar_height > leds_per_band) bar_height = leds_per_band;

        /* Fill bar bottom-to-top (higher index = higher LED on strip) */
        uint16_t i;
        for (i = band_start; i < band_end; i++) {
            uint16_t off = i * 3;
            uint16_t rel = i - band_start;

            if (rel < bar_height) {
                /* Lit: use a hue based on band index + effect selection */
                uint8_t hue;
                switch (effect_id) {
                case EFFECT_SOLID:
                    /* White with brightness proportional to value */
                    hue = value;
                    led_buffer[off]     = hue;
                    led_buffer[off + 1] = hue;
                    led_buffer[off + 2] = hue;
                    break;
                case EFFECT_RAINBOW:
                default:
                    /* Spread hue wheel across bands */
                    {
                        uint8_t band_hue = (uint8_t)(b * 255u / num_bands);
                        /* Simple RGB from hue: 3-segment like rainbow LUT but inline */
                        uint8_t seg  = band_hue / 85;
                        uint8_t ramp = (uint8_t)((band_hue % 85) * 3u);

                        switch (seg) {
                        case 0:
                            led_buffer[off]     = 255;
                            led_buffer[off + 1] = ramp;
                            led_buffer[off + 2] = 0;
                            break;
                        case 1:
                            led_buffer[off]     = (uint8_t)(255u - ramp);
                            led_buffer[off + 1] = 255;
                            led_buffer[off + 2] = ramp;
                            break;
                        default:
                            led_buffer[off]     = ramp;
                            led_buffer[off + 1] = (uint8_t)(255u - ramp);
                            led_buffer[off + 2] = 255;
                            break;
                        }
                        /* Scale by the band value intensity */
                        led_buffer[off]     = (uint8_t)(((uint16_t)led_buffer[off]     * value) / 255u);
                        led_buffer[off + 1] = (uint8_t)(((uint16_t)led_buffer[off + 1] * value) / 255u);
                        led_buffer[off + 2] = (uint8_t)(((uint16_t)led_buffer[off + 2] * value) / 255u);
                    }
                    break;
                }
            } else {
                /* Dark — above the bar */
                led_buffer[off]     = 0;
                led_buffer[off + 1] = 0;
                led_buffer[off + 2] = 0;
            }
        }
    }
}

/* ── UDP receive callback ──────────────────────────────────────────── */

static void music_sync_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                            const ip_addr_t *src, u16_t port) {
    (void)arg;
    (void)pcb;
    (void)src;
    (void)port;

    unsigned int len = p->tot_len;
    if (len < 3) {
        pbuf_free(p);
        return;
    }

    /* Read packet: 2 header bytes + up to MUSIC_MAX_BANDS band values */
    unsigned char buf[MUSIC_MAX_BANDS + 2];
    size_t copy_len = pbuf_copy_partial(p, buf, sizeof(buf), 0);
    pbuf_free(p);

    uint8_t effect_id = buf[0];
    uint8_t num_bands = buf[1];

    if (num_bands == 0 || num_bands > MUSIC_MAX_BANDS) return;

    /* Verify we received enough data for the declared band count */
    if (copy_len < (size_t)(2u + num_bands)) return;

    /* Map bands to LED zones */
    music_map_bands(&buf[2], num_bands, (effect_id_t)effect_id);

    /* Notify rest of system that client is active and buffer is dirty */
    led_update_pending = 1;
    effects_engine_client_active();
}

/* ── Initialisation ────────────────────────────────────────────────── */

void *music_sync_init(void) {
    struct udp_pcb *pcb = udp_new();
    if (!pcb) {
        printf("MusicSync: udp_new failed\n");
        return NULL;
    }

    ip_addr_t addr;
    IP4_ADDR(&addr, 0, 0, 0, 0);
    err_t err = udp_bind(pcb, &addr, MUSIC_SYNC_PORT);
    if (err != ERR_OK) {
        printf("MusicSync: udp_bind port %d failed (%d)\n", MUSIC_SYNC_PORT, err);
        udp_remove(pcb);
        return NULL;
    }

    udp_recv(pcb, music_sync_recv, NULL);
    printf("MusicSync: listening on port %d\n", MUSIC_SYNC_PORT);
    return (void *)pcb;
}
