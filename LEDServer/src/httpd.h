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
    int16_t fx;      /* WLED effect id (0..EFFECT_COUNT-1); -1 = client didn't send one */
    uint8_t speed;   /* WLED seg[0].sx — effect speed (0-255) */
    uint8_t color2_r; /* WLED seg[0].col[1] — secondary colour (chase bg, sparkle base) */
    uint8_t color2_g;
    uint8_t color2_b;
    uint8_t changed; /* bitmask (WLED_CHANGED_*) of fields the client actually sent */
} wled_state_t;

/* Which fields an incoming WLED update carried. parse_wled_state sets the
 * bits; apply_wled_state uses them so it never acts on stale merged state
 * (e.g. a brightness-only POST must not re-select the effect, and an
 * {on:false} POST must always power off even if on was already 0). */
#define WLED_CHANGED_ON    0x01
#define WLED_CHANGED_BRI   0x02
#define WLED_CHANGED_COL   0x04
#define WLED_CHANGED_FX    0x08
#define WLED_CHANGED_SPEED 0x10
#define WLED_CHANGED_COL2  0x20

/* Report the device's IP in GET /json/info (the WLED app's identity). */
void httpd_set_device_ip(const char *ip);

/* Report the board-id-derived MAC/deviceId in /json/info (uppercase hex). */
void httpd_set_device_id(const char *id);

/* Build the /json/cfg object (LED count, pin, wifi.ip). Returns length or -1. */
int build_wled_cfg_json(char *buf, size_t len);

/* AP captive-portal mode (default 1): GET / serves the WiFi provisioning
 * form. STA mode (0): GET / serves the WLED-style control page instead —
 * the WLED app embeds http://<ip>/ as its control screen, so STA must
 * serve real controls there. Set by the firmware once the boot mode is
 * known (LEDServer.cpp STA path → 0; AP path keeps the default 1). */
void httpd_set_portal_mode(int portal);

/* The flash-resident WLED-style control page served at GET / in STA mode
 * (self-contained HTML/JS: power, brightness, RGB colour + swatches, live
 * WS sync via GET /ws). Too large for the 1536-byte stack resp_buf, so it
 * is written directly via a dedicated header + body tcp_write path. */
const char *wled_control_page(void);

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
