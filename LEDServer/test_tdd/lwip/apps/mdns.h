#ifndef LWIP_MDNS_H
#define LWIP_MDNS_H

#include <stdint.h>
#include <stddef.h>

typedef int err_t;
#define ERR_OK    0
#define DNSSD_PROTO_UDP 0

struct mdns_service;

err_t mdns_resp_init(void);
err_t mdns_resp_add_netif(void *netif, const char *hostname);
err_t mdns_resp_add_service(void *netif, const char *service_name,
                            const char *service_type, const char *proto,
                            uint16_t port,
                            void (*txt_callback)(struct mdns_service *, void *), void *arg);
err_t mdns_resp_add_service_txtitem(void *service, const char *item, size_t item_len,
                                    const char *value, size_t value_len);
err_t mdns_resp_announce(void *netif);

#endif
