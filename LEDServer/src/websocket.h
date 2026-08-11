/* RFC 6455 WebSocket support — hand-rolled (no lwIP WebSocket builtin).
 *
 * The WLED mobile app connects to the device on port 80 and opens a
 * WebSocket on GET /ws (RFC 6455) as its realtime control channel:
 *   client → masked TEXT frames carrying JSON state updates
 *   server → unmasked TEXT/PING/PONG/CLOSE frames
 *
 * This module provides the pieces httpd.c needs on top of the lwIP raw
 * TCP API: SHA-1 + base64 (the Sec-WebSocket-Accept computation) and
 * unmasked-frame build / masked-frame parse. No permessage-deflate:
 * we simply never offer it in the handshake, and the WLED app falls
 * back to uncompressed frames. */
#ifndef WEBSOCKET_H
#define WEBSOCKET_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* RFC 6455 §1.3 — GUID appended to the client's Sec-WebSocket-Key before
 * SHA-1 when computing Sec-WebSocket-Accept. */
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/* Frame opcodes (RFC 6455 §5.2). */
#define WS_OPCODE_CONT   0x0
#define WS_OPCODE_TEXT   0x1
#define WS_OPCODE_BINARY 0x2
#define WS_OPCODE_CLOSE  0x8
#define WS_OPCODE_PING   0x9
#define WS_OPCODE_PONG   0xA

/* Compute the Sec-WebSocket-Accept value for a client handshake:
 * accept = base64( SHA1( sec_websocket_key + WS_GUID ) ).
 * sec_websocket_key must be the raw base64 string from the request.
 * Writes a NUL-terminated 28-char accept token to out.
 * Returns 0 on success, -1 on error. */
int ws_compute_accept(const char *sec_websocket_key, char *out, size_t out_cap);

/* Build a server→client frame (unmasked, FIN set) for opcode with payload.
 * Returns the total frame length, or -1 if out_cap is too small or the
 * payload needs an unsupported 64-bit length. */
int ws_build_frame(uint8_t opcode, const uint8_t *payload, size_t payload_len,
                   uint8_t *out, size_t out_cap);

/* Parse the first frame from data (which may be a partial buffer).
 * If complete: unmask the payload into payload, NUL-terminate it (so a
 * TEXT payload is a C string), set *opcode, *payload_len, *frame_len
 * (total bytes consumed) and return 1.
 * If more bytes are needed return 0 (incomplete).
 * Return -1 on a protocol error (payload won't fit).
 * payload_cap must be > the frame's payload length (room for the NUL). */
int ws_parse_frame(const uint8_t *data, size_t len,
                   uint8_t *opcode,
                   uint8_t *payload, size_t payload_cap, size_t *payload_len,
                   size_t *frame_len);

/* SHA-1 (exposed for tests) — digest is 20 bytes. */
void ws_sha1(const uint8_t *data, size_t len, uint8_t digest[20]);

#ifdef __cplusplus
}
#endif

#endif /* WEBSOCKET_H */
