/* Stub for hardware-specific LED strip functions (WS2812B / PIO) */
#include <stdint.h>

void led_strip_set_brightness(uint8_t brightness) {
    (void)brightness;
}

void led_strip_set_pixel(uint32_t i, uint8_t r, uint8_t g, uint8_t b) {
    (void)i; (void)r; (void)g; (void)b;
}

void led_strip_show(void) {
}
