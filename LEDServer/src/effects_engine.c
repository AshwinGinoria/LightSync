#include "effects_engine.h"
#include "led_engine.h"

#include <string.h>   /* memset */

/* ── Rainbow lookup table ──────────────────────────────────────────────
 *
 * 256-entry RGB colour wheel, computed once at init.
 * Three equal segments (85 entries each, i=255 aliases i=0):
 *   seg 0 (i 0..84):   red→green  (R=255, G 0→252, B=0)
 *   seg 1 (i 85..169): green→blue (G=255, B 0→252, R=0)
 *   seg 2 (i 170..254): blue→red  (B=255, R 0→252, G=0)
 *   i=255: same as i=0
 *
 * 768 bytes of LUT avoids any trig on the Cortex-M0+. */

#define RAINBOW_LEN       256
#define RAINBOW_SEG_LEN    85

static uint8_t rainbow_r[RAINBOW_LEN];
static uint8_t rainbow_g[RAINBOW_LEN];
static uint8_t rainbow_b[RAINBOW_LEN];

static void rainbow_lut_init(void) {
    uint16_t i;
    uint16_t seg, pos, ramp;

    for (i = 0; i < RAINBOW_LEN; i++) {
        seg  = i / RAINBOW_SEG_LEN;
        pos  = i % RAINBOW_SEG_LEN;
        ramp = (uint8_t)(pos * 3);   /* 0 → 252 across 85 positions */

        switch (seg) {
        case 0: /* red → green */
            rainbow_r[i] = 255;
            rainbow_g[i] = (uint8_t)ramp;
            rainbow_b[i] = 0;
            break;
        case 1: /* green → blue */
            rainbow_r[i] = 255u - (uint8_t)ramp;
            rainbow_g[i] = 255;
            rainbow_b[i] = (uint8_t)ramp;
            break;
        case 2: /* blue → red */
            rainbow_r[i] = (uint8_t)ramp;
            rainbow_g[i] = 255u - (uint8_t)ramp;
            rainbow_b[i] = 255;
            break;
        default: /* i == 255, alias i == 0 */
            rainbow_r[i] = 255;
            rainbow_g[i] = 0;
            rainbow_b[i] = 0;
            break;
        }
    }
}

/* ── Quadratic sine approximation ─────────────────────────────────────
 *
 * Approximates sin(t) for t ∈ [0, π] using 4·(t/π)·(1 − t/π).
 * Mapped to [0, 255] → [0, π] for a full inhale/exhale cycle.
 *
 *   t in  0..127  →  brightness rising  0 → 255
 *   t in 128..255 →  brightness falling 255 → 0
 *
 * Exactly matches the plan's quadratic approach, verified in tests. */

static uint8_t sine_approx(uint8_t t) {
    /* Map to [0, 255] representing [0, π].
     * half_sine = 4 * t * (255 - t) / 255
     * The numerator maxes at t=127→128: 4*127*128 = 65024.
     * Dividing by 255 gives 0..255. */
    uint16_t numer = 4u * (uint16_t)t * (uint16_t)(255u - t);
    return (uint8_t)(numer / 255u);
}

/* ── Pseudo-random number generator (LCG) ───────────────────────────── */

static uint32_t sparkle_rng;

static uint16_t sparkle_rand(void) {
    sparkle_rng = sparkle_rng * 1103515245u + 12345u;
    return (uint16_t)((sparkle_rng >> 16) & 0x7FFF);
}

/* ── Internal state ─────────────────────────────────────────────────── */

static effect_id_t     current_effect = EFFECT_RAINBOW;
static effect_params_t current_params;

static uint32_t main_loop_tick;            /* increments every main loop */
static uint32_t last_client_tick;          /* reset per packet */
static uint32_t effect_frame_count;        /* increments per effect frame */

/* ── Effects mode ────────────────────────────────────────────────────── */

static effects_mode_t current_mode = EFFECT_MODE_CLIENT;

/* ── Forward-declare all effect tick functions ──────────────────────── */

static void effect_solid_tick(effect_params_t *p, uint32_t frame);
static void effect_rainbow_tick(effect_params_t *p, uint32_t frame);
static void effect_pulse_tick(effect_params_t *p, uint32_t frame);
static void effect_chase_tick(effect_params_t *p, uint32_t frame);
static void effect_sparkle_tick(effect_params_t *p, uint32_t frame);
static void effect_theater_chase_tick(effect_params_t *p, uint32_t frame);

/* ── Effect registry ────────────────────────────────────────────────── */

typedef void (*effect_tick_fn)(effect_params_t *, uint32_t);

typedef struct {
    const char    *name;
    effect_tick_fn tick;
} effect_def_t;

static const effect_def_t effects[EFFECT_COUNT] = {
    [EFFECT_SOLID]         = { "solid",          effect_solid_tick          },
    [EFFECT_RAINBOW]       = { "rainbow",        effect_rainbow_tick        },
    [EFFECT_PULSE]         = { "pulse",          effect_pulse_tick          },
    [EFFECT_CHASE]         = { "chase",          effect_chase_tick          },
    [EFFECT_SPARKLE]       = { "sparkle",        effect_sparkle_tick        },
    [EFFECT_THEATER_CHASE] = { "theater_chase",  effect_theater_chase_tick  },
};

/* ── Public API ─────────────────────────────────────────────────────── */

void effects_engine_init(void) {
    memset(&current_params, 0, sizeof(current_params));
    current_params.speed      = 128;
    current_params.brightness = 255;
    current_params.color_r    = 255;
    current_params.color_g    = 0;
    current_params.color_b    = 0;
    current_params.color2_r   = 0;
    current_params.color2_g   = 0;
    current_params.color2_b   = 0;

    current_effect   = EFFECT_RAINBOW;
    main_loop_tick   = 0;
    last_client_tick = 0;
    effect_frame_count = 0;

    rainbow_lut_init();
    sparkle_rng = 0xDEADBEEF;  /* fixed seed — deterministic sparkle test */
}

void effects_engine_set_mode(effects_mode_t mode) {
    current_mode = mode;
}

effects_mode_t effects_engine_get_mode(void) {
    return current_mode;
}

void effects_engine_client_active(void) {
    /* In AUTO mode, client_active is a no-op — effects always run. */
    if (current_mode == EFFECT_MODE_AUTO) return;
    last_client_tick = main_loop_tick;
}

void effects_engine_set_effect(effect_id_t id, const effect_params_t *params) {
    if (id < 0 || id >= EFFECT_COUNT) return;
    current_effect = id;
    if (params) {
        current_params = *params;
    }
}

uint8_t effects_engine_update(void) {
    main_loop_tick++;

    /* In AUTO mode, always render effects — skip timeout check. */
    if (current_mode == EFFECT_MODE_AUTO) {
        /* No-op: always proceed to render */
    } else {
        /* Check timeout: client must have been silent for EFFECT_TIMEOUT_MS */
        uint32_t elapsed = main_loop_tick - last_client_tick;
        uint32_t timeout_ticks = EFFECT_TIMEOUT_MS / EFFECT_TICK_MS;
        if (elapsed < timeout_ticks) {
            return 0;   /* client is in control */
        }
    }

    /* Decimate: only render every Nth main-loop iteration (~30 FPS) */
    if (main_loop_tick % TICKS_PER_EFFECT_FRAME != 0) {
        return 1;   /* effect running but skip this frame */
    }

    /* Run the active effect */
    if (current_effect >= 0 && current_effect < EFFECT_COUNT) {
        effects[current_effect].tick(&current_params, effect_frame_count);
        led_update_pending = 1;
    }

    effect_frame_count++;
    return 1;
}

/* ── Effect implementations ─────────────────────────────────────────── */

/* -- Solid ----------------------------------------------------------- */
static void effect_solid_tick(effect_params_t *p, uint32_t frame) {
    (void)frame;
    uint8_t br = p->brightness;
    uint16_t i;
    for (i = 0; i < LED_LENGTH; i++) {
        uint16_t off = i * 3;
        led_buffer[off]     = (uint8_t)(((uint16_t)p->color_r  * br) / 255u);
        led_buffer[off + 1] = (uint8_t)(((uint16_t)p->color_g * br) / 255u);
        led_buffer[off + 2] = (uint8_t)(((uint16_t)p->color_b * br) / 255u);
    }
}

/* -- Rainbow --------------------------------------------------------- */
static void effect_rainbow_tick(effect_params_t *p, uint32_t frame) {
    uint8_t speed = p->speed;
    uint8_t br    = p->brightness;

    /* Advance hue offset based on frame and speed */
    uint16_t hue_offset = (uint16_t)((frame * (uint32_t)speed) & 0xFF);

    uint16_t i;
    for (i = 0; i < LED_LENGTH; i++) {
        uint16_t off = i * 3;
        uint8_t  hue = (uint8_t)((hue_offset + i * RAINBOW_LEN / LED_LENGTH) & 0xFF);

        led_buffer[off]     = (uint8_t)(((uint16_t)rainbow_r[hue] * br) / 255u);
        led_buffer[off + 1] = (uint8_t)(((uint16_t)rainbow_g[hue] * br) / 255u);
        led_buffer[off + 2] = (uint8_t)(((uint16_t)rainbow_b[hue] * br) / 255u);
    }
}

/* -- Pulse / breathe ------------------------------------------------- */
static void effect_pulse_tick(effect_params_t *p, uint32_t frame) {
    uint8_t speed = p->speed;
    uint8_t phase = (uint8_t)((frame * (uint32_t)speed) & 0xFF);
    uint8_t intensity = sine_approx(phase);
    uint8_t br = (uint8_t)(((uint16_t)p->brightness * intensity) / 255u);

    uint16_t i;
    for (i = 0; i < LED_LENGTH; i++) {
        uint16_t off = i * 3;
        led_buffer[off]     = (uint8_t)(((uint16_t)p->color_r * br) / 255u);
        led_buffer[off + 1] = (uint8_t)(((uint16_t)p->color_g * br) / 255u);
        led_buffer[off + 2] = (uint8_t)(((uint16_t)p->color_b * br) / 255u);
    }
}

/* -- Chase ----------------------------------------------------------- */
static void effect_chase_tick(effect_params_t *p, uint32_t frame) {
    uint8_t speed = p->speed;
    uint16_t chase_pos = (uint16_t)((frame * (uint32_t)speed) % (LED_LENGTH * 2));
    uint16_t chase_width = 3;

    uint16_t i;
    for (i = 0; i < LED_LENGTH; i++) {
        uint16_t off = i * 3;
        uint16_t dist = (i + chase_pos) % LED_LENGTH;

        if (dist < chase_width) {
            led_buffer[off]     = p->color_r;
            led_buffer[off + 1] = p->color_g;
            led_buffer[off + 2] = p->color_b;
        } else {
            led_buffer[off]     = p->color2_r;
            led_buffer[off + 1] = p->color2_g;
            led_buffer[off + 2] = p->color2_b;
        }
    }
}

/* -- Sparkle --------------------------------------------------------- */
static void effect_sparkle_tick(effect_params_t *p, uint32_t frame) {
    (void)frame;
    uint8_t  br       = p->brightness;
    uint16_t sparkles = (uint16_t)p->speed * 4u + 4u;
    if (sparkles > LED_LENGTH) sparkles = LED_LENGTH;

    uint8_t base_r = (uint8_t)(((uint16_t)p->color2_r * br) / 255u);
    uint8_t base_g = (uint8_t)(((uint16_t)p->color2_g * br) / 255u);
    uint8_t base_b = (uint8_t)(((uint16_t)p->color2_b * br) / 255u);

    uint16_t i, s;

    /* Fill with background colour first */
    for (i = 0; i < LED_LENGTH; i++) {
        uint16_t off = i * 3;
        led_buffer[off]     = base_r;
        led_buffer[off + 1] = base_g;
        led_buffer[off + 2] = base_b;
    }

    /* Overlay sparkle pixels */
    for (s = 0; s < sparkles; s++) {
        uint16_t idx = sparkle_rand() % LED_LENGTH;
        uint16_t off = idx * 3;
        led_buffer[off]     = p->color_r;
        led_buffer[off + 1] = p->color_g;
        led_buffer[off + 2] = p->color_b;
    }
}

/* -- Theater chase --------------------------------------------------- */
static void effect_theater_chase_tick(effect_params_t *p, uint32_t frame) {
    uint8_t  speed     = p->speed;
    uint8_t  br        = p->brightness;
    uint16_t chase_pos = (uint16_t)((frame * (uint32_t)speed) % 6);

    uint8_t on_r = (uint8_t)(((uint16_t)p->color_r * br) / 255u);
    uint8_t on_g = (uint8_t)(((uint16_t)p->color_g * br) / 255u);
    uint8_t on_b = (uint8_t)(((uint16_t)p->color_b * br) / 255u);

    uint16_t i;
    for (i = 0; i < LED_LENGTH; i++) {
        uint16_t off = i * 3;
        if (((i + chase_pos) % 6) < 3) {
            led_buffer[off]     = on_r;
            led_buffer[off + 1] = on_g;
            led_buffer[off + 2] = on_b;
        } else {
            led_buffer[off]     = 0;
            led_buffer[off + 1] = 0;
            led_buffer[off + 2] = 0;
        }
    }
}
