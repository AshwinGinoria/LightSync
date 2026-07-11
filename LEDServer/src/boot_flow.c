/* Boot Flow — STA/AP mode selection based on config storage.
 *
 * Red-Green-Refactor: tests in test_boot_flow.c drive this design.
 *
 * On Pico: calls cyw43_arch_* and config_* directly.
 * On x86 test builds: uses stub functions set via boot_flow_set_stubs(). */
#include "boot_flow.h"
#include "config_storage.h"

#include <string.h>
#include <stdio.h>

/* ── Stub-based x86 testing ────────────────────────────────────────── */
#ifdef FLASH_MOCK
static boot_flow_stubs_t *g_stubs = NULL;
#endif

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
    effects_engine_set_mode((effects_mode_t)cfg.effects_mode);
    effect_params_t params = {
        cfg.speed, cfg.brightness,
        cfg.color_r, cfg.color_g, cfg.color_b,
        cfg.color2_r, cfg.color2_g, cfg.color2_b
    };
    effects_engine_set_effect((effect_id_t)cfg.effect_id, &params);
}
#endif

/* ── Public API ────────────────────────────────────────────────────── */

boot_mode_t boot_flow_run(void) {
    printf("boot_flow: start, checking config_valid...\n");
    /* Step 1: Try to load config from flash */
    if (config_is_valid()) {
        printf("boot_flow: config valid, trying STA mode\n");
        /* Step 2: Valid config — try STA mode */
        printf("boot_flow: calling cyw43_arch_init...\n");
        if (stub_cyw43_init() != 0) {
            printf("boot_flow: cyw43_init FAILED\n");
            return BOOT_MODE_FAIL;
        }
        printf("boot_flow: cyw43_init OK\n");

        stub_cyw43_enable_sta();

        /* Load stored credentials */
        config_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        config_load(&cfg);

        int rc = stub_wifi_connect(cfg.ssid, cfg.password,
#ifdef FLASH_MOCK
                                   0, 30000);
#else
                                   CYW43_AUTH_WPA2_AES_PSK, 30000);
#endif
        if (rc == 0) {
            /* Apply effect settings from config */
            stub_apply_effect_settings();
            return BOOT_MODE_STA;
        }
        /* STA failed — fall through to AP */
    }

    /* Step 3: No valid config or STA failed — AP mode */
    printf("boot_flow: entering AP mode path\n");
    printf("boot_flow: calling cyw43_arch_init for AP...\n");
    if (stub_cyw43_init() != 0) {
        printf("boot_flow: AP cyw43_init FAILED\n");
        return BOOT_MODE_FAIL;
    }
    printf("boot_flow: AP cyw43_init OK\n");

    printf("boot_flow: enabling AP mode...\n");
    stub_cyw43_enable_ap();
    printf("boot_flow: AP mode enabled\n");

    /* Start DHCP server so clients can get an IP address */
    stub_dhcp_init();

    /* Start captive portal services */
    stub_dns_init();
    stub_httpd_init();

    /* Apply effect settings from config */
    stub_apply_effect_settings();

    return BOOT_MODE_AP;
}
