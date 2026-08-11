/* Boot Flow — STA/AP mode selection based on config storage.
 *
 * Red-Green-Refactor: tests in test_boot_flow.c drive this design.
 *
 * On Pico: calls cyw43_arch_* and config_* directly.
 * On x86 test builds: uses stub functions set via boot_flow_set_stubs(). */
#include "boot_flow.h"
#include "config_storage.h"
#include "logger.h"

#include <string.h>
#include <stdio.h>
#include <unistd.h>

/* ── Stub-based x86 testing ────────────────────────────────────────── */
#ifdef FLASH_MOCK
static boot_flow_stubs_t *g_stubs = NULL;
#endif

/* Heap tracking — captures initial break for free-size calculations */
static uint32_t g_heap_initial = 0;

void boot_flow_set_stubs(const boot_flow_stubs_t *stubs) {
#ifdef FLASH_MOCK
    g_stubs = (boot_flow_stubs_t *)stubs;
#endif
}

/* ── Stub wrapper functions ────────────────────────────────────────── */

#ifdef FLASH_MOCK
static int stub_cyw43_init(void) {
    return g_stubs && g_stubs->cyw43_init ? g_stubs->cyw43_init() : 0;
}

static void stub_cyw43_enable_sta(void) {
    if (g_stubs && g_stubs->cyw43_enable_sta) g_stubs->cyw43_enable_sta();
}

static int stub_wifi_connect(const char *ssid, const char *pass,
                             int auth, uint32_t timeout_ms) {
    if (g_stubs && g_stubs->wifi_connect)
        return g_stubs->wifi_connect(ssid, pass, auth, timeout_ms);
    return -1;
}

static void stub_cyw43_enable_ap(void) {
    if (g_stubs && g_stubs->cyw43_enable_ap) g_stubs->cyw43_enable_ap();
}

static void stub_dns_init(void) {
    if (g_stubs && g_stubs->dns_init) g_stubs->dns_init();
}

static void stub_httpd_init(void) {
    if (g_stubs && g_stubs->httpd_init) g_stubs->httpd_init();
}

static void stub_dhcp_init(void) {
    if (g_stubs && g_stubs->dhcp_init) g_stubs->dhcp_init();
}

static void stub_apply_effect_settings(void) {
    if (g_stubs && g_stubs->apply_effect_settings)
        g_stubs->apply_effect_settings();
}
#else
/* Pico build: direct calls to real APIs */
#include <pico/cyw43_arch.h>
#include "captive_dns.h"
#include "httpd.h"
#include "dhcpserver.h"
#include "effects_engine.h"

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
    cyw43_arch_enable_ap_mode("LightSync", "lightsync", CYW43_AUTH_WPA2_AES_PSK);
}

static void stub_dns_init(void) {
    captive_dns_init();
}

static void stub_httpd_init(void) {
    httpd_init();
}

static dhcp_server_t dhcp_server;

static void stub_dhcp_init(void) {
    ip4_addr_t ip, mask;
    IP4_ADDR(&ip, 192, 168, 4, 1);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    dhcp_server_init(&dhcp_server, (ip_addr_t *)&ip, (ip_addr_t *)&mask);
}

static void stub_apply_effect_settings(void) {
    /* Load effect settings from config and apply to effects engine */
    config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    config_load(&cfg);

    /* A zeroed config (no flash data / failed load) must not paint solid
     * black — default the primary colour to white (real WLED's fresh-device
     * default) so a stored/solid effect is visible at boot. Mirrors the
     * non-black fallback in httpd.c's wled_state_init. */
    uint8_t r = cfg.color_r;
    uint8_t g = cfg.color_g;
    uint8_t b = cfg.color_b;
    if (!r && !g && !b) { r = 255; g = 255; b = 255; }

    effects_engine_set_mode((effects_mode_t)cfg.effects_mode);
    effect_params_t params = {
        cfg.speed, cfg.brightness,
        r, g, b,
        cfg.color2_r, cfg.color2_g, cfg.color2_b
    };
    effects_engine_set_effect((effect_id_t)cfg.effect_id, &params);
}
#endif

/* ── Public API ────────────────────────────────────────────────────── */

boot_mode_t boot_flow_run(void) {
    fwrite("BF\n", 1, 3, stdout);
    /* Capture initial heap break for free-size calculations */
    g_heap_initial = (uint32_t)(uintptr_t)sbrk(0);
    {
        char _h[64];
        int _n = snprintf(_h, sizeof(_h), "HEAP:initial=%u\n", g_heap_initial);
        if (_n > 0) fwrite(_h, 1, _n, stdout);
    }
    LOG_INFO(MOD_BOOT, "start, checking config_valid...");

    /* cyw43_arch_init() is NOT idempotent: calling it a second time (e.g. STA
     * connect fails then falling back to AP) re-runs cyw43_driver_init(), which
     * hard-asserts. Call it exactly once here, before branching — the AP
     * fallback reuses the already-initialised driver (cyw43_arch_enable_ap_mode()
     * only asserts cyw43_is_initialized). */
    LOG_INFO(MOD_BOOT, "calling cyw43_arch_init...");
    if (stub_cyw43_init() != 0) {
        LOG_ERROR(MOD_BOOT, "cyw43_init FAILED");
        return BOOT_MODE_FAIL;
    }
    LOG_INFO(MOD_BOOT, "cyw43_init OK");

    /* Step 1: Valid config — try STA mode */
    if (config_is_valid()) {
        LOG_INFO(MOD_BOOT, "config valid, trying STA mode");

        stub_cyw43_enable_sta();
        LOG_INFO(MOD_BOOT, "STA mode enabled");

        /* Load stored credentials */
        config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        config_load(&cfg);
        LOG_INFO(MOD_BOOT, "config loaded, connecting to '%s'", cfg.ssid);

        LOG_INFO(MOD_BOOT, "calling wifi_connect (timeout 30s)...");
        int rc = stub_wifi_connect(cfg.ssid, cfg.password,
#ifdef FLASH_MOCK
                                   0, 30000);
#else
                                   CYW43_AUTH_WPA2_AES_PSK, 30000);
#endif
        LOG_INFO(MOD_BOOT, "wifi_connect rc=%d", rc);
        if (rc == 0) {
            /* Apply effect settings from config */
            LOG_INFO(MOD_BOOT, "STA join OK — applying effects");
            stub_apply_effect_settings();
            LOG_INFO(MOD_BOOT, "effects applied — returning STA mode");
            return BOOT_MODE_STA;
        }
        /* STA failed — fall through to AP (cyw43 already initialised above) */
        LOG_INFO(MOD_BOOT, "STA connect failed rc=%d — falling back to AP", rc);
    }

    /* Step 2: No valid config or STA failed — AP mode.
     * No second cyw43_arch_init() here — the driver was initialised once at the
     * top, and cyw43_arch_enable_ap_mode() only requires it initialised. */
    LOG_INFO(MOD_BOOT, "entering AP mode path");
    LOG_INFO(MOD_BOOT, "enabling AP mode...");
    stub_cyw43_enable_ap();
    LOG_INFO(MOD_BOOT, "AP mode enabled");

    /* Heap snapshot after cyw43 init */
    {
        char _h[64];
        int _n = snprintf(_h, sizeof(_h), "HEAP:cyw43=%u\n", (uint32_t)(uintptr_t)sbrk(0) - g_heap_initial);
        if (_n > 0) fwrite(_h, 1, _n, stdout);
    }

    LOG_INFO(MOD_BOOT, "enabling AP mode...");
    stub_cyw43_enable_ap();
    LOG_INFO(MOD_BOOT, "AP mode enabled");

    /* Start DHCP server so clients can get an IP address */
    LOG_INFO(MOD_BOOT, "init dhcp server...");
    stub_dhcp_init();
    LOG_INFO(MOD_BOOT, "dhcp server OK");

    /* Heap snapshot after DHCP */
    {
        char _h[64];
        int _n = snprintf(_h, sizeof(_h), "HEAP:dhcp=%u\n", (uint32_t)(uintptr_t)sbrk(0) - g_heap_initial);
        if (_n > 0) fwrite(_h, 1, _n, stdout);
    }

    /* Start captive portal services */
    LOG_INFO(MOD_BOOT, "init dns...");
    stub_dns_init();
    LOG_INFO(MOD_BOOT, "dns OK");
    LOG_INFO(MOD_BOOT, "init httpd...");
    stub_httpd_init();
    /* AP captive portal: GET / serves the WiFi provisioning form (not the
     * STA WLED control page). Explicit even though 1 is the default, so the
     * boot mode is self-documenting here. Mock builds don't link httpd.c. */
#ifndef FLASH_MOCK
    httpd_set_portal_mode(1);
#endif
    LOG_INFO(MOD_BOOT, "httpd OK");

    /* Heap snapshot after HTTPD */
    {
        char _h[64];
        int _n = snprintf(_h, sizeof(_h), "HEAP:httpd=%u\n", (uint32_t)(uintptr_t)sbrk(0) - g_heap_initial);
        if (_n > 0) fwrite(_h, 1, _n, stdout);
    }

    /* Apply effect settings from config */
    LOG_INFO(MOD_BOOT, "applying effect settings...");
    stub_apply_effect_settings();
    LOG_INFO(MOD_BOOT, "effect settings OK");

    /* Final boot marker — everything initialized */
    LOG_INFO(MOD_BOOT, "BOOT_COMPLETE");

    /* Memory snapshot before main loop — tells us heap state at boot */
    LOG_INFO(MOD_BOOT, "boot complete, entering main loop");

    return BOOT_MODE_AP;
}
