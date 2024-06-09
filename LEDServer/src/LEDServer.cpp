#include <stdio.h>
#include <iostream>

#include <lwip/udp.h>
#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>

#include "PicoLed.hpp"

#define LED_PIN 2
#define LED_LENGTH 288
#define MAX_BRIGHTNESS 255

#define SERVER_PORT 5005 
#define BUFFER_SIZE 1024

#define DEBUG_printf printf
#define ERROR_printf printf

const char SSID[] = "VM0259584";
const char PASSWORD[] = "r8qrMmydqxcs";

// const char SSID[] = "VM8065056";
// const char PASSWORD[] = "Ye4krbxandmM";

PicoLed::Color GREEN = PicoLed::GRB(255, 0, 0);
PicoLed::Color RED = PicoLed::GRB(0, 255, 0);
PicoLed::Color BLUE = PicoLed::GRB(0, 0, 255);
PicoLed::Color BLACK = PicoLed::GRB(0, 0, 0);
auto ledStrip = PicoLed::addLeds<PicoLed::WS2812B>(pio0, 0, LED_PIN, LED_LENGTH, PicoLed::FORMAT_GRB);

uint8_t buffer[BUFFER_SIZE];
bool UPDATE = 0;

void update_lights() {
    if (!UPDATE) {
        return;        
    }

    UPDATE = false;
    for (int i = 0, offset = 0; i < LED_LENGTH; i++, offset += 3) 
        ledStrip.setPixelColor(i, PicoLed::RGB(buffer[offset], buffer[offset + 1], buffer[offset + 2]));

    ledStrip.show();
}

typedef struct server_t_ {
    struct udp_pcb *_pcb;
} server_t;

static void server_process(void *arg, struct udp_pcb *upcb, struct pbuf *p, const ip_addr_t *src_addr, u16_t src_port) {
    size_t msg_len = pbuf_copy_partial(p, buffer, sizeof(buffer), 0);
    UPDATE = true;
    pbuf_free(p);
}

static int server_init(server_t *d, ip_addr_t *ip, int port) {
    d->_pcb = udp_new();
    if (d->_pcb == NULL) {
        ERROR_printf("server failed to start\n");
        return -ENOMEM;
    }
    udp_recv(d->_pcb, server_process, d);

    err_t err = udp_bind(d->_pcb, ip, port);
    if (err != ERR_OK) {
        ERROR_printf("udp failed to bind to port %u: %d", port, err);
        assert(false);
    }

    return err;
}

void server_deinit(server_t *d) {
    if (d->_pcb != NULL) {
        udp_remove(d->_pcb);
        d->_pcb = NULL;
    }
}

int main() {
    stdio_init_all();
    // Set Max Brightness for LEDs
    ledStrip.setBrightness(MAX_BRIGHTNESS);
    memset(buffer, 0, sizeof(buffer));

    // // Initialize Network Driver
    if (cyw43_arch_init()) {
        printf("failed to initialise\n");
        return 1;
    }

    // Set Wifi Mode to STA
    cyw43_arch_enable_sta_mode();

    ledStrip.fill(RED);
    ledStrip.show();

    // // Connect to Wifi
    DEBUG_printf("Connecting to Wi-Fi...\n");
    while (cyw43_arch_wifi_connect_timeout_ms(SSID, PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("failed to Connect\n");
    }

    ip_addr_t addr;
    IP4_ADDR(ip_2_ip4(&addr), 0, 0, 0, 0);

    // Initialize Server
    server_t _server;
    int err = server_init(&_server, &addr, SERVER_PORT);
    if (err != ERR_OK) {
        ERROR_printf("Falied to start Server");
        server_deinit(&_server);
        return 1;
    } else {
        DEBUG_printf("Server running at %s on port %u\n", ip4addr_ntoa(netif_ip4_addr(netif_list)), SERVER_PORT);
    }

    ledStrip.fill(GREEN);
    ledStrip.show();

    // 10 ms sleep => ~100 fps
    while(true) {
        sleep_ms(10);
        cyw43_arch_poll();
        update_lights();
    }

    DEBUG_printf("Closing UDP Server\n");
    server_deinit(&_server);

    cyw43_arch_deinit();
    return 0;
}
