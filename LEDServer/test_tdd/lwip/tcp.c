/* lwIP TCP stubs for x86 test builds */
#include "tcp.h"
#include "udp.h"
#include <stdlib.h>
#include <string.h>

/* ── Stub state ───────────────────────────────────────────────────── */
static struct tcp_pcb fake_listen_pcb;
static struct tcp_pcb fake_client_pcb;
static char tcp_sent_buf[2048];
static int tcp_sent_buf_len = 0;
static struct tcp_pcb *g_listen_pcb = NULL;  /* tracks the listening pcb */

/* ── tcp_new ──────────────────────────────────────────────────────── */
struct tcp_pcb *tcp_new(void) {
    /* Return a fresh PCB each time */
    static struct tcp_pcb pcb_pool[8];
    static int pool_idx = 0;
    struct tcp_pcb *pcb = &pcb_pool[pool_idx++];
    memset(pcb, 0, sizeof(*pcb));
    pool_idx %= 8; /* wrap around */
    return pcb;
}

/* ── tcp_bind ─────────────────────────────────────────────────────── */
err_t tcp_bind(struct tcp_pcb *pcb, const ip_addr_t *addr, u16_t port) {
    (void)addr;
    if (!pcb) return ERR_VAL;
    pcb->local_port = port;
    g_listen_pcb = pcb;
    return ERR_OK;
}

/* ── tcp_listen ───────────────────────────────────────────────────── */
struct tcp_pcb *tcp_listen(struct tcp_pcb *pcb) {
    if (!pcb) return NULL;
    pcb->state = TCP_STATE_LISTEN;
    g_listen_pcb = pcb;
    return pcb;
}

/* ── tcp_accept / tcp_recv / tcp_err ──────────────────────────────── */
err_t tcp_accept(struct tcp_pcb *pcb, err_t (*callback)(void *, struct tcp_pcb *, err_t)) {
    if (!pcb) return ERR_OK;
    pcb->accept_arg = callback ? pcb : NULL;
    pcb->accept_cb = callback;
    return ERR_OK;
}

err_t tcp_recv(struct tcp_pcb *pcb, err_t (*callback)(void *, struct tcp_pcb *, struct pbuf *, err_t)) {
    if (!pcb) return ERR_OK;
    pcb->recv_cb = callback;
    return ERR_OK;
}

/* ── Test helpers: invoke callbacks directly ──────────────────────── */

/* Simulate a client connection — calls the accept callback on the
 * listening pcb with a new client pcb.  This triggers the full
 * httpd_accept → tcp_recv(client, httpd_client_recv) chain. */
void tcp_simulate_connection(struct tcp_pcb *listen_pcb, struct tcp_pcb **out_client) {
    if (!listen_pcb || !listen_pcb->accept_cb) return;
    struct tcp_pcb *client = tcp_new();
    if (client) {
        listen_pcb->accept_cb(listen_pcb->accept_arg, client, ERR_OK);
        *out_client = client;
    }
}

/* Feed raw HTTP data through the recv callback chain.
 * Creates a pbuf with the data and invokes the pcb's recv_cb. */
void tcp_simulate_recv(struct tcp_pcb *pcb, const char *data, size_t len) {
    if (!pcb || !pcb->recv_cb) return;
    if (!data || len == 0) return;

    struct {
        struct pbuf *next;
        void *payload;
        uint16_t tot_len;
        uint16_t len;
    } fake_pbuf;
    static char payload_buf[1024];
    size_t copy_len = len < sizeof(payload_buf) ? len : sizeof(payload_buf);
    memcpy(payload_buf, data, copy_len);

    fake_pbuf.next = NULL;
    fake_pbuf.payload = payload_buf;
    fake_pbuf.tot_len = (uint16_t)copy_len;
    fake_pbuf.len = (uint16_t)copy_len;

    pcb->recv_cb(pcb->callback_arg, pcb, (struct pbuf *)&fake_pbuf, ERR_OK);
}

void tcp_err(struct tcp_pcb *pcb, void (*callback)(void *, err_t)) {
    if (!pcb) return;
    pcb->err_cb = callback;
}

/* ── tcp_write / tcp_sent ─────────────────────────────────────────── */
err_t tcp_write(struct tcp_pcb *pcb, const void *data, size_t len, int copy) {
    (void)pcb; (void)copy;
    if (!data || len == 0) return ERR_ARG;
    if (tcp_sent_buf_len + len > sizeof(tcp_sent_buf)) {
        len = sizeof(tcp_sent_buf) - tcp_sent_buf_len;
    }
    memcpy(tcp_sent_buf + tcp_sent_buf_len, data, len);
    tcp_sent_buf_len += len;
    return ERR_OK;
}

err_t tcp_sent(struct tcp_pcb *pcb, err_t (*callback)(void *, struct tcp_pcb *, u16_t)) {
    (void)pcb; (void)callback;
    return ERR_OK;
}

/* ── tcp_close / tcp_recved ───────────────────────────────────────── */
err_t tcp_close(struct tcp_pcb *pcb) {
    (void)pcb;
    return ERR_OK;
}

void tcp_recved(struct tcp_pcb *pcb, size_t len) {
    (void)pcb; (void)len;
}

/* ── tcp_arg ──────────────────────────────────────────────────────── */
void tcp_arg(struct tcp_pcb *pcb, void *arg) {
    if (!pcb) return;
    pcb->callback_arg = arg;
}

/* ── Helpers for tests ────────────────────────────────────────────── */

/* Get the data written via tcp_write (for assertion in tests) */
const char *tcp_get_sent_data(void) {
    return tcp_sent_buf;
}

int tcp_get_sent_len(void) {
    return tcp_sent_buf_len;
}

void tcp_reset_sent_buf(void) {
    tcp_sent_buf_len = 0;
    memset(tcp_sent_buf, 0, sizeof(tcp_sent_buf));
}

/* ── pbuf helpers needed by httpd.c ────────────────────────────────── */
/* Re-export from udp.c — declared in udp.h */
struct pbuf *pbuf_alloc(int layer, uint16_t length, int type);
void pbuf_free(struct pbuf *p);
size_t pbuf_copy_partial(struct pbuf *p, void *buf, size_t len, size_t offset);

/* ── Test helper: simulate client sending data via recv callback ───── */
static err_t (*g_sim_recv_cb)(void *, struct tcp_pcb *, struct pbuf *, err_t) = NULL;
static struct tcp_pcb *g_sim_client_pcb = NULL;

/* Set the recv callback from httpd's perspective (called by tcp_recv) */
void tcp_set_sim_recv_cb(err_t (*cb)(void *, struct tcp_pcb *, struct pbuf *, err_t)) {
    g_sim_recv_cb = cb;
}

/* Simulate a client sending data — creates a fake pbuf and calls recv_cb */
void tcp_simulate_client_data(const char *data, size_t len) {
    if (!g_sim_recv_cb || !g_sim_client_pcb) return;

    /* Create a minimal fake pbuf on stack — just enough fields */
    struct {
        struct pbuf *next;
        void *payload;
        uint16_t tot_len;
        uint16_t len;
        uint16_t type;
        uint16_t layer;
    } fake_pbuf;

    /* Allocate temp storage for payload (static to survive across calls) */
    static char payload_buf[1024];
    size_t copy_len = len < sizeof(payload_buf) ? len : sizeof(payload_buf);
    memcpy(payload_buf, data, copy_len);

    fake_pbuf.next = NULL;
    fake_pbuf.payload = payload_buf;
    fake_pbuf.tot_len = (uint16_t)copy_len;
    fake_pbuf.len = (uint16_t)copy_len;
    fake_pbuf.type = 0;
    fake_pbuf.layer = 0;

    g_sim_recv_cb(NULL, g_sim_client_pcb, (struct pbuf *)&fake_pbuf, ERR_OK);
}

/* Register a client pcb for simulation */
void tcp_simulate_accept(struct tcp_pcb *client_pcb) {
    g_sim_client_pcb = client_pcb;
}
