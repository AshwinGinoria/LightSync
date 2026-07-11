#include "protocol_ddp.h"
#include "led_engine.h"
#include "effects_engine.h"
#include "lwip/udp.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int  tests_run     = 0;
static int  tests_failed  = 0;

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

extern uint8_t led_buffer[BUFFER_SIZE];
extern volatile uint8_t led_update_pending;

static struct udp_pcb *fake_pcb;
static void (*ddp_recv_callback)(void *, struct udp_pcb *, struct pbuf *,
                                  const ip_addr_t *, u16_t) = NULL;

struct udp_pcb *udp_new(void) {
    printf("  [stub] udp_new\n");
    fake_pcb = (struct udp_pcb *)malloc(1);
    printf("  [stub] udp_new = %p\n", (void*)fake_pcb);
    return fake_pcb;
}

err_t udp_bind(struct udp_pcb *pcb, const ip_addr_t *addr, u16_t port) {
    printf("  [stub] udp_bind\n");
    (void)pcb; (void)addr; (void)port;
    return ERR_OK;
}

void udp_recv(struct udp_pcb *pcb,
              void (*recv)(void *, struct udp_pcb *, struct pbuf *,
                           const ip_addr_t *, u16_t),
              void *arg) {
    printf("  [stub] udp_recv\n");
    (void)pcb; (void)arg;
    ddp_recv_callback = recv;
    printf("  [stub] callback stored = %p\n", (void*)ddp_recv_callback);
}

void udp_remove(struct udp_pcb *pcb) { (void)pcb; }

struct pbuf *pbuf_alloc(void *layer, uint16_t length, void *type) {
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

size_t pbuf_copy_partial(struct pbuf *p, void *dst, size_t len, size_t offset) {
    size_t avail = (p->tot_len > offset) ? (p->tot_len - offset) : 0;
    size_t copy = (len < avail) ? len : avail;
    if (copy > 0 && dst && p->payload) {
        memcpy(dst, (char *)p->payload + offset, copy);
    }
    return copy;
}

static void send_ddp_packet(uint8_t flags, uint16_t sequence,
                            uint16_t data_type, uint16_t source_id,
                            uint16_t frame_offset,
                            const uint8_t *pixel_data, uint16_t data_length) {
    uint16_t total_len = 11 + data_length;
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
    if (pixel_data && data_length > 0) {
        memcpy(&buf[11], pixel_data, data_length);
    }
    struct pbuf *p = pbuf_alloc(0, total_len, 0);
    if (!p) return;
    memcpy(p->payload, buf, total_len);
    printf("  [debug] sending packet: type=0x%04x offset=%d len=%d\n", data_type, frame_offset, data_length);
    if (ddp_recv_callback) {
        ddp_recv_callback(NULL, fake_pcb, p, NULL, 4048);
    }
    pbuf_free(p);
}

static void clear_led_buffer(void) {
    memset(led_buffer, 0, BUFFER_SIZE);
    led_update_pending = 0;
}

static void test_ddp_init_creates_listener(void) {
    TEST("DDP init creates a UDP listener");
    clear_led_buffer();
    printf("  [debug] calling protocol_ddp_init...\n");
    fflush(stdout);
    void *result = protocol_ddp_init();
    printf("  [debug] protocol_ddp_init returned %p\n", (void*)result);
    CHECK(result != NULL);
check_exit:;
}

static void test_ddp_valid_packet_updates_leds(void) {
    TEST("DDP valid RGB packet updates LEDs");
    clear_led_buffer();
    uint8_t pixels[9] = { 255, 0, 0, 0, 255, 0, 0, 0, 255 };
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

int main(void) {
    printf("=== DDP Debug ===\n");
    test_ddp_init_creates_listener();
    test_ddp_valid_packet_updates_leds();
    printf("\n%d tests run, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
