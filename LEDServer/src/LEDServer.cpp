#include <stdio.h>
// #include <iostream>  // CRASH: iostream static ctors crash before main() on Pico W

#include <lwip/udp.h>
#include <pico/cyw43_arch.h>
#include <pico/stdlib.h>

#include "PicoLed.hpp"
#include "led_engine.h"
#include "mdns_service.h"
#include "protocol_ddp.h"
#include "effects_engine.h"
#include "music_sync.h"
#include "boot_flow.h"

#define DEBUG_printf printf
#define ERROR_printf printf

PicoLed::Color GREEN = PicoLed::RGB(10, 0, 0);
PicoLed::Color RED = PicoLed::RGB(0, 10, 0);
PicoLed::Color BLUE = PicoLed::RGB(0, 0, 10);
PicoLed::Color BLACK = PicoLed::RGB(0, 0, 0);
// TEST: delay addLeds to main() to check if static constructors cause silent crash
// addLeds returns PicoLedController, so we store a pointer to one and deref via macro.
// The actual object lives as a stack local in main().
static void* ledStripStorage[sizeof(PicoLed::PicoLedController) / sizeof(void*) + 1];
#define ledStrip (*(PicoLed::PicoLedController*)ledStripStorage)

static bool ledStripReady = false;

// Hardware stubs for led_engine
extern "C" void led_strip_set_brightness(uint8_t brightness) {
    if (ledStripReady) ledStrip.setBrightness(brightness);
}
extern "C" void led_strip_set_pixel(uint32_t i, uint8_t r, uint8_t g, uint8_t b) {
    if (ledStripReady) ledStrip.setPixelColor(i, PicoLed::RGB(r, g, b));
}
extern "C" void led_strip_show(void) {
    if (ledStripReady) ledStrip.show();
}

/* ── Multi-protocol UDP server ─────────────────────────────────────────
 * Owns one udp_pcb per protocol.  All protocols share led_buffer[] and
 * led_update_pending — last-write-wins at ~100 Hz refresh.
 *
 * Individual protocol modules can fail to initialise (non-fatal) and
 * the main loop continues with the protocols that succeeded. */

typedef struct {
    struct udp_pcb *raw_pcb;    /* port 5005 — legacy raw RGB frames */
    struct udp_pcb *ddp_pcb;    /* port 4048 — DDP raw pixels        */
    struct udp_pcb *music_pcb;  /* port 5006 — music sync spectrum   */
} multi_server_t;

/* ── raw UDP (port 5005) — unchanged behaviour ─────────────────── */

static void raw_udp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                         const ip_addr_t *src, u16_t port) {
    (void)arg; (void)pcb; (void)src; (void)port;
    pbuf_copy_partial(p, led_buffer, sizeof(led_buffer), 0);
    led_update_pending = 1;
    effects_engine_client_active();
    pbuf_free(p);
}

/* ── init / deinit ──────────────────────────────────────────────── */

static int multi_server_init(multi_server_t *s) {
    ip_addr_t any;
    IP4_ADDR(&any, 0, 0, 0, 0);

    /* Raw UDP (port 5005) — must succeed */
    s->raw_pcb = udp_new();
    if (!s->raw_pcb) {
        ERROR_printf("raw UDP: udp_new failed\n");
        return -1;
    }
    if (udp_bind(s->raw_pcb, &any, SERVER_PORT) != ERR_OK) {
        ERROR_printf("raw UDP: bind port %d failed\n", SERVER_PORT);
        return -1;
    }
    udp_recv(s->raw_pcb, raw_udp_recv, NULL);
    DEBUG_printf("Raw UDP server on port %d\n", SERVER_PORT);

    /* DDP (port 4048) — best-effort */
    s->ddp_pcb = (struct udp_pcb *)protocol_ddp_init();
    if (!s->ddp_pcb) {
        printf("DDP init failed — continuing without DDP\n");
    }

    /* Music sync (port 5006) — best-effort */
    s->music_pcb = (struct udp_pcb *)music_sync_init();
    if (!s->music_pcb) {
        printf("MusicSync init failed — continuing without music sync\n");
    }

    return 0;
}

static void multi_server_deinit(multi_server_t *s) {
    if (s->raw_pcb)   { udp_remove(s->raw_pcb);   s->raw_pcb   = NULL; }
    if (s->ddp_pcb)   { udp_remove(s->ddp_pcb);   s->ddp_pcb   = NULL; }
    if (s->music_pcb) { udp_remove(s->music_pcb); s->music_pcb   = NULL; }
}

/* ── Boot stubs — bridge boot_flow to real Pico APIs ───────────────── */

static int stub_cyw43_init(void) {
    return cyw43_arch_init();
}

static void stub_cyw43_enable_sta(void) {
    cyw43_arch_enable_sta_mode();
}

static int stub_wifi_connect(const char *ssid, const char *pass,
                             int auth, uint32_t timeout_ms) {
    return cyw43_arch_wifi_connect_timeout_ms(ssid, pass, auth, timeout_ms);
}

static void stub_cyw43_enable_ap(void) {
    cyw43_arch_enable_ap_mode("LightSync", "", 0);
}

static void stub_dns_init(void) {
    // DNS is handled by cyw43_arch internally in newer SDK
}

static void stub_httpd_init(void) {
    // HTTPD is handled by cyw43_arch internally in newer SDK
}

static void configure_boot_stubs(void) {
    boot_flow_stubs_t stubs;
    memset(&stubs, 0, sizeof(stubs));
    stubs.cyw43_init   = stub_cyw43_init;
    stubs.cyw43_enable_sta = stub_cyw43_enable_sta;
    stubs.wifi_connect   = stub_wifi_connect;
    stubs.cyw43_enable_ap  = stub_cyw43_enable_ap;
    stubs.dns_init     = stub_dns_init;
    stubs.httpd_init   = stub_httpd_init;
    boot_flow_set_stubs(&stubs);
}

int main() {
    stdio_init_all();
    printf("MAIN reached\n");

    /* USB CDC on Pico SDK 2.1.0 needs ~2s to enumerate on the host;
     * a brief delay ensures serial output isn't lost during init. */
    sleep_ms(2000);

    memset(led_buffer, 0, sizeof(led_buffer));
    led_update_pending = 0;

    // Initialize PicoLED strip (placement-new: avoids global static constructor)
    new (ledStripStorage) PicoLed::PicoLedController(
        PicoLed::addLeds<PicoLed::WS2812B>(pio0, 0, LED_PIN, LED_LENGTH, PicoLed::FORMAT_GRB));
    ledStripReady = true;

    led_strip_init();
    effects_engine_init();
    configure_boot_stubs();

    boot_mode_t mode = boot_flow_run();

    if (mode == BOOT_MODE_FAIL) {
        ERROR_printf("Boot failed — hardware init error\n");
        return 1;
    }

    if (mode == BOOT_MODE_STA) {
        // STA mode: advertise via mDNS so standard LED apps can discover us
        mdns_service_init();

        ip_addr_t addr;
        IP4_ADDR(ip_2_ip4(&addr), 0, 0, 0, 0);

        // Initialize multi-protocol server (raw UDP + DDP)
        multi_server_t _server;
        int err = multi_server_init(&_server);
        if (err != 0) {
            ERROR_printf("Failed to start Server\n");
            multi_server_deinit(&_server);
            return 1;
        }
        DEBUG_printf("Server running at %s\n", ip4addr_ntoa(netif_ip4_addr(netif_list)));

        // Boot blink: green then black
        ledStrip.fill(GREEN);
        ledStrip.show();
        sleep_ms(100);
        led_strip_clear();

        // 10 ms sleep => ~100 fps
        while(true) {
            sleep_ms(10);
            cyw43_arch_poll();
            effects_engine_update();  // runs autonomous effects when client is idle
            led_strip_update();
        }

        DEBUG_printf("Closing UDP Server\n");
        multi_server_deinit(&_server);
    }

    // AP mode: captive portal is already running (DNS + HTTPD started by boot_flow)
    if (mode == BOOT_MODE_AP) {
        // Boot blink: amber then black
        ledStrip.fill(PicoLed::RGB(10, 10, 0));
        ledStrip.show();
        sleep_ms(100);
        led_strip_clear();

        while(true) {
            sleep_ms(10);
            cyw43_arch_poll();
        }
    }

    return 0;
}
