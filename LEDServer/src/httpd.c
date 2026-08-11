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
#include "led_engine.h"
#include "effects_engine.h"
#include "logger.h"
#include "websocket.h"

#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <lwip/tcp.h>

/* ── Buffer sizes ─────────────────────────────────────────────────── */
#define HTTP_RECV_BUF   1024    /* request buffer — WS handshake is ~400 bytes */
#define HTTP_RESP_BUF   1536    /* response buffer — HTML page + headers */
#define CONFIG_BUF      1024
#define HTML_BUF        1536    /* WLED /json combined (state+info) ~1.1 KB */

/* WebSocket (RFC 6455) buffer sizes. */
#define WS_RX_BUF       512     /* raw frame accumulator per WS connection */
#define WS_PAYLOAD_BUF  512     /* unmasked TEXT payload (WLED state ~350 B) */
#define WS_RESP_BUF     256     /* 101 Switching Protocols response */

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

/* HTTP 200 OK with JSON content type — used by the WLED JSON API. */
int build_json_response(char *buf, size_t len, const char *body) {
    int n = snprintf(buf, len,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: %zu\r\n"
        "\r\n"
        "%s",
        strlen(body), body);
    return (n < 0 || (size_t)n >= len) ? -1 : n;
}

/* Find a request header by name (case-insensitive), value trimmed.
 * Returns 0 and writes the value to out if found, else -1. */
static int httpd_get_header(const char *raw, size_t len, const char *name,
                            char *out, size_t out_cap) {
    if (!raw || !name || !out || out_cap == 0) return -1;
    size_t name_len = strlen(name);
    const char *p = raw;
    const char *end = raw + len;
    while (p < end) {
        const char *line_end = memchr(p, '\n', (size_t)(end - p));
        if (!line_end) line_end = end;
        size_t l = (size_t)(line_end - p);
        const char *q = p;
        if (l > 0 && q[l - 1] == '\r') l--;
        if (l > name_len + 1 &&
            strncasecmp(q, name, name_len) == 0 &&
            q[name_len] == ':') {
            const char *v = q + name_len + 1;
            const char *line_abs_end = q + l;
            while (v < line_abs_end && (*v == ' ' || *v == '\t')) v++;
            size_t vlen = (size_t)(line_abs_end - v);
            if (vlen >= out_cap) vlen = out_cap - 1;
            memcpy(out, v, vlen);
            out[vlen] = '\0';
            return 0;
        }
        p = line_end + 1;
    }
    return -1;
}

/* ── STA control page (Phase 2.6) ──────────────────────────────────────
 * The WLED Android app's full control screen (DeviceDetail → DeviceWebView)
 * is a WebView of the device's own web UI at http://<ip>/. Its device-list
 * row only has native on/off + brightness — every colour/effect control
 * lives on that embedded page. So in STA mode GET / must serve a real
 * WLED-style control page, not the AP WiFi-provisioning form.
 *
 * Memory constraint: HTTP_RESP_BUF / HTML_BUF are 1536 bytes and live on
 * the stack in the recv path, so this ~4.5 KB page is a flash-resident
 * static string written directly (small header + body) via
 * httpd_send_static_page() — no buffer bump, no extra stack. */
static int g_httpd_portal_mode = 1;  /* AP captive portal by default */

void httpd_set_portal_mode(int portal) {
    g_httpd_portal_mode = portal;
}

static const char wled_control_page_html[] =
    "<!doctype html><html lang='en'><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>LightSync</title><style>"
    "body{margin:0;background:#0f1419;color:#e6edf3;font-family:-apple-system,'Segoe UI',Roboto,sans-serif;padding:14px 14px 40px}"
    "h1{font-size:19px;margin:2px 0;letter-spacing:.3px}"
    ".sub{color:#8b949e;font-size:12px;margin:0 0 12px}"
    ".card{background:#161b22;border:1px solid #21262d;border-radius:12px;padding:14px 16px;margin-bottom:12px}"
    ".row{display:flex;align-items:center;justify-content:space-between}"
    ".lbl{font-size:13px;color:#c9d1d9;font-weight:600;margin:0 0 10px}"
    ".sw{position:relative;display:inline-block;width:56px;height:31px}"
    ".sw input{display:none}"
    ".sl{position:absolute;inset:0;background:#30363d;border-radius:31px;transition:.18s;cursor:pointer}"
    ".sl:before{content:'';position:absolute;width:23px;height:23px;left:4px;top:4px;background:#8b949e;border-radius:50%;transition:.18s}"
    ".sw input:checked+.sl{background:#238636}"
    ".sw input:checked+.sl:before{transform:translateX(25px);background:#fff}"
    "input[type=range]{-webkit-appearance:none;appearance:none;width:100%;height:26px;background:none;margin:2px 0}"
    "input[type=range]::-webkit-slider-runnable-track{height:6px;border-radius:3px;background:#30363d}"
    "input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:22px;height:22px;border-radius:50%;background:#58a6ff;margin-top:-8px;border:2px solid #0f1419}"
    ".bv{float:right;font-size:13px;color:#8b949e}"
    "input[type=color]{width:100%;height:44px;border:1px solid #30363d;border-radius:8px;background:#161b22;padding:3px;cursor:pointer;box-sizing:border-box}"
    "select{width:100%;height:38px;background:#0f1419;color:#e6edf3;border:1px solid #30363d;border-radius:8px;padding:0 8px;font-size:14px;box-sizing:border-box}"
    ".swatch{display:inline-block;width:36px;height:36px;border-radius:50%;border:2px solid #30363d;margin:8px 8px 0 0;cursor:pointer;box-sizing:border-box}"
    "body.off .card{opacity:.55}"
    "body.off input[type=color]{filter:grayscale(1)}"
    "#st{position:fixed;left:0;right:0;bottom:0;padding:6px;font-size:11px;text-align:center;color:#8b949e;background:#0f1419}"
    "</style></head><body>"
    "<h1>LightSync</h1><p class='sub' id='host'></p>"
    "<div class='card'>"
    "<div class='row'><p class='lbl' style='margin:0'>Power</p>"
    "<label class='sw'><input type='checkbox' id='on'><span class='sl'></span></label></div>"
    "<div id='br'>"
    "<p class='lbl'>Brightness <span class='bv' id='bv'>255</span></p>"
    "<input type='range' id='bri' min='0' max='255' value='255'>"
    "</div>"
    "</div>"
    "<div class='card' id='cc'>"
    "<p class='lbl'>Color</p>"
    "<input type='color' id='col' value='#ff2d2d'>"
    "<div id='sw'></div>"
    "<div id='c2w'>"
    "<p class='lbl' style='margin-top:10px'>Color 2</p>"
    "<input type='color' id='c2' value='#000000'>"
    "</div>"
    "</div>"
    "<div class='card'>"
    "<p class='lbl'>Mode</p>"
    "<select id='fx'>"
    "<option value='0'>Solid</option>"
    "<option value='1'>Rainbow</option>"
    "<option value='2'>Pulse</option>"
    "<option value='3'>Chase</option>"
    "<option value='4'>Sparkle</option>"
    "<option value='5'>Theater Chase</option>"
    "<option value='6'>DDP (External)</option>"
    "</select>"
    "<div id='sp'>"
    "<p class='lbl' style='margin-top:10px'>Speed <span class='bv' id='sv'>128</span></p>"
    "<input type='range' id='sx' min='1' max='255' value='128'>"
    "</div>"
    "</div>"
    "<div id='st'>connecting…</div>"
    "<script>"
    "(function(){function g(id){return document.getElementById(id)}"
    "var pre=[[255,0,0],[255,128,0],[255,255,0],[0,255,0],[0,255,255],[0,0,255],[255,0,255],[255,255,255]];"
    "function p(x){x|=0;var s=x.toString(16);return s.length<2?'0'+s:s}"
    "function hex(r,g,b){return'#'+p(r)+p(g)+p(b)}"
    "function rgb(h){h=h.replace('#','');return[parseInt(h.substr(0,2),16),parseInt(h.substr(2,2),16),parseInt(h.substr(4,2),16)]}"
    "var col=g('col'),c2=g('c2');"
    "function apply(st){"
    "if(st&&st.on!==undefined){g('on').checked=!!st.on;document.body.className=st.on?'':'off'}"
    "if(st&&st.bri!==undefined){g('bri').value=st.bri;g('bv').textContent=st.bri}"
    "if(st&&st.seg&&st.seg[0]&&st.seg[0].col&&st.seg[0].col[0]){var c=st.seg[0].col[0];col.value=hex(c[0],c[1],c[2])}"
    "if(st&&st.seg&&st.seg[0]&&st.seg[0].col&&st.seg[0].col[1]){var d=st.seg[0].col[1];c2.value=hex(d[0],d[1],d[2])}"
    "if(st&&st.fx!==undefined){g('fx').value=st.fx;showControls(st.fx)}"
    "if(st&&st.sx!==undefined){g('sx').value=st.sx;g('sv').textContent=st.sx}"
    "}"
    "var masks=[0x0a,0x09,0x0b,0x0f,0x0f,0x0b,0x00];" /* EFFECT_PARAM_* per effects_engine.c registry (Solid..DDP) */
    "function showControls(fx){var m=(fx>=0&&fx<masks.length)?masks[fx]:0;"
    "function v(id,on){g(id).style.display=on?'':'none'}"
    "v('br',m&0x08);v('cc',m&0x02);v('c2w',m&0x04);v('sp',m&0x01)}"
    "function post(o){fetch('/json/state',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)}).catch(function(){})}"
    "g('on').onchange=function(){post({on:this.checked})};"
    "g('bri').oninput=function(){g('bv').textContent=this.value};"
    "g('bri').onchange=function(){post({bri:+this.value})};"
    "col.onchange=function(){post({seg:[{col:[rgb(this.value),rgb(c2.value)]}]})};"
    "c2.onchange=function(){post({seg:[{col:[rgb(col.value),rgb(this.value)]}]})};"
    "g('fx').onchange=function(){showControls(+this.value);post({fx:+this.value})};"
    "g('sx').oninput=function(){g('sv').textContent=this.value};"
    "g('sx').onchange=function(){post({sx:+this.value})};"
    "var sw=g('sw');"
    "for(var i=0;i<pre.length;i++){(function(c){var d=document.createElement('div');d.className='swatch';d.style.background=hex(c[0],c[1],c[2]);d.onclick=function(){col.value=hex(c[0],c[1],c[2]);post({seg:[{col:[c]}]})};sw.appendChild(d)})(pre[i])}"
    "showControls(+g('fx').value);"
    "g('host').textContent=location.host+' · LightSync';"
    "var st=g('st');"
    "fetch('/json/state').then(function(r){return r.json()}).then(apply).catch(function(){});"
    "try{var ws=new WebSocket('ws://'+location.host+'/ws');"
    "ws.onopen=function(){st.textContent='live'};"
    "ws.onmessage=function(e){st.textContent='live';try{apply(JSON.parse(e.data))}catch(_){}};"
    "ws.onclose=function(){st.textContent='reconnecting…';setTimeout(function(){location.reload()},4000)};"
    "ws.onerror=function(){st.textContent='ws error'};"
    "}catch(e){st.textContent='live (poll)'}"
    "})();</script></body></html>";

const char *wled_control_page(void) {
    return wled_control_page_html;
}

/* Write a full HTTP 200 response for a flash-resident static body without
 * copying it into the small stack resp_buf. The header is COPY-written; the
 * body is referenced in place (static const, never freed). Connection close
 * is handled by the caller's shared close path. */
static err_t httpd_send_static_page(struct tcp_pcb *tpcb, const char *body) {
    size_t body_len = strlen(body);
    if (body_len > 0xFFFF) return ERR_MEM;
    char hdr[160];
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"
        "Content-Length: %zu\r\n"
        "\r\n", body_len);
    if (hlen < 0 || (size_t)hlen >= sizeof(hdr)) return ERR_MEM;
    err_t e = tcp_write(tpcb, hdr, (u16_t)hlen, TCP_WRITE_FLAG_COPY);
    if (e != ERR_OK) return e;
    return tcp_write(tpcb, body, (u16_t)body_len, 0);
}

/* ── WLED JSON API (Phase 2) ─────────────────────────────────────────
 * The WLED mobile app connects to a device over HTTP using WLED's JSON
 * API on port 80. Manual IP entry still requires:
 *   GET  /json/info  → device identity (must contain "brand":"WLED")
 *   GET  /json/state → current power/brightness/colour
 *   POST /json/state → control on/bri/seg[0].col
 * We map that into the effects engine exactly like a UDP/DDP client:
 * a control message takes over the strip until the client goes silent. */

static wled_state_t g_wled_state = { 1, 255, 255, 0, 0, EFFECT_RAINBOW, 128, 0, 0, 0, 0 };
static char g_device_ip[16] = "0.0.0.0";
static char g_device_id[13] = "000000000000";  /* uppercase hex MAC, e.g. "D6C432C4A173" */

void httpd_set_device_ip(const char *ip) {
    if (!ip) return;
    strncpy(g_device_ip, ip, sizeof(g_device_ip) - 1);
    g_device_ip[sizeof(g_device_ip) - 1] = '\0';
}

void httpd_set_device_id(const char *id) {
    if (!id) return;
    strncpy(g_device_id, id, sizeof(g_device_id) - 1);
    g_device_id[sizeof(g_device_id) - 1] = '\0';
}

/* Minimal JSON field scanner: returns a pointer to the value after
 * "key":, picking the occurrence at the shallowest object depth so a
 * top-level "on"/"bri" wins over the copy nested inside "seg". NULL if
 * the key is absent. */
static const char *json_find_value(const char *json, const char *key) {
    if (!json) return NULL;
    int depth = 0;
    int best_depth = -1;
    const char *best = NULL;
    size_t keylen = strlen(key);
    const char *p = json;
    while (*p) {
        if (*p == '{' || *p == '[') {
            depth++;
        } else if (*p == '}' || *p == ']') {
            if (depth > 0) depth--;
        } else if (*p == '"') {
            const char *q = p + 1;
            while (*q && *q != '"') q++;
            size_t klen = (size_t)(q - (p + 1));
            if (klen == keylen && strncmp(p + 1, key, keylen) == 0) {
                const char *colon = q + 1;
                while (*colon == ' ' || *colon == '\t') colon++;
                if (*colon == ':') {
                    if (best_depth < 0 || depth < best_depth) {
                        best_depth = depth;
                        best = colon + 1;
                    }
                }
            }
            p = q; /* resume after the closing quote */
        }
        p++;
    }
    return best;
}

static int json_parse_bool(const char *v, int *out) {
    while (*v == ' ' || *v == '\t') v++;
    if (strncmp(v, "true", 4) == 0)  { *out = 1; return 0; }
    if (strncmp(v, "false", 5) == 0) { *out = 0; return 0; }
    return -1;
}

static int json_parse_int(const char *v, int *out) {
    while (*v == ' ' || *v == '\t') v++;
    int neg = 0;
    if (*v == '-') { neg = 1; v++; }
    if (*v < '0' || *v > '9') return -1;
    *out = 0;
    while (*v >= '0' && *v <= '9') { *out = *out * 10 + (*v - '0'); v++; }
    if (neg) *out = -*out;
    return 0;
}

/* Parse seg[0].col — an array of colours: [[r,g,b],[...],...].
 * Writes the first triplet to r/g/b and, if a second triplet is present,
 * writes it to r2/g2/b2. Returns the number of triplets parsed (0 on error). */
static int json_parse_col(const char *v, uint8_t *r, uint8_t *g, uint8_t *b,
                          uint8_t *r2, uint8_t *g2, uint8_t *b2) {
    while (*v == ' ' || *v == '\t') v++;
    if (*v != '[') return 0;          /* seg.col is an array of colours */
    v++;
    while (*v == ' ' || *v == '\t') v++;
    if (*v != '[') return 0;          /* first colour triplet */
    v++;
    int vals[3];
    for (int i = 0; i < 3; i++) {
        while (*v == ' ' || *v == '\t' || *v == ',') v++;
        if (*v < '0' || *v > '9') return 0;
        vals[i] = 0;
        while (*v >= '0' && *v <= '9') { vals[i] = vals[i] * 10 + (*v - '0'); v++; }
    }
    *r = (uint8_t)(vals[0] & 0xFF);
    *g = (uint8_t)(vals[1] & 0xFF);
    *b = (uint8_t)(vals[2] & 0xFF);

    /* Skip to the next triplet: ']' then optional ',' then '['. A malformed
     * or absent second triplet is fine — the primary colour is still valid. */
    while (*v == ' ' || *v == '\t') v++;
    if (*v != ']') return 1;
    v++;
    while (*v == ' ' || *v == '\t' || *v == ',') v++;
    if (*v != '[') return 1;
    v++;
    for (int i = 0; i < 3; i++) {
        while (*v == ' ' || *v == '\t' || *v == ',') v++;
        if (*v < '0' || *v > '9') return 1;
        vals[i] = 0;
        while (*v >= '0' && *v <= '9') { vals[i] = vals[i] * 10 + (*v - '0'); v++; }
    }
    *r2 = (uint8_t)(vals[0] & 0xFF);
    *g2 = (uint8_t)(vals[1] & 0xFF);
    *b2 = (uint8_t)(vals[2] & 0xFF);
    return 2;
}

/* Overlay a partial WLED state JSON onto *st. Returns # fields parsed.
 * The changed bitmask is re-derived from THIS payload only (never merged with
 * the previous mask), so apply_wled_state can tell exactly which fields the
 * client sent — a brightness-only POST must not re-select the effect. */
int parse_wled_state(const char *json, wled_state_t *st) {
    const char *val;
    int tmp;
    int n = 0;

    st->changed = 0;

    val = json_find_value(json, "on");
    if (val && json_parse_bool(val, &tmp) == 0) {
        st->on = (uint8_t)tmp;
        st->changed |= WLED_CHANGED_ON;
        n++;
    }

    val = json_find_value(json, "bri");
    if (val && json_parse_int(val, &tmp) == 0) {
        if (tmp < 0) tmp = 0;
        if (tmp > 255) tmp = 255;
        st->bri = (uint8_t)tmp;
        st->changed |= WLED_CHANGED_BRI;
        n++;
    }

    val = json_find_value(json, "col");
    if (val) {
        int ncol = json_parse_col(val, &st->color_r, &st->color_g, &st->color_b,
                                  &st->color2_r, &st->color2_g, &st->color2_b);
        if (ncol >= 1) {
            st->changed |= WLED_CHANGED_COL;
            n++;
        }
        if (ncol >= 2) {
            st->changed |= WLED_CHANGED_COL2;
            n++;
        }
    }

    val = json_find_value(json, "fx");
    if (val && json_parse_int(val, &tmp) == 0) {
        if (tmp < -1) tmp = -1;                 /* -1 = no onboard effect (external control) */
        /* Out-of-range WLED fx ids (the app sends its own 100+ effect list)
         * clamp to the LAST ONBOARD effect. fx=EFFECT_DDP (6) is deliberately
         * reachable — the control page's "DDP (External)" option posts it — so
         * only ids beyond the whole enum clamp, never into DDP by accident. */
        if (tmp > EFFECT_COUNT - 1) tmp = EFFECT_DDP - 1;
        st->fx = (int16_t)tmp;
        st->changed |= WLED_CHANGED_FX;
        n++;
    }

    val = json_find_value(json, "sx");
    if (val && json_parse_int(val, &tmp) == 0) {
        if (tmp < 0) tmp = 0;
        if (tmp > 255) tmp = 255;
        st->speed = (uint8_t)tmp;
        st->changed |= WLED_CHANGED_SPEED;
        n++;
    }

    return n;
}

/* Apply a WLED state to the strip.
 *
 * The changed bitmask (from parse_wled_state) decides what the client actually
 * sent, so a stale merged state can never re-trigger an action:
 *
 *   {on:false}  → blank the strip; if an AUTO effect is running, select
 *                 EFFECT_NONE so it cannot repaint the blank next frame.
 *                 g_wled_state.fx is untouched, so power-on can resume it.
 *   fx present  → run that onboard effect in AUTO mode (renders at 30 FPS,
 *                 ignores client silence).
 *   {on:true}   → if an AUTO effect was running, resume it (the engine was
 *                 paused with EFFECT_NONE at power-off).
 *   bri/col     → while an AUTO effect runs, re-apply it with the new values
 *                 (client_active is a no-op in AUTO mode, so without this the
 *                 change would be invisible); otherwise solid colour holds in
 *                 CLIENT mode until the app goes quiet.
 */
void apply_wled_state(const wled_state_t *st) {
    if ((st->changed & WLED_CHANGED_ON) && !st->on) {
        /* Power off: blank the strip, pause autonomous effects. */
        memset((uint8_t *)led_buffer, 0, LED_LENGTH * 3);
        led_update_pending = 1;
        if (effects_engine_get_mode() == EFFECT_MODE_AUTO) {
            effects_engine_set_effect(EFFECT_NONE, NULL);
        } else {
            effects_engine_client_active();
        }
        return;
    }

    /* Does the strip run an onboard effect? Either the client picked one now
     * (WLED_CHANGED_FX), one is already running (AUTO mode), or power-on is
     * resuming one paused by the off path above. */
    int auto_running = effects_engine_get_mode() == EFFECT_MODE_AUTO;
    int resume_auto  = (st->changed & WLED_CHANGED_ON) && auto_running;

    /* Speed slider moved (WLED sx): update the engine's speed in place — a
     * running AUTO effect picks it up next frame, and an idle CLIENT-mode
     * engine keeps it for the next effect selection. */
    if (st->changed & WLED_CHANGED_SPEED) {
        effects_engine_set_speed(st->speed);
    }

    if ((st->changed & WLED_CHANGED_FX) || resume_auto ||
        (auto_running && (st->changed & (WLED_CHANGED_BRI | WLED_CHANGED_COL)))) {
        effect_params_t params;
        memset(&params, 0, sizeof(params));
        params.speed      = effects_engine_get_speed();  /* preserve chosen speed */
        params.brightness = st->bri;
        params.color_r    = st->color_r;
        params.color_g    = st->color_g;
        params.color_b    = st->color_b;
        params.color2_r   = st->color2_r;   /* chase bg / sparkle base */
        params.color2_g   = st->color2_g;
        params.color2_b   = st->color2_b;
        effects_engine_set_mode(EFFECT_MODE_AUTO);
        effects_engine_set_effect((effect_id_t)st->fx, &params);
        return;
    }

    /* Solid colour / brightness from the app: fill the buffer directly and put
     * the engine in CLIENT mode so the colour holds until the app goes quiet. */
    led_strip_set_brightness(st->bri);
    uint32_t i;
    for (i = 0; i < LED_LENGTH; i++) {
        led_buffer[i * 3 + 0] = st->color_r;
        led_buffer[i * 3 + 1] = st->color_g;
        led_buffer[i * 3 + 2] = st->color_b;
    }
    led_update_pending = 1;
    effects_engine_client_active();
}

/* Inner builders (unwrapped) so GET /json can combine state + info. */
static int wled_state_object(char *buf, size_t len) {
    return snprintf(buf, len,
        "{\"on\":%s,\"bri\":%u,\"transition\":0,"
        "\"seg\":[{\"id\":0,\"start\":0,\"stop\":%d,\"len\":%d,"
        "\"col\":[[%u,%u,%u],[%u,%u,%u],[0,0,0]],"
        "\"fx\":%d,\"sx\":%u,\"ix\":128,\"pal\":0,"
        "\"sel\":true,\"on\":%s,\"bri\":%u}]}",
        g_wled_state.on ? "true" : "false", g_wled_state.bri,
        LED_LENGTH, LED_LENGTH,
        g_wled_state.color_r, g_wled_state.color_g, g_wled_state.color_b,
        g_wled_state.color2_r, g_wled_state.color2_g, g_wled_state.color2_b,
        g_wled_state.fx, effects_engine_get_speed(),
        g_wled_state.on ? "true" : "false", g_wled_state.bri);
}

/* Full field set matching real WLED 0.14.0 serializeInfo(). The WLED app
 * rejects a minimal info object (empirically proven via PC capture): it
 * needs the deviceId/mac, wifi{}, fs{} and freeheap fields to accept the
 * device. Values are Pico-real where we have them (mac/deviceId from the
 * board ID, ip from the DHCP lease); the rest are plausible constants. */
static int wled_info_object(char *buf, size_t len) {
    return snprintf(buf, len,
        "{\"ver\":\"0.14.0\",\"vid\":2102280,\"cn\":\"Tres\","
        "\"release\":\"0.14.0\",\"repo\":\"master\","
        "\"deviceId\":\"%s\",\"ndc\":-1,"
        "\"leds\":{\"count\":%d,\"pwr\":0,\"fps\":0,\"maxpwr\":0,"
        "\"maxseg\":16,\"bootps\":0,\"seglc\":[0],\"lc\":0,"
        "\"rgbw\":false,\"wv\":0,\"cct\":0},"
        "\"fs\":{\"u\":0,\"t\":0,\"pmt\":0},"
        "\"freeheap\":20000,\"uptime\":0,\"time\":\"\","
        "\"opt\":0,\"brand\":\"WLED\",\"product\":\"LightSync\","
        "\"mac\":\"%s\",\"ip\":\"%s\",\"name\":\"LightSync\","
        "\"arch\":\"rp2040\",\"core\":\"2.1.0\",\"clock\":133,"
        "\"flash\":2,\"lwip\":2,"
        "\"wifi\":{\"bssid\":\"%s\",\"rssi\":-50,\"signal\":80,"
        "\"channel\":6,\"band\":\"2.4GHz\",\"ap\":false},"
        "\"wledversion\":\"0.14.0\"}",
        g_device_id, LED_LENGTH, g_device_id, g_device_ip, g_device_id);
}

int build_wled_state_json(char *buf, size_t len) {
    /* Real WLED serves the state object UNWRAPPED on GET /json/state —
     * the app parses on/bri/seg at the top level. Only GET /json wraps it. */
    return wled_state_object(buf, len);
}

int build_wled_info_json(char *buf, size_t len) {
    /* Real WLED serves the info object UNWRAPPED on GET /json/info —
     * the app reads "brand":"WLED" at the top level to identify the device.
     * Only GET /json wraps it under {"info":...}. */
    return wled_info_object(buf, len);
}

/* Minimal /json/cfg — the app reads pin/count/wifi.ip from it. */
static int wled_cfg_object(char *buf, size_t len) {
    return snprintf(buf, len,
        "{\"count\":%d,\"pin\":[2],\"boot\":2,\"atype\":0,\"inq\":0,"
        "\"grouping\":1,\"spacing\":0,\"colseg\":0,\"colbri\":0,"
        "\"wifi\":{\"ip\":\"%s\"},\"wl\":0,\"wm\":0,\"ws\":-1,\"wh\":0,"
        "\"wf\":0,\"wb\":0,\"wc\":-1,\"dns\":0,\"ntp\":0,\"now\":0,"
        "\"nw\":0,\"nd\":30,\"ns\":3,\"nt\":0,\"nm\":0,\"nb\":0,\"nl\":0,"
        "\"np\":0,\"nq\":0,\"al\":0,\"ar\":0,\"as\":0,\"asnc\":0,"
        "\"um\":0,\"ud\":0,\"wledversion\":\"0.14.0\"}",
        LED_LENGTH, g_device_ip);
}

int build_wled_cfg_json(char *buf, size_t len) {
    return wled_cfg_object(buf, len);
}

int build_wled_combined_json(char *buf, size_t len) {
    char st[512], inf[1024];
    if (wled_state_object(st, sizeof(st)) < 0) return -1;
    if (wled_info_object(inf, sizeof(inf)) < 0) return -1;
    return snprintf(buf, len, "{\"state\":%s,\"info\":%s}", st, inf);
}

/* Seed reported state from stored config so the app's first GET matches
 * reality. Side-effect-free (no strip writes, no brightness change). */
static void wled_state_init(void) {
    config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    if (config_load(&cfg) == 0 && config_is_valid()) {
        g_wled_state.bri     = cfg.brightness ? cfg.brightness : 255;
        g_wled_state.color_r = cfg.color_r;
        g_wled_state.color_g = cfg.color_g;
        g_wled_state.color_b = cfg.color_b;
        /* A black primary colour (the zeroed config default) makes effect
         * selection invisible — chase/pulse/theater-chase paint their
         * foreground over the black background, and a bare {fx:N} POST (what
         * the control-page dropdown sends) would render black-on-black. Real
         * WLED ships a fresh-device default of white; seed white so the
         * effects selector is usable out of the box. */
        if (!g_wled_state.color_r && !g_wled_state.color_g && !g_wled_state.color_b) {
            g_wled_state.color_r = 255;
            g_wled_state.color_g = 255;
            g_wled_state.color_b = 255;
        }
        g_wled_state.color2_r = cfg.color2_r;   /* black default — chase bg / sparkle base */
        g_wled_state.color2_g = cfg.color2_g;
        g_wled_state.color2_b = cfg.color2_b;
        g_wled_state.fx      = (cfg.effect_id < EFFECT_COUNT) ? (int16_t)cfg.effect_id
                                                              : (int16_t)EFFECT_RAINBOW;
        g_wled_state.speed   = cfg.speed ? cfg.speed : 128;
        g_wled_state.on      = 1;
    }
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
    struct tcp_pcb *tpcb;        /* owning pcb — for peer-table removal from tcp_err */
    char recv_buf[HTTP_RECV_BUF];
    size_t recv_len;
    int is_websocket;            /* upgraded via RFC 6455 — connection stays open */
    char ws_rx_buf[WS_RX_BUF];   /* raw frame bytes, parsed by httpd_ws_process */
    size_t ws_rx_len;
} httpd_conn_t;

/* ── lwIP TCP callback state ──────────────────────────────────────── */
static struct tcp_pcb *g_listen_pcb = NULL;

/* Diagnostics: track connection lifecycle for hang debugging. */
static volatile int g_accept_count = 0;
static volatile int g_active_count = 0;
static volatile int g_complete_count = 0;

/* Live WebSocket peers. The WLED app keeps one WS channel open for control;
 * real WLED broadcasts the state JSON to every connected WS client after any
 * change (updateWS()), and the Android app confirms/keeps its sliders in sync
 * from that broadcast — without it the strip changes but the UI reverts.
 * Fixed array: the app never holds more than a couple of channels and the
 * pcb pool already caps concurrent TCP at 8. */
#define MAX_WS_PEERS 8
static struct tcp_pcb *g_ws_peers[MAX_WS_PEERS];

static void httpd_ws_add_peer(struct tcp_pcb *tpcb) {
    for (int i = 0; i < MAX_WS_PEERS; i++) {
        if (g_ws_peers[i] == NULL) {
            g_ws_peers[i] = tpcb;
            return;
        }
    }
    LOG_WARN(MOD_HTTPD, "ws: peer table full (%d)", MAX_WS_PEERS);
}

static void httpd_ws_remove_peer(struct tcp_pcb *tpcb) {
    for (int i = 0; i < MAX_WS_PEERS; i++) {
        if (g_ws_peers[i] == tpcb) {
            g_ws_peers[i] = NULL;
            return;
        }
    }
}

/* Push the current WLED state to every connected WS client (real WLED's
 * updateWS()). Sends an unmasked TEXT frame carrying the state JSON — the
 * WS frame wrapper is essential: the WLED app parses only framed payloads. */
static void httpd_ws_broadcast_state(void) {
    char json[HTTP_RESP_BUF];
    int n = build_wled_state_json(json, sizeof(json));
    if (n <= 0) return;
    uint8_t frame[HTTP_RESP_BUF + 8];
    int fn = ws_build_frame(WS_OPCODE_TEXT, (const uint8_t *)json, (size_t)n,
                            frame, sizeof(frame));
    if (fn <= 0) return;
    for (int i = 0; i < MAX_WS_PEERS; i++) {
        struct tcp_pcb *peer = g_ws_peers[i];
        if (!peer) continue;
        err_t wr = tcp_write(peer, frame, (u16_t)fn, 0);
        if (wr != ERR_OK) {
            LOG_WARN(MOD_HTTPD, "ws: broadcast tcp_write failed (err=%d)", wr);
        }
        tcp_sent(peer, NULL);
    }
}

static void httpd_conn_free(httpd_conn_t *conn) {
    LOG_DEBUG(MOD_HTTPD, "conn_free(%p)", (void*)conn);
    if (conn) free(conn);
}

/* ── WebSocket (RFC 6455) ────────────────────────────────────────────
 * The WLED app upgrades GET /ws into a persistent WebSocket used as its
 * realtime control channel. We implement the minimum the app needs:
 *   - RFC 6455 handshake (Sec-WebSocket-Accept, no permessage-deflate)
 *   - keep the connection open after the upgrade
 *   - echo CLOSE, answer PING→PONG, apply TEXT state frames
 * All framing is hand-rolled in websocket.c (SHA-1, base64, frame codec). */

static err_t httpd_ws_teardown(struct tcp_pcb *tpcb, httpd_conn_t *conn,
                               int echo_close) {
    if (echo_close) {
        /* Echo a close frame back (RFC 6455 §5.5.1): status 1000. */
        uint8_t frame[4];
        static const uint8_t normal_close[] = {0x03, 0xE8};
        int n = ws_build_frame(WS_OPCODE_CLOSE, normal_close, sizeof(normal_close),
                               frame, sizeof(frame));
        if (n > 0) tcp_write(tpcb, frame, n, 0);
    }
    LOG_DEBUG(MOD_HTTPD, "ws: teardown conn=%p", (void*)conn);
    httpd_ws_remove_peer(tpcb);
    tcp_arg(tpcb, NULL);
    g_active_count--;
    httpd_conn_free(conn);
    tcp_close(tpcb);
    return ERR_OK;
}

/* Process complete frames in conn->ws_rx_buf. Returns ERR_OK. On a
 * close frame or protocol error the connection is torn down. */
static err_t httpd_ws_process(struct tcp_pcb *tpcb, httpd_conn_t *conn) {
    while (conn->ws_rx_len >= 2) {
        uint8_t opcode;
        char payload[WS_PAYLOAD_BUF];
        size_t payload_len, frame_len;
        int r = ws_parse_frame((const uint8_t *)conn->ws_rx_buf, conn->ws_rx_len,
                               &opcode, (uint8_t *)payload, sizeof(payload),
                               &payload_len, &frame_len);
        if (r == 0) break;  /* partial frame — wait for more bytes */
        if (r < 0) {
            LOG_ERROR(MOD_HTTPD, "ws: protocol error (conn=%p)", (void*)conn);
            return httpd_ws_teardown(tpcb, conn, 1);
        }

        /* Consume this frame from the accumulator. */
        memmove(conn->ws_rx_buf, conn->ws_rx_buf + frame_len,
                conn->ws_rx_len - frame_len);
        conn->ws_rx_len -= frame_len;

        switch (opcode) {
        case WS_OPCODE_CLOSE:
            LOG_INFO(MOD_HTTPD, "ws: close frame — closing conn=%p", (void*)conn);
            return httpd_ws_teardown(tpcb, conn, 1);

        case WS_OPCODE_PING: {
            uint8_t pong[WS_PAYLOAD_BUF + 4];
            int n = ws_build_frame(WS_OPCODE_PONG, (const uint8_t *)payload,
                                   payload_len, pong, sizeof(pong));
            if (n > 0) tcp_write(tpcb, pong, n, 0);
            LOG_DEBUG(MOD_HTTPD, "ws: ping -> pong (%u bytes)", (unsigned)payload_len);
            break;
        }

        case WS_OPCODE_TEXT: {
            LOG_INFO(MOD_HTTPD, "ws: text frame: %.*s", (int)payload_len, payload);
            wled_state_t st = g_wled_state;
            int nf = parse_wled_state(payload, &st);
            if (nf > 0) {
                LOG_INFO(MOD_HTTPD, "ws: applied %d fields (on=%d bri=%u col=%u,%u,%u)",
                         nf, st.on, st.bri, st.color_r, st.color_g, st.color_b);
                apply_wled_state(&st);
                g_wled_state = st;
                httpd_ws_broadcast_state();
            } else {
                LOG_WARN(MOD_HTTPD, "ws: text frame had no known fields");
            }
            break;
        }

        case WS_OPCODE_BINARY:
        case WS_OPCODE_PONG:
        case WS_OPCODE_CONT:
        default:
            LOG_DEBUG(MOD_HTTPD, "ws: opcode 0x%x len=%u ignored",
                      opcode, (unsigned)payload_len);
            break;
        }
    }
    return ERR_OK;
}

/* Respond 101 and flip the connection into WebSocket mode. The caller
 * must not free conn or close tpcb on success — the WS frame loop owns
 * the connection from here on. */
static err_t httpd_ws_handshake(struct tcp_pcb *tpcb, httpd_conn_t *conn) {
    char key[128];
    if (httpd_get_header(conn->recv_buf, conn->recv_len, "Sec-WebSocket-Key",
                         key, sizeof(key)) != 0) {
        LOG_ERROR(MOD_HTTPD, "ws: missing Sec-WebSocket-Key, rejecting");
        return ERR_VAL;
    }
    char accept[64];
    if (ws_compute_accept(key, accept, sizeof(accept)) != 0) {
        LOG_ERROR(MOD_HTTPD, "ws: accept computation failed");
        return ERR_VAL;
    }

    char resp[WS_RESP_BUF];
    int n = snprintf(resp, sizeof(resp),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n", accept);
    if (n < 0 || (size_t)n >= sizeof(resp)) return ERR_VAL;

    err_t wr = tcp_write(tpcb, resp, n, 0);
    if (wr != ERR_OK) {
        LOG_ERROR(MOD_HTTPD, "ws: handshake tcp_write failed (err=%d)", wr);
        return wr;
    }
    tcp_sent(tpcb, NULL);
    conn->is_websocket = 1;
    httpd_ws_add_peer(tpcb);
    LOG_INFO(MOD_HTTPD, "ws: upgraded conn=%p", (void*)conn);
    return ERR_OK;
}

static err_t httpd_client_recv(void *arg, struct tcp_pcb *tpcb,
                               struct pbuf *p, err_t err) {
    (void)err;

    /* Defensive: if arg is NULL, the connection was already closed.
     * Late packets (FIN/ACK from client after our tcp_close) arrive
     * on the PCB that lwIP is already cleaning up through the TCP
     * state machine.  We must NOT call tcp_abort() here — that would
     * allocate a new PCB (malloc 76) to send RST, creating a storm
     * when many connections close concurrently (8+ clients).
     * Instead, silently acknowledge the pbuf and let lwIP finish. */
    if (arg == NULL) {
        if (p) {
            tcp_recved(tpcb, p->len);
            pbuf_free(p);
        }
        return ERR_OK;
    }

    httpd_conn_t *conn = (httpd_conn_t *)arg;

    LOG_DEBUG(MOD_HTTPD, "recv_start: conn=%p, p=%p, err=%d", (void*)conn, (void*)p, err);
    LOG_DEBUG(MOD_HTTPD, "recv(%p, p=%p, err=%d)", (void*)conn, (void*)p, err);

    if (!p) {
        /* Peer closed connection */
        LOG_DEBUG(MOD_HTTPD, "recv: peer closed, closing pcb");
        httpd_ws_remove_peer(tpcb);
        tcp_arg(tpcb, NULL);
        tcp_close(tpcb);
        LOG_DEBUG(MOD_HTTPD, "recv: pcb closed, freeing conn");
        httpd_conn_free(conn);
        LOG_DEBUG(MOD_HTTPD, "recv: conn freed, returning");
        return ERR_OK;
    }

    /* WebSocket connections never go through the HTTP request path:
     * accumulate raw bytes and run the frame loop. */
    if (conn->is_websocket) {
        size_t avail = sizeof(conn->ws_rx_buf) - conn->ws_rx_len - 1;
        size_t to_copy = p->len;
        if (to_copy > avail) to_copy = avail;
        pbuf_copy_partial(p, conn->ws_rx_buf + conn->ws_rx_len, to_copy, 0);
        conn->ws_rx_len += to_copy;
        LOG_DEBUG(MOD_HTTPD, "recv: ws accumulate %u bytes (ws_len=%u)",
                  (unsigned)to_copy, (unsigned)conn->ws_rx_len);
        tcp_recved(tpcb, p->len);
        pbuf_free(p);
        return httpd_ws_process(tpcb, conn);
    }

    /* Accumulate data into this connection's private buffer */
    size_t to_copy = p->len;
    if (conn->recv_len + to_copy >= sizeof(conn->recv_buf)) {
        to_copy = sizeof(conn->recv_buf) - conn->recv_len - 1;
    }
    LOG_DEBUG(MOD_HTTPD, "recv: copy %u bytes (recv_len=%u -> %u)", to_copy, conn->recv_len, conn->recv_len + to_copy);
    pbuf_copy_partial(p, conn->recv_buf + conn->recv_len, to_copy, 0);
    conn->recv_len += to_copy;
    LOG_DEBUG(MOD_HTTPD, "recv: copy done, current len=%u", conn->recv_len);

    /* Free the pbuf */
    tcp_recved(tpcb, p->len);
    pbuf_free(p);

    /* Check if we have a complete request (double \r\n\r\n) */
    if (conn->recv_len >= 4 && strstr(conn->recv_buf, "\r\n\r\n")) {
        LOG_DEBUG(MOD_HTTPD, "recv: complete request found at len=%u", conn->recv_len);
        LOG_DEBUG(MOD_HTTPD, "recv: parsing request");
        http_request_t req;
        char resp_buf[HTTP_RESP_BUF];
        char html_buf[HTML_BUF];
        memset(&req, 0, sizeof(req));

        if (parse_request(conn->recv_buf, conn->recv_len, &req) == 0) {
            LOG_DEBUG(MOD_HTTPD, "recv: parsed %s %s", req.method == HTTP_GET ? "GET" : "POST", req.path);

            /* WebSocket upgrade: answer 101 and hand the connection to the
             * WS frame loop. Must NOT fall through to the close path below. */
            char up_val[16];
            if (httpd_get_header(conn->recv_buf, conn->recv_len, "Upgrade",
                                 up_val, sizeof(up_val)) == 0 &&
                strncasecmp(up_val, "websocket", 9) == 0) {
                err_t hserr = httpd_ws_handshake(tpcb, conn);
                if (hserr == ERR_OK) return ERR_OK;
                LOG_ERROR(MOD_HTTPD, "ws: handshake failed (err=%d), tearing down", hserr);
                return httpd_ws_teardown(tpcb, conn, 0);
            }

            int resp_len = 0;
            int wrote_static = 0;  /* static page written directly (no resp_buf) */

            if (req.method == HTTP_GET && strcmp(req.path, "/") == 0) {
                if (g_httpd_portal_mode) {
                    /* AP captive portal — WiFi provisioning form */
                    build_provisioning_html(html_buf, sizeof(html_buf));
                    resp_len = build_200_response(resp_buf, sizeof(resp_buf), html_buf);
                } else {
                    /* STA — WLED-style control UI. The WLED app embeds
                     * http://<ip>/ as its control screen, so this is what
                     * makes colour/effect controls appear in the app.
                     * Served straight from flash (too large for resp_buf). */
                    err_t se = httpd_send_static_page(tpcb, wled_control_page());
                    if (se != ERR_OK)
                        LOG_ERROR(MOD_HTTPD, "recv: static page write failed (err=%d)", se);
                    wrote_static = 1;
                }
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
                    LOG_DEBUG(MOD_HTTPD, "recv: config_save completed");

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
            } else if (req.method == HTTP_GET && strcmp(req.path, "/json/info") == 0) {
                /* WLED app device identification */
                build_wled_info_json(html_buf, sizeof(html_buf));
                resp_len = build_json_response(resp_buf, sizeof(resp_buf), html_buf);
            } else if (req.method == HTTP_GET && strcmp(req.path, "/json/state") == 0) {
                /* WLED current state */
                build_wled_state_json(html_buf, sizeof(html_buf));
                resp_len = build_json_response(resp_buf, sizeof(resp_buf), html_buf);
            } else if (req.method == HTTP_GET && strcmp(req.path, "/json") == 0) {
                /* Combined state + info (what WLED serves on GET /json) */
                build_wled_combined_json(html_buf, sizeof(html_buf));
                resp_len = build_json_response(resp_buf, sizeof(resp_buf), html_buf);
            } else if (req.method == HTTP_GET && strcmp(req.path, "/json/cfg") == 0) {
                /* WLED app reads pin/count/wifi.ip from cfg */
                build_wled_cfg_json(html_buf, sizeof(html_buf));
                resp_len = build_json_response(resp_buf, sizeof(resp_buf), html_buf);
            } else if (req.method == HTTP_POST &&
                       (strcmp(req.path, "/json/state") == 0 || strcmp(req.path, "/json") == 0)) {
                /* WLED control: overlay partial JSON, apply to strip */
                wled_state_t st = g_wled_state;
                int n = parse_wled_state(req.body, &st);
                LOG_INFO(MOD_HTTPD, "POST /json: parsed %d fields (on=%d bri=%u col=%u,%u,%u)",
                         n, st.on, st.bri, st.color_r, st.color_g, st.color_b);
                apply_wled_state(&st);
                g_wled_state = st;
                httpd_ws_broadcast_state();
                build_wled_state_json(html_buf, sizeof(html_buf));
                resp_len = build_json_response(resp_buf, sizeof(resp_buf), html_buf);
            } else {
                /* 404 */
                const char *not_found =
                    "<html><body><h3>404 Not Found</h3>"
                    "<a href='/'>Home</a></body></html>";
                resp_len = build_200_response(resp_buf, sizeof(resp_buf), not_found);
            }

            if (!wrote_static) {
                if (resp_len > 0) {
                    LOG_DEBUG(MOD_HTTPD, "recv: tcp_write(%d bytes)", resp_len);
                    err_t wr = tcp_write(tpcb, resp_buf, resp_len, 0);
                    if (wr != ERR_OK) {
                        LOG_ERROR(MOD_HTTPD, "recv: tcp_write FAILED (err=%d)", wr);
                    }
                    tcp_sent(tpcb, NULL);
                    LOG_DEBUG(MOD_HTTPD, "recv: tcp_write done");
                } else {
                    LOG_ERROR(MOD_HTTPD, "recv: response build failed (resp_len=%d, buf_size=%d)",
                              resp_len, (int)sizeof(resp_buf));
                }
            }
        }

        /* Close connection: free conn, then tcp_close.
         * tcp_close() sends FIN and lets lwIP clean up the PCB
         * gracefully through the TCP state machine — no heap corruption.
         * The response was already sent via tcp_write() so we don't
         * need to hold the connection open. */
        g_complete_count++;
        LOG_DEBUG(MOD_HTTPD, "recv(#%d): freeing conn (accept=%d complete=%d active=%d)",
                  g_complete_count, g_accept_count, g_complete_count, g_active_count);
        tcp_arg(tpcb, NULL);
        httpd_conn_free(conn);
        LOG_DEBUG(MOD_HTTPD, "recv: calling tcp_close(#%d)", g_complete_count);
        
        tcp_close(tpcb);
        g_active_count--;
        LOG_DEBUG(MOD_HTTPD, "recv(#%d): tcp_close returned (accept=%d complete=%d active=%d)",
                  g_complete_count, g_accept_count, g_complete_count, g_active_count);
    }
    return ERR_OK;
}

static void httpd_client_error(void *arg, err_t err) {
    (void)err;
    LOG_DEBUG(MOD_HTTPD, "client_error_entry: arg=%p, err=%d", arg, err);
    /* Connection error — the PCB is being torn down by lwIP.
     * Free per-connection state. tcp_err is called with the
     * arg set via tcp_arg(), so arg == conn.
     *
     * If arg is NULL, we already freed the conn before abort
     * (normal fast path) — safe to skip free(NULL).
     * If arg is non-NULL, the error is from lwIP itself before
     * we freed the conn — free it. */
    LOG_DEBUG(MOD_HTTPD, "client_error(arg=%p, active=%d)", arg, g_active_count);
    if (arg) {
        httpd_conn_t *conn = (httpd_conn_t *)arg;
        g_active_count--;
        httpd_ws_remove_peer(conn->tpcb);
        httpd_conn_free(conn);
    }
}

static err_t httpd_accept(void *arg, struct tcp_pcb *client_pcb, err_t err) {
    (void)arg; (void)err;
    LOG_DEBUG(MOD_HTTPD, "accept_entry: pcb=%p, err=%d", (void*)client_pcb, err);

    g_accept_count++;
    g_active_count++;
    LOG_DEBUG(MOD_HTTPD, "accept(#%d active=%d, pcb=%p, err=%d)",
              g_accept_count, g_active_count, (void*)client_pcb, err);

    if (!client_pcb) {
        g_active_count--;
        return ERR_OK;
    }

    /* Allocate per-connection receive buffer */
    httpd_conn_t *conn = (httpd_conn_t *)malloc(sizeof(httpd_conn_t));
    if (!conn) {
        g_active_count--;
        LOG_DEBUG(MOD_HTTPD, "accept(#%d): malloc(%d) failed, rejecting (active=%d free_heap=?)",
                  g_accept_count, (int)sizeof(httpd_conn_t), g_active_count);
        tcp_close(client_pcb);
        return ERR_OK;
    }
    memset(conn, 0, sizeof(*conn));
    LOG_DEBUG(MOD_HTTPD, "accept: conn=%p allocated", (void*)conn);

    /* Set arg BEFORE registering callbacks to avoid race condition
     * where data arrives before tcp_arg() is called. */
    tcp_arg(client_pcb, conn);
    conn->tpcb = client_pcb;
    tcp_recv(client_pcb, httpd_client_recv);
    tcp_err(client_pcb, httpd_client_error);
    LOG_DEBUG(MOD_HTTPD, "accept_end: registered callbacks (#%d)", g_accept_count);
    return ERR_OK;
}

/* ── Public API ───────────────────────────────────────────────────── */

int httpd_init(void) {
    wled_state_init();
    g_listen_pcb = tcp_new();
    if (!g_listen_pcb) {
        LOG_ERROR(MOD_HTTPD, "tcp_new failed");
        return -1;
    }

    ip_addr_t any;
    IP4_ADDR(&any, 0, 0, 0, 0);

    err_t err = tcp_bind(g_listen_pcb, &any, 80);
    if (err != ERR_OK) {
        LOG_ERROR(MOD_HTTPD, "bind port 80 failed (err=%d)", err);
        tcp_close(g_listen_pcb);
        g_listen_pcb = NULL;
        return -1;
    }

    g_listen_pcb = tcp_listen(g_listen_pcb);
    if (!g_listen_pcb) {
        LOG_ERROR(MOD_HTTPD, "tcp_listen failed");
        return -1;
    }

    tcp_arg(g_listen_pcb, NULL);
    tcp_accept(g_listen_pcb, httpd_accept);

    LOG_INFO(MOD_HTTPD, "listening on port 80");
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

#ifdef HTTPD_TEST
/* Test-only: clear the WS peer table so each test starts deterministic
 * (earlier tests register peers that never get torn down). */
void httpd_test_ws_reset_peers(void) {
    for (int i = 0; i < MAX_WS_PEERS; i++) g_ws_peers[i] = NULL;
}

/* Test-only wrappers exposing the static lwIP callbacks so native tests
 * can drive the full accept → recv → dispatch chain over the stub pcb. */
err_t httpd_test_accept(void *arg, struct tcp_pcb *pcb, err_t err) {
    return httpd_accept(arg, pcb, err);
}

err_t httpd_test_client_recv(void *arg, struct tcp_pcb *pcb,
                             struct pbuf *p, err_t err) {
    return httpd_client_recv(arg, pcb, p, err);
}
#endif
