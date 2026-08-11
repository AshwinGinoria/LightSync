/* lwIP options override for LEDServer.
 *
 * Based on pico-examples pico_w/wifi/lwipopts_examples_common.h,
 * extended with mDNS support and checksum offload. */

#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

/* ── Allow overrides ────────────────────────────────────────────────── */
#ifndef NO_SYS
#define NO_SYS                      1   /* No OS — polling mode           */
#endif
#ifndef LWIP_SOCKET
#define LWIP_SOCKET                 0
#endif

/* ── Memory allocation (critical: MEM_LIBC_MALLOC=1 for cyw43 init) ─── */
#define MEM_LIBC_MALLOC             1
#define MEM_ALIGNMENT               4
#ifndef MEM_SIZE
#define MEM_SIZE                    4000
#endif

/* ── Pool sizes ─────────────────────────────────────────────────────── */
#define MEMP_NUM_TCP_SEG            32
#define MEMP_NUM_ARP_QUEUE          10
#define PBUF_POOL_SIZE              48

/* ── Protocols ──────────────────────────────────────────────────────── */
#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_ICMP                   1
#define LWIP_RAW                    1
#define LWIP_DHCP                   1
#define LWIP_IPV4                   1
#define LWIP_TCP                    1
#define LWIP_UDP                    1
#define LWIP_DNS                    1

/* ── TCP tuning ─────────────────────────────────────────────────────── */
#define TCP_WND                     (4 * TCP_MSS)   /* smaller than default 8× */
#define TCP_MSS                     1460
#define TCP_SND_BUF                 (8 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define LWIP_WND_SCALE              0
#define LWIP_TCP_KEEPALIVE          1

/* ── Netif / link callbacks ─────────────────────────────────────────── */
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_HOSTNAME         1
#define LWIP_NETIF_TX_SINGLE_PBUF   1

/* ── Socket / netconn API — raw API only ────────────────────────────── */
#define LWIP_NETCONN                0

/* ── Stats (enabled for OOM debugging) ─────────────────────────────── */
#define LWIP_STATS                  1
#define LWIP_STATS_DISPLAY          1
#define MEM_STATS                   1
#define SYS_STATS                   0
#define MEMP_STATS                  1
#define LINK_STATS                  0

/* ── Checksum generation ────────────────────────────────────────────── */
/* CYW43 HW checksum offload is BROKEN in AP mode (cksum == 0x0000).
 * Enable software generation so TCP/ICMP packets are not rejected.     */
#define LWIP_CHKSUM_ALGORITHM       3   /* 3 = 32-bit folded checksum    */
#define CHECKSUM_GEN_IP             1   /* Software: HW offload broken    */
#define CHECKSUM_GEN_UDP            1   /* Software: HW offload broken    */
#define CHECKSUM_GEN_TCP            1   /* Software: was 0x0000 in AP!    */
#define CHECKSUM_GEN_ICMP           1   /* Software: was 0x0000 in AP!    */
#define CHECKSUM_CHECK_IP           0
#define CHECKSUM_CHECK_UDP          0
#define CHECKSUM_CHECK_TCP          0
#define CHECKSUM_CHECK_ICMP         0

/* ── DHCP tuning ────────────────────────────────────────────────────── */
#define DHCP_DOES_ARP_CHECK         0
#define LWIP_DHCP_DOES_ACD_CHECK    0

/* ── mDNS responder ─────────────────────────────────────────────────── */
#define LWIP_IGMP                   1   /* Required by mDNS for IPv4       */
#define LWIP_MDNS_RESPONDER         1
#define LWIP_NUM_NETIF_CLIENT_DATA  1   /* netif_alloc_client_data_id()   */

/* ── TCP pool tuning (custom) ───────────────────────────────────────── */
#define MEMP_NUM_TCP_PCB            16   /* 8 is too few for captive portal — browsers open 6-8 concurrent connections per host */
#define MEMP_NUM_TCP_PCB_LISTEN     4
#define MEMP_NUM_UDP_PCB            8

/* ── Debug — OFF for production stability ─ */
#define LWIP_DBG_MIN_LEVEL          LWIP_DBG_LEVEL_ALL
#define ETHARP_DEBUG                LWIP_DBG_OFF
#define NETIF_DEBUG                 LWIP_DBG_OFF
#define PBUF_DEBUG                  LWIP_DBG_ON
#define API_LIB_DEBUG               LWIP_DBG_OFF
#define API_MSG_DEBUG               LWIP_DBG_OFF
#define SOCKETS_DEBUG               LWIP_DBG_OFF
#define ICMP_DEBUG                  LWIP_DBG_OFF
#define INET_DEBUG                  LWIP_DBG_OFF
#define IP_DEBUG                    LWIP_DBG_OFF
#define IP_REASS_DEBUG              LWIP_DBG_OFF
#define RAW_DEBUG                   LWIP_DBG_ON
#define MEM_DEBUG                   LWIP_DBG_ON
#define MEMP_DEBUG                  LWIP_DBG_ON
#define SYS_DEBUG                   LWIP_DBG_OFF
#define TCP_DEBUG                   LWIP_DBG_ON
#define TCP_INPUT_DEBUG             LWIP_DBG_OFF
#define TCP_OUTPUT_DEBUG            LWIP_DBG_ON
#define TCP_RTO_DEBUG               LWIP_DBG_OFF
#define TCP_CWND_DEBUG              LWIP_DBG_OFF
#define TCP_WND_DEBUG               LWIP_DBG_OFF
#define TCP_FR_DEBUG                LWIP_DBG_OFF
#define TCP_QLEN_DEBUG              LWIP_DBG_OFF
#define TCP_RST_DEBUG               LWIP_DBG_OFF
#define UDP_DEBUG                   LWIP_DBG_ON
#define RAW_DEBUG                   LWIP_DBG_ON
#define TCPIP_DEBUG                 LWIP_DBG_OFF
#define PPP_DEBUG                   LWIP_DBG_OFF
#define SLIP_DEBUG                  LWIP_DBG_OFF
#define DHCP_DEBUG                  LWIP_DBG_ON

#endif /* _LWIPOPTS_H */
