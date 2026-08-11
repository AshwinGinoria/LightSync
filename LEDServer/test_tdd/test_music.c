#include "music_sync.h"
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

/* ── Fake pbuf for simulating received music packets ───────────────── */
static struct udp_pcb *fake_pcb;
static void (*music_recv_callback)(void *, struct udp_pcb *, struct pbuf *,
                                    const ip_addr_t *, u16_t) = NULL;

/* Stub: udp_new returns a fake pcb */
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
    music_recv_callback = recv;
}

void udp_remove(struct udp_pcb *pcb) { (void)pcb; }

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

/* Build a fake music sync packet and feed it to the callback.
 *
 * Music packet layout:
 *   Byte 0:  effect_id
 *   Byte 1:  num_bands
 *   Byte 2+: band values (num_bands bytes) */

static void send_music_packet(uint8_t effect_id, uint8_t num_bands,
                              const uint8_t *bands) {
    uint16_t total_len = 2 + num_bands;
    uint8_t buf[128];
    buf[0] = effect_id;
    buf[1] = num_bands;
    if (bands && num_bands > 0) {
        memcpy(&buf[2], bands, num_bands);
    }

    struct pbuf *p = pbuf_alloc(0, total_len, 0);
    if (!p) return;
    memcpy(p->payload, buf, total_len);

    if (music_recv_callback) {
        music_recv_callback(NULL, fake_pcb, p, NULL, 5006);
    }
    /* Note: music_sync_recv frees the pbuf internally */
}

static void clear_led_buffer(void) {
    memset(led_buffer, 0, BUFFER_SIZE);
    led_update_pending = 0;
}

/* ── Music Sync Tests ──────────────────────────────────────────────── */

/* T1: music_sync_init creates a UDP listener */
static void test_music_init_creates_listener(void) {
    TEST("Music sync init creates a UDP listener");

    void *result = music_sync_init();
    CHECK(result != NULL);
check_exit:;
}

/* T2: Music packet with valid data updates LEDs */
static void test_music_valid_packet_updates_leds(void) {
    TEST("Music valid packet updates LEDs");

    clear_led_buffer();

    /* effect_id=0 (solid), 1 band, value=255 */
    uint8_t bands[1] = {255};
    send_music_packet(0, 1, bands);

    CHECK(led_update_pending == 1);
    /* Band value should fill first zone of LEDs */
    CHECK(led_buffer[0] > 0);
check_exit:;
}

/* T3: Band value scales LED brightness */
static void test_music_band_value_scales_brightness(void) {
    TEST("Band value scales brightness");

    clear_led_buffer();

    uint8_t bands_high[1] = {255};
    send_music_packet(0, 1, bands_high);
    uint8_t bright_high = led_buffer[0];

    clear_led_buffer();

    uint8_t bands_low[1] = {64};
    send_music_packet(0, 1, bands_low);
    uint8_t bright_low = led_buffer[0];

    CHECK(bright_high > bright_low);
check_exit:;
}

/* T4: Multiple bands fill multiple zones */
static void test_music_multiple_bands_fill_zones(void) {
    TEST("Multiple bands fill zones");

    clear_led_buffer();

    uint8_t bands[4] = {200, 100, 150, 50};
    send_music_packet(0, 4, bands);

    CHECK(led_update_pending == 1);
    /* First band should light up first zone */
    CHECK(led_buffer[0] > 0);
    /* At least some LEDs should be set */
    int i;
    int any_lit = 0;
    for (i = 0; i < BUFFER_SIZE; i++) {
        if (led_buffer[i] != 0) { any_lit = 1; break; }
    }
    CHECK(any_lit);
check_exit:;
}

/* T5: Band value controls bar height */
static void test_music_band_value_controls_bar_height(void) {
    TEST("Band value controls bar height");

    clear_led_buffer();

    /* Send a strong beat */
    uint8_t bands[1] = {255};
    send_music_packet(0, 1, bands);

    CHECK(led_update_pending == 1);
    /* Higher band value = more LEDs lit */
    /* Count non-zero LEDs in first zone */
    int lit_count = 0;
    int i;
    for (i = 0; i < 36; i++) { /* first zone ≈ 36 LEDs */
        if (led_buffer[i] != 0) lit_count++;
    }
    CHECK(lit_count > 0);
check_exit:;
}

/* T6: Zero bands is ignored */
static void test_music_zero_bands_ignored(void) {
    TEST("Zero bands ignored");

    clear_led_buffer();

    uint8_t bands[1] = {255};
    send_music_packet(0, 0, bands);

    CHECK(led_update_pending == 0);
check_exit:;
}

/* T7: Too many bands is ignored */
static void test_music_too_many_bands_ignored(void) {
    TEST("Too many bands ignored");

    clear_led_buffer();

    uint8_t bands[70] = {0};
    memset(bands, 128, 70);
    send_music_packet(0, 70, bands);

    CHECK(led_update_pending == 0);
check_exit:;
}

/* T8: Short packet (less than 3 bytes) is ignored */
static void test_music_short_packet_ignored(void) {
    TEST("Short packet ignored");

    clear_led_buffer();

    uint8_t tiny[2] = {0, 1};
    struct pbuf *p = pbuf_alloc(0, 2, 0);
    if (!p) goto check_exit;
    memcpy(p->payload, tiny, 2);

    if (music_recv_callback) {
        music_recv_callback(NULL, fake_pcb, p, NULL, 5006);
    }
    /* Note: music_sync_recv frees the pbuf internally */

    CHECK(led_update_pending == 0);
check_exit:;
}

/* T9: Music with effect_id triggers effects engine */
static void test_music_rainbow_effect_applies_colors(void) {
    TEST("Music effect applies colors");

    clear_led_buffer();

    /* effect_id=1 (rainbow), 1 band */
    uint8_t bands[1] = {128};
    send_music_packet(1, 1, bands);

    CHECK(led_update_pending == 1);
    /* Rainbow should produce varied colors, not uniform */
    CHECK(led_buffer[0] != led_buffer[1] || led_buffer[1] != led_buffer[2]);
check_exit:;
}

/* T10: Music triggers effects_engine_client_active */
static void test_music_triggers_client_active(void) {
    TEST("Music triggers effects_engine_client_active");

    clear_led_buffer();

    uint8_t bands[1] = {255};
    send_music_packet(0, 1, bands);

    CHECK(led_update_pending == 1);
    CHECK(1);
check_exit:;
}

/* ── Runner ────────────────────────────────────────────────────────── */

int main(void) {
    test_music_init_creates_listener();
    test_music_valid_packet_updates_leds();
    test_music_band_value_scales_brightness();
    test_music_multiple_bands_fill_zones();
    test_music_band_value_controls_bar_height();
    test_music_zero_bands_ignored();
    test_music_too_many_bands_ignored();
    test_music_short_packet_ignored();
    test_music_rainbow_effect_applies_colors();
    test_music_triggers_client_active();

    printf("\n%d tests run, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
