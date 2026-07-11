/* HTTP Provisioning Server Tests — Phase 3: Captive Portal Form */
#include "provisioning_http.h"
#include "lwip/udp.h"

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

/* ── Fake pbuf storage ─────────────────────────────────────────────── */
static uint8_t fake_buf[1024];

static void init_fake_pbuf(struct pbuf *p, const uint8_t *data, uint16_t len) {
    memset(fake_buf, 0, sizeof(fake_buf));
    memcpy(fake_buf, data, len);
    p->next = NULL;
    p->payload = fake_buf;
    p->tot_len = len;
    p->len = len;
}

/* ── Stub: udp_new/udp_bind/udp_recv/udp_sendto/udp_remove ─────────── */
static struct udp_pcb new_pcb_instance;
static struct udp_pcb *fake_pcb = NULL;
static u16_t bind_port = 0;
static int sendto_called = 0;
static struct pbuf *captured_send_pbuf = NULL;
static void (*saved_recv_cb)(void *, struct udp_pcb *, struct pbuf *,
                             const ip_addr_t *, u16_t) = NULL;
static void *saved_recv_arg = NULL;

struct udp_pcb *udp_new(void) {
    memset(&new_pcb_instance, 0, sizeof(new_pcb_instance));
    fake_pcb = &new_pcb_instance;
    return fake_pcb;
}

err_t udp_bind(struct udp_pcb *pcb, const ip_addr_t *addr, u16_t port) {
    (void)pcb; (void)addr;
    bind_port = port;
    return ERR_OK;
}

void udp_recv(struct udp_pcb *pcb,
              void (*recv)(void *, struct udp_pcb *, struct pbuf *,
                           const ip_addr_t *, u16_t),
              void *arg) {
    (void)pcb;
    saved_recv_cb = recv;
    saved_recv_arg = arg;
}

err_t udp_sendto(struct udp_pcb *pcb, struct pbuf *p,
                 const ip_addr_t *addr, u16_t port) {
    (void)pcb; (void)addr; (void)port;
    captured_send_pbuf = p;
    sendto_called = 1;
    return ERR_OK;
}

void udp_remove(struct udp_pcb *pcb) {
    (void)pcb;
}

/* ═════════════════════════════════════════════════════════════════════
 * FORM PARSING TESTS (unit-testable without full HTTP stack)
 * ═════════════════════════════════════════════════════════════════════ */

/* P1: parse_form extracts ssid from POST body */
static void test_parse_form_extracted_ssid(void) {
    TEST("parse_form extracts ssid from POST body");
    char ssid[32], pass[64];
    memset(ssid, 0, sizeof(ssid));
    memset(pass, 0, sizeof(pass));

    const char body[] = "ssid=MyWiFi&password=secret123";
    int ok = parse_form(body, ssid, pass, sizeof(ssid), sizeof(pass));

    CHECK(ok == 1);
    CHECK_STR_EQ(ssid, "MyWiFi");
    CHECK_STR_EQ(pass, "secret123");
}

/* P2: parse_form extracts password from POST body */
static void test_parse_form_extracted_password(void) {
    TEST("parse_form extracts password from POST body");
    char ssid[32], pass[64];
    memset(ssid, 0, sizeof(ssid));
    memset(pass, 0, sizeof(pass));

    const char body[] = "ssid=MyWiFi&password=secret123";
    int ok = parse_form(body, ssid, pass, sizeof(ssid), sizeof(pass));

    CHECK(ok == 1);
    CHECK_STR_EQ(pass, "secret123");
}

/* P3: parse_form rejects empty ssid */
static void test_parse_form_empty_ssid(void) {
    TEST("parse_form rejects empty ssid");
    char ssid[32], pass[64];
    memset(ssid, 0, sizeof(ssid));
    memset(pass, 0, sizeof(pass));

    const char body[] = "ssid=&password=secret123";
    int ok = parse_form(body, ssid, pass, sizeof(ssid), sizeof(pass));

    CHECK(ok == 0);
}

/* P4: parse_form truncates ssid to buffer size */
static void test_parse_form_truncates_ssid(void) {
    TEST("parse_form truncates ssid to buffer size");
    char ssid[8], pass[64];
    memset(ssid, 0, sizeof(ssid));
    memset(pass, 0, sizeof(pass));

    const char body[] = "ssid=VeryLongSSIDName&password=secret";
    int ok = parse_form(body, ssid, pass, sizeof(ssid), sizeof(pass));

    CHECK(ok == 1);
    CHECK(strlen(ssid) < sizeof(ssid));
    CHECK_STR_EQ(ssid, "VeryLon"); /* truncated to 7 chars + null */
}

/* P5: parse_form truncates password to buffer size */
static void test_parse_form_truncates_password(void) {
    TEST("parse_form truncates password to buffer size");
    char ssid[32], pass[8];
    memset(ssid, 0, sizeof(ssid));
    memset(pass, 0, sizeof(pass));

    const char body[] = "ssid=home&password=VeryLongPasswordHere";
    int ok = parse_form(body, ssid, pass, sizeof(ssid), sizeof(pass));

    CHECK(ok == 1);
    CHECK(strlen(pass) < sizeof(pass));
    CHECK_STR_EQ(pass, "VeryLon"); /* truncated to 7 chars + null */
}

/* P6: parse_form handles password with = and + characters */
static void test_parse_form_special_chars(void) {
    TEST("parse_form handles special chars in password");
    char ssid[32], pass[64];
    memset(ssid, 0, sizeof(ssid));
    memset(pass, 0, sizeof(pass));

    const char body[] = "ssid=home&password=p%2B%3Dpass";
    int ok = parse_form(body, ssid, pass, sizeof(ssid), sizeof(pass));

    CHECK(ok == 1);
    CHECK_STR_EQ(ssid, "home");
    CHECK_STR_EQ(pass, "p+=pass");
}

/* P7: parse_form ignores extra params */
static void test_parse_form_ignores_extra_params(void) {
    TEST("parse_form ignores extra params");
    char ssid[32], pass[64];
    memset(ssid, 0, sizeof(ssid));
    memset(pass, 0, sizeof(pass));

    const char body[] = "ssid=home&password=secret&extra=value";
    int ok = parse_form(body, ssid, pass, sizeof(ssid), sizeof(pass));

    CHECK(ok == 1);
    CHECK_STR_EQ(ssid, "home");
    CHECK_STR_EQ(pass, "secret");
}

/* ═════════════════════════════════════════════════════════════════════
 * HTTP RESPONSE BUILDER TESTS
 * ═════════════════════════════════════════════════════════════════════ */

/* P8: build_response_200 creates valid HTTP 200 response */
static void test_build_response_200(void) {
    TEST("build_response_200 creates valid HTTP response");
    char resp[1024];
    int len = build_response_200(resp, sizeof(resp), "Hello", 5);

    CHECK(len > 0);
    CHECK(len < (int)sizeof(resp));
    CHECK(strncmp(resp, "HTTP/1.1 200 OK", 15) == 0);
    CHECK(strstr(resp, "Content-Length") != NULL);
    CHECK(strstr(resp, "\r\n\r\n") != NULL);
}

/* P9: build_response_302 creates redirect */
static void test_build_response_302(void) {
    TEST("build_response_302 creates redirect");
    char resp[1024];
    int len = build_response_302(resp, sizeof(resp), "http://192.168.4.1/");

    CHECK(len > 0);
    CHECK(strncmp(resp, "HTTP/1.1 302 Found", 18) == 0);
    CHECK(strstr(resp, "Location: http://192.168.4.1/") != NULL);
}

/* P10: build_provisioning_html contains form */
static void test_build_provisioning_html(void) {
    TEST("build_provisioning_html contains form");
    char html[2048];
    int len = build_provisioning_html(html, sizeof(html));

    CHECK(len > 0);
    CHECK(strstr(html, "<form") != NULL);
    CHECK(strstr(html, "name=\"ssid\"") != NULL);
    CHECK(strstr(html, "name=\"password\"") != NULL);
    CHECK(strstr(html, "type=\"password\"") != NULL);
    CHECK(strstr(html, "action=\"/connect\"") != NULL);
}

/* ═════════════════════════════════════════════════════════════════════
/* ── Runner ────────────────────────────────────────────────────────── */

int main(void) {
    test_parse_form_extracted_ssid();
    test_parse_form_extracted_password();
    test_parse_form_empty_ssid();
    test_parse_form_truncates_ssid();
    test_parse_form_truncates_password();
    test_parse_form_special_chars();
    test_parse_form_ignores_extra_params();
    test_build_response_200();
    test_build_response_302();
    test_build_provisioning_html();

    printf("\n%d tests run, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
