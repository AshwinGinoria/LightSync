/* lwIP TCP stubs for x86 test builds */
#ifndef LWIP_TCP_H
#define LWIP_TCP_H

#include "udp.h"

/* ── TCP error type (extends udp.h err_t with additional codes) ─────── */
#undef ERR_VAL  /* udp.h stub defines this as 1; real lwIP uses 2 for TCP */
typedef int err_t;
#define ERR_OK          0
#define ERR_USE         1
#define ERR_VAL         2
#define ERR_WOULDBLOCK  3
#define ERR_ARG         4

/* ── TCP PCB ──────────────────────────────────────────────────────── */
struct tcp_pcb {
    struct tcp_pcb *next;
    u16_t state;          /* TCP state enum */
    u16_t local_port;
    u16_t remote_port;
    ip_addr_t local_addr;
    ip_addr_t remote_addr;
    void *callback_arg;
    err_t (*recv_cb)(void *, struct tcp_pcb *, struct pbuf *, err_t);
    err_t (*sent_cb)(void *, struct tcp_pcb *, u16_t);
    void (*err_cb)(void *, err_t);
    err_t (*accept_cb)(void *, struct tcp_pcb *, err_t);
    void *accept_arg;
    struct tcp_pcb *accept_pcb;
    void *callback_arg2;
};

/* ── TCP state constants ──────────────────────────────────────────── */
#define TCP_STATE_NONE    0
#define TCP_STATE_LISTEN  1

/* ── TCP function declarations ────────────────────────────────────── */
struct tcp_pcb *tcp_new(void);
err_t tcp_bind(struct tcp_pcb *pcb, const ip_addr_t *addr, u16_t port);
struct tcp_pcb *tcp_listen(struct tcp_pcb *pcb);
err_t tcp_accept(struct tcp_pcb *pcb, err_t (*callback)(void *, struct tcp_pcb *, err_t));
err_t tcp_recv(struct tcp_pcb *pcb, err_t (*callback)(void *, struct tcp_pcb *, struct pbuf *, err_t));
void tcp_err(struct tcp_pcb *pcb, void (*callback)(void *, err_t));
err_t tcp_write(struct tcp_pcb *pcb, const void *data, size_t len, int copy);
err_t tcp_sent(struct tcp_pcb *pcb, err_t (*callback)(void *, struct tcp_pcb *, u16_t));
err_t tcp_close(struct tcp_pcb *pcb);
void tcp_recved(struct tcp_pcb *pcb, size_t len);
void tcp_arg(struct tcp_pcb *pcb, void *arg);

/* ── Test helpers: invoke callbacks directly ──────────────────────── */
void tcp_simulate_connection(struct tcp_pcb *listen_pcb, struct tcp_pcb **out_client);
void tcp_simulate_recv(struct tcp_pcb *pcb, const char *data, size_t len);

#endif /* LWIP_TCP_H */
