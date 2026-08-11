#ifndef LWIP_UDP_H
#define LWIP_UDP_H

#include <stdint.h>
#include <stddef.h>

/* ── lwIP type stubs for x86 testing ───────────────────────────────── */

typedef int err_t;
#define ERR_OK    0
#define ERR_VAL   1

typedef uint16_t u16_t;

/* Named struct so &addr works in IP4_ADDR macro.
 * Using 'dst' as macro param to avoid conflict with local var named 'addr'. */
struct ip_addr {
    uint32_t addr;
};
typedef struct ip_addr ip_addr_t;

/* ── pbuf type constants ─────────────────────────────────────────── */
#define PBUF_RAM      1
#define PBUF_ROM      2
#define PBUF_REF      3
#define PBUF_POOL     4
#define PBUF_RAW      5

/* Fake pbuf structure — complete definition for pbuf_alloc/pbuf_free */
struct pbuf {
    struct pbuf *next;
    void *payload;
    uint16_t tot_len;
    uint16_t len;
};

/* ── UDP PCB — full definition (needed by stubs and tests) ─────────── */

struct udp_pcb {
    struct udp_pcb *next;
    uint8_t flags;
    u16_t port;
    ip_addr_t local_addr;
    ip_addr_t remote_addr;
    u16_t local_port;
    u16_t remote_port;
    void (*recv_cb)(void *, struct udp_pcb *, struct pbuf *,
                    const ip_addr_t *, u16_t);
    void *recv_arg;
};

/* IP4_ADDR macro — creates an address.
 * Uses 'dst' param name to avoid conflict with local vars named 'addr'. */
#define IP4_ADDR(dst, a, b, c, d) \
    do { \
        (dst)->addr = ((uint32_t)(a) << 24) | \
                      ((uint32_t)(b) << 16) | \
                      ((uint32_t)(c) <<  8) | \
                      ((uint32_t)(d) <<  0); \
    } while (0)

/* ── Function declarations (stubs) ─────────────────────────────────── */

struct udp_pcb *udp_new(void);
err_t udp_bind(struct udp_pcb *pcb, const ip_addr_t *addr, u16_t port);
void udp_recv(struct udp_pcb *pcb,
              void (*recv)(void *, struct udp_pcb *, struct pbuf *,
                           const ip_addr_t *, u16_t),
              void *arg);
err_t udp_sendto(struct udp_pcb *pcb, struct pbuf *p,
                 const ip_addr_t *addr, u16_t port);
void udp_remove(struct udp_pcb *pcb);

struct pbuf *pbuf_alloc(int layer, uint16_t length, int type);
void pbuf_free(struct pbuf *p);
size_t pbuf_copy_partial(struct pbuf *p, void *dst, size_t len, size_t offset);

#endif /* LWIP_UDP_H */
