/* RFC 6455 WebSocket helpers — hand-rolled SHA-1, base64, frame codec.
 * Small, malloc-free, sized for the WLED app's state frames (~350 B).
 * Verified against the RFC 6455 §1.3 example handshake in test_websocket.c. */
#include "websocket.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* SHA-1 (RFC 3174)                                                    */
/* ------------------------------------------------------------------ */

static uint32_t ws_rotl(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

void ws_sha1(const uint8_t *data, size_t len, uint8_t digest[20]) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    uint64_t bitlen = (uint64_t)len * 8;
    size_t padded = ((len + 8) / 64 + 1) * 64;

    /* Message padded with 0x80 ... 0x00 ... 64-bit bit-length (big endian).
     * We process the message in-place-looking chunks: copy into a fixed
     * scratch block since the input is usually not 64-byte aligned. */
    uint8_t block[64];
    size_t off = 0;

    while (off < padded) {
        size_t avail = len - (off < len ? off : len);
        size_t take = (padded - off) < 64 ? (padded - off) : 64;
        memset(block, 0, sizeof(block));

        size_t i = 0;
        if (avail > 0) {
            size_t copy = avail < take ? avail : take;
            memcpy(block, data + off, copy);
            i = copy;
        }
        if (i < 64 && (off + i) == len) {
            /* first padding byte */
            block[i] = 0x80;
        }
        if (off + take == padded) {
            /* last block: append the 64-bit length */
            block[56] = (uint8_t)(bitlen >> 56);
            block[57] = (uint8_t)(bitlen >> 48);
            block[58] = (uint8_t)(bitlen >> 40);
            block[59] = (uint8_t)(bitlen >> 32);
            block[60] = (uint8_t)(bitlen >> 24);
            block[61] = (uint8_t)(bitlen >> 16);
            block[62] = (uint8_t)(bitlen >> 8);
            block[63] = (uint8_t)(bitlen);
        }

        /* schedule */
        uint32_t w[80];
        for (int i = 0; i < 16; i++) {
            w[i] = ((uint32_t)block[i * 4] << 24) |
                   ((uint32_t)block[i * 4 + 1] << 16) |
                   ((uint32_t)block[i * 4 + 2] << 8) |
                   ((uint32_t)block[i * 4 + 3]);
        }
        for (int i = 16; i < 80; i++) {
            w[i] = ws_rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20)      { f = (b & c) | ((~b) & d);     k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d;                k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else             { f = b ^ c ^ d;                k = 0xCA62C1D6; }
            uint32_t tmp = ws_rotl(a, 5) + f + e + k + w[i];
            e = d; d = c; c = ws_rotl(b, 30); b = a; a = tmp;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
        off += take;
    }

    for (int i = 0; i < 5; i++) {
        digest[i * 4]     = (uint8_t)(h[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(h[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(h[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)(h[i]);
    }
}

/* ------------------------------------------------------------------ */
/* base64 (RFC 4648) — encode only (we never decode from the client)    */
/* ------------------------------------------------------------------ */

static const char ws_b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t ws_b64_encode(const uint8_t *src, size_t src_len,
                            char *out, size_t out_cap) {
    size_t out_len = ((src_len + 2) / 3) * 4;
    if (out_len + 1 > out_cap) return 0;

    size_t o = 0;
    size_t i = 0;
    while (i + 3 <= src_len) {
        uint32_t n = ((uint32_t)src[i] << 16) | ((uint32_t)src[i + 1] << 8) | src[i + 2];
        out[o++] = ws_b64_table[(n >> 18) & 0x3F];
        out[o++] = ws_b64_table[(n >> 12) & 0x3F];
        out[o++] = ws_b64_table[(n >> 6) & 0x3F];
        out[o++] = ws_b64_table[n & 0x3F];
        i += 3;
    }
    size_t rem = src_len - i;
    if (rem == 1) {
        uint32_t n = (uint32_t)src[i] << 16;
        out[o++] = ws_b64_table[(n >> 18) & 0x3F];
        out[o++] = ws_b64_table[(n >> 12) & 0x3F];
        out[o++] = '=';
        out[o++] = '=';
    } else if (rem == 2) {
        uint32_t n = ((uint32_t)src[i] << 16) | ((uint32_t)src[i + 1] << 8);
        out[o++] = ws_b64_table[(n >> 18) & 0x3F];
        out[o++] = ws_b64_table[(n >> 12) & 0x3F];
        out[o++] = ws_b64_table[(n >> 6) & 0x3F];
        out[o++] = '=';
    }
    out[o] = '\0';
    return o;
}

/* ------------------------------------------------------------------ */
/* Handshake accept                                                     */
/* ------------------------------------------------------------------ */

int ws_compute_accept(const char *sec_websocket_key, char *out, size_t out_cap) {
    if (!sec_websocket_key || !out) return -1;
    size_t key_len = strlen(sec_websocket_key);
    /* key (≤ 64) + GUID (36) + NUL */
    char concat[128];
    if (key_len + sizeof(WS_GUID) > sizeof(concat)) return -1;
    memcpy(concat, sec_websocket_key, key_len);
    memcpy(concat + key_len, WS_GUID, sizeof(WS_GUID)); /* includes NUL */

    uint8_t digest[20];
    ws_sha1((const uint8_t *)concat, key_len + sizeof(WS_GUID) - 1, digest);

    size_t n = ws_b64_encode(digest, 20, out, out_cap);
    return n > 0 ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* Frame codec                                                          */
/* ------------------------------------------------------------------ */

int ws_build_frame(uint8_t opcode, const uint8_t *payload, size_t payload_len,
                   uint8_t *out, size_t out_cap) {
    size_t hdr;
    if (payload_len < 126) {
        hdr = 2;
    } else if (payload_len <= 0xFFFF) {
        hdr = 4;
    } else {
        hdr = 10;
    }
    if (hdr + payload_len > out_cap) return -1;

    out[0] = 0x80 | (opcode & 0x0F); /* FIN + opcode */
    if (payload_len < 126) {
        out[1] = (uint8_t)payload_len;
    } else if (payload_len <= 0xFFFF) {
        out[1] = 126;
        out[2] = (uint8_t)(payload_len >> 8);
        out[3] = (uint8_t)(payload_len);
    } else {
        out[1] = 127;
        /* only 32-bit lengths are meaningful here; zero upper 4 bytes */
        out[2] = 0; out[3] = 0; out[4] = 0; out[5] = 0;
        out[6] = (uint8_t)(payload_len >> 24);
        out[7] = (uint8_t)(payload_len >> 16);
        out[8] = (uint8_t)(payload_len >> 8);
        out[9] = (uint8_t)(payload_len);
    }
    if (payload_len > 0 && payload) {
        memcpy(out + hdr, payload, payload_len);
    }
    return (int)(hdr + payload_len);
}

int ws_parse_frame(const uint8_t *data, size_t len,
                   uint8_t *opcode,
                   uint8_t *payload, size_t payload_cap, size_t *payload_len,
                   size_t *frame_len) {
    if (!data || !opcode || !payload || !payload_len || !frame_len) return -1;
    if (len < 2) return 0;

    *opcode = data[0] & 0x0F;
    uint8_t fin = data[0] & 0x80;
    (void)fin;
    int masked = (data[1] & 0x80) != 0;
    size_t plen = data[1] & 0x7F;
    size_t idx = 2;

    if (plen == 126) {
        if (len < 4) return 0;
        plen = ((size_t)data[2] << 8) | data[3];
        idx = 4;
    } else if (plen == 127) {
        if (len < 10) return 0;
        /* only 32-bit payload lengths supported */
        if (data[2] != 0 || data[3] != 0 || data[4] != 0 || data[5] != 0) return -1;
        plen = ((size_t)data[6] << 24) | ((size_t)data[7] << 16) |
               ((size_t)data[8] << 8) | data[9];
        idx = 10;
    }

    const uint8_t *mask = NULL;
    if (masked) {
        if (len < idx + 4) return 0;
        mask = data + idx;
        idx += 4;
    }

    if (plen >= payload_cap) return -1; /* need room for NUL */
    if (len < idx + plen) return 0;

    for (size_t i = 0; i < plen; i++) {
        payload[i] = mask ? (data[idx + i] ^ mask[i & 3]) : data[idx + i];
    }
    payload[plen] = '\0';

    *payload_len = plen;
    *frame_len = idx + plen;
    return 1;
}
