#ifndef SETTINGS_HTTP_H
#define SETTINGS_HTTP_H

#include <stdint.h>
#include <stddef.h>

/* ── Parsed settings from POST form ─────────────────────────────────── */

typedef struct {
    uint8_t  mode;          /* 0=CLIENT, 1=AUTO */
    uint8_t  effect_id;     /* 0..EFFECT_COUNT-1 */
    uint8_t  speed;         /* 1..255 */
    uint8_t  brightness;    /* 0..255 */
    uint8_t  color_r;       /* 0..255 */
    uint8_t  color_g;       /* 0..255 */
    uint8_t  color_b;       /* 0..255 */
    uint8_t  color2_r;      /* 0..255 */
    uint8_t  color2_g;      /* 0..255 */
    uint8_t  color2_b;      /* 0..255 */
    int      valid;         /* 1 if all fields parsed OK */
} settings_t;

/* ── HTML form generation ───────────────────────────────────────────── */

/* Build the complete HTML for the settings page into buf.
 * Returns the number of bytes written (excluding NUL).
 * buf_size is the total size of buf. */
size_t build_settings_html(char *buf, size_t buf_size, const settings_t *current);

/* ── POST form parsing ──────────────────────────────────────────────── */

/* Parse a URL-encoded POST body into settings.
 * body is the raw POST data (e.g. "mode=1&effect_id=0&...").
 * Returns 0 on success, -1 on parse error. */
int parse_settings_form(const char *body, size_t body_len, settings_t *out);

#endif /* SETTINGS_HTTP_H */
