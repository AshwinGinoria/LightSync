#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <ctime>
#include <chrono>
#include <iostream>
#include <codecvt>

#include "lwip/udp.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "pico/mutex.h"

#include "PicoLed.hpp"

#define LED_PIN 2
#define LED_LENGTH 288
#define MAX_BRIGHTNESS 50

#define PORT_SERVER 5005 
#define BUFFER_SIZE 1024

#define DEBUG_printf printf
#define ERROR_printf printf

// const char SSID[] = "VM0259584";
// const char PASSWORD[] = "r8qrMmydqxcs";

const char SSID[] = "VM8065056";
const char PASSWORD[] = "Ye4krbxandmM";

PicoLed::Color GREEN = PicoLed::RGB(255, 0, 0);
PicoLed::Color RED = PicoLed::RGB(0, 255, 0);
PicoLed::Color BLUE = PicoLed::RGB(0, 0, 255);
PicoLed::Color BLACK = PicoLed::RGB(0, 0, 0);
auto ledStrip = PicoLed::addLeds<PicoLed::WS2812B>(pio0, 0, LED_PIN, LED_LENGTH, PicoLed::FORMAT_RGB);

void debug_break(int);

const char SERVER_IP[] = "0.0.0.0";
uint8_t buffer[BUFFER_SIZE];
mutex_t buffer_mutex;

void update_lights() {
    mutex_enter_blocking(&buffer_mutex);
    for (int i = 0, offset = 0; i < LED_LENGTH; i++, offset += 3) 
        ledStrip.setPixelColor(i, PicoLed::RGB(buffer[offset], buffer[offset + 1], buffer[offset + 2]));

    ledStrip.show();
    mutex_exit(&buffer_mutex);
}

typedef struct udp_server_t_ {
    struct udp_pcb *udp;
    ip_addr_t ip;
} udp_server_t;

static int udp_socket_new(struct udp_pcb **udp, void *cb_data, udp_recv_fn cb_udp_recv) {
    *udp = udp_new();
    if (*udp == NULL) {
        return -ENOMEM;
    }
    udp_recv(*udp, cb_udp_recv, (void *)cb_data);
    return ERR_OK;
}

static void udp_socket_free(struct udp_pcb **udp) {
    if (*udp != NULL) {
        udp_remove(*udp);
        *udp = NULL;
    }
}

static int udp_socket_bind(struct udp_pcb **udp, uint32_t ip, uint16_t port) {
    ip_addr_t addr;
    IP4_ADDR(&addr, ip >> 24 & 0xff, ip >> 16 & 0xff, ip >> 8 & 0xff, ip & 0xff);
    err_t err = udp_bind(*udp, &addr, port);
    if (err != ERR_OK) {
        ERROR_printf("udp failed to bind to port %u: %d", port, err);
        assert(false);
    }
    return err;
}

static void udp_server_process(void *arg, struct udp_pcb *upcb, struct pbuf *p, const ip_addr_t *src_addr, u16_t src_port) {
    udp_server_t *d = (udp_server_t*)arg;

    mutex_enter_blocking(&buffer_mutex);
    size_t msg_len = pbuf_copy_partial(p, buffer, sizeof(buffer), 0);
    mutex_exit(&buffer_mutex);
}

void udp_server_init(udp_server_t *d, ip_addr_t *ip) {
    if (udp_socket_new(&d->udp, d, udp_server_process) != ERR_OK) {
        DEBUG_printf("server failed to start\n");
        return;
    }
    if (udp_socket_bind(&d->udp, 0, PORT_SERVER) != ERR_OK) {
        DEBUG_printf("server failed to bind\n");
        return;
    }
    ip_addr_copy(d->ip, *ip);
    DEBUG_printf("server listening on port %d\n", PORT_SERVER);
}

void udp_server_deinit(udp_server_t *d) {
    udp_socket_free(&d->udp);
}

int connect_wifi() {
    return cyw43_arch_wifi_connect_async(SSID, PASSWORD, CYW43_AUTH_WPA_TKIP_PSK);
}

int main() {
    stdio_init_all();
    // Set Max Brightness for LEDs
    ledStrip.setBrightness(MAX_BRIGHTNESS);
    memset(buffer, 0, sizeof(buffer));
    mutex_init(&buffer_mutex);

    // Run
    ledStrip.fill(BLUE);
    ledStrip.show();

    // Initialize Network Driver
    if (cyw43_arch_init()) {
        printf("failed to initialise\n");
        return 1;
    }

    // Set Wifi Mode to STA
    cyw43_arch_enable_sta_mode();

    // Connect to Wifi
    DEBUG_printf("Connecting to Wi-Fi...\n");
    if (cyw43_arch_wifi_connect_timeout_ms(SSID, PASSWORD, CYW43_AUTH_WPA2_AES_PSK, 30000)) {
        printf("failed to Connect\n");
        ledStrip.fill(RED);
        ledStrip.show();
        return 1;
    } else {
        DEBUG_printf("Starting server at %s on port %u\n", ip4addr_ntoa(netif_ip4_addr(netif_list)), PORT_SERVER);
        ledStrip.fill(GREEN);
        ledStrip.show();
    }

    DEBUG_printf("Initializing UDP Server\n");
    ip_addr_t addr;
    IP4_ADDR(ip_2_ip4(&addr), 0, 0, 0, 0);

    udp_server_t _server;
    udp_server_init(&_server, &addr);

    int count_requests = 0;

    // 30 ms sleep => ~30 fps
    while(true) {
        update_lights();
        sleep_ms(30);
    }

    DEBUG_printf("Closing UDP Server\n");
    udp_server_deinit(&_server);

    cyw43_arch_deinit();
    return 0;
}
