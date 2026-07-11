#ifndef CONFIG_STORAGE_H
#define CONFIG_STORAGE_H

#include <stdint.h>
#include <stddef.h>

/* ── Config layout (stored at end of flash, 1 sector) ──────────────── */

#define CONFIG_MAGIC        "LSYN"    /* 4-byte magic              */
#define CONFIG_VERSION      0x0002    /* bumped from 0x0001 (v2: effect fields) */
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024) /* 2 MB RP2040 default */
#endif
#ifndef FLASH_SECTOR_SIZE
#define FLASH_SECTOR_SIZE    4096               /* RP2040 flash sector = 4 KB */
#endif
#define CONFIG_OFFSET       (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
#define CONFIG_SSID_MAX     32
#define CONFIG_PASS_MAX     64

#define CONFIG_FLAG_VALID   (1u << 0)

/* Effects mode — used by config and effects_engine. */
typedef enum {
    EFFECT_MODE_CLIENT = 0,   /* DDP client controls LEDs */
    EFFECT_MODE_AUTO = 1      /* Autonomous effects always run */
} effects_mode_t;

typedef struct __attribute__((packed)) {
    uint8_t  magic[4];            /* 0x00: "LSYN"                  */
    uint16_t version;             /* 0x04: 0x0002                  */
    uint16_t flags;               /* 0x06: bit flags               */
    char     ssid[CONFIG_SSID_MAX];       /* 0x08: AP name (32)    */
    char     password[CONFIG_PASS_MAX];   /* 0x28: WiFi pass (64)  */
    /* ── v2 additions ── */
    uint8_t  effects_mode;        /* 0x68: EFFECT_MODE_CLIENT/AUTO */
    uint8_t  effect_id;           /* 0x69: 0..EFFECT_COUNT-1       */
    uint8_t  speed;               /* 0x6A: 1-255                   */
    uint8_t  brightness;          /* 0x6B: 0-255                   */
    uint8_t  color_r;             /* 0x6C: primary colour          */
    uint8_t  color_g;             /* 0x6D                          */
    uint8_t  color_b;             /* 0x6E                          */
    uint8_t  color2_r;            /* 0x6F: secondary colour        */
    uint8_t  color2_g;            /* 0x70                          */
    uint8_t  color2_b;            /* 0x71                          */
    uint16_t checksum;            /* 0x72: Fletcher-16 over 0x00-0x71 */
} config_t;  /* 116 bytes (up from 106) */

/* ── Public API ─────────────────────────────────────────────────────── */

/* Load config from flash into *out. Returns 0 on success. */
int config_load(config_t *out);

/* Save config to flash (erase sector, write page, compute checksum).
 * Returns 0 on success, -1 if ssid is empty. */
int config_save(const config_t *cfg);

/* Erase config sector (returns to blank state). */
int config_erase(void);

/* Check if valid config is present. Returns 1/0. */
int config_is_valid(void);

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_STORAGE_H */
