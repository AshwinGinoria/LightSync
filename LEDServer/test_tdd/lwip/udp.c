/* lwIP UDP stubs for x86 test builds */
#include "udp.h"
#include <stdlib.h>
#include <string.h>

struct udp_pcb *udp_new(void) {
    static struct udp_pcb pool[8];
    static int idx = 0;
    struct udp_pcb *pcb = &pool[idx++];
    memset(pcb, 0, sizeof(*pcb));
    idx %= 8;
    return pcb;
}

err_t udp_bind(struct udp_pcb *pcb, const ip_addr_t *addr, u16_t port) {
    (void)addr;
    if (!pcb) return ERR_VAL;
    pcb->port = port;
    pcb->local_port = port;
    return ERR_OK;
}

void udp_recv(struct udp_pcb *pcb, void (*callback)(void *, struct udp_pcb *, struct pbuf *, const ip_addr_t *, u16_t), void *arg) {
    if (!pcb) return;
    pcb->recv_cb = callback;
    pcb->recv_arg = arg;
}

err_t udp_sendto(struct udp_pcb *pcb, struct pbuf *p, const ip_addr_t *addr, u16_t port) {
    (void)pcb; (void)p; (void)addr; (void)port;
    return ERR_OK;
}

void udp_remove(struct udp_pcb *pcb) {
    (void)pcb;
}
