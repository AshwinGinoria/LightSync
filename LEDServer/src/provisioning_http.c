/* HTTP Provisioning Server — captive portal form handler.
 *
 * Provides parse_form(), build_response_200(), build_response_302(),
 * and build_provisioning_html() for the HTTP server to serve.
 *
 * The full HTTP server (lwIP httpd integration) is in a separate file.
 * This file focuses on the form parsing and response building logic,
 * which is the core functionality that needs rigorous testing. */
#include "provisioning_http.h"

#include <string.h>
#include <stdio.h>
#include <stdint.h>

/* ── URL decoding helpers ──────────────────────────────────────────── */

/* Decode a hex digit. Returns -1 if not a valid hex char. */
static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

/* Decode a percent-encoded character.
 * Decodes in-place: reads from src, writes to dst.
 * Handles %XX sequences, '+' -> ' ', and passthrough.
 * Returns 1 if decoded successfully, 0 on invalid encoding. */
static int decode_url(const char *src, char *dst, size_t dst_len) {
    size_t i = 0, j = 0;
    while (src[i] && i < dst_len - 1) {
        if (src[i] == '%') {
            /* %XX sequence */
            if (src[i + 1] && src[i + 2]) {
                int hi = hex_digit(src[i + 1]);
                int lo = hex_digit(src[i + 2]);
                if (hi < 0 || lo < 0) return 0; /* invalid */
                dst[j++] = (char)(hi * 16 + lo);
                i += 3;
            } else {
                return 0; /* incomplete %XX */
            }
        } else if (src[i] == '+') {
            dst[j++] = ' ';
            i++;
        } else {
            dst[j++] = src[i++];
        }
    }
    dst[j] = '\0';
    return 1;
}

/* ── parse_form ────────────────────────────────────────────────────── */

int parse_form(const char *body, char *ssid, char *pass,
               size_t ssid_len, size_t pass_len) {
    /* Parse "ssid=...&password=..." format.
     * We do a single pass, finding ssid= then password=. */
    const char *p = body;

    /* Find ssid= parameter */
    const char *ssid_key = strstr(p, "ssid=");
    if (!ssid_key) return 0;
    ssid_key += 5; /* skip "ssid=" */

    /* Find end of ssid value (next & or \0) */
    const char *ssid_end = strchr(ssid_key, '&');
    if (!ssid_end) ssid_end = ssid_key + strlen(ssid_key);

    /* Decode ssid */
    size_t ssid_raw_len = (size_t)(ssid_end - ssid_key);
    if (ssid_raw_len == 0) return 0; /* empty ssid */

    char raw[256];
    if (ssid_raw_len >= sizeof(raw)) return 0;
    memcpy(raw, ssid_key, ssid_raw_len);
    raw[ssid_raw_len] = '\0';

    if (!decode_url(raw, ssid, ssid_len)) return 0;
    if (strlen(ssid) == 0) return 0;

    /* Find password= parameter */
    const char *pass_key = strstr(ssid_end, "password=");
    if (!pass_key) {
        /* password is optional — set empty */
        pass[0] = '\0';
        return 1;
    }
    pass_key += 9; /* skip "password=" */

    /* Find end of password value (next & or \0) */
    const char *pass_end = strchr(pass_key, '&');
    if (!pass_end) pass_end = pass_key + strlen(pass_key);

    size_t pass_raw_len = (size_t)(pass_end - pass_key);
    if (pass_raw_len == 0) {
        pass[0] = '\0';
        return 1;
    }

    char raw_pass[256];
    if (pass_raw_len >= sizeof(raw_pass)) return 0;
    memcpy(raw_pass, pass_key, pass_raw_len);
    raw_pass[pass_raw_len] = '\0';

    if (!decode_url(raw_pass, pass, pass_len)) return 0;

    return 1;
}

/* ── Response builders ─────────────────────────────────────────────── */

int build_response_200(char *buf, size_t buf_size,
                       const char *body, size_t body_len) {
    /* Build: HTTP/1.1 200 OK\r\nContent-Length: N\r\n\r\n<body> */
    int needed = snprintf(buf, buf_size,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        (int)body_len);

    if (needed < 0 || (size_t)needed >= buf_size) return -1;

    size_t header_len = (size_t)needed;
    if (header_len + body_len >= buf_size) return -1;

    memcpy(buf + header_len, body, body_len);
    return (int)(header_len + body_len);
}

int build_response_302(char *buf, size_t buf_size, const char *location) {
    /* Build: HTTP/1.1 302 Found\r\nLocation: <url>\r\n\r\n */
    return snprintf(buf, buf_size,
        "HTTP/1.1 302 Found\r\n"
        "Location: %s\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n",
        location);
}

int build_provisioning_html(char *buf, size_t buf_size) {
    const char *html =
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head><title>LEDServer Setup</title></head>\n"
        "<body>\n"
        "<h2>LEDServer WiFi Setup</h2>\n"
        "<form action=\"/connect\" method=\"POST\">\n"
        "<label>SSID:</label><br>\n"
        "<input type=\"text\" name=\"ssid\" required><br>\n"
        "<label>Password:</label><br>\n"
        "<input type=\"password\" name=\"password\"><br><br>\n"
        "<button type=\"submit\">Connect</button>\n"
        "</form>\n"
        "</body>\n"
        "</html>\n";

    int len = snprintf(buf, buf_size,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n"
        "%s",
        (int)strlen(html), html);

    return len;
}
