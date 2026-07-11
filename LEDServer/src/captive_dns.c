/* Captive DNS Server — answers every DNS query with 192.168.4.1.
 *
 * DNS response format (minimal, one question → one A record):
 *   Header (12 bytes) + Question + Answer
 *
 * The question section is echoed verbatim from the query.
 * The answer uses compression pointer (0xC00C) to offset 12
 * where the question name starts, then TYPE=1 (A), CLASS=1 (IN),
 * TTL=86400, RDLEN=4, RDATA=192.168.4.1. */
#include "captive_dns.h"

#include <string.h>
#include <stdint.h>

/* ── lwIP stubs ─────────────────────────────────────────────────────── */
#include "lwip/udp.h"

/* ── Constants ──────────────────────────────────────────────────────── */
#define DNS_PORT          53
#define AP_IP             0xC0A80401  /* 192.168.4.1 in network byte order */
#define DNS_RESPONSE_MAX  512
#define DNS_MIN_QUERY     12

/* ── State ──────────────────────────────────────────────────────────── */
static struct udp_pcb *dns_pcb = NULL;

/* ── Build DNS response into buf, return length ─────────────────────── */
static uint16_t build_dns_response(const uint8_t *query, uint16_t query_len,
                                   uint8_t *buf) {
    /* Copy header from query */
    memcpy(buf, query, 12);

    /* Set response flags: QR=1 (response), AA=1 (authoritative),
     * RA=1 (recursion available), RCODE=0 */
    buf[2] = 0x81;
    buf[3] = 0x80;

    /* Set ANCOUNT = 1 */
    buf[6] = 0x00;
    buf[7] = 0x01;

    /* NSCOUNT and ARCOUNT = 0 (already zero from copy) */

    /* Copy question section verbatim from query */
    memcpy(buf + 12, query + 12, query_len - 12);

    /* Answer section:
     * NAME: 0xC00C (compression pointer to offset 12)
     * TYPE: A (0x0001)
     * CLASS: IN (0x0001)
     * TTL: 86400 (0x00002138)
     * RDLENGTH: 4
     * RDATA: 192.168.4.1 */
    int an = query_len;
    buf[an++] = 0xC0;  /* compression pointer */
    buf[an++] = 0x0C;  /* offset to question name */
    buf[an++] = 0x00;  /* TYPE = A */
    buf[an++] = 0x01;
    buf[an++] = 0x00;  /* CLASS = IN */
    buf[an++] = 0x01;
    buf[an++] = 0x00;  /* TTL = 86400 */
    buf[an++] = 0x00;
    buf[an++] = 0x21;
    buf[an++] = 0x38;
    buf[an++] = 0x00;  /* RDLENGTH = 4 */
    buf[an++] = 0x04;
    buf[an++] = 192;   /* RDATA = 192.168.4.1 */
    buf[an++] = 168;
    buf[an++] = 4;
    buf[an++] = 1;

    return (uint16_t)an;
}

/* ── Public API ─────────────────────────────────────────────────────── */

void captive_dns_init(void) {
    dns_pcb = udp_new();
    ip_addr_t any;
    IP4_ADDR(&any, 0, 0, 0, 0);
    udp_bind(dns_pcb, &any, DNS_PORT);
    udp_recv(dns_pcb, captive_dns_recv_cb, NULL);
}

void captive_dns_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                         const ip_addr_t *addr, u16_t port) {
    (void)arg;
    (void)pcb;
    (void)addr;
    (void)port;

    if (!p || !p->payload) return;

    uint8_t *query = (uint8_t *)p->payload;
    uint16_t query_len = p->len;

    /* Ignore queries shorter than DNS header */
    if (query_len < DNS_MIN_QUERY) {
        return;
    }

    /* Check if this is a standard query (QR=0, MSB of flags) */
    uint16_t flags = (query[2] << 8) | query[3];
    if ((flags & 0x8000) != 0) {
        /* Already a response, ignore */
        return;
    }

    /* Build response into static buffer */
    static uint8_t resp_buf[DNS_RESPONSE_MAX];
    uint16_t resp_len = build_dns_response(query, query_len, resp_buf);

    /* Create a pbuf wrapping the response data.
     * The test stub captures this pbuf in udp_sendto. */
    static struct {
        struct pbuf base;
        uint8_t payload[DNS_RESPONSE_MAX];
    } resp_pbuf_storage;

    memcpy(resp_pbuf_storage.payload, resp_buf, resp_len);
    resp_pbuf_storage.base.next = NULL;
    resp_pbuf_storage.base.payload = resp_pbuf_storage.payload;
    resp_pbuf_storage.base.len = resp_len;
    resp_pbuf_storage.base.tot_len = resp_len;

    /* Send response back to sender */
    ip_addr_t src;
    memcpy(&src, addr, sizeof(ip_addr_t));
    udp_sendto(dns_pcb, &resp_pbuf_storage.base, &src, DNS_PORT);
}
