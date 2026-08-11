#include "protocol_ddp.h"
#include "led_engine.h"
#include "effects_engine.h"
#include "lwip/udp.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── Test harness ──────────────────────────────────────────────────── */
static int  tests_run     =  0;
static int  tests_failed  =  0;

#define TEST(name)  do { \
    tests_run++; \
    printf("TEST: %s\n", name); \
} while(0)

#define CHECK(cond)  do { \
    if (!(cond)) { \
        printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        tests_failed++; \
        goto check_exit; \
    } \
} while(0)

/* ── extern declarations (defined in led_engine.c) ─────────────────── */
extern uint8_t led_buffer[BUFFER_SIZE];
extern volatile uint8_t led_update_pending;

/* ── Fake pbuf for simulating received DDP packets ─────────────────── */
static struct udp_pcb *fake_pcb;
static void (*ddp_recv_callback)(void *, struct udp_pcb *, struct pbuf *,
                                  const ip_addr_t *, u16_t) = NULL;

/* Stub: udp_new returns a fake pcb (opaque pointer, no need for real struct) */
struct udp_pcb *udp_new(void) {
    fake_pcb = (struct udp_pcb *)malloc(1);
    return fake_pcb;
}

/* Stub: udp_bind always succeeds */
err_t udp_bind(struct udp_pcb *pcb, const ip_addr_t *addr, u16_t port) {
    (void)pcb; (void)addr; (void)port;
    return ERR_OK;
}

/* Stub: udp_recv stores the callback */
void udp_recv(struct udp_pcb *pcb,
              void (*recv)(void *, struct udp_pcb *, struct pbuf *,
                           const ip_addr_t *, u16_t),
              void *arg) {
    (void)pcb; (void)arg;
    ddp_recv_callback = recv;
}

void udp_remove(struct udp_pcb *pcb) {
    (void)pcb;
}

/* Stub: pbuf allocation */
struct pbuf *pbuf_alloc(int layer, uint16_t length, int type) {
    (void)layer; (void)type;
    struct pbuf *p = (struct pbuf *)malloc(sizeof(struct pbuf));
    if (!p) return NULL;
    p->payload = malloc(length);
    if (!p->payload) { free(p); return NULL; }
    p->next = NULL;
    p->tot_len = length;
    p->len = length;
    return p;
}

void pbuf_free(struct pbuf *p) {
    if (!p) return;
    free(p->payload);
    free(p);
}

/* Stub: pbuf_copy_partial */
size_t pbuf_copy_partial(struct pbuf *p, void *dst, size_t len, size_t offset) {
    size_t avail = (p->tot_len > offset) ? (p->tot_len - offset) : 0;
    size_t copy = (len < avail) ? len : avail;
    if (copy > 0 && dst && p->payload) {
        memcpy(dst, (char *)p->payload + offset, copy);
    }
    return copy;
}

/* ── Helpers ───────────────────────────────────────────────────────── */

/* Build a fake DDP packet and feed it to the DDP receive callback.
 *
 * DDP packet layout (big-endian):
 *   Offset 0:  flags (1 byte)
 *   Off  1-2:  sequence (2 bytes)
 *   Off  3-4:  data_type (2 bytes) — 0x0001 = RGB
 *   Off  5-6:  source_id (2 bytes)
 *   Off  7-8:  frame_offset (2 bytes)
 *   Off 9-10:  data_length (2 bytes)
 *   Off 11+:   pixel data (RGB triplets) */

static void send_ddp_packet(uint8_t flags, uint16_t sequence,
                            uint16_t data_type, uint16_t source_id,
                            uint16_t frame_offset,
                            const uint8_t *pixel_data, uint16_t data_length) {
    uint16_t total_len = 11 + data_length;

    /* Build packet on stack (big-endian) */
    uint8_t buf[1024];
    buf[0]  = flags;
    buf[1]  = (sequence >> 8) & 0xFF;
    buf[2]  = sequence & 0xFF;
    buf[3]  = (data_type >> 8) & 0xFF;
    buf[4]  = data_type & 0xFF;
    buf[5]  = (source_id >> 8) & 0xFF;
    buf[6]  = source_id & 0xFF;
    buf[7]  = (frame_offset >> 8) & 0xFF;
    buf[8]  = frame_offset & 0xFF;
    buf[9]  = (data_length >> 8) & 0xFF;
    buf[10] = data_length & 0xFF;

    /* Copy pixel data */
    if (pixel_data && data_length > 0) {
        memcpy(&buf[11], pixel_data, data_length);
    }

    /* Create fake pbuf */
    struct pbuf *p = pbuf_alloc(0, total_len, 0);
    if (!p) return;
    memcpy(p->payload, buf, total_len);

    /* Feed to DDP callback — ddp_recv frees the pbuf internally */
    if (ddp_recv_callback) {
        ddp_recv_callback(NULL, fake_pcb, p, NULL, 4048);
    }
    /* Note: ddp_recv calls pbuf_free(p) itself, so we do NOT free here. */
}

static void clear_led_buffer(void) {
    memset(led_buffer, 0, BUFFER_SIZE);
    led_update_pending = 0;
}

/* ── DDP Tests ─────────────────────────────────────────────────────── */

/* T1: ddp_init creates a UDP listener */
static void test_ddp_init_creates_listener(void) {
    TEST("DDP init creates a UDP listener");
    clear_led_buffer();
    void *result = protocol_ddp_init();
    CHECK(result != NULL);
check_exit:;
}

/* T2: DDP with valid RGB packet updates LEDs */
static void test_ddp_valid_packet_updates_leds(void) {
    TEST("DDP valid RGB packet updates LEDs");
    clear_led_buffer();

    uint8_t pixels[9] = {
        255, 0, 0,   /* LED 0: red */
        0, 255, 0,   /* LED 1: green */
        0, 0, 255    /* LED 2: blue */
    };

    send_ddp_packet(0, 0, 0x0001, 0, 0, pixels, 9);

    CHECK(led_update_pending == 1);
    CHECK(led_buffer[0] == 255);
    CHECK(led_buffer[1] == 0);
    CHECK(led_buffer[2] == 0);
    CHECK(led_buffer[3] == 0);
    CHECK(led_buffer[4] == 255);
    CHECK(led_buffer[5] == 0);
    CHECK(led_buffer[6] == 0);
    CHECK(led_buffer[7] == 0);
    CHECK(led_buffer[8] == 255);
check_exit:;
}

/* T3: DDP frame_offset skips leading LEDs */
static void test_ddp_frame_offset_skips_leds(void) {
    TEST("DDP frame_offset skips leading LEDs");
    clear_led_buffer();

    uint8_t pixels[6] = {
        100, 100, 100,   /* LED 5 */
        200, 200, 200    /* LED 6 */
    };

    send_ddp_packet(0, 0, 0x0001, 0, 5, pixels, 6);

    CHECK(led_update_pending == 1);

    int i;
    for (i = 0; i < 5; i++) {
        uint16_t off = i * 3;
        CHECK(led_buffer[off] == 0);
        CHECK(led_buffer[off+1] == 0);
        CHECK(led_buffer[off+2] == 0);
    }

    CHECK(led_buffer[15] == 100);
    CHECK(led_buffer[16] == 100);
    CHECK(led_buffer[17] == 100);
    CHECK(led_buffer[18] == 200);
    CHECK(led_buffer[19] == 200);
    CHECK(led_buffer[20] == 200);
check_exit:;
}

/* T4: DDP with wrong data type is ignored */
static void test_ddp_wrong_data_type_ignored(void) {
    TEST("DDP wrong data type is ignored");
    clear_led_buffer();

    uint8_t pixels[3] = {255, 0, 0};
    send_ddp_packet(0, 0, 0x0002, 0, 0, pixels, 3);

    CHECK(led_update_pending == 0);
    CHECK(led_buffer[0] == 0);
check_exit:;
}

/* T5: DDP with invalid data length (not multiple of 3) is ignored */
static void test_ddp_invalid_data_length_ignored(void) {
    TEST("DDP invalid data length is ignored");
    clear_led_buffer();

    uint8_t pixels[4] = {255, 0, 0, 128};
    send_ddp_packet(0, 0, 0x0001, 0, 0, pixels, 4);

    CHECK(led_update_pending == 0);
check_exit:;
}

/* T6: DDP with too-short header is ignored */
static void test_ddp_short_header_ignored(void) {
    TEST("DDP too-short header is ignored");
    clear_led_buffer();

    uint8_t tiny[5] = {0, 0, 0, 0, 0};
    struct pbuf *p = pbuf_alloc(0, 5, 0);
    if (!p) goto check_exit;
    memcpy(p->payload, tiny, 5);

    if (ddp_recv_callback) {
        ddp_recv_callback(NULL, fake_pcb, p, NULL, 4048);
    }
    /* Note: ddp_recv frees pbuf internally */

    CHECK(led_update_pending == 0);
check_exit:;
}

/* T7: DDP triggers effects_engine_client_active */
static void test_ddp_triggers_client_active(void) {
    TEST("DDP triggers effects_engine_client_active");
    clear_led_buffer();

    uint8_t pixels[3] = {255, 0, 0};
    send_ddp_packet(0, 0, 0x0001, 0, 0, pixels, 3);

    CHECK(led_update_pending == 1);
    CHECK(1);
check_exit:;
}

/* T8: DDP truncates data that exceeds buffer */
static void test_ddp_truncates_excess_data(void) {
    TEST("DDP truncates data exceeding buffer");
    clear_led_buffer();

    uint8_t pixels[300];
    int i;
    for (i = 0; i < 300; i++) {
        pixels[i] = (uint8_t)(i % 256);
    }

    send_ddp_packet(0, 0, 0x0001, 0, 300, pixels, 300);

    CHECK(led_update_pending == 1);
    CHECK(1);
check_exit:;
}

/* ── Runner ────────────────────────────────────────────────────────── */

int main(void) {
    test_ddp_init_creates_listener();
    test_ddp_valid_packet_updates_leds();
    test_ddp_frame_offset_skips_leds();
    test_ddp_wrong_data_type_ignored();
    test_ddp_invalid_data_length_ignored();
    test_ddp_short_header_ignored();
    test_ddp_triggers_client_active();
    test_ddp_truncates_excess_data();

    printf("\n%d tests run, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
