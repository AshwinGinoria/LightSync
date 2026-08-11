/* lwIP pbuf stubs for x86 test builds — shared across test targets */
#include "udp.h"
#include <string.h>
#include <stdlib.h>

struct pbuf *pbuf_alloc(int layer, uint16_t length, int type) {
    (void)layer; (void)type;
    struct pbuf *p = (struct pbuf *)malloc(sizeof(struct pbuf));
    if (!p) return NULL;
    uint8_t *payload = (uint8_t *)malloc(length > 0 ? length : 1);
    if (!payload) { free(p); return NULL; }
    memset(payload, 0, length > 0 ? length : 1);
    p->next = NULL;
    p->payload = payload;
    p->tot_len = length;
    p->len = length;
    return p;
}

void pbuf_free(struct pbuf *p) {
    if (!p) return;
    /* Test build: do not free — pbufs may be stack-allocated by tests */
    (void)p;
}

size_t pbuf_copy_partial(struct pbuf *p, void *buf, size_t len, size_t offset) {
    if (!p || !buf || len == 0) return 0;
    size_t copy_len = len < p->len ? len : p->len;
    if (offset >= p->len) return 0;
    if (offset + copy_len > p->len) copy_len = p->len - offset;
    memcpy((char *)buf, (char *)p->payload + offset, copy_len);
    return copy_len;
}
