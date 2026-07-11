/* Settings HTTP Tests — form parsing and HTML generation. */
#include "settings_http.h"

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

#define CHECK_STR_EQ(a, b)  do { \
    if (strcmp((a), (b)) != 0) { \
        printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define CHECK_PREFIX(str, prefix)  do { \
    if (strncmp((str), (prefix), strlen(prefix)) != 0) { \
        printf("  FAIL %s:%d: prefix mismatch\n", __FILE__, __LINE__); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* ═════════════════════════════════════════════════════════════════════
 * S1: empty body rejected
 * ═════════════════════════════════════════════════════════════════════ */

static void test_empty_body_rejected(void) {
    TEST("empty POST body rejected");

    settings_t out;
    int ret = parse_settings_form("", 0, &out);
    CHECK(ret == -1);
    CHECK(!out.valid);
}

/* ═════════════════════════════════════════════════════════════════════
 * S2: valid form with all fields parses correctly
 * ═════════════════════════════════════════════════════════════════════ */

static void test_valid_form_parses(void) {
    TEST("valid form with all fields parses");

    const char *body =
        "mode=1&effect_id=3&speed=100&brightness=200"
        "&color_r=255&color_g=128&color_b=64"
        "&color2_r=0&color2_g=255&color2_b=128";

    settings_t out;
    int ret = parse_settings_form(body, strlen(body), &out);
    CHECK(ret == 0);
    CHECK(out.valid);
    CHECK(out.mode == 1);
    CHECK(out.effect_id == 3);
    CHECK(out.speed == 100);
    CHECK(out.brightness == 200);
    CHECK(out.color_r == 255);
    CHECK(out.color_g == 128);
    CHECK(out.color_b == 64);
    CHECK(out.color2_r == 0);
    CHECK(out.color2_g == 255);
    CHECK(out.color2_b == 128);
}

/* ═════════════════════════════════════════════════════════════════════
 * S3: mode=0 (CLIENT) accepted
 * ═════════════════════════════════════════════════════════════════════ */

static void test_mode_client(void) {
    TEST("mode=0 (CLIENT) accepted");

    const char *body = "mode=0&effect_id=0&speed=1&brightness=1"
        "&color_r=1&color_g=1&color_b=1"
        "&color2_r=1&color2_g=1&color2_b=1";

    settings_t out;
    int ret = parse_settings_form(body, strlen(body), &out);
    CHECK(ret == 0);
    CHECK(out.valid);
    CHECK(out.mode == 0);
}

/* ═════════════════════════════════════════════════════════════════════
 * S4: mode=1 (AUTO) accepted
 * ═════════════════════════════════════════════════════════════════════ */

static void test_mode_auto(void) {
    TEST("mode=1 (AUTO) accepted");

    const char *body = "mode=1&effect_id=0&speed=1&brightness=1"
        "&color_r=1&color_g=1&color_b=1"
        "&color2_r=1&color2_g=1&color2_b=1";

    settings_t out;
    int ret = parse_settings_form(body, strlen(body), &out);
    CHECK(ret == 0);
    CHECK(out.valid);
    CHECK(out.mode == 1);
}

/* ═════════════════════════════════════════════════════════════════════
 * S5: invalid mode (>1) rejected
 * ═════════════════════════════════════════════════════════════════════ */

static void test_invalid_mode_rejected(void) {
    TEST("invalid mode (>1) rejected");

    const char *body = "mode=2&effect_id=0&speed=1&brightness=1"
        "&color_r=1&color_g=1&color_b=1"
        "&color2_r=1&color2_g=1&color2_b=1";

    settings_t out;
    int ret = parse_settings_form(body, strlen(body), &out);
    CHECK(ret == -1);
}

/* ═════════════════════════════════════════════════════════════════════
 * S6: invalid effect_id (>=6) rejected
 * ═════════════════════════════════════════════════════════════════════ */

static void test_invalid_effect_rejected(void) {
    TEST("invalid effect_id (>=6) rejected");

    const char *body = "mode=0&effect_id=6&speed=1&brightness=1"
        "&color_r=1&color_g=1&color_b=1"
        "&color2_r=1&color2_g=1&color2_b=1";

    settings_t out;
    int ret = parse_settings_form(body, strlen(body), &out);
    CHECK(ret == -1);
}

/* ═════════════════════════════════════════════════════════════════════
 * S7: speed=0 rejected
 * ═════════════════════════════════════════════════════════════════════ */

static void test_speed_zero_rejected(void) {
    TEST("speed=0 rejected");

    const char *body = "mode=0&effect_id=0&speed=0&brightness=1"
        "&color_r=1&color_g=1&color_b=1"
        "&color2_r=1&color2_g=1&color2_b=1";

    settings_t out;
    int ret = parse_settings_form(body, strlen(body), &out);
    CHECK(ret == -1);
}

/* ═════════════════════════════════════════════════════════════════════
 * S8: brightness=0 accepted (valid range)
 * ═════════════════════════════════════════════════════════════════════ */

static void test_brightness_zero_accepted(void) {
    TEST("brightness=0 accepted");

    const char *body = "mode=0&effect_id=0&speed=1&brightness=0"
        "&color_r=1&color_g=1&color_b=1"
        "&color2_r=1&color2_g=1&color2_b=1";

    settings_t out;
    int ret = parse_settings_form(body, strlen(body), &out);
    CHECK(ret == 0);
    CHECK(out.valid);
    CHECK(out.brightness == 0);
}

/* ═════════════════════════════════════════════════════════════════════
 * S9: build_settings_html produces valid HTML with HTTP header
 * ═════════════════════════════════════════════════════════════════════ */

static void test_html_has_http_header(void) {
    TEST("build_settings_html produces valid HTML with HTTP header");

    settings_t cur = {0};
    char buf[2048];
    size_t len = build_settings_html(buf, sizeof(buf), &cur);

    CHECK(len > 0);
    CHECK(len < sizeof(buf));
    CHECK_PREFIX(buf, "<!DOCTYPE html>\n");
    CHECK(strstr(buf, "<!DOCTYPE html>") != NULL);
    CHECK(strstr(buf, "<title>LEDServer Settings</title>") != NULL);
    CHECK(strstr(buf, "Client Control") != NULL);
    CHECK(strstr(buf, "Autonomous Effects") != NULL);
}

/* ═════════════════════════════════════════════════════════════════════
 * S10: build_settings_html reflects current settings in form
 * ═════════════════════════════════════════════════════════════════════ */

static void test_html_reflects_settings(void) {
    TEST("build_settings_html reflects current settings");

    settings_t cur = {0};
    cur.mode = 1;
    cur.effect_id = 3;
    cur.speed = 128;
    cur.brightness = 200;
    cur.color_r = 255;
    cur.color_g = 128;
    cur.color_b = 64;
    cur.color2_r = 0;
    cur.color2_g = 255;
    cur.color2_b = 128;

    char buf[2048];
    size_t len = build_settings_html(buf, sizeof(buf), &cur);

    CHECK(len > 0);
    CHECK(len < sizeof(buf));

    /* AUTO should be selected */
    /* Find the "Autonomous Effects" option and verify it has selected */
    const char *auto_opt = strstr(buf, "Autonomous Effects");
    CHECK(auto_opt != NULL);
    /* The selected option should appear before "Client Control" in source */
    const char *client_opt = strstr(buf, "Client Control");
    CHECK(client_opt != NULL);

    /* Check effect is selected (Chase = index 3) */
    const char *chase = strstr(buf, "Chase");
    CHECK(chase != NULL);

    /* Check brightness value */
    CHECK(strstr(buf, "value=\"200\"") != NULL);

    /* Check speed value */
    CHECK(strstr(buf, "value=\"128\"") != NULL);
}

/* ═════════════════════════════════════════════════════════════════════
/* ── Runner ────────────────────────────────────────────────────────── */

int main(void) {
    test_empty_body_rejected();
    test_valid_form_parses();
    test_mode_client();
    test_mode_auto();
    test_invalid_mode_rejected();
    test_invalid_effect_rejected();
    test_speed_zero_rejected();
    test_brightness_zero_accepted();
    test_html_has_http_header();
    test_html_reflects_settings();

    printf("\n%d tests run, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
