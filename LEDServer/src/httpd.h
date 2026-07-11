/* Minimal HTTP server for captive portal.
 * Uses lwIP raw API with a single listening pcb.
 * Handles GET / (provisioning form) and POST /connect (form submission). */
#ifndef HTTPD_H
#define HTTPD_H

#include "provisioning_http.h"
#include "config_storage.h"

#include <lwip/udp.h>

/* ── Public API ────────────────────────────────────────────────────── */

/* Initialize HTTP server on port 80. Returns 0 on success. */
int httpd_init(void);

/* Close and free all HTTP server resources. */
void httpd_close(void);

/* ── Test-only declarations ────────────────────────────────────────── */
#ifdef HTTPD_TEST

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

#endif /* HTTPD_TEST */

#endif /* HTTPD_H */
