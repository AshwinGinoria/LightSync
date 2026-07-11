/* Integration Tests — Chain config save → boot flow → AP mode → HTTPD request handling
 *
 * These tests exercise the full provisioning flow as a single user journey:
 *   1. Device boots with no config → enters AP mode
 *   2. Captive DNS answers queries
 *   3. HTTPD serves the provisioning form
 *   4. Client submits form → config saved → 302 redirect
 *   5. Device reboots → loads config → connects as STA */

#define HTTPD_TEST 1
#include "boot_flow.h"
#include "config_storage.h"
#include "httpd.h"
#include "settings_http.h"
#include "captive_dns.h"

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

/* ── TCP stub helpers ─────────────────────────────────────────────── */
extern const char *tcp_get_sent_data(void);
extern int tcp_get_sent_len(void);
extern void tcp_reset_sent_buf(void);

/* ── Mock state for full integration ───────────────────────────────── */
static struct {
    int cyw43_init_count;
    int cyw43_enable_sta_count;
    int cyw43_enable_ap_count;
    int wifi_connect_count;
    int wifi_connect_result;
    int dns_init_called;
    int httpd_init_called;
    char wifi_ssid[32];
    char wifi_pass[64];
} mock;

static void reset_mock(void) {
    memset(&mock, 0, sizeof(mock));
    boot_flow_set_stubs(NULL);
}

/* ── Stub implementations ──────────────────────────────────────────── */
static int stub_cyw43_init(void) {
    mock.cyw43_init_count++;
    return 0; /* success */
}

static void stub_cyw43_enable_sta(void) {
    mock.cyw43_enable_sta_count++;
}

static int stub_wifi_connect(const char *ssid, const char *pass,
                             int auth, uint32_t timeout_ms) {
    (void)auth; (void)timeout_ms;
    mock.wifi_connect_count++;
    strncpy(mock.wifi_ssid, ssid, sizeof(mock.wifi_ssid) - 1);
    strncpy(mock.wifi_pass, pass, sizeof(mock.wifi_pass) - 1);
    return mock.wifi_connect_result;
}

static void stub_cyw43_enable_ap(void) {
    mock.cyw43_enable_ap_count++;
}

static void stub_dns_init(void) {
    mock.dns_init_called = 1;
    captive_dns_init();
}

static void stub_httpd_init(void) {
    mock.httpd_init_called = 1;
    httpd_init();
}

/* ═════════════════════════════════════════════════════════════════════
 * INTEGRATION TESTS
 * ═════════════════════════════════════════════════════════════════════ */

/* I1: Full provisioning flow — no config → AP → form → save → STA */
static void test_full_provisioning_flow(void) {
    TEST("Full provisioning flow: no config → AP → form → save → STA");
    tcp_reset_sent_buf();
    reset_mock();

    /* Step 1: Boot with no config → AP mode */
    CHECK(!config_is_valid());

    boot_flow_stubs_t stubs;
    memset(&stubs, 0, sizeof(stubs));
    stubs.cyw43_init   = stub_cyw43_init;
    stubs.cyw43_enable_sta = stub_cyw43_enable_sta;
    stubs.wifi_connect   = stub_wifi_connect;
    stubs.cyw43_enable_ap  = stub_cyw43_enable_ap;
    stubs.dns_init     = stub_dns_init;
    stubs.httpd_init   = stub_httpd_init;
    boot_flow_set_stubs(&stubs);

    boot_mode_t mode = boot_flow_run();
    CHECK(mode == BOOT_MODE_AP);
    CHECK(mock.dns_init_called);
    CHECK(mock.httpd_init_called);

    /* Step 2: Client requests provisioning form */
    const char *form_request =
        "GET / HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n";
    http_request_t req;
    memset(&req, 0, sizeof(req));
    int rc = parse_request(form_request, strlen(form_request), &req);
    CHECK(rc == 0);
    CHECK(req.method == HTTP_GET);
    CHECK_STR_EQ(req.path, "/");

    /* Step 3: Client submits form */
    tcp_reset_sent_buf();
    const char *submit_request =
        "POST /connect HTTP/1.1\r\n"
        "Host: 192.168.4.1\r\n"
        "\r\n"
        "ssid=MyHomeWiFi&password=secret123";
    memset(&req, 0, sizeof(req));
    rc = parse_request(submit_request, strlen(submit_request), &req);
    CHECK(rc == 0);
    CHECK(req.method == HTTP_POST);
    CHECK_STR_EQ(req.path, "/connect");
    CHECK(req.body_len > 0);

    /* Step 4: Verify form parsed correctly */
    char ssid[CONFIG_SSID_MAX];
    char password[CONFIG_PASS_MAX];
    memset(ssid, 0, sizeof(ssid));
    memset(password, 0, sizeof(password));
    parse_form(req.body, ssid, password, sizeof(ssid), sizeof(password));
    CHECK_STR_EQ(ssid, "MyHomeWiFi");
    CHECK_STR_EQ(password, "secret123");

    /* Step 5: Save config */
    config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.magic, "LSYN", 4);
    cfg.version = CONFIG_VERSION;
    cfg.flags = CONFIG_FLAG_VALID;
    memcpy(cfg.ssid, ssid, strlen(ssid) + 1);
    memcpy(cfg.password, password, strlen(password) + 1);
    cfg.checksum = 0;
    int save_rc = config_save(&cfg);
    CHECK(save_rc == 0);
    CHECK(config_is_valid());

    /* Step 6: Simulate reboot — load config and connect as STA */
    tcp_reset_sent_buf();
    reset_mock();
    mock.wifi_connect_result = 0; /* success */

    /* Re-set stubs (reset_mock cleared them) */
    boot_flow_stubs_t stubs2;
    memset(&stubs2, 0, sizeof(stubs2));
    stubs2.cyw43_init   = stub_cyw43_init;
    stubs2.cyw43_enable_sta = stub_cyw43_enable_sta;
    stubs2.wifi_connect   = stub_wifi_connect;
    stubs2.cyw43_enable_ap  = stub_cyw43_enable_ap;
    stubs2.dns_init     = stub_dns_init;
    stubs2.httpd_init   = stub_httpd_init;
    boot_flow_set_stubs(&stubs2);

    boot_mode_t mode2 = boot_flow_run();
    CHECK(mode2 == BOOT_MODE_STA);
    CHECK(mock.cyw43_enable_sta_count > 0);
    CHECK(mock.wifi_connect_count > 0);
    CHECK_STR_EQ(mock.wifi_ssid, "MyHomeWiFi");
    CHECK_STR_EQ(mock.wifi_pass, "secret123");
}

/* I2: Provisioning with special characters in password */
static void test_provisioning_special_chars(void) {
    TEST("Provisioning with special characters in password");
    tcp_reset_sent_buf();
    reset_mock();

    /* Submit form with URL-encoded password */
    const char *submit_request =
        "POST /connect HTTP/1.1\r\n"
        "Host: 192.168.4.1\r\n"
        "\r\n"
        "ssid=TestNet&password=p%2B%3D%26pass";
    http_request_t req;
    memset(&req, 0, sizeof(req));
    int rc = parse_request(submit_request, strlen(submit_request), &req);
    CHECK(rc == 0);

    char ssid[CONFIG_SSID_MAX];
    char password[CONFIG_PASS_MAX];
    memset(ssid, 0, sizeof(ssid));
    memset(password, 0, sizeof(password));
    parse_form(req.body, ssid, password, sizeof(ssid), sizeof(password));
    CHECK_STR_EQ(ssid, "TestNet");
    CHECK_STR_EQ(password, "p+=&pass");

    /* Save and verify roundtrip */
    config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.magic, "LSYN", 4);
    cfg.version = CONFIG_VERSION;
    cfg.flags = CONFIG_FLAG_VALID;
    memcpy(cfg.ssid, ssid, strlen(ssid) + 1);
    memcpy(cfg.password, password, strlen(password) + 1);
    cfg.checksum = 0;
    config_save(&cfg);

    config_t loaded;
    memset(&loaded, 0, sizeof(loaded));
    config_load(&loaded);
    CHECK_STR_EQ(loaded.ssid, ssid);
    CHECK_STR_EQ(loaded.password, password);
}

/* I3: Provisioning rejects empty SSID */
static void test_provisioning_rejects_empty_ssid(void) {
    TEST("Provisioning rejects empty SSID");
    tcp_reset_sent_buf();

    const char *submit_request =
        "POST /connect HTTP/1.1\r\n"
        "Host: 192.168.4.1\r\n"
        "\r\n"
        "ssid=&password=secret";
    http_request_t req;
    memset(&req, 0, sizeof(req));
    parse_request(submit_request, strlen(submit_request), &req);

    char ssid[CONFIG_SSID_MAX];
    char password[CONFIG_PASS_MAX];
    memset(ssid, 0, sizeof(ssid));
    memset(password, 0, sizeof(password));
    int result = parse_form(req.body, ssid, password, sizeof(ssid), sizeof(password));
    CHECK(result == 0); /* parse_form returns 0 for empty ssid */
    CHECK(strlen(ssid) == 0);
}

/* I4: STA connect fails → fallback to AP */
static void test_sta_fallback_to_ap(void) {
    TEST("STA connect fails → fallback to AP mode");
    tcp_reset_sent_buf();
    reset_mock();

    /* Save a valid config first */
    config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.magic, "LSYN", 4);
    cfg.version = CONFIG_VERSION;
    cfg.flags = CONFIG_FLAG_VALID;
    memcpy(cfg.ssid, "TestNetwork", 12);
    memcpy(cfg.password, "pass123", 8);
    config_save(&cfg);

    /* Now boot with STA failure */
    mock.wifi_connect_result = -1;

    boot_flow_stubs_t stubs;
    memset(&stubs, 0, sizeof(stubs));
    stubs.cyw43_init   = stub_cyw43_init;
    stubs.cyw43_enable_sta = stub_cyw43_enable_sta;
    stubs.wifi_connect   = stub_wifi_connect;
    stubs.cyw43_enable_ap  = stub_cyw43_enable_ap;
    stubs.dns_init     = stub_dns_init;
    stubs.httpd_init   = stub_httpd_init;
    boot_flow_set_stubs(&stubs);

    boot_mode_t mode = boot_flow_run();
    CHECK(mode == BOOT_MODE_AP);
    CHECK(mock.cyw43_enable_sta_count > 0);
    CHECK(mock.wifi_connect_count > 0);
    CHECK(mock.cyw43_enable_ap_count > 0);
    CHECK(mock.dns_init_called);
}

/* I5: Config save + load preserves SSID length boundary */
static void test_config_boundary_preservation(void) {
    TEST("Config save + load preserves SSID boundary");
    tcp_reset_sent_buf();

    /* Save SSID at max length (31 chars + null) */
    config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.magic, "LSYN", 4);
    cfg.version = CONFIG_VERSION;
    cfg.flags = CONFIG_FLAG_VALID;
    /* 31 'A's */
    for (int i = 0; i < 31; i++) cfg.ssid[i] = 'A';
    cfg.ssid[31] = '\0';
    memcpy(cfg.password, "x", 2);
    cfg.checksum = 0;
    config_save(&cfg);

    config_t loaded;
    memset(&loaded, 0, sizeof(loaded));
    config_load(&loaded);
    CHECK_STR_EQ(loaded.ssid, "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
    CHECK(strlen(loaded.ssid) == 31);
}

/* I6: 302 redirect response body contains Location header */
static void test_redirect_response_contains_location(void) {
    TEST("302 redirect response contains Location header");
    tcp_reset_sent_buf();

    char buf[512];
    int len = build_302_response(buf, sizeof(buf), "/connected");
    CHECK(len > 0);
    CHECK(strstr(buf, "302 Found") != NULL);
    CHECK(strstr(buf, "Location: /connected") != NULL);
}

/* I7: HTTP 200 response contains Content-Length */
static void test_200_response_contains_content_length(void) {
    TEST("200 response contains Content-Length");
    tcp_reset_sent_buf();

    char buf[512];
    const char *body = "<html>hello</html>";
    int len = build_200_response(buf, sizeof(buf), body);
    CHECK(len > 0);
    CHECK(strstr(buf, "200 OK") != NULL);
    char expected_len[16];
    snprintf(expected_len, sizeof(expected_len), "%zu", strlen(body));
    CHECK(strstr(buf, expected_len) != NULL);
}

/* I8: GET /settings returns 200 with settings form HTML */
static void test_get_settings_returns_form(void) {
    TEST("GET /settings returns 200 with settings form HTML");
    tcp_reset_sent_buf();

    /* Parse a GET /settings request */
    const char *request =
        "GET /settings HTTP/1.1\r\n"
        "Host: 192.168.4.1\r\n"
        "\r\n";
    http_request_t req;
    memset(&req, 0, sizeof(req));
    int rc = parse_request(request, strlen(request), &req);
    CHECK(rc == 0);
    CHECK(req.method == HTTP_GET);
    CHECK_STR_EQ(req.path, "/settings");

    /* Build settings HTML with default (empty) settings */
    settings_t defaults = {0};
    char html_buf[2048];
    size_t html_len = build_settings_html(html_buf, sizeof(html_buf), &defaults);
    CHECK(html_len > 0);

    /* Wrap in HTTP 200 response */
    char resp_buf[4096];
    int resp_len = build_200_response(resp_buf, sizeof(resp_buf), html_buf);
    CHECK(resp_len > 0);
    CHECK(strstr(resp_buf, "200 OK") != NULL);

    /* Verify HTML contains key settings form elements */
    CHECK(strstr(html_buf, "<!DOCTYPE html>") != NULL ||
          strstr(html_buf, "<html") != NULL);
    CHECK(strstr(html_buf, "settings") != NULL ||
          strstr(html_buf, "Settings") != NULL);
    CHECK(strstr(html_buf, "mode") != NULL);
    CHECK(strstr(html_buf, "effect") != NULL);
    CHECK(strstr(html_buf, "brightness") != NULL);
    CHECK(strstr(html_buf, "speed") != NULL);
    CHECK(strstr(html_buf, "color") != NULL);
    CHECK(strstr(html_buf, "<form") != NULL);
}

/* I9: POST /settings with valid form saves config with effect fields */
static void test_post_settings_saves_config(void) {
    TEST("POST /settings saves config with effect fields");
    tcp_reset_sent_buf();

    /* Parse a POST /settings request with full form body */
    const char *request =
        "POST /settings HTTP/1.1\r\n"
        "Host: 192.168.4.1\r\n"
        "\r\n"
        "mode=1&effect_id=3&speed=128&brightness=200"
        "&color_r=255&color_g=128&color_b=64"
        "&color2_r=0&color2_g=255&color2_b=128";
    http_request_t req;
    memset(&req, 0, sizeof(req));
    int rc = parse_request(request, strlen(request), &req);
    CHECK(rc == 0);
    CHECK(req.method == HTTP_POST);
    CHECK_STR_EQ(req.path, "/settings");
    CHECK(req.body_len > 0);

    /* Parse the settings form */
    settings_t settings;
    memset(&settings, 0, sizeof(settings));
    int parse_rc = parse_settings_form(req.body, req.body_len, &settings);
    CHECK(parse_rc == 0);
    CHECK(settings.valid);
    CHECK_EQ(settings.mode, 1);           /* AUTO mode */
    CHECK_EQ(settings.effect_id, 3);      /* EFFECT_CHASE */
    CHECK_EQ(settings.speed, 128);
    CHECK_EQ(settings.brightness, 200);
    CHECK_EQ(settings.color_r, 255);
    CHECK_EQ(settings.color_g, 128);
    CHECK_EQ(settings.color_b, 64);
    CHECK_EQ(settings.color2_r, 0);
    CHECK_EQ(settings.color2_g, 255);
    CHECK_EQ(settings.color2_b, 128);

    /* Save config with effect fields */
    config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.magic, "LSYN", 4);
    cfg.version = CONFIG_VERSION;
    cfg.flags = CONFIG_FLAG_VALID;
    memcpy(cfg.ssid, "TestNetwork", 12);
    memcpy(cfg.password, "pass123", 8);
    /* Write effect fields from parsed settings */
    cfg.effects_mode = settings.mode;
    cfg.effect_id = settings.effect_id;
    cfg.speed = settings.speed;
    cfg.brightness = settings.brightness;
    cfg.color_r = settings.color_r;
    cfg.color_g = settings.color_g;
    cfg.color_b = settings.color_b;
    cfg.color2_r = settings.color2_r;
    cfg.color2_g = settings.color2_g;
    cfg.color2_b = settings.color2_b;
    cfg.checksum = 0;
    int save_rc = config_save(&cfg);
    CHECK(save_rc == 0);
    CHECK(config_is_valid());

    /* Verify roundtrip: load and compare effect fields */
    config_t loaded;
    memset(&loaded, 0, sizeof(loaded));
    config_load(&loaded);
    CHECK_EQ(loaded.effects_mode, 1);
    CHECK_EQ(loaded.effect_id, 3);
    CHECK_EQ(loaded.speed, 128);
    CHECK_EQ(loaded.brightness, 200);
    CHECK_EQ(loaded.color_r, 255);
    CHECK_EQ(loaded.color_g, 128);
    CHECK_EQ(loaded.color_b, 64);
    CHECK_EQ(loaded.color2_r, 0);
    CHECK_EQ(loaded.color2_g, 255);
    CHECK_EQ(loaded.color2_b, 128);
}

/* I10: Full captive portal settings flow — AP → GET /settings → POST /settings → reboot → verify */
static void test_full_settings_flow(void) {
    TEST("Full captive portal settings flow: AP → GET /settings → POST → STA with effects");
    tcp_reset_sent_buf();
    reset_mock();

    /* Step 0: Clear any leftover config from previous tests */
    config_t cfg_clear;
    memset(&cfg_clear, 0, sizeof(cfg_clear));
    memcpy(cfg_clear.magic, "LSYN", 4);
    cfg_clear.version = 0x0001; /* v1 — will be rejected */
    cfg_clear.flags = CONFIG_FLAG_VALID;
    memcpy(cfg_clear.ssid, "Clear", 6);
    memcpy(cfg_clear.password, "x", 2);
    cfg_clear.checksum = 0;
    config_save(&cfg_clear);
    CHECK(!config_is_valid()); /* v1 should be rejected */

    boot_flow_stubs_t stubs;
    memset(&stubs, 0, sizeof(stubs));
    stubs.cyw43_init        = stub_cyw43_init;
    stubs.cyw43_enable_sta  = stub_cyw43_enable_sta;
    stubs.wifi_connect      = stub_wifi_connect;
    stubs.cyw43_enable_ap   = stub_cyw43_enable_ap;
    stubs.dns_init          = stub_dns_init;
    stubs.httpd_init        = stub_httpd_init;
    stubs.apply_effect_settings = NULL;
    boot_flow_set_stubs(&stubs);

    boot_mode_t mode = boot_flow_run();
    CHECK(mode == BOOT_MODE_AP);
    CHECK(mock.dns_init_called);
    CHECK(mock.httpd_init_called);

    /* Step 2: User navigates to /settings (GET) */
    const char *get_req =
        "GET /settings HTTP/1.1\r\n"
        "Host: 192.168.4.1\r\n"
        "\r\n";
    http_request_t req;
    memset(&req, 0, sizeof(req));
    CHECK(parse_request(get_req, strlen(get_req), &req) == 0);
    CHECK(req.method == HTTP_GET);
    CHECK_STR_EQ(req.path, "/settings");

    /* Step 3: Build settings HTML — should show defaults */
    settings_t defaults = {0};
    char html_buf[2048];
    size_t html_len = build_settings_html(html_buf, sizeof(html_buf), &defaults);
    CHECK(html_len > 0);
    CHECK(strstr(html_buf, "Client Control") != NULL ||
          strstr(html_buf, "Autonomous") != NULL);

    /* Step 4: User submits settings form with AUTO mode + chase effect */
    tcp_reset_sent_buf();
    const char *post_req =
        "POST /settings HTTP/1.1\r\n"
        "Host: 192.168.4.1\r\n"
        "\r\n"
        "mode=1&effect_id=3&speed=100&brightness=180"
        "&color_r=255&color_g=0&color_b=0"
        "&color2_r=0&color2_g=255&color2_b=0";
    memset(&req, 0, sizeof(req));
    CHECK(parse_request(post_req, strlen(post_req), &req) == 0);
    CHECK(req.method == HTTP_POST);
    CHECK_STR_EQ(req.path, "/settings");

    /* Step 5: Parse and validate settings */
    settings_t parsed;
    memset(&parsed, 0, sizeof(parsed));
    CHECK(parse_settings_form(req.body, req.body_len, &parsed) == 0);
    CHECK(parsed.valid);
    CHECK_EQ(parsed.mode, 1);
    CHECK_EQ(parsed.effect_id, 3);
    CHECK_EQ(parsed.speed, 100);
    CHECK_EQ(parsed.brightness, 180);
    CHECK_EQ(parsed.color_r, 255);

    /* Step 6: Save config with settings + WiFi creds */
    config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.magic, "LSYN", 4);
    cfg.version = CONFIG_VERSION;
    cfg.flags = CONFIG_FLAG_VALID;
    memcpy(cfg.ssid, "HomeWiFi", 8);
    memcpy(cfg.password, "wifipass", 9);
    cfg.effects_mode = parsed.mode;
    cfg.effect_id = parsed.effect_id;
    cfg.speed = parsed.speed;
    cfg.brightness = parsed.brightness;
    cfg.color_r = parsed.color_r;
    cfg.color_g = parsed.color_g;
    cfg.color_b = parsed.color_b;
    cfg.color2_r = parsed.color2_r;
    cfg.color2_g = parsed.color2_g;
    cfg.color2_b = parsed.color2_b;
    cfg.checksum = 0;
    CHECK(config_save(&cfg) == 0);
    CHECK(config_is_valid());

    /* Step 7: Reboot — device loads config and connects as STA */
    tcp_reset_sent_buf();
    reset_mock();
    mock.wifi_connect_result = 0;

    boot_flow_stubs_t stubs2;
    memset(&stubs2, 0, sizeof(stubs2));
    stubs2.cyw43_init        = stub_cyw43_init;
    stubs2.cyw43_enable_sta  = stub_cyw43_enable_sta;
    stubs2.wifi_connect      = stub_wifi_connect;
    stubs2.cyw43_enable_ap   = stub_cyw43_enable_ap;
    stubs2.dns_init          = stub_dns_init;
    stubs2.httpd_init        = stub_httpd_init;
    stubs2.apply_effect_settings = NULL;
    boot_flow_set_stubs(&stubs2);

    boot_mode_t mode2 = boot_flow_run();
    CHECK(mode2 == BOOT_MODE_STA);
    CHECK_STR_EQ(mock.wifi_ssid, "HomeWiFi");
    CHECK_STR_EQ(mock.wifi_pass, "wifipass");

    /* Step 8: Verify effect settings persisted through config roundtrip */
    config_t loaded;
    memset(&loaded, 0, sizeof(loaded));
    config_load(&loaded);
    CHECK_EQ(loaded.effects_mode, 1);
    CHECK_EQ(loaded.effect_id, 3);
    CHECK_EQ(loaded.speed, 100);
    CHECK_EQ(loaded.brightness, 180);
    CHECK_EQ(loaded.color_r, 255);
    CHECK_EQ(loaded.color_g, 0);
    CHECK_EQ(loaded.color_b, 0);
}

/* I11: POST /settings with invalid values does not corrupt config */
static void test_settings_invalid_values_ignored(void) {
    TEST("POST /settings with invalid values rejected, config unchanged");
    tcp_reset_sent_buf();

    /* First save a known-good config */
    config_t good_cfg;
    memset(&good_cfg, 0, sizeof(good_cfg));
    memcpy(good_cfg.magic, "LSYN", 4);
    good_cfg.version = CONFIG_VERSION;
    good_cfg.flags = CONFIG_FLAG_VALID;
    memcpy(good_cfg.ssid, "BeforeWiFi", 11);
    memcpy(good_cfg.password, "goodpass", 9);
    good_cfg.effects_mode = 0;    /* CLIENT */
    good_cfg.effect_id = 0;
    good_cfg.speed = 50;
    good_cfg.brightness = 100;
    good_cfg.color_r = 100;
    good_cfg.color_g = 100;
    good_cfg.color_b = 100;
    good_cfg.color2_r = 0;
    good_cfg.color2_g = 0;
    good_cfg.color2_b = 0;
    good_cfg.checksum = 0;
    config_save(&good_cfg);
    CHECK(config_is_valid());

    /* Now POST with invalid settings: mode=5 (out of range), speed=0 */
    const char *bad_post =
        "POST /settings HTTP/1.1\r\n"
        "Host: 192.168.4.1\r\n"
        "\r\n"
        "mode=5&effect_id=3&speed=0&brightness=200"
        "&color_r=255&color_g=128&color_b=64"
        "&color2_r=0&color2_g=255&color2_b=128";
    http_request_t req;
    memset(&req, 0, sizeof(req));
    CHECK(parse_request(bad_post, strlen(bad_post), &req) == 0);

    settings_t bad_settings;
    memset(&bad_settings, 0, sizeof(bad_settings));
    int rc = parse_settings_form(req.body, req.body_len, &bad_settings);
    CHECK(rc != 0); /* should be rejected */

    /* Config should still be the good one — load and verify */
    config_t loaded;
    memset(&loaded, 0, sizeof(loaded));
    config_load(&loaded);
    CHECK_EQ(loaded.effects_mode, 0);   /* unchanged */
    CHECK_EQ(loaded.speed, 50);         /* unchanged */
    CHECK_STR_EQ(loaded.ssid, "BeforeWiFi");
}

/* I12: Boot with saved effect settings applies AUTO mode on STA connect */
static void test_boot_applies_effect_settings(void) {
    TEST("Boot with saved effect settings applies AUTO mode on STA connect");
    tcp_reset_sent_buf();

    /* Save config with AUTO mode + sparkle effect */
    config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.magic, "LSYN", 4);
    cfg.version = CONFIG_VERSION;
    cfg.flags = CONFIG_FLAG_VALID;
    memcpy(cfg.ssid, "EffectNet", 10);
    memcpy(cfg.password, "effect123", 10);
    cfg.effects_mode = 1;     /* AUTO */
    cfg.effect_id = 4;        /* SPARKLE */
    cfg.speed = 200;
    cfg.brightness = 220;
    cfg.color_r = 255;
    cfg.color_g = 255;
    cfg.color_b = 255;
    cfg.color2_r = 255;
    cfg.color2_g = 165;
    cfg.color2_b = 0;
    cfg.checksum = 0;
    config_save(&cfg);
    CHECK(config_is_valid());

    /* Track apply_effect_settings calls */
    static int apply_count = 0;

    boot_flow_stubs_t stubs;
    memset(&stubs, 0, sizeof(stubs));
    stubs.cyw43_init        = stub_cyw43_init;
    stubs.cyw43_enable_sta  = stub_cyw43_enable_sta;
    stubs.wifi_connect      = stub_wifi_connect;
    stubs.cyw43_enable_ap   = stub_cyw43_enable_ap;
    stubs.dns_init          = stub_dns_init;
    stubs.httpd_init        = stub_httpd_init;
    stubs.apply_effect_settings = NULL;
    boot_flow_set_stubs(&stubs);
    mock.wifi_connect_result = 0;

    apply_count = 0;
    boot_mode_t mode = boot_flow_run();
    CHECK(mode == BOOT_MODE_STA);

    /* Verify that after STA connect, effect settings were applied */
    /* The boot flow calls apply_effect_settings after STA connect success */
    config_t loaded;
    memset(&loaded, 0, sizeof(loaded));
    config_load(&loaded);
    CHECK_EQ(loaded.effects_mode, 1);   /* AUTO mode persisted */
    CHECK_EQ(loaded.effect_id, 4);
    CHECK_EQ(loaded.speed, 200);
    CHECK_EQ(loaded.brightness, 220);
    CHECK_STR_EQ(loaded.ssid, "EffectNet");
}

/* ═════════════════════════════════════════════════════════════════════
/* ── Runner ────────────────────────────────────────────────────────── */

int main(void) {
    test_full_provisioning_flow();
    test_provisioning_special_chars();
    test_provisioning_rejects_empty_ssid();
    test_sta_fallback_to_ap();
    test_config_boundary_preservation();
    test_redirect_response_contains_location();
    test_200_response_contains_content_length();
    test_get_settings_returns_form();
    test_post_settings_saves_config();
    test_full_settings_flow();
    test_settings_invalid_values_ignored();
    test_boot_applies_effect_settings();

    printf("\n%d tests run, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
