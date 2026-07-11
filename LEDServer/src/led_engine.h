#ifndef LED_ENGINE_H
#define LED_ENGINE_H

#include <stdint.h>

/* LED hardware constants */
#define LED_PIN 2
#define LED_LENGTH 288
#define MAX_BRIGHTNESS 255

/* Network buffer for incoming RGB data */
#define SERVER_PORT 5005
#define BUFFER_SIZE 1024

#ifdef __cplusplus
extern "C" {
#endif

/* Shared state (written by network handler, read by engine) */
extern uint8_t led_buffer[BUFFER_SIZE];
extern volatile uint8_t led_update_pending;

/* Initialize the LED strip hardware (PIO + PicoLed) */
void led_strip_init(void);

/* Process one frame: read led_buffer, update LEDs, call ledStrip.show() */
void led_strip_update(void);

/* Clear all LEDs to black */
void led_strip_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* LED_ENGINE_H */
