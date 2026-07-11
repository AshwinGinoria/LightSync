/* Boot Flow — decides STA vs AP mode based on config storage.
 *
 * On boot:
 *   1. Load config from flash
 *   2. If valid config exists → enable STA mode, connect to stored WiFi
 *   3. If STA connects successfully → start mDNS + UDP servers
 *   4. If no valid config OR STA fails → fallback to AP mode
 *      with captive DNS + HTTP provisioning portal
 *
 * This file is the only place that calls cyw43_arch_* functions.
 * All other modules are decoupled via stubs for x86 testing. */
#ifndef BOOT_FLOW_H
#define BOOT_FLOW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Boot mode determined by config check. */
typedef enum {
    BOOT_MODE_STA,    /* Connected to WiFi as client */
    BOOT_MODE_AP,     /* Running as AP with captive portal */
    BOOT_MODE_FAIL    /* Hardware init failed */
} boot_mode_t;

/* Run the full boot flow: check config, connect or start AP.
 * Returns the determined boot mode.
 *
 * On Pico: calls cyw43_arch_* and config_* directly.
 * On x86 test builds: uses stub functions set via boot_flow_set_stubs(). */
boot_mode_t boot_flow_run(void);

/* Set stubs for x86 testing. Call before boot_flow_run(). */
typedef struct {
    int (*config_is_valid)(void);
    int (*cyw43_init)(void);
    void (*cyw43_enable_sta)(void);
    int (*wifi_connect)(const char *ssid, const char *pass, int auth, uint32_t timeout_ms);
    void (*cyw43_enable_ap)(void);
    void (*dns_init)(void);
    void (*httpd_init)(void);
    void (*dhcp_init)(void);
    void (*apply_effect_settings)(void);
} boot_flow_stubs_t;

void boot_flow_set_stubs(const boot_flow_stubs_t *stubs);

#ifdef __cplusplus
}
#endif

#endif /* BOOT_FLOW_H */
