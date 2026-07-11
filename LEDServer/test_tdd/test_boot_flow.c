/* Boot Flow Integration Tests — Phase 4: STA/AP mode selection */
#include "boot_flow.h"
#include "config_storage.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ── Test harness ──────────────────────────────────────────────────── */
static int  tests_run     =  0;
static int  tests_failed  =  0;

#define TEST(name)  do { \
    tests_run++; \
    printf("TEST: %s\n", name); \
} while(0)

#define CHECK(cond)  do { \
    if (!(cond)) { \
        printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define CHECK_EQ(a, b)  do { \
    if ((a) != (b)) { \
        printf("  FAIL %s:%d: %s (%d) != %s (%d)\n", \
            __FILE__, __LINE__, #a, (int)(a), #b, (int)(b)); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define CHECK_STR_EQ(a, b)  do { \
    if (strcmp((a), (b)) != 0) { \
        printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", \
            __FILE__, __LINE__, (a), (b)); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* ── Mock state ────────────────────────────────────────────────────── */
static struct {
    int cyw43_init_called;
    int cyw43_enable_sta_called;
    int cyw43_enable_ap_called;
    int wifi_connect_called;
    int wifi_connect_result;
    int dns_init_called;
    int httpd_init_called;
    int dhcp_init_called;
    const char *wifi_ssid;
    const char *wifi_pass;
} mock;

/* Global counter for cyw43_init failure test */
static int fail_init_count = 0;

/* Track whether apply_effect_settings stub was called */
static int stub_apply_effect_settings_called = 0;

static void reset_mock(void) {
    memset(&mock, 0, sizeof(mock));
    fail_init_count = 0;
    stub_apply_effect_settings_called = 0;
    boot_flow_set_stubs(NULL); /* clear stubs */
}

/* ── Stub implementations ──────────────────────────────────────────── */
static int stub_cyw43_init(void) {
    mock.cyw43_init_called = 1;
    return 0; /* success */
}

static int fail_cyw43_init(void) {
    fail_init_count++;
    /* First call fails, second call succeeds (AP mode init) */
    return (fail_init_count == 1) ? -1 : 0;
}

static void stub_cyw43_enable_sta(void) {
    mock.cyw43_enable_sta_called = 1;
}

static int stub_wifi_connect(const char *ssid, const char *pass,
                             int auth, uint32_t timeout_ms) {
    (void)auth; (void)timeout_ms;
    mock.wifi_connect_called = 1;
    mock.wifi_ssid = ssid;
    mock.wifi_pass = pass;
    return mock.wifi_connect_result;
}

static void stub_cyw43_enable_ap(void) {
    mock.cyw43_enable_ap_called = 1;
}

static void stub_dns_init(void) {
    mock.dns_init_called = 1;
}

static void stub_httpd_init(void) {
    mock.httpd_init_called = 1;
}

static void stub_dhcp_init(void) {
    mock.dhcp_init_called = 1;
}

static int stub_apply_effect_settings_called;
static void stub_apply_effect_settings(void) {
    stub_apply_effect_settings_called = 1;
}

/* ═════════════════════════════════════════════════════════════════════
 * BOOT FLOW TESTS
 * ═════════════════════════════════════════════════════════════════════ */

/* B1: no config → AP mode */
static void test_boot_flow_no_config_ap_mode(void) {
    TEST("No config → AP mode");
    reset_mock();

    boot_flow_stubs_t stubs;
    memset(&stubs, 0, sizeof(stubs));
    stubs.cyw43_init = stub_cyw43_init;
    stubs.cyw43_enable_sta = stub_cyw43_enable_sta;
    stubs.wifi_connect = stub_wifi_connect;
    stubs.cyw43_enable_ap = stub_cyw43_enable_ap;
    stubs.dns_init = stub_dns_init;
    stubs.httpd_init = stub_httpd_init;
    stubs.apply_effect_settings = NULL;
    boot_flow_set_stubs(&stubs);

    boot_mode_t mode = boot_flow_run();

    CHECK(mode == BOOT_MODE_AP);
    CHECK(mock.cyw43_init_called);
    CHECK(mock.cyw43_enable_ap_called);
    CHECK(mock.dns_init_called);
    CHECK(mock.httpd_init_called);
}

/* B2: valid config + STA connects → STA mode */
static void test_boot_flow_valid_config_sta_connects(void) {
    TEST("Valid config + STA connects → STA mode");
    reset_mock();
    mock.wifi_connect_result = 0; /* success */

    /* Save a valid config */
    config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.magic, "LSYN", 4);
    cfg.version = CONFIG_VERSION;
    cfg.flags = CONFIG_FLAG_VALID;
    memcpy(cfg.ssid, "TestWiFi", 8);
    memcpy(cfg.password, "secret", 7);
    cfg.checksum = 0; /* will be computed by config_save */
    config_save(&cfg);

    boot_flow_stubs_t stubs;
    memset(&stubs, 0, sizeof(stubs));
    stubs.cyw43_init = stub_cyw43_init;
    stubs.cyw43_enable_sta = stub_cyw43_enable_sta;
    stubs.wifi_connect = stub_wifi_connect;
    stubs.cyw43_enable_ap = stub_cyw43_enable_ap;
    stubs.dns_init = stub_dns_init;
    stubs.httpd_init = stub_httpd_init;
    stubs.apply_effect_settings = NULL;
    boot_flow_set_stubs(&stubs);

    boot_mode_t mode = boot_flow_run();

    CHECK(mode == BOOT_MODE_STA);
    CHECK(mock.cyw43_enable_sta_called);
    CHECK(!mock.cyw43_enable_ap_called);
    CHECK(!mock.dns_init_called);
}

/* B3: valid config + STA fails → AP mode */
static void test_boot_flow_valid_config_sta_fails_ap(void) {
    TEST("Valid config + STA fails → AP mode");
    reset_mock();
    mock.wifi_connect_result = -1; /* failure */

    /* Save a valid config */
    config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.magic, "LSYN", 4);
    cfg.version = CONFIG_VERSION;
    cfg.flags = CONFIG_FLAG_VALID;
    memcpy(cfg.ssid, "TestWiFi", 8);
    memcpy(cfg.password, "secret", 7);
    config_save(&cfg);

    boot_flow_stubs_t stubs;
    memset(&stubs, 0, sizeof(stubs));
    stubs.cyw43_init = stub_cyw43_init;
    stubs.cyw43_enable_sta = stub_cyw43_enable_sta;
    stubs.wifi_connect = stub_wifi_connect;
    stubs.cyw43_enable_ap = stub_cyw43_enable_ap;
    stubs.dns_init = stub_dns_init;
    stubs.httpd_init = stub_httpd_init;
    stubs.apply_effect_settings = NULL;
    boot_flow_set_stubs(&stubs);

    boot_mode_t mode = boot_flow_run();

    CHECK(mode == BOOT_MODE_AP);
    CHECK(mock.cyw43_enable_sta_called);
    CHECK(mock.wifi_connect_called);
    CHECK(mock.cyw43_enable_ap_called);
    CHECK(mock.dns_init_called);
}

/* B4: cyw43_init fails → BOOT_MODE_FAIL */
static void test_boot_flow_cyw43_init_fails(void) {
    TEST("cyw43_init fails → BOOT_MODE_FAIL");
    reset_mock();

    boot_flow_stubs_t stubs;
    memset(&stubs, 0, sizeof(stubs));
    stubs.cyw43_init = fail_cyw43_init;
    boot_flow_set_stubs(&stubs);

    boot_mode_t mode = boot_flow_run();

    CHECK(mode == BOOT_MODE_FAIL);
}

/* B5: STA connects with stored SSID/password */
static void test_boot_flow_uses_stored_credentials(void) {
    TEST("STA connects with stored SSID/password");
    reset_mock();
    mock.wifi_connect_result = 0; /* success */

    /* Save a valid config */
    config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.magic, "LSYN", 4);
    cfg.version = CONFIG_VERSION;
    cfg.flags = CONFIG_FLAG_VALID;
    memcpy(cfg.ssid, "MyHomeNetwork", 14);
    memcpy(cfg.password, "myP@ssw0rd!", 11);
    config_save(&cfg);

    boot_flow_stubs_t stubs;
    memset(&stubs, 0, sizeof(stubs));
    stubs.cyw43_init = stub_cyw43_init;
    stubs.cyw43_enable_sta = stub_cyw43_enable_sta;
    stubs.wifi_connect = stub_wifi_connect;
    stubs.cyw43_enable_ap = stub_cyw43_enable_ap;
    stubs.dns_init = stub_dns_init;
    stubs.httpd_init = stub_httpd_init;
    stubs.apply_effect_settings = NULL;
    boot_flow_set_stubs(&stubs);

    boot_mode_t mode = boot_flow_run();

    CHECK(mode == BOOT_MODE_STA);
    CHECK(mock.wifi_connect_called);
    CHECK_STR_EQ(mock.wifi_ssid, "MyHomeNetwork");
    CHECK_STR_EQ(mock.wifi_pass, "myP@ssw0rd!");
}

/* B6: AP mode starts captive DNS */
static void test_boot_flow_ap_starts_dns(void) {
    TEST("AP mode starts captive DNS");
    reset_mock();

    boot_flow_stubs_t stubs;
    memset(&stubs, 0, sizeof(stubs));
    stubs.cyw43_init = stub_cyw43_init;
    stubs.cyw43_enable_ap = stub_cyw43_enable_ap;
    stubs.dns_init = stub_dns_init;
    stubs.httpd_init = stub_httpd_init;
    stubs.apply_effect_settings = NULL;
    boot_flow_set_stubs(&stubs);

    boot_mode_t mode = boot_flow_run();

    CHECK(mode == BOOT_MODE_AP);
    CHECK(mock.dns_init_called);
}

/* B7: AP mode starts HTTPD */
static void test_boot_flow_ap_starts_httpd(void) {
    TEST("AP mode starts HTTPD");
    reset_mock();

    boot_flow_stubs_t stubs;
    memset(&stubs, 0, sizeof(stubs));
    stubs.cyw43_init = stub_cyw43_init;
    stubs.cyw43_enable_ap = stub_cyw43_enable_ap;
    stubs.dns_init = stub_dns_init;
    stubs.httpd_init = stub_httpd_init;
    stubs.apply_effect_settings = NULL;
    boot_flow_set_stubs(&stubs);

    boot_mode_t mode = boot_flow_run();

    CHECK(mode == BOOT_MODE_AP);
    CHECK(mock.httpd_init_called);
}

/* B8: erasing config causes AP mode */
static void test_boot_flow_erase_config_causes_ap(void) {
    TEST("Erasing config causes AP mode");
    reset_mock();

    /* First, save a valid config */
    config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.magic, "LSYN", 4);
    cfg.version = CONFIG_VERSION;
    cfg.flags = CONFIG_FLAG_VALID;
    memcpy(cfg.ssid, "TestWiFi", 8);
    memcpy(cfg.password, "secret", 7);
    config_save(&cfg);
    CHECK(config_is_valid());

    /* Now erase config */
    config_erase();
    CHECK(!config_is_valid());

    boot_flow_stubs_t stubs;
    memset(&stubs, 0, sizeof(stubs));
    stubs.cyw43_init = stub_cyw43_init;
    stubs.cyw43_enable_ap = stub_cyw43_enable_ap;
    stubs.dns_init = stub_dns_init;
    stubs.httpd_init = stub_httpd_init;
    stubs.apply_effect_settings = NULL;
    boot_flow_set_stubs(&stubs);

    boot_mode_t mode = boot_flow_run();

    CHECK(mode == BOOT_MODE_AP);
}

/* B9: BOOT_MODE_STA constant exists */
static void test_boot_mode_sta_exists(void) {
    TEST("BOOT_MODE_STA constant");
    CHECK(BOOT_MODE_STA == 0);
}

/* B10: BOOT_MODE_AP constant exists */
static void test_boot_mode_ap_exists(void) {
    TEST("BOOT_MODE_AP constant");
    CHECK(BOOT_MODE_AP == 1);
}

/* B11: apply_effect_settings called after STA connects */
static void test_boot_flow_apply_settings_on_sta(void) {
    TEST("apply_effect_settings called after STA connects");
    reset_mock();
    stub_apply_effect_settings_called = 0;
    mock.wifi_connect_result = 0; /* success */

    /* Save a valid config */
    config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.magic, "LSYN", 4);
    cfg.version = CONFIG_VERSION;
    cfg.flags = CONFIG_FLAG_VALID;
    memcpy(cfg.ssid, "TestWiFi", 8);
    memcpy(cfg.password, "secret", 7);
    config_save(&cfg);

    boot_flow_stubs_t stubs;
    memset(&stubs, 0, sizeof(stubs));
    stubs.cyw43_init = stub_cyw43_init;
    stubs.cyw43_enable_sta = stub_cyw43_enable_sta;
    stubs.wifi_connect = stub_wifi_connect;
    stubs.cyw43_enable_ap = stub_cyw43_enable_ap;
    stubs.dns_init = stub_dns_init;
    stubs.httpd_init = stub_httpd_init;
    stubs.apply_effect_settings = stub_apply_effect_settings;
    boot_flow_set_stubs(&stubs);

    boot_mode_t mode = boot_flow_run();

    CHECK(mode == BOOT_MODE_STA);
    CHECK(stub_apply_effect_settings_called);
}

/* B12: apply_effect_settings called after AP init */
static void test_boot_flow_apply_settings_on_ap(void) {
    TEST("apply_effect_settings called after AP init");
    reset_mock();
    stub_apply_effect_settings_called = 0;

    boot_flow_stubs_t stubs;
    memset(&stubs, 0, sizeof(stubs));
    stubs.cyw43_init = stub_cyw43_init;
    stubs.cyw43_enable_ap = stub_cyw43_enable_ap;
    stubs.dns_init = stub_dns_init;
    stubs.httpd_init = stub_httpd_init;
    stubs.apply_effect_settings = stub_apply_effect_settings;
    boot_flow_set_stubs(&stubs);

    boot_mode_t mode = boot_flow_run();

    CHECK(mode == BOOT_MODE_AP);
    CHECK(stub_apply_effect_settings_called);
}

/* B13: AP mode starts DHCP server */
static void test_boot_flow_ap_starts_dhcp(void) {
    TEST("AP mode starts DHCP server");
    reset_mock();

    boot_flow_stubs_t stubs;
    memset(&stubs, 0, sizeof(stubs));
    stubs.cyw43_init = stub_cyw43_init;
    stubs.cyw43_enable_ap = stub_cyw43_enable_ap;
    stubs.dhcp_init = stub_dhcp_init;
    stubs.dns_init = stub_dns_init;
    stubs.httpd_init = stub_httpd_init;
    stubs.apply_effect_settings = NULL;
    boot_flow_set_stubs(&stubs);

    boot_mode_t mode = boot_flow_run();

    CHECK(mode == BOOT_MODE_AP);
    CHECK(mock.dhcp_init_called);
}

/* B14: STA mode does NOT start DHCP server */
static void test_boot_flow_sta_no_dhcp(void) {
    TEST("STA mode does NOT start DHCP server");
    reset_mock();
    mock.wifi_connect_result = 0; /* success */

    /* Save a valid config */
    config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.magic, "LSYN", 4);
    cfg.version = CONFIG_VERSION;
    cfg.flags = CONFIG_FLAG_VALID;
    memcpy(cfg.ssid, "TestWiFi", 8);
    memcpy(cfg.password, "secret", 7);
    config_save(&cfg);

    boot_flow_stubs_t stubs;
    memset(&stubs, 0, sizeof(stubs));
    stubs.cyw43_init = stub_cyw43_init;
    stubs.cyw43_enable_sta = stub_cyw43_enable_sta;
    stubs.wifi_connect = stub_wifi_connect;
    stubs.cyw43_enable_ap = stub_cyw43_enable_ap;
    stubs.dhcp_init = stub_dhcp_init;
    stubs.dns_init = stub_dns_init;
    stubs.httpd_init = stub_httpd_init;
    stubs.apply_effect_settings = NULL;
    boot_flow_set_stubs(&stubs);

    boot_mode_t mode = boot_flow_run();

    CHECK(mode == BOOT_MODE_STA);
    CHECK(!mock.dhcp_init_called);
}

/* ═════════════════════════════════════════════════════════════════════
/* ── Runner ────────────────────────────────────────────────────────── */

int main(void) {
    test_boot_flow_no_config_ap_mode();
    test_boot_flow_valid_config_sta_connects();
    test_boot_flow_valid_config_sta_fails_ap();
    test_boot_flow_cyw43_init_fails();
    test_boot_flow_uses_stored_credentials();
    test_boot_flow_ap_starts_dns();
    test_boot_flow_ap_starts_httpd();
    test_boot_flow_erase_config_causes_ap();
    test_boot_mode_sta_exists();
    test_boot_mode_ap_exists();
    test_boot_flow_apply_settings_on_sta();
    test_boot_flow_apply_settings_on_ap();
    test_boot_flow_ap_starts_dhcp();
    test_boot_flow_sta_no_dhcp();

    printf("\n%d tests run, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
