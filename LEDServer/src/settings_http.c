/* Settings HTTP handler — form parser and HTML generator.
 *
 * Provides parse_settings_form() and build_settings_html() for the
 * captive portal settings page. */
#include "settings_http.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ── URL decoding (copied from provisioning_http for standalone use) ── */

static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int decode_url(const char *src, char *dst, size_t dst_len) {
    size_t i = 0, j = 0;
    while (src[i] && i < dst_len - 1) {
        if (src[i] == '%') {
            if (src[i + 1] && src[i + 2]) {
                int hi = hex_digit(src[i + 1]);
                int lo = hex_digit(src[i + 2]);
                if (hi < 0 || lo < 0) return 0;
                dst[j++] = (char)(hi * 16 + lo);
                i += 3;
            } else {
                return 0;
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

/* ── URL-encoded value extraction ──────────────────────────────────── */

/* Find "key=" in the body, return the value start (caller must free).
 * Returns NULL if key not found. */
static const char *find_value(const char *body, const char *key) {
    char search[64];
    snprintf(search, sizeof(search), "%s=", key);
    const char *p = strstr(body, search);
    if (!p) return NULL;
    p += strlen(search);
    return p;
}

/* Extract and decode a single URL-encoded value. */
static int extract_value(const char *body, const char *key,
                         char *dst, size_t dst_len) {
    const char *val = find_value(body, key);
    if (!val) return -1;

    const char *end = strchr(val, '&');
    size_t len = end ? (size_t)(end - val) : strlen(val);
    if (len == 0) {
        dst[0] = '\0';
        return 0;
    }
    if (len >= dst_len) len = dst_len - 1;

    char raw[256];
    if (len >= sizeof(raw)) return -1;
    memcpy(raw, val, len);
    raw[len] = '\0';

    if (!decode_url(raw, dst, dst_len)) return -1;
    return 0;
}

/* ── parse_settings_form ───────────────────────────────────────────── */

int parse_settings_form(const char *body, size_t body_len, settings_t *out) {
    char mode_str[8];
    char effect_str[8];
    char speed_str[8];
    char brightness_str[8];
    char cr_str[8], cg_str[8], cb_str[8];
    char cr2_str[8], cg2_str[8], cb2_str[8];

    /* Clear output */
    memset(out, 0, sizeof(*out));
    out->valid = 0;

    /* Validate body length */
    if (body_len == 0 || body_len > 512) return -1;

    /* Extract mode */
    if (extract_value(body, "mode", mode_str, sizeof(mode_str)) < 0) return -1;
    out->mode = (uint8_t)atoi(mode_str);
    if (out->mode > 1) return -1; /* 0=CLIENT, 1=AUTO only */

    /* Extract effect_id */
    if (extract_value(body, "effect_id", effect_str, sizeof(effect_str)) < 0) return -1;
    out->effect_id = (uint8_t)atoi(effect_str);
    if (out->effect_id >= 6) return -1; /* 6 effects max */

    /* Extract speed (1-255) */
    if (extract_value(body, "speed", speed_str, sizeof(speed_str)) < 0) return -1;
    out->speed = (uint8_t)atoi(speed_str);
    if (out->speed < 1) return -1;

    /* Extract brightness (0-255) */
    if (extract_value(body, "brightness", brightness_str, sizeof(brightness_str)) < 0) return -1;
    out->brightness = (uint8_t)atoi(brightness_str);
    if (out->brightness > 255) return -1;

    /* Extract primary color */
    if (extract_value(body, "color_r", cr_str, sizeof(cr_str)) < 0) return -1;
    if (extract_value(body, "color_g", cg_str, sizeof(cg_str)) < 0) return -1;
    if (extract_value(body, "color_b", cb_str, sizeof(cb_str)) < 0) return -1;
    out->color_r = (uint8_t)atoi(cr_str);
    out->color_g = (uint8_t)atoi(cg_str);
    out->color_b = (uint8_t)atoi(cb_str);

    /* Extract secondary color */
    if (extract_value(body, "color2_r", cr2_str, sizeof(cr2_str)) < 0) return -1;
    if (extract_value(body, "color2_g", cg2_str, sizeof(cg2_str)) < 0) return -1;
    if (extract_value(body, "color2_b", cb2_str, sizeof(cb2_str)) < 0) return -1;
    out->color2_r = (uint8_t)atoi(cr2_str);
    out->color2_g = (uint8_t)atoi(cg2_str);
    out->color2_b = (uint8_t)atoi(cb2_str);

    out->valid = 1;
    return 0;
}

/* ── build_settings_html ───────────────────────────────────────────── */

size_t build_settings_html(char *buf, size_t buf_size, const settings_t *cur) {
    uint8_t mode = cur ? cur->mode : 0;
    uint8_t effect = cur ? cur->effect_id : 0;
    uint8_t speed = cur ? cur->speed : 128;
    uint8_t brightness = cur ? cur->brightness : 200;
    uint8_t cr = cur ? cur->color_r : 255;
    uint8_t cg = cur ? cur->color_g : 0;
    uint8_t cb = cur ? cur->color_b : 0;
    uint8_t cr2 = cur ? cur->color2_r : 0;
    uint8_t cg2 = cur ? cur->color2_g : 128;
    uint8_t cb2 = cur ? cur->color2_b : 255;

    const char *effect_names[] = {
        "Solid", "Rainbow", "Pulse", "Chase", "Sparkle", "Theater Chase"
    };

    int off = 0;
    int added;

    added = snprintf(buf + off, buf_size - off, "<!DOCTYPE html>\n"
        "<html>\n"
        "<head><title>LEDServer Settings</title></head>\n"
        "<body>\n"
        "<h2>LEDServer Settings</h2>\n"
        "<form action=\"/settings\" method=\"POST\">\n"
        "<label>Mode:</label><br>\n"
        "<select name=\"mode\">\n"
        "<option value=\"0\"%s>Client Control</option>\n"
        "<option value=\"1\"%s>Autonomous Effects</option>\n"
        "</select><br><br>\n"
        "<label>Effect:</label><br>\n"
        "<select name=\"effect_id\">\n",
        mode == 0 ? " selected" : "",
        mode == 1 ? " selected" : "");

    if (added < 0) return 0;
    off += added;

    /* Effect options */
    for (int i = 0; i < 6; i++) {
        added = snprintf(buf + off, buf_size - off,
            "<option value=\"%d\"%s>%s</option>\n",
            i, i == (int)effect ? " selected" : "", effect_names[i]);
        if (added < 0) return 0;
        off += added;
    }

    /* Remaining form fields */
    added = snprintf(buf + off, buf_size - off,
        "</select><br><br>\n"
        "<label>Brightness (0-255):</label><br>\n"
        "<input type=\"range\" name=\"brightness\" min=\"0\" max=\"255\" "
        "value=\"%u\"><br>\n"
        "<span id=\"brightness_val\">%u</span><br><br>\n"
        "<label>Speed (1-255):</label><br>\n"
        "<input type=\"range\" name=\"speed\" min=\"1\" max=\"255\" "
        "value=\"%u\"><br>\n"
        "<span id=\"speed_val\">%u</span><br><br>\n"
        "<label>Primary Color:</label><br>\n"
        "<input type=\"color\" name=\"color_r\" value=\"#%02x%02x%02x\">\n"
        "<br><br>\n"
        "<label>Secondary Color:</label><br>\n"
        "<input type=\"color\" name=\"color2_r\" value=\"#%02x%02x%02x\">\n"
        "<br><br>\n"
        "<button type=\"submit\">Save</button>\n"
        "</form>\n"
        "<script>\n"
        "document.querySelector('[name=brightness]').addEventListener('input',\n"
        "  e => document.getElementById('brightness_val').textContent = e.target.value);\n"
        "document.querySelector('[name=speed]').addEventListener('input',\n"
        "  e => document.getElementById('speed_val').textContent = e.target.value);\n"
        "</script>\n"
        "</body>\n"
        "</html>\n",
        brightness, brightness, speed, speed,
        cr, cg, cb, cr2, cg2, cb2);

    if (added < 0) return 0;
    off += added;

    return (size_t)off;
}
