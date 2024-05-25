#include <unistd.h>
#include <stdio.h>
#include <string>
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "boards/pico_w.h"

#include "PicoLed.hpp"

#define LED_PIN 0
#define LED_LENGTH 288
const uint8_t MAX_BRIGHTNESS = 50;
const uint SERVER_PORT = 2020;
const char SSID[] = "VM0259584";
const char PASSWORD[] = "r8qrMmydqxcs";

PicoLed::Color RED = PicoLed::RGB(MAX_BRIGHTNESS, 0, 0);
PicoLed::Color GREEN = PicoLed::RGB(0, MAX_BRIGHTNESS, 0);
PicoLed::Color BLUE = PicoLed::RGB(0, 0, MAX_BRIGHTNESS);

void debug_break(int);

// Initialize LED

// We will run 2 threads 
//  1. That will control LED lights
//  2. That checks wifi server for udpates and communicates to 1.

int connect_wifi() {
    return cyw43_arch_wifi_connect_async(SSID, PASSWORD, CYW43_AUTH_WPA_TKIP_PSK);
}

int run()
{
    stdio_init_all();

    // Set Max Brightness for LEDs
    auto ledStrip = PicoLed::addLeds<PicoLed::WS2812B>(pio0, 0, LED_PIN, LED_LENGTH, PicoLed::FORMAT_RGB);
    ledStrip.setBrightness(MAX_BRIGHTNESS);

    // Run
    ledStrip.fill(BLUE);
    ledStrip.show();

    debug_break(30);

    // Initialize Network Driver
    if (cyw43_arch_init()) {
        printf("failed to initialise\n");
        return 1;
    }

    // Set Wifi Mode to STA
    cyw43_arch_enable_sta_mode();

    // Connect to Wifi
    printf("Connecting to Wi-Fi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms(SSID, PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("failed to Connect\n");
        return 1;
    } else {
        printf("Connected.\n");
    }
    
    // Start LED Lights Server in a new thread
    // Continue Wifi Server in main thread here.

    while (true) {
        int status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);

        printf("status : %d, comparison: %d\n", status, status == 3);
        if (status == 3) {
            printf("GREEN\n", status);
            ledStrip.fill(GREEN);
            ledStrip.show();
        }

        cyw43_arch_poll();
        sleep_ms(500);
    }

    cyw43_arch_deinit();
}

void debug_break(int _seconds) {
    printf("Taking a Break!!");
    sleep_ms(_seconds * 100);
    printf("Running");
}

int main() {
    return run();
}