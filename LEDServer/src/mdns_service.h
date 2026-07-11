#ifndef MDNS_SERVICE_H
#define MDNS_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize mDNS responder and register _lightsync._udp.local service.
 * Must be called after WiFi is connected and netif has an IP address.
 * The hostname is derived from the RP2040 unique board ID.
 *
 * Returns the netif pointer on success, NULL on failure.
 * The caller can ignore the return value; it's available for debugging. */
void *mdns_service_init(void);

/* Re-announce all mDNS services. Call after an IP address change
 * (DHCP renew) to ensure service records remain visible. */
void mdns_service_announce(void);

#ifdef __cplusplus
}
#endif

#endif /* MDNS_SERVICE_H */
