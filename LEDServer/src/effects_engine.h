#ifndef EFFECTS_ENGINE_H
#define EFFECTS_ENGINE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "config_storage.h"

/* ── Constants ──────────────────────────────────────────────────────── */

/* Client timeout before autonomous effects take over.
 * 5000 ms / 10 ms per main-loop tick = 500 ticks. */
#define EFFECT_TIMEOUT_MS    5000
#define EFFECT_TICK_MS       10

/* Effects render at ~30 FPS (every 3rd main-loop iteration at 100 Hz). */
#define EFFECT_FRAME_HZ      30
#define TICKS_PER_EFFECT_FRAME (1000 / (EFFECT_FRAME_HZ * EFFECT_TICK_MS))
/* = 1000 / (30 * 10) = 1000 / 300 ≈ 3 */

/* ── Effect ID ──────────────────────────────────────────────────────── */

typedef enum {
    EFFECT_NONE = -1,
    EFFECT_SOLID = 0,
    EFFECT_RAINBOW,
    EFFECT_PULSE,
    EFFECT_CHASE,
    EFFECT_SPARKLE,
    EFFECT_THEATER_CHASE,
    EFFECT_DDP,           /* external control — no onboard rendering; the
                          * strip shows whatever the DDP receiver (or any
                          * other external writer) puts in led_buffer[] */
    EFFECT_COUNT
} effect_id_t;

/* ── Parameters ─────────────────────────────────────────────────────── */

typedef struct {
    uint8_t speed;       /* 1-255, higher = faster */
    uint8_t brightness;  /* 0-255 */
    uint8_t color_r;     /* primary colour */
    uint8_t color_g;
    uint8_t color_b;
    uint8_t color2_r;    /* secondary colour (chase background, sparkle base) */
    uint8_t color2_g;
    uint8_t color2_b;
} effect_params_t;

/* ── Public API ─────────────────────────────────────────────────────── */

/* One-shot initialisation.  Builds lookup tables, seeds PRNG, resets
 * timeout state.  Safe to call before any network is up. */
void effects_engine_init(void);

/* Call once per main-loop iteration (~10 ms).  Handles timeout
 * detection, tick scheduling, and delegates to the active effect.
 *
 * Returns 1 if an autonomous effect is actively writing to led_buffer[],
 * 0 if the client is in control (effects are paused). */
uint8_t effects_engine_update(void);

/* Signal that a network packet arrived — pauses autonomous effects
 * and gives control back to the client.  Call from any UDP receive
 * callback. */
void effects_engine_client_active(void);

/* Select an autonomous effect and provide its parameters.
 * Pass NULL for params to keep the currently-stored parameters. */
void effects_engine_set_effect(effect_id_t id, const effect_params_t *params);

/* Set the effects mode (CLIENT or AUTO).
 * In AUTO mode, effects run continuously regardless of client activity.
 * In CLIENT mode, effects pause while the DDP client is active. */
void effects_engine_set_mode(effects_mode_t mode);

/* Get the current effects mode. */
effects_mode_t effects_engine_get_mode(void);

/* Get/set the current effect speed (1-255) in place. The running effect reads
 * p->speed every frame, so set_speed takes effect immediately in AUTO mode;
 * in CLIENT mode it is preserved for the next effect selection. */
uint8_t effects_engine_get_speed(void);
void   effects_engine_set_speed(uint8_t speed);

/* ── Per-effect parameter masks ─────────────────────────────────────── */

/* Which effect_params_t fields an effect actually consumes. The control page
 * renders exactly these (solid has no speed, rainbow has no colour, a future
 * bounce effect might add a gravity bit, etc.). Add a new bit here when an
 * effect gains a parameter it reads from p. */
#define EFFECT_PARAM_SPEED   0x01   /* animation rate */
#define EFFECT_PARAM_COLOR   0x02   /* primary colour */
#define EFFECT_PARAM_COLOR2  0x04   /* secondary colour (chase bg, sparkle base) */
#define EFFECT_PARAM_BRIGHT  0x08   /* global brightness scaling */

/* Bitmask (EFFECT_PARAM_*) of the params the given effect consumes.
 * Returns 0 for out-of-range ids. */
uint8_t effects_engine_get_param_mask(effect_id_t id);

#ifdef __cplusplus
}
#endif

#endif /* EFFECTS_ENGINE_H */
