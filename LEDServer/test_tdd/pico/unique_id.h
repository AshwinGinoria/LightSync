#ifndef PICO_UNIQUE_ID_H
#define PICO_UNIQUE_ID_H

#include <stdint.h>

/* Stub for RP2040 unique board ID.
 * In tests, we provide a fixed ID so hostname construction is deterministic. */

#define PICO_UNIQUE_BOARD_ID_SIZE_BITS 64
#define PICO_UNIQUE_BOARD_ID_SIZE_BYTES 8

typedef struct {
    uint8_t id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES];
} pico_unique_board_id_t;

static inline void pico_get_unique_board_id(pico_unique_board_id_t *id) {
    /* Fixed test ID: 00:00:00:DE:AD:BE:EF */
    id->id[0] = 0;
    id->id[1] = 0;
    id->id[2] = 0;
    id->id[3] = 0;
    id->id[4] = 0;
    id->id[5] = 0;
    id->id[6] = 0xDE;
    id->id[7] = 0xEF;
}

#endif /* PICO_UNIQUE_ID_H */
