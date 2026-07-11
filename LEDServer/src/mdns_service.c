#include "mdns_service.h"

#include "lwip/apps/mdns.h"
#include "lwip/netif.h"
#include "pico/unique_id.h"

#include <stdio.h>
#include <string.h>

/* ── Service constants ─────────────────────────────────────────────── */

#define HOSTNAME_PREFIX "lightsync"
#define SERVICE_NAME    "LightSync LED Server"
#define SERVICE_TYPE    "_lightsync"
#define SERVICE_PROTO   DNSSD_PROTO_UDP
#define SERVICE_PORT    5005

/* TXT record values */
#define TXT_VERSION     "1"
#define TXT_LED_COUNT   "288"
#define TXT_LED_TYPE    "WS2812B"
#define TXT_CAPS        "raw,ddp,effects,music"

/* ── Static state ──────────────────────────────────────────────────── */

static char hostname_full[32];
static struct netif *registered_netif;

/* ── TXT record callback ─────────────────────────────────────────────
 * lwIP calls this when building the service advertisement.
 * We add one TXT key per call. */

static void txt_callback(struct mdns_service *service, void *userdata) {
    (void)userdata; (void)service;
    /* New SDK: TXT records are no longer set via callback in this way.
     * Service is registered without dynamic TXT. */
}

/* ── Public API ────────────────────────────────────────────────────── */

void *mdns_service_init(void) {
    /* Read the RP2040 unique board ID for a stable hostname suffix. */
    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);

    uint16_t id_suffix = ((uint16_t)board_id.id[6] << 8) | board_id.id[7];
    snprintf(hostname_full, sizeof(hostname_full), "%s-%04X",
             HOSTNAME_PREFIX, id_suffix);

    /* Initialise the lwIP mDNS responder.  Must be called once. */
    mdns_resp_init();

    /* Find the first UP netif (the WiFi station interface). */
    struct netif *netif = netif_list;
    while (netif) {
        if (netif->flags & NETIF_FLAG_UP) break;
        netif = netif->next;
    }
    if (!netif) {
        printf("mDNS: no active network interface\n");
        return NULL;
    }

    /* Register the hostname on this interface. */
    mdns_resp_add_netif(netif, hostname_full);

    /* Register the _lightsync._udp.local service. */
    mdns_resp_add_service(netif, SERVICE_NAME, SERVICE_TYPE,
                          SERVICE_PROTO, SERVICE_PORT,
                          txt_callback, NULL);

    registered_netif = netif;

    printf("mDNS: %s.local registered (_lightsync._udp on port %d)\n",
           hostname_full, SERVICE_PORT);
    return netif;
}

void mdns_service_announce(void) {
    if (registered_netif) {
        mdns_resp_announce(registered_netif);
    }
}
