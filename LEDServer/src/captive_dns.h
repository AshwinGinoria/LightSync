/* Captive DNS — UDP listener on port 53 that answers every query
 * with 192.168.4.1 (the Pico W AP's IP address).
 *
 * Used for captive portal: any browser that resolves a hostname
 * while connected to the AP gets redirected to the provisioning page. */
#ifndef CAPTIVE_DNS_H
#define CAPTIVE_DNS_H

#include <stdint.h>
#include "lwip/udp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize DNS listener on UDP port 53.
 * Must be called after cyw43_arch_enable_ap_mode(). */
void captive_dns_init(void);

/* Receive callback — called by lwIP when a UDP packet arrives on port 53. */
void captive_dns_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                         const ip_addr_t *addr, u16_t port);

#ifdef __cplusplus
}
#endif

#endif /* CAPTIVE_DNS_H */
