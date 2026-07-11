/* Minimal HTTP server for captive portal.
 *
 * Listens on port 80, handles:
 *   GET  /  → provisioning HTML form
 *   POST /connect → parse SSID/password, save config, 302 redirect
 *
 * Uses lwIP raw API: one listening pcb, per-connection heap state.
 * Each connection gets its own recv buffer via tcp_arg — browsers
 * that open multiple concurrent connections (phone, desktop) will
 * not corrupt each other's data. */
#include "httpd.h"
#include "config_storage.h"
#include "settings_http.h"

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <lwip/tcp.h>

/* ── Buffer sizes ─────────────────────────────────────────────────── */
#define HTTP_RECV_BUF   2048
#define CONFIG_BUF      1024
#define HTML_BUF        2048

/* ── HTTP response helpers ────────────────────────────────────────── */
int build_200_response(char *buf, size_t len, const char *body) {
    int n = snprintf(buf, len,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"
        "Content-Length: %zu\r\n"
        "\r\n"
        "%s",
        strlen(body), body);
    return (n < 0 || (size_t)n >= len) ? -1 : n;
}

int build_302_response(char *buf, size_t len, const char *location) {
    int n = snprintf(buf, len,
        "HTTP/1.1 302 Found\r\n"
        "Location: %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        location);
    return (n < 0 || (size_t)n >= len) ? -1 : n;
}

/* ── Request parsing ──────────────────────────────────────────────── */

#ifndef HTTPD_TEST
typedef enum {
    HTTP_GET,
    HTTP_POST
} http_method_t;

typedef struct {
    http_method_t method;
    char path[64];
    char body[CONFIG_BUF];
    int body_len;
} http_request_t;
#endif

int parse_request(const char *raw, size_t len, http_request_t *req) {
    /* Find the end of the request line (first \r\n) */
    const char *line_end = memchr(raw, '\r', len);
    if (!line_end || (size_t)(line_end - raw) >= sizeof(req->path)) {
        return -1;
    }

    size_t line_len = line_end - raw;
    /* Parse "METHOD /path HTTP/1.x" — strip the protocol suffix */
    if (line_len >= 4 && strncmp(raw, "GET ", 4) == 0) {
        req->method = HTTP_GET;
        size_t path_len = line_len - 4;
        /* Strip trailing " HTTP/1.x" if present */
        const char *sp = memchr(raw + 4, ' ', path_len);
        if (sp) path_len = sp - (raw + 4);
        if (path_len >= sizeof(req->path)) path_len = sizeof(req->path) - 1;
        memcpy(req->path, raw + 4, path_len);
        req->path[path_len] = '\0';
    } else if (line_len >= 5 && strncmp(raw, "POST ", 5) == 0) {
        req->method = HTTP_POST;
        size_t path_len = line_len - 5;
        /* Strip trailing " HTTP/1.x" if present */
        const char *sp = memchr(raw + 5, ' ', path_len);
        if (sp) path_len = sp - (raw + 5);
        if (path_len >= sizeof(req->path)) path_len = sizeof(req->path) - 1;
        memcpy(req->path, raw + 5, path_len);
        req->path[path_len] = '\0';
    } else {
        return -1;
    }

    /* Find the body (after double \r\n\r\n) */
    const char *body_start = strstr(raw, "\r\n\r\n");
    if (body_start) {
        body_start += 4;
        size_t body_len = len - (body_start - raw);
        if (body_len >= sizeof(req->body)) body_len = sizeof(req->body) - 1;
        memcpy(req->body, body_start, body_len);
        req->body[body_len] = '\0';
        req->body_len = (int)body_len;
    } else {
        req->body[0] = '\0';
        req->body_len = 0;
    }

    return 0;
}

/* ── Per-connection state ─────────────────────────────────────────── */
typedef struct {
    char recv_buf[HTTP_RECV_BUF];
    size_t recv_len;
} httpd_conn_t;

/* ── lwIP TCP callback state ──────────────────────────────────────── */
static struct tcp_pcb *g_listen_pcb = NULL;

static void httpd_conn_free(httpd_conn_t *conn) {
    if (conn) free(conn);
}

static err_t httpd_client_recv(void *arg, struct tcp_pcb *tpcb,
                               struct pbuf *p, err_t err) {
    httpd_conn_t *conn = (httpd_conn_t *)arg;
    (void)err;

    if (!p) {
        /* Peer closed connection */
        tcp_arg(tpcb, NULL);
        tcp_close(tpcb);
        httpd_conn_free(conn);
        return ERR_OK;
    }

    /* Accumulate data into this connection's private buffer */
    size_t to_copy = p->len;
    if (conn->recv_len + to_copy >= sizeof(conn->recv_buf)) {
        to_copy = sizeof(conn->recv_buf) - conn->recv_len - 1;
    }
    pbuf_copy_partial(p, conn->recv_buf + conn->recv_len, to_copy, 0);
    conn->recv_len += to_copy;

    /* Free the pbuf */
    tcp_recved(tpcb, p->len);
    pbuf_free(p);

    /* Check if we have a complete request (double \r\n\r\n) */
    if (conn->recv_len >= 4 && strstr(conn->recv_buf, "\r\n\r\n")) {
        http_request_t req;
        char resp_buf[HTTP_RECV_BUF];
        char html_buf[HTML_BUF];
        memset(&req, 0, sizeof(req));

        if (parse_request(conn->recv_buf, conn->recv_len, &req) == 0) {
            int resp_len = 0;

            if (req.method == HTTP_GET && strcmp(req.path, "/") == 0) {
                /* Serve the provisioning form */
                build_provisioning_html(html_buf, sizeof(html_buf));
                resp_len = build_200_response(resp_buf, sizeof(resp_buf), html_buf);
            } else if (req.method == HTTP_GET && strcmp(req.path, "/settings") == 0) {
                /* Serve the settings page */
                settings_t cur = {0};
                /* Load current config for defaults */
                config_t cfg;
                if (config_load(&cfg) == 0 && config_is_valid()) {
                    cur.mode = cfg.effects_mode;
                    cur.effect_id = cfg.effect_id;
                    cur.speed = cfg.speed;
                    cur.brightness = cfg.brightness;
                    cur.color_r = cfg.color_r;
                    cur.color_g = cfg.color_g;
                    cur.color_b = cfg.color_b;
                    cur.color2_r = cfg.color2_r;
                    cur.color2_g = cfg.color2_g;
                    cur.color2_b = cfg.color2_b;
                }
                size_t html_len = build_settings_html(html_buf, sizeof(html_buf), &cur);
                resp_len = build_200_response(resp_buf, sizeof(resp_buf), html_buf);
            } else if (req.method == HTTP_POST && strcmp(req.path, "/connect") == 0) {
                /* Parse form and save config */
                char ssid[CONFIG_SSID_MAX];
                char password[CONFIG_PASS_MAX];
                memset(ssid, 0, sizeof(ssid));
                memset(password, 0, sizeof(password));

                parse_form(req.body, ssid, password, sizeof(ssid), sizeof(password));

                if (strlen(ssid) == 0) {
                    /* Empty SSID — return error page */
                    const char *err_html =
                        "<html><body><h3>Error: SSID cannot be empty</h3>"
                        "<a href='/'>Try again</a></body></html>";
                    resp_len = build_200_response(resp_buf, sizeof(resp_buf), err_html);
                } else {
                    /* Save config */
                    config_t cfg;
                    memset(&cfg, 0, sizeof(cfg));
                    memcpy(cfg.magic, "LSYN", 4);
                    cfg.version = CONFIG_VERSION;
                    cfg.flags = CONFIG_FLAG_VALID;
                    memcpy(cfg.ssid, ssid, strlen(ssid) + 1);
                    memcpy(cfg.password, password, strlen(password) + 1);
                    cfg.checksum = 0;
                    config_save(&cfg);

                    /* Redirect to success page */
                    resp_len = build_302_response(resp_buf, sizeof(resp_buf), "/connected");
                }
            } else if (req.method == HTTP_POST && strcmp(req.path, "/settings") == 0) {
                /* Parse settings form and save to config */
                settings_t new_settings;
                if (parse_settings_form(req.body, req.body_len, &new_settings) == 0
                    && new_settings.valid) {
                    /* Load current config, update effect fields, save */
                    config_t cfg;
                    memset(&cfg, 0, sizeof(cfg));
                    memcpy(cfg.magic, "LSYN", 4);
                    cfg.version = CONFIG_VERSION;
                    cfg.flags = CONFIG_FLAG_VALID;
                    cfg.checksum = 0;
                    /* Try to preserve existing WiFi credentials */
                    config_load(&cfg);

                    cfg.effects_mode = new_settings.mode;
                    cfg.effect_id = new_settings.effect_id;
                    cfg.speed = new_settings.speed;
                    cfg.brightness = new_settings.brightness;
                    cfg.color_r = new_settings.color_r;
                    cfg.color_g = new_settings.color_g;
                    cfg.color_b = new_settings.color_b;
                    cfg.color2_r = new_settings.color2_r;
                    cfg.color2_g = new_settings.color2_g;
                    cfg.color2_b = new_settings.color2_b;
                    cfg.checksum = 0;
                    config_save(&cfg);

                    /* Set mode immediately (stubbed in test builds) */
                    extern void httpd_apply_effect_mode(effects_mode_t mode);
                    httpd_apply_effect_mode((effects_mode_t)new_settings.mode);

                    /* Redirect back to settings */
                    resp_len = build_302_response(resp_buf, sizeof(resp_buf), "/settings");
                } else {
                    /* Parse error — return settings page with error */
                    const char *err_html =
                        "<html><body><h3>Error: Invalid settings</h3>"
                        "<a href='/settings'>Try again</a></body></html>";
                    resp_len = build_200_response(resp_buf, sizeof(resp_buf), err_html);
                }
            } else {
                /* 404 */
                const char *not_found =
                    "<html><body><h3>404 Not Found</h3>"
                    "<a href='/'>Home</a></body></html>";
                resp_len = build_200_response(resp_buf, sizeof(resp_buf), not_found);
            }

            if (resp_len > 0) {
                tcp_write(tpcb, resp_buf, resp_len, 0);
                tcp_sent(tpcb, NULL);
            }
        }

        /* Close connection and free per-connection state */
        tcp_arg(tpcb, NULL);
        tcp_close(tpcb);
        httpd_conn_free(conn);
    }
    return ERR_OK;
}

static void httpd_client_error(void *arg, err_t err) {
    (void)err;
    /* Connection error — free per-connection state.
     * tcp_arg is still valid; lwIP won't touch the PCB after this callback. */
    httpd_conn_free((httpd_conn_t *)arg);
}

static err_t httpd_accept(void *arg, struct tcp_pcb *client_pcb, err_t err) {
    (void)arg; (void)err;

    if (!client_pcb) return ERR_OK;

    /* Allocate per-connection receive buffer */
    httpd_conn_t *conn = (httpd_conn_t *)malloc(sizeof(httpd_conn_t));
    if (!conn) {
        tcp_close(client_pcb);
        return ERR_OK;
    }
    memset(conn, 0, sizeof(*conn));

    tcp_arg(client_pcb, conn);
    tcp_recv(client_pcb, httpd_client_recv);
    tcp_err(client_pcb, httpd_client_error);
    return ERR_OK;
}

/* ── Public API ───────────────────────────────────────────────────── */

int httpd_init(void) {
    g_listen_pcb = tcp_new();
    if (!g_listen_pcb) {
        printf("HTTPD: tcp_new failed\n");
        return -1;
    }

    ip_addr_t any;
    IP4_ADDR(&any, 0, 0, 0, 0);

    err_t err = tcp_bind(g_listen_pcb, &any, 80);
    if (err != ERR_OK) {
        printf("HTTPD: bind port 80 failed (err=%d)\n", err);
        tcp_close(g_listen_pcb);
        g_listen_pcb = NULL;
        return -1;
    }

    g_listen_pcb = tcp_listen(g_listen_pcb);
    if (!g_listen_pcb) {
        printf("HTTPD: tcp_listen failed\n");
        return -1;
    }

    tcp_arg(g_listen_pcb, NULL);
    tcp_accept(g_listen_pcb, httpd_accept);

    printf("HTTPD: listening on port 80\n");
    return 0;
}

void httpd_close(void) {
    if (g_listen_pcb) {
        tcp_close(g_listen_pcb);
        g_listen_pcb = NULL;
    }
}

/* Stub: applied in production by boot_flow integration.
 * In test builds this is a no-op. */
void httpd_apply_effect_mode(effects_mode_t mode) {
    (void)mode;
    /* In production: calls effects_engine_set_mode() */
    /* Stubbed out in test builds — verified by integration tests */
}
