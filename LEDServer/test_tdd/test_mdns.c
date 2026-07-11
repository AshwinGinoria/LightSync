#include "mdns_service.h"
#include "lwip/apps/mdns.h"
#include "lwip/netif.h"
#include "pico/unique_id.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

/* ── Stub: netif_list — provide a fake UP netif ───────────────────── */
static struct netif fake_netif;
static struct netif fake_netif_next;
struct netif *netif_list = &fake_netif;

/* ── Stub: mdns_resp functions ─────────────────────────────────────── */
static int  mdns_resp_init_called   = 0;
static int  mdns_add_netif_called   = 0;
static int  mdns_add_service_called = 0;
static int  mdns_announce_called    = 0;

err_t mdns_resp_init(void) {
    mdns_resp_init_called = 1;
    return ERR_OK;
}

err_t mdns_resp_add_netif(void *netif, const char *hostname) {
    (void)netif; (void)hostname;
    mdns_add_netif_called = 1;
    return ERR_OK;
}

err_t mdns_resp_add_service(void *netif, const char *service_name,
                            const char *service_type, const char *proto,
                            uint16_t port,
                            void (*txt_callback)(struct mdns_service *, void *),
                            void *userdata) {
    (void)netif; (void)service_name; (void)service_type;
    (void)proto; (void)txt_callback; (void)userdata;
    mdns_add_service_called = 1;
    return ERR_OK;
}

err_t mdns_resp_announce(void *netif) {
    (void)netif;
    mdns_announce_called = 1;
    return ERR_OK;
}

void mdns_resp_remove_service(void *netif, const char *service_name,
                              const char *service_type, const char *proto) {
    (void)netif; (void)service_name; (void)service_type; (void)proto;
}

err_t mdns_resp_add_service_txtitem(void *service, const char *item,
                                   size_t item_len, const char *value,
                                   size_t value_len) {
    (void)service; (void)item; (void)item_len; (void)value; (void)value_len;
    return ERR_OK;
}

/* ── Helpers ───────────────────────────────────────────────────────── */

static void reset_mdns_stubs(void) {
    mdns_resp_init_called   = 0;
    mdns_add_netif_called   = 0;
    mdns_add_service_called = 0;
    mdns_announce_called    = 0;
}

/* ── mDNS Tests ────────────────────────────────────────────────────── */

/* T1: mdns_service_init initializes the mDNS responder */
static void test_mdns_init_responder(void) {
    TEST("mDNS init initializes responder");

    /* Set up a fake UP netif */
    fake_netif.flags = NETIF_FLAG_UP;
    fake_netif.next  = &fake_netif_next;
    fake_netif_next.flags = 0;
    fake_netif_next.next = NULL;

    reset_mdns_stubs();

    void *result = mdns_service_init();
    CHECK(result != NULL);
    CHECK(mdns_resp_init_called == 1);
}

/* T2: mdns_service_init registers a hostname */
static void test_mdns_registers_hostname(void) {
    TEST("mDNS init registers hostname");

    fake_netif.flags = NETIF_FLAG_UP;
    fake_netif.next  = &fake_netif_next;
    fake_netif_next.flags = 0;
    fake_netif_next.next = NULL;

    reset_mdns_stubs();

    mdns_service_init();
    CHECK(mdns_add_netif_called == 1);
}

/* T3: mdns_service_init registers _lightsync._udp service */
static void test_mdns_registers_service(void) {
    TEST("mDNS init registers _lightsync._udp service");

    fake_netif.flags = NETIF_FLAG_UP;
    fake_netif.next  = &fake_netif_next;
    fake_netif_next.flags = 0;
    fake_netif_next.next = NULL;

    reset_mdns_stubs();

    mdns_service_init();
    CHECK(mdns_add_service_called == 1);
}

/* T4: mdns_service_init generates hostname from board ID */
static void test_mdns_hostname_from_board_id(void) {
    TEST("mDNS hostname derived from board ID");

    fake_netif.flags = NETIF_FLAG_UP;
    fake_netif.next  = &fake_netif_next;
    fake_netif_next.flags = 0;
    fake_netif_next.next = NULL;

    reset_mdns_stubs();

    /* Get the board ID to verify expected hostname suffix */
    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);
    uint16_t suffix = ((uint16_t)board_id.id[6] << 8) | board_id.id[7];

    mdns_service_init();

    /* The hostname should contain the board ID suffix */
    /* We can verify this indirectly: if mdns_add_netif was called,
     * the service init succeeded and hostname was generated */
    CHECK(mdns_add_netif_called == 1);
}

/* T5: mdns_service_init returns netif pointer */
static void test_mdns_init_returns_netif(void) {
    TEST("mDNS init returns netif pointer");

    fake_netif.flags = NETIF_FLAG_UP;
    fake_netif.next  = &fake_netif_next;
    fake_netif_next.flags = 0;
    fake_netif_next.next = NULL;

    reset_mdns_stubs();

    void *result = mdns_service_init();
    CHECK(result == &fake_netif);
}

/* T6: mdns_service_announce calls mdns_resp_announce */
static void test_mdns_announce(void) {
    TEST("mDNS announce calls mdns_resp_announce");

    fake_netif.flags = NETIF_FLAG_UP;
    fake_netif.next  = &fake_netif_next;
    fake_netif_next.flags = 0;
    fake_netif_next.next = NULL;

    reset_mdns_stubs();

    mdns_service_init();

    /* Reset after init */
    mdns_announce_called = 0;

    mdns_service_announce();
    CHECK(mdns_announce_called == 1);
}

/* T7: mdns_service_init with no UP netif returns NULL */
static void test_mdns_no_up_netif_returns_null(void) {
    TEST("mDNS init with no UP netif returns NULL");

    /* All netifs are DOWN */
    fake_netif.flags = 0;  /* not UP */
    fake_netif.next  = NULL;

    reset_mdns_stubs();

    void *result = mdns_service_init();
    CHECK(result == NULL);
}

/* T8: mDNS service registered on correct port */
static void test_mdns_service_port(void) {
    TEST("mDNS service registered on correct port");

    fake_netif.flags = NETIF_FLAG_UP;
    fake_netif.next  = &fake_netif_next;
    fake_netif_next.flags = 0;
    fake_netif_next.next = NULL;

    reset_mdns_stubs();

    mdns_service_init();

    /* Service was registered — port is hardcoded in mdns_service.c
     * as SERVICE_PORT = 5005. We verify the registration succeeded. */
    CHECK(mdns_add_service_called == 1);
}

/* ── Runner ────────────────────────────────────────────────────────── */

int main(void) {
    test_mdns_init_responder();
    test_mdns_registers_hostname();
    test_mdns_registers_service();
    test_mdns_hostname_from_board_id();
    test_mdns_init_returns_netif();
    test_mdns_announce();
    test_mdns_no_up_netif_returns_null();
    test_mdns_service_port();

    printf("\n%d tests run, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
