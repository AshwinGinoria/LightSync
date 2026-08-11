/* Minimal HTTP server for captive portal.
 * Uses lwIP raw API with a single listening pcb.
 * Handles GET / (provisioning form) and POST /connect (form submission). */
#ifndef HTTPD_H
#define HTTPD_H

#include "provisioning_http.h"
#include "config_storage.h"

#include <stdint.h>
#include <lwip/udp.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Public API ────────────────────────────────────────────────────── */

/* Initialize HTTP server on port 80. Returns 0 on success. */
int httpd_init(void);

/* Close and free all HTTP server resources. */
void httpd_close(void);

/* ── WLED JSON API (Phase 2) ───────────────────────────────────────── */

/* State reported by /json/state and driven by POST /json/state. */
typedef struct {
    uint8_t on;      /* master power */
    uint8_t bri;     /* 0-255 global brightness */
    uint8_t color_r; /* static colour (WLED seg[0].col[0]) */
    uint8_t color_g;
    uint8_t color_b;
} wled_state_t;

/* Report the device's IP in GET /json/info (the WLED app's identity). */
void httpd_set_device_ip(const char *ip);

/* Report the board-id-derived MAC/deviceId in /json/info (uppercase hex). */
void httpd_set_device_id(const char *id);

/* Build the /json/cfg object (LED count, pin, wifi.ip). Returns length or -1. */
int build_wled_cfg_json(char *buf, size_t len);

/* ── Test-only declarations ────────────────────────────────────────── */
#ifdef HTTPD_TEST

/* Forward declarations so the wrapper signatures below refer to the same
 * structs lwIP's tcp.h/pbuf.h define — without these, `struct tcp_pcb`
 * inside a parameter list declares a NEW anonymous type distinct from the
 * one the definitions in httpd.c use. */
struct tcp_pcb;
struct pbuf;

typedef enum {
    HTTP_GET,
    HTTP_POST
} http_method_t;

typedef struct {
    http_method_t method;
    char path[64];
    char body[1024];
    int body_len;
} http_request_t;

/* Parse raw HTTP request into structured request. Returns 0 on success. */
int parse_request(const char *raw, size_t len, http_request_t *req);

/* Build HTTP 200 OK response. Returns length or -1 on error. */
int build_200_response(char *buf, size_t len, const char *body);

/* Build HTTP 302 Found response. Returns length or -1 on error. */
int build_302_response(char *buf, size_t len, const char *location);

/* Apply effect mode change from POST /settings. No-op in test builds. */
void httpd_apply_effect_mode(effects_mode_t mode);

/* HTTP 200 OK with JSON content type. Returns length or -1 on error. */
int build_json_response(char *buf, size_t len, const char *body);

/* WLED JSON API builders (wrapped bodies). Returns length or -1. */
int build_wled_state_json(char *buf, size_t len);
int build_wled_info_json(char *buf, size_t len);
int build_wled_combined_json(char *buf, size_t len);

/* Overlay partial WLED state JSON onto *st. Returns # fields parsed. */
int parse_wled_state(const char *json, wled_state_t *st);

/* Apply a WLED state to the strip (writes led_buffer, pauses effects). */
void apply_wled_state(const wled_state_t *st);

/* Drive the static lwIP callbacks directly (WebSocket + full recv chain). */
err_t httpd_test_accept(void *arg, struct tcp_pcb *pcb, err_t err);
err_t httpd_test_client_recv(void *arg, struct tcp_pcb *pcb,
                             struct pbuf *p, err_t err);

/* Test-only: clear the WS peer table (earlier tests register peers that
 * never get torn down, so the broadcast count would be nondeterministic). */
void httpd_test_ws_reset_peers(void);

#endif /* HTTPD_TEST */

#ifdef __cplusplus
}
#endif

#endif /* HTTPD_H */
