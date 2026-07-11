#ifndef LWIP_NETIF_H
#define LWIP_NETIF_H

#include <stdint.h>

/* Stub for lwIP netif structure.
 * mdns_service.c iterates netif_list looking for UP interfaces. */

struct netif {
    struct netif *next;
    uint8_t flags;
};

#define NETIF_FLAG_UP  0x01

/* Global list of netifs — we'll populate this in the test */
extern struct netif *netif_list;

#endif /* LWIP_NETIF_H */
