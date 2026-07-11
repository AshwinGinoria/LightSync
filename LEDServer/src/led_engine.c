#include "led_engine.h"

/* Global LED buffer and update flag (shared with network handler / test harness) */
uint8_t led_buffer[BUFFER_SIZE];
volatile uint8_t led_update_pending;

/* The LED strip hardware functions — implemented by firmware or test stubs.
 * Declared extern "C" in the header so C++ code can call them too. */
void led_strip_set_brightness(uint8_t brightness);
void led_strip_set_pixel(uint32_t i, uint8_t r, uint8_t g, uint8_t b);
void led_strip_show(void);

void led_strip_init(void) {
    led_strip_set_brightness(MAX_BRIGHTNESS);
    led_strip_clear();
}

void led_strip_update(void) {
    if (!led_update_pending) {
        return;
    }
    led_update_pending = 0;

    for (int i = 0, offset = 0; i < LED_LENGTH; i++, offset += 3) {
        led_strip_set_pixel(i, led_buffer[offset], led_buffer[offset + 1], led_buffer[offset + 2]);
    }

    led_strip_show();
}

void led_strip_clear(void) {
    for (int i = 0; i < LED_LENGTH; i++) {
        led_strip_set_pixel(i, 0, 0, 0);
    }
    led_strip_show();
}
