/* Captive DNS Server Tests — Phase 2: DNS for Captive Portal */
#include "captive_dns.h"
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

/* ── Fake pbuf storage — real memory, not a fake pointer ───────────── */
static uint8_t fake_buf[512];
static uint8_t resp_buf_storage[512];
static struct {
    struct pbuf base;
    uint8_t payload[512];
} dns_pbuf_storage;

static void init_fake_pbuf(struct pbuf *p, const uint8_t *data, uint16_t len) {
    memset(fake_buf, 0, sizeof(fake_buf));
    memcpy(fake_buf, data, len);
    p->next = NULL;
    p->payload = fake_buf;
    p->tot_len = len;
    p->len = len;
}

/* ── Stub: udp_sendto — captures the response ─────────────────────── */
static struct pbuf *captured_send_pbuf = NULL;
static int sendto_called = 0;

static struct udp_pcb new_pcb;

struct udp_pcb *fake_pcb = NULL;

err_t udp_sendto(struct udp_pcb *pcb, struct pbuf *p,
                 const ip_addr_t *addr, u16_t port) {
    (void)pcb; (void)addr; (void)port;
    captured_send_pbuf = p;
    sendto_called = 1;
    return ERR_OK;
}

/* ── Stub: udp_new/udp_bind/udp_remove ─────────────────────────────── */
static u16_t bind_port = 0;

static struct udp_pcb new_pcb_instance;

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

void udp_remove(struct udp_pcb *pcb) {
    (void)pcb;
}

/* ── Stub: udp_recv — stores callback for later invocation ─────────── */
static void (*saved_recv_cb)(void *, struct udp_pcb *, struct pbuf *,
                             const ip_addr_t *, u16_t) = NULL;
static void *saved_recv_arg = NULL;

void udp_recv(struct udp_pcb *pcb,
              void (*recv)(void *, struct udp_pcb *, struct pbuf *,
                           const ip_addr_t *, u16_t),
              void *arg) {
    (void)pcb;
    saved_recv_cb = recv;
    saved_recv_arg = arg;
}

/* ── DNS packet helpers ────────────────────────────────────────────── */

/* Build a minimal DNS query for "example.com" */
static void build_dns_query(uint8_t *buf) {
    /* Header: ID=0x1234, flags=0x0100 (standard query), QD=1 */
    buf[0] = 0x12; buf[1] = 0x34; /* ID */
    buf[2] = 0x01; buf[3] = 0x00; /* flags */
    buf[4] = 0x00; buf[5] = 0x01; /* QDCount */
    buf[6] = 0x00; buf[7] = 0x00; /* ANCount */
    buf[8] = 0x00; buf[9] = 0x00; /* NSCount */
    buf[10] = 0x00; buf[11] = 0x00; /* ARCount */

    /* Question: "example.com" */
    buf[12] = 7; memcpy(buf+13, "example", 7);
    buf[20] = 3; memcpy(buf+21, "com", 3);
    buf[24] = 0; /* null terminator */

    /* QType = A (1), QClass = IN (1) */
    buf[25] = 0; buf[26] = 1;
    buf[27] = 0; buf[28] = 1;
}

/* ═════════════════════════════════════════════════════════════════════
 * CAPTIVE DNS TESTS
 * ═════════════════════════════════════════════════════════════════════ */

/* D1: init creates UDP listener on port 53 */
static void test_dns_init_creates_listener(void) {
    TEST("DNS init creates listener on port 53");
    sendto_called = 0;
    bind_port = 0;
    fake_pcb = NULL;
    captive_dns_init();
    CHECK(fake_pcb != NULL);
    CHECK_EQ(bind_port, 53);
}

/* D2: responds to valid DNS query */
static void test_dns_responds_to_query(void) {
    TEST("DNS responds to valid query");
    sendto_called = 0;
    captive_dns_init();

    uint8_t query[512];
    build_dns_query(query);
    struct pbuf p;
    init_fake_pbuf(&p, query, 29);

    ip_addr_t src;
    IP4_ADDR(&src, 192, 168, 4, 100);
    captive_dns_recv_cb(NULL, fake_pcb, &p, &src, 53);

    CHECK(sendto_called);
}

/* D3: response echoes query ID */
static void test_dns_response_echoes_id(void) {
    TEST("DNS response echoes query ID");
    sendto_called = 0;
    captive_dns_init();

    uint8_t query[512];
    build_dns_query(query);
    query[0] = 0xAB; query[1] = 0xCD; /* custom ID */
    struct pbuf p;
    init_fake_pbuf(&p, query, 29);

    ip_addr_t src;
    IP4_ADDR(&src, 192, 168, 4, 100);
    captive_dns_recv_cb(NULL, fake_pcb, &p, &src, 53);

    CHECK(sendto_called);
    CHECK_EQ(((uint8_t *)captured_send_pbuf->payload)[0], 0xAB);
    CHECK_EQ(((uint8_t *)captured_send_pbuf->payload)[1], 0xCD);
}

/* D4: response flags correct (QR=1, AA=1, RA=1, RCODE=0) */
static void test_dns_flags_correct(void) {
    TEST("DNS response flags correct");
    sendto_called = 0;
    captive_dns_init();

    uint8_t query[512];
    build_dns_query(query);
    struct pbuf p;
    init_fake_pbuf(&p, query, 29);

    ip_addr_t src;
    IP4_ADDR(&src, 192, 168, 4, 100);
    captive_dns_recv_cb(NULL, fake_pcb, &p, &src, 53);

    CHECK(sendto_called);
    CHECK_EQ(((uint8_t *)captured_send_pbuf->payload)[2], 0x81);
    CHECK_EQ(((uint8_t *)captured_send_pbuf->payload)[3], 0x80);
}

/* D5: QDCOUNT echoed as 1 */
static void test_dns_qdcount_echoed(void) {
    TEST("DNS QDCOUNT echoed");
    sendto_called = 0;
    captive_dns_init();

    uint8_t query[512];
    build_dns_query(query);
    struct pbuf p;
    init_fake_pbuf(&p, query, 29);

    ip_addr_t src;
    IP4_ADDR(&src, 192, 168, 4, 100);
    captive_dns_recv_cb(NULL, fake_pcb, &p, &src, 53);

    CHECK_EQ(((uint8_t *)captured_send_pbuf->payload)[4], 0x00);
    CHECK_EQ(((uint8_t *)captured_send_pbuf->payload)[5], 0x01);
}

/* D6: ANCOUNT = 1 */
static void test_dns_ancount_one(void) {
    TEST("DNS ANCOUNT = 1");
    sendto_called = 0;
    captive_dns_init();

    uint8_t query[512];
    build_dns_query(query);
    struct pbuf p;
    init_fake_pbuf(&p, query, 29);

    ip_addr_t src;
    IP4_ADDR(&src, 192, 168, 4, 100);
    captive_dns_recv_cb(NULL, fake_pcb, &p, &src, 53);

    CHECK_EQ(((uint8_t *)captured_send_pbuf->payload)[6], 0x00);
    CHECK_EQ(((uint8_t *)captured_send_pbuf->payload)[7], 0x01);
}

/* D7: answer contains 192.168.4.1 */
static void test_dns_answer_ip(void) {
    TEST("DNS answer IP = 192.168.4.1");
    sendto_called = 0;
    captive_dns_init();

    uint8_t query[512];
    build_dns_query(query);
    struct pbuf p;
    init_fake_pbuf(&p, query, 29);

    ip_addr_t src;
    IP4_ADDR(&src, 192, 168, 4, 100);
    captive_dns_recv_cb(NULL, fake_pcb, &p, &src, 53);

    uint8_t *resp = (uint8_t *)captured_send_pbuf->payload;

    /* Answer starts after question section (29 bytes) */
    /* NAME: compression pointer 0xC00C */
    CHECK_EQ(resp[29], 0xC0);
    CHECK_EQ(resp[30], 0x0C);

    /* TYPE = A (0x0001), CLASS = IN (0x0001) */
    CHECK_EQ(resp[31], 0x00);
    CHECK_EQ(resp[32], 0x01);
    CHECK_EQ(resp[33], 0x00);
    CHECK_EQ(resp[34], 0x01);

    /* TTL (4 bytes) + RDLEN (1 byte) + RDATA (4 bytes) */
    CHECK_EQ(resp[40], 4);
    CHECK_EQ(resp[41], 192);
    CHECK_EQ(resp[42], 168);
    CHECK_EQ(resp[43], 4);
    CHECK_EQ(resp[44], 1);
}

/* D8: TYPE=A, CLASS=IN */
static void test_dns_answer_type_class(void) {
    TEST("DNS answer TYPE=A, CLASS=IN");
    sendto_called = 0;
    captive_dns_init();

    uint8_t query[512];
    build_dns_query(query);
    struct pbuf p;
    init_fake_pbuf(&p, query, 29);

    ip_addr_t src;
    IP4_ADDR(&src, 192, 168, 4, 100);
    captive_dns_recv_cb(NULL, fake_pcb, &p, &src, 53);

    uint8_t *resp = (uint8_t *)captured_send_pbuf->payload;
    CHECK_EQ(resp[31], 0x00);
    CHECK_EQ(resp[32], 0x01);
    CHECK_EQ(resp[33], 0x00);
    CHECK_EQ(resp[34], 0x01);
}

/* D9: short queries ignored */
static void test_dns_short_query_ignored(void) {
    TEST("DNS short query ignored");
    sendto_called = 0;
    captive_dns_init();

    uint8_t short_query[16];
    memset(short_query, 0, sizeof(short_query));
    short_query[0] = 0x12; short_query[1] = 0x34;
    short_query[2] = 0x01; short_query[3] = 0x00;
    short_query[4] = 0x00; short_query[5] = 0x01;
    struct pbuf p;
    init_fake_pbuf(&p, short_query, 11);

    ip_addr_t src;
    IP4_ADDR(&src, 192, 168, 4, 100);
    captive_dns_recv_cb(NULL, fake_pcb, &p, &src, 53);

    CHECK(!sendto_called);
}

/* D10: compression pointer in answer */
static void test_dns_compression_pointer(void) {
    TEST("DNS answer uses compression pointer");
    sendto_called = 0;
    captive_dns_init();

    uint8_t query[512];
    build_dns_query(query);
    struct pbuf p;
    init_fake_pbuf(&p, query, 29);

    ip_addr_t src;
    IP4_ADDR(&src, 192, 168, 4, 100);
    captive_dns_recv_cb(NULL, fake_pcb, &p, &src, 53);

    uint8_t *resp = (uint8_t *)captured_send_pbuf->payload;
    CHECK_EQ(resp[29], 0xC0);
    CHECK_EQ(resp[30], 0x0C);
}

/* ═════════════════════════════════════════════════════════════════════
/* ── Runner ────────────────────────────────────────────────────────── */

int main(void) {
    test_dns_init_creates_listener();
    test_dns_responds_to_query();
    test_dns_response_echoes_id();
    test_dns_flags_correct();
    test_dns_qdcount_echoed();
    test_dns_ancount_one();
    test_dns_answer_ip();
    test_dns_answer_type_class();
    test_dns_short_query_ignored();
    test_dns_compression_pointer();

    printf("\n%d tests run, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
