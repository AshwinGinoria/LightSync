#include "protocol_ddp.h"
#include "led_engine.h"
#include "effects_engine.h"
#include "logger.h"

#include "lwip/udp.h"

#include <stdio.h>
#include <string.h>

/* ── DDP constants ─────────────────────────────────────────────────── */

#define DDP_PORT             4048

/* DDP header (10 bytes, all network byte order / big-endian):
 *   Offset 0:  flags        (1 byte)
 *   Off  1-2:  sequence     (2 bytes, currently unused by us)
 *   Off  3-4:  data_type    (2 bytes — 0x0001 = 24-bit RGB)
 *   Off  5-6:  source_id    (2 bytes)
 *   Off  7-8:  frame_offset (2 bytes — starting pixel index)
 *   Off 9-10:  data_length  (2 bytes — number of bytes of pixel data) */
#define DDP_HEADER_SIZE      11   /* 10 bytes header + 1 byte spacer/alignment */

#define DDP_OFF_FLAGS        0
#define DDP_OFF_SEQUENCE     1
#define DDP_OFF_DATA_TYPE    3
#define DDP_OFF_SOURCE_ID    5
#define DDP_OFF_FRAME_OFFSET 7
#define DDP_OFF_DATA_LENGTH  9
#define DDP_OFF_PIXEL_DATA   11

#define DDP_DATA_TYPE_RGB    0x0001

#define DDP_RGB_BYTES        3

/* ── UDP receive callback ──────────────────────────────────────────── */

static void ddp_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                     const ip_addr_t *src, u16_t port) {
    (void)arg;
    (void)pcb;
    (void)src;
    (void)port;

    unsigned int len = p->tot_len;
    if (len < DDP_HEADER_SIZE) {
        pbuf_free(p);
        return;
    }

    /* Read header + data into a stack buffer */
    unsigned char buf[1024];
    size_t copy_len = pbuf_copy_partial(p, buf, sizeof(buf), 0);
    pbuf_free(p);

    /* Validate data type: must be 24-bit RGB per pixel */
    uint16_t data_type = ((uint16_t)buf[DDP_OFF_DATA_TYPE] << 8)
                       |  buf[DDP_OFF_DATA_TYPE + 1];
    if (data_type != DDP_DATA_TYPE_RGB) return;

    /* Validate data length: must be a multiple of 3 (one RGB triplet) */
    uint16_t data_length = ((uint16_t)buf[DDP_OFF_DATA_LENGTH] << 8)
                         |  buf[DDP_OFF_DATA_LENGTH + 1];
    if (data_length == 0 || (data_length % DDP_RGB_BYTES) != 0) return;

    /* Read frame offset (starting pixel index) */
    uint16_t frame_offset = ((uint16_t)buf[DDP_OFF_FRAME_OFFSET] << 8)
                          |  buf[DDP_OFF_FRAME_OFFSET + 1];

    /* Calculate destination offset in led_buffer */
    uint16_t dst_offset = (uint16_t)frame_offset * DDP_RGB_BYTES;
    if (dst_offset + data_length > BUFFER_SIZE) {
        /* Truncate to what fits */
        data_length = (BUFFER_SIZE > dst_offset) ? (BUFFER_SIZE - dst_offset) : 0;
        if (data_length == 0) return;
    }

    /* Copy pixel data directly — DDP sends raw RGB triplets */
    memcpy(&led_buffer[dst_offset], &buf[DDP_OFF_PIXEL_DATA], data_length);

    /* Receive visibility: rate-limited to ~1 line/sec (first frame always,
     * then every 30th) so a 30 fps stream doesn't flood the serial log. */
    static uint32_t ddp_frame_count;
    ddp_frame_count++;
    if (ddp_frame_count == 1 || (ddp_frame_count % 30) == 0) {
        LOG_INFO(MOD_DDP, "Received DDP frame #%lu off=%u len=%u",
                 (unsigned long)ddp_frame_count, frame_offset, data_length);
    }

    led_update_pending = 1;
    effects_engine_client_active();
}

/* ── Initialisation ────────────────────────────────────────────────── */

void *protocol_ddp_init(void) {
    struct udp_pcb *pcb = udp_new();
    if (!pcb) {
        LOG_ERROR(MOD_DDP, "udp_new failed");
        return NULL;
    }

    ip_addr_t addr;
    IP4_ADDR(&addr, 0, 0, 0, 0);
    err_t err = udp_bind(pcb, &addr, DDP_PORT);
    if (err != ERR_OK) {
        LOG_ERROR(MOD_DDP, "udp_bind port %d failed (err=%d)", DDP_PORT, err);
        udp_remove(pcb);
        return NULL;
    }

    udp_recv(pcb, ddp_recv, NULL);
    LOG_INFO(MOD_DDP, "listening on port %d", DDP_PORT);
    return (void *)pcb;
}
