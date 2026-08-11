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
#include <stdio.h>

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

/* Non-blocking serial write: only write if space is available.
 * Prevents the DNS handler from blocking the main loop when the
 * serial USB CDC buffer is full. */
static void dns_serial_write(const char *msg, size_t len) {
    /* pico_stdio fwrite is non-blocking on USB CDC when the ring
     * buffer is full — it returns -1. On UART it blocks, but we
     * only build for USB CDC on the Pico W. */
    ssize_t n = fwrite(msg, 1, len, stdout);
    (void)n;
    /* If n == -1 or n < len, the buffer was full. Silently drop
     * the diagnostic to avoid blocking the main loop. */
}

void captive_dns_recv_cb(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                         const ip_addr_t *addr, u16_t port) {
    (void)arg;
    (void)pcb;
    (void)addr;
    (void)port;

    dns_serial_write("DNS:entry\n", 10);

    if (!p || !p->payload) {
        dns_serial_write("DNS:drop-null\n", 14);
        return;
    }

    uint8_t *query = (uint8_t *)p->payload;
    uint16_t query_len = p->len;

    dns_serial_write("DNS:valid-pbuf\n", 15);

    /* Ignore queries shorter than DNS header */
    if (query_len < DNS_MIN_QUERY) {
        dns_serial_write("DNS:drop-short\n", 15);
        return;
    }

    /* Check if this is a standard query (QR=0, MSB of flags) */
    uint16_t flags = (query[2] << 8) | query[3];
    if ((flags & 0x8000) != 0) {
        dns_serial_write("DNS:drop-response\n", 18);
        return;
    }

    dns_serial_write("DNS:building\n", 12);

    /* Build response into static buffer */
    static uint8_t resp_buf[DNS_RESPONSE_MAX];
    uint16_t resp_len = build_dns_response(query, query_len, resp_buf);

    dns_serial_write("DNS:built\n", 10);

    /* Free the received pbuf — we've copied the data we need.
     * Without this, the pbuf pool is exhausted after a few queries. */
    pbuf_free(p);

    dns_serial_write("DNS:sending\n", 11);

    /* Allocate pbuf from pool for response */
    struct pbuf *resp = pbuf_alloc(PBUF_RAW, resp_len, PBUF_POOL);
    if (!resp) {
        dns_serial_write("DNS:pbuf_alloc_failed\n", 23);
        /* Try PBUF_RAM as fallback */
        resp = pbuf_alloc(PBUF_RAW, resp_len, PBUF_RAM);
        if (!resp) {
            dns_serial_write("DNS:pbuf_ram_failed\n", 20);
            return;
        }
        dns_serial_write("DNS:pbuf_ram_used\n", 18);
        memcpy(resp->payload, resp_buf, resp_len);
    } else {
        dns_serial_write("DNS:pbuf_alloc_ok\n", 18);
        memcpy(resp->payload, resp_buf, resp_len);
    }

    /* Send response back to sender */
    ip_addr_t src;
    memcpy(&src, addr, sizeof(ip_addr_t));

    err_t err = udp_sendto(dns_pcb, resp, &src, DNS_PORT);
    if (err != ERR_OK) {
        char _e[32];
        int _n = snprintf(_e, sizeof(_e), "DNS:send_err=%d\n", (int)err);
        if (_n > 0) dns_serial_write(_e, (size_t)_n);
    } else {
        dns_serial_write("DNS:send_ok\n", 12);
    }
    pbuf_free(resp);
    dns_serial_write("DNS:done\n", 9);
}
