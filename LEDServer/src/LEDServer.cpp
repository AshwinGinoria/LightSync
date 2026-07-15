#include "logger.h"
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
        LOG_ERROR(MOD_UDP, "raw UDP: udp_new failed");
        return -1;
    }
    if (udp_bind(s->raw_pcb, &any, SERVER_PORT) != ERR_OK) {
        LOG_ERROR(MOD_UDP, "raw UDP: bind port %d failed", SERVER_PORT);
        return -1;
    }
    udp_recv(s->raw_pcb, raw_udp_recv, NULL);
    LOG_INFO(MOD_UDP, "Raw UDP server on port %d", SERVER_PORT);

    /* DDP (port 4048) — best-effort */
    s->ddp_pcb = (struct udp_pcb *)protocol_ddp_init();
    if (!s->ddp_pcb) {
        LOG_WARN(MOD_DDP, "init failed — continuing without DDP");
    }

    /* Music sync (port 5006) — best-effort */
    s->music_pcb = (struct udp_pcb *)music_sync_init();
    if (!s->music_pcb) {
        LOG_WARN(MOD_MUSIC, "init failed — continuing without music sync");
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

/* WFI deadline loop body: run work until the 10ms deadline expires,
 * measuring idle time via DWT.
 *
 * heartbeat_count is used to throttle diagnostics to ~1/sec.
 * update_leds: in STA mode we update effects + strip each iteration;
 * in AP mode (captive portal) the loop is lighter — only cyw43_poll + WFI. */
static void main_loop_body(uint32_t &heartbeat_count, bool update_leds) {
    uint32_t deadline = dwt_read_cycles() + 1330000; /* ~10 ms in cycles */
    while (dwt_read_cycles() < deadline) {
        cyw43_arch_poll();
        if (update_leds) {
            effects_engine_update();
            led_strip_update();
        }
        __asm__ volatile("wfi");
    }
    /* Measure idle time: idle_cycles = 10ms_wall_cycles - work_cycles. */
    {
        uint32_t work_cycles = dwt_read_cycles() - (deadline - 1330000);
        uint32_t idle_cycles = (work_cycles < 1330000) ? (1330000 - work_cycles) : 0;
        dwt_sample_window(1330000, idle_cycles);
    }
    heartbeat_count++;
    if (heartbeat_count % 100 == 0) {
        memory_heartbeat_report();
        stack_watermark_report();
        LOG_INFO(MOD_MEM, "cpu_load=%u%% total_cycles=%u",
                 dwt_get_cpu_load_pct(),
                 dwt_read_cycles());
    }

    /* Fast diagnostic: every 50 iterations (~500ms) print a marker
     * so we can see if the main loop is still running after a hang. */
    if (heartbeat_count % 50 == 0) {
        char _hb[32];
        int _n = snprintf(_hb, sizeof(_hb), "HB:%u\n", heartbeat_count);
        if (_n > 0) fwrite(_hb, 1, _n, stdout);
    }
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
    LOG_INFO(MOD_MAIN, "LEDServer starting");

    /* Initialise memory heartbeat tracking. */
    memory_heartbeat_init();

    /* Initialise DWT cycle counter for CPU profiling. */
    dwt_init();

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
        LOG_ERROR(MOD_BOOT, "Boot failed — hardware init error");
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
            LOG_ERROR(MOD_MAIN, "Failed to start Server");
            multi_server_deinit(&_server);
            return 1;
        }
        LOG_INFO(MOD_MAIN, "Server running at %s", ip4addr_ntoa(netif_ip4_addr(netif_list)));

        // Boot blink: green then black
        ledStrip.fill(GREEN);
        ledStrip.show();
        sleep_ms(100);
        led_strip_clear();

        {
            uint32_t heartbeat = 0;
            while(true) {
                main_loop_body(heartbeat, true); /* STA: update effects + strip */
            }
        }

        LOG_INFO(MOD_MAIN, "Closing UDP Server");
        multi_server_deinit(&_server);
    }

    // AP mode: captive portal is already running (DNS + HTTPD started by boot_flow)
    if (mode == BOOT_MODE_AP) {
        LOG_INFO(MOD_MAIN, "AP mode: entering main loop");
        LOG_INFO(MOD_MAIN, "ledStripReady=%d", ledStripReady);
        // Boot blink: amber then black
        LOG_INFO(MOD_MAIN, "boot blink: amber");
        ledStrip.fill(PicoLed::RGB(10, 10, 0));
        LOG_INFO(MOD_MAIN, "ledStrip.fill done");
        ledStrip.show();
        LOG_INFO(MOD_MAIN, "ledStrip.show done");
        sleep_ms(100);
        led_strip_clear();
        LOG_INFO(MOD_MAIN, "led_strip_clear done");

        {
            uint32_t heartbeat = 0;
            LOG_INFO(MOD_MAIN, "entering main loop");
            while(true) {
                main_loop_body(heartbeat, false); /* AP: lightweight, no effects/strip */
            }
        }
    }

    return 0;
}
