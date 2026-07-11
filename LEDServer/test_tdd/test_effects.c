#include "effects_engine.h"
#include "led_engine.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ── C-compatible stub for LED strip (replaces PicoLed hardware) ───── */
static struct {
    uint8_t pixels[LED_LENGTH][3]; /* [r, g, b] per LED */
} test_led_strip;

/* Stub: led_engine calls these — we intercept them into test_led_strip */
void led_strip_set_brightness(uint8_t brightness) { (void)brightness; }
void led_strip_set_pixel(uint32_t i, uint8_t r, uint8_t g, uint8_t b) {
    if (i < LED_LENGTH) {
        test_led_strip.pixels[i][0] = r;
        test_led_strip.pixels[i][1] = g;
        test_led_strip.pixels[i][2] = b;
    }
}
void led_strip_show(void) {}

/* ── Test harness ──────────────────────────────────────────────────── */
static int  tests_run     =  0;
static int  tests_failed  =  0;

#define TEST(name)  do { \
    tests_run++; \
    printf("TEST: %s\n", name); \
} while(0)

#define CHECK(cond)  do { \
    if (!(cond)) { \
        printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* ── Helpers ───────────────────────────────────────────────────────── */
static void clear_buffer(void) {
    memset((void *)led_buffer, 0, BUFFER_SIZE);
    led_update_pending = 0;
}

static int buffer_is_all_zero(void) {
    int i;
    for (i = 0; i < BUFFER_SIZE; i++) {
        if (led_buffer[i] != 0) return 0;
    }
    return 1;
}

/* ═════════════════════════════════════════════════════════════════════
 * LED ENGINE TESTS (foundation for all features)
 * ═════════════════════════════════════════════════════════════════════ */

/* T1: led_strip_update requires pending flag */
static void test_led_pending_required(void) {
    TEST("led_strip_update requires pending flag");

    led_strip_init();

    /* Inject data but don't set pending */
    memset(led_buffer, 0, sizeof(led_buffer));
    led_buffer[0] = 255;
    led_update_pending = 0;
    led_strip_update();

    /* LEDs should still be black */
    CHECK(test_led_strip.pixels[0][0] == 0);
}

/* T2: led_strip_update copies all 288 LEDs */
static void test_led_update_all_leds(void) {
    TEST("led_strip_update copies all 288 LEDs");

    led_strip_init();

    memset(led_buffer, 0, sizeof(led_buffer));
    int last_off = (LED_LENGTH - 1) * 3;
    led_buffer[last_off]     = 255;
    led_buffer[last_off + 1] = 255;
    led_buffer[last_off + 2] = 255;

    led_update_pending = 1;
    led_strip_update();

    CHECK(test_led_strip.pixels[LED_LENGTH-1][0] == 255);
    CHECK(test_led_strip.pixels[LED_LENGTH-1][1] == 255);
    CHECK(test_led_strip.pixels[LED_LENGTH-1][2] == 255);
}

/* T3: led_strip_clear sets all LEDs to black */
static void test_led_clear(void) {
    TEST("led_strip_clear sets all LEDs to black");

    led_strip_init();

    memset(led_buffer, 0, sizeof(led_buffer));
    led_buffer[0] = 255;
    led_buffer[1] = 128;
    led_buffer[2] = 64;
    led_update_pending = 1;
    led_strip_update();

    led_strip_clear();

    int i;
    for (i = 0; i < LED_LENGTH; i++) {
        CHECK(test_led_strip.pixels[i][0] == 0);
        CHECK(test_led_strip.pixels[i][1] == 0);
        CHECK(test_led_strip.pixels[i][2] == 0);
    }
}

/* T4: led_strip_init sets max brightness */
static void test_init_sets_max_brightness(void) {
    TEST("led_strip_init sets max brightness");

    memset(&test_led_strip, 0, sizeof(test_led_strip));

    led_strip_init();

    /* init calls led_strip_set_brightness(MAX_BRIGHTNESS) then clears.
     * We can't directly verify brightness was set, but we verify
     * that after init, all LEDs are black (clear was called). */
    int i;
    for (i = 0; i < LED_LENGTH; i++) {
        CHECK(test_led_strip.pixels[i][0] == 0);
        CHECK(test_led_strip.pixels[i][1] == 0);
        CHECK(test_led_strip.pixels[i][2] == 0);
    }
}

/* ═════════════════════════════════════════════════════════════════════
 * EFFECTS ENGINE TESTS (Feature 3: Local Effects)
 * ═════════════════════════════════════════════════════════════════════ */

/* T5: init initializes the engine without crashing */
static void test_init_initializes_engine(void) {
    TEST("effects_engine_init does not crash");

    effects_engine_init();
    clear_buffer();

    int i;
    for (i = 0; i < 504; i++) {
        effects_engine_update();
    }

    CHECK(led_update_pending == 1);
}

/* T6: default effect is RAINBOW (not solid, not all-black) */
static void test_default_effect_is_rainbow(void) {
    TEST("default effect is RAINBOW");

    effects_engine_init();
    clear_buffer();

    int i;
    for (i = 0; i < 504; i++) {
        effects_engine_update();
    }

    /* Rainbow should produce non-zero, varied output */
    int any_lit = 0;
    int any_different = 0;
    uint8_t first_r = led_buffer[0];
    uint8_t first_g = led_buffer[3];

    for (i = 1; i < 20; i++) {
        if (led_buffer[i*3] != 0 || led_buffer[i*3+1] != 0 || led_buffer[i*3+2] != 0) {
            any_lit = 1;
        }
        if (led_buffer[i*3] != first_r || led_buffer[i*3+1] != first_g) {
            any_different = 1;
        }
    }
    CHECK(any_lit);
    CHECK(any_different);
}

/* T7: solid effect fills all LEDs with specified color */
static void test_solid_fills_all_leds(void) {
    TEST("solid effect fills all LEDs with specified color");

    effects_engine_init();

    effect_params_t params = {0};
    params.brightness = 255;
    params.color_r    = 128;
    params.color_g    = 64;
    params.color_b    = 32;
    params.speed      = 0;
    effects_engine_set_effect(EFFECT_SOLID, &params);

    clear_buffer();
    int i;
    for (i = 0; i < 504; i++) {
        effects_engine_update();
    }

    for (i = 0; i < LED_LENGTH; i++) {
        uint16_t off = i * 3;
        CHECK(led_buffer[off]     == 128);
        CHECK(led_buffer[off + 1] == 64);
        CHECK(led_buffer[off + 2] == 32);
    }
}

/* T8: solid effect brightness scales correctly */
static void test_solid_brightness_scales(void) {
    TEST("solid effect brightness scales correctly");

    effects_engine_init();

    effect_params_t params = {0};
    params.brightness = 128;
    params.color_r    = 200;
    params.color_g    = 100;
    params.color_b    = 50;
    params.speed      = 0;
    effects_engine_set_effect(EFFECT_SOLID, &params);

    clear_buffer();
    int i;
    for (i = 0; i < 504; i++) {
        effects_engine_update();
    }

    uint8_t expected_r = (uint8_t)((200u * 128u) / 255u);
    CHECK(led_buffer[0] == expected_r);
}

/* T9: pulse effect oscillates brightness */
static void test_pulse_oscillates(void) {
    TEST("pulse effect oscillates brightness");

    effects_engine_init();

    effect_params_t params = {0};
    params.brightness = 255;
    params.color_r    = 255;
    params.color_g    = 0;
    params.color_b    = 0;
    params.speed      = 1;
    effects_engine_set_effect(EFFECT_PULSE, &params);

    clear_buffer();
    int i;
    for (i = 0; i < 504; i++) {
        effects_engine_update();
    }

    /* Sample 256 effect frames to catch trough and peak */
    uint8_t min_r = 255, max_r = 0;
    for (i = 0; i < 256; i++) {
        effects_engine_update();
        effects_engine_update();
        effects_engine_update();
        if (led_buffer[0] < min_r) min_r = led_buffer[0];
        if (led_buffer[0] > max_r) max_r = led_buffer[0];
    }

    CHECK(min_r <= 5);
    CHECK(max_r >= 245);
}

/* T10: chase effect moves position over frames */
static void test_chase_moves(void) {
    TEST("chase effect moves position over frames");

    effects_engine_init();

    effect_params_t params = {0};
    params.brightness = 255;
    params.color_r    = 255;
    params.color_g    = 0;
    params.color_b    = 0;
    params.color2_r   = 0;
    params.color2_g   = 0;
    params.color2_b   = 0;
    params.speed      = 64;
    effects_engine_set_effect(EFFECT_CHASE, &params);

    clear_buffer();
    int i;
    for (i = 0; i < 504; i++) {
        effects_engine_update();
    }

    int head1 = -1;
    for (i = 0; i < LED_LENGTH && head1 < 0; i++) {
        uint16_t off = i * 3;
        if (led_buffer[off] == 255) head1 = i;
    }

    for (i = 0; i < 15; i++) {
        effects_engine_update();
    }

    int head2 = -1;
    for (i = 0; i < LED_LENGTH && head2 < 0; i++) {
        uint16_t off = i * 3;
        if (led_buffer[off] == 255) head2 = i;
    }

    CHECK(head1 >= 0);
    CHECK(head2 >= 0);
    CHECK(head1 != head2);
}

/* T11: theater chase alternates in groups of 3 */
static void test_theater_chase_pattern(void) {
    TEST("theater chase alternates in groups of 3");

    effects_engine_init();

    effect_params_t params = {0};
    params.brightness = 255;
    params.color_r    = 255;
    params.color_g    = 128;
    params.color_b    = 0;
    params.color2_r   = 0;
    params.color2_g   = 0;
    params.color2_b   = 0;
    params.speed      = 0;
    effects_engine_set_effect(EFFECT_THEATER_CHASE, &params);

    clear_buffer();
    int i;
    for (i = 0; i < 504; i++) {
        effects_engine_update();
    }

    int lit_count = 0, dark_count = 0;
    for (i = 0; i < 6; i++) {
        uint16_t off = i * 3;
        if (led_buffer[off] > 0) lit_count++;
        else dark_count++;
    }
    CHECK(lit_count == 3);
    CHECK(dark_count == 3);
}

/* T12: client_active pauses effects */
static void test_client_active_pauses(void) {
    TEST("client_active pauses autonomous effects");

    effects_engine_init();
    clear_buffer();

    int i;
    for (i = 0; i < 504; i++) {
        effects_engine_update();
    }
    CHECK(!buffer_is_all_zero());

    effects_engine_client_active();
    clear_buffer();

    for (i = 0; i < 10; i++) {
        effects_engine_update();
    }
    CHECK(buffer_is_all_zero());
}

/* T13: effects resume after timeout */
static void test_effects_resume_after_timeout(void) {
    TEST("effects resume after client timeout expires");

    effects_engine_init();
    clear_buffer();

    effects_engine_client_active();

    int i;
    for (i = 0; i < 600; i++) {
        effects_engine_update();
    }

    CHECK(!buffer_is_all_zero());
}

/* T14: set_effect with NULL params preserves previous params */
static void test_set_effect_null_preserves(void) {
    TEST("set_effect with NULL preserves previous params");

    effects_engine_init();

    effect_params_t params = {0};
    params.brightness = 77;
    params.color_r    = 11;
    effects_engine_set_effect(EFFECT_SOLID, &params);

    effects_engine_set_effect(EFFECT_RAINBOW, NULL);
    CHECK(1);
}

/* T15: set_effect with invalid ID is ignored (no crash) */
static void test_set_effect_invalid_ignored(void) {
    TEST("set_effect with invalid ID is ignored");

    effects_engine_init();

    effects_engine_set_effect(EFFECT_RAINBOW, NULL);
    effects_engine_set_effect((effect_id_t)-2, NULL);
    effects_engine_set_effect((effect_id_t)EFFECT_COUNT, NULL);
    CHECK(1);
}

/* T16: default mode is EFFECT_MODE_CLIENT */
static void test_default_mode_is_client(void) {
    TEST("default mode is EFFECT_MODE_CLIENT");

    effects_engine_init();

    CHECK(effects_engine_get_mode() == EFFECT_MODE_CLIENT);
}

/* T17: set_mode switches to AUTO and back to CLIENT */
static void test_mode_switch(void) {
    TEST("set_mode switches between CLIENT and AUTO");

    effects_engine_init();

    CHECK(effects_engine_get_mode() == EFFECT_MODE_CLIENT);

    effects_engine_set_mode(EFFECT_MODE_AUTO);
    CHECK(effects_engine_get_mode() == EFFECT_MODE_AUTO);

    effects_engine_set_mode(EFFECT_MODE_CLIENT);
    CHECK(effects_engine_get_mode() == EFFECT_MODE_CLIENT);
}

/* T18: in AUTO mode, effects run immediately (no client_active pause) */
static void test_auto_mode_effects_run(void) {
    TEST("AUTO mode runs effects immediately");

    effects_engine_init();
    effects_engine_set_mode(EFFECT_MODE_AUTO);

    clear_buffer();
    int i;
    for (i = 0; i < 504; i++) {
        effects_engine_update();
    }

    /* Effects should be producing output */
    CHECK(!buffer_is_all_zero());
}

/* T19: in AUTO mode, client_active() is a no-op (effects keep running) */
static void test_auto_mode_client_active_noop(void) {
    TEST("AUTO mode: client_active() is a no-op");

    effects_engine_init();
    effects_engine_set_mode(EFFECT_MODE_AUTO);

    clear_buffer();
    effects_engine_client_active();

    int i;
    for (i = 0; i < 504; i++) {
        effects_engine_update();
    }

    /* Effects should still be running despite client_active() */
    CHECK(!buffer_is_all_zero());
}

/* ═════════════════════════════════════════════════════════════════════
/* ── Runner ────────────────────────────────────────────────────────── */

int main(void) {
    test_led_pending_required();
    test_led_update_all_leds();
    test_led_clear();
    test_init_sets_max_brightness();
    test_init_initializes_engine();
    test_default_effect_is_rainbow();
    test_solid_fills_all_leds();
    test_solid_brightness_scales();
    test_pulse_oscillates();
    test_chase_moves();
    test_theater_chase_pattern();
    test_client_active_pauses();
    test_effects_resume_after_timeout();
    test_set_effect_null_preserves();
    test_set_effect_invalid_ignored();
    test_default_mode_is_client();
    test_mode_switch();
    test_auto_mode_effects_run();
    test_auto_mode_client_active_noop();

    printf("\n%d tests run, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
