/* Config Storage — persistent WiFi credentials on RP2040 flash.
 *
 * Layout (one 4KB flash sector at end of flash):
 *   Offset 0x00: magic "LSYN" (4 bytes)
 *   Offset 0x04: version 0x0001 (2 bytes)
 *   Offset 0x06: flags (2 bytes, bit0 = valid)
 *   Offset 0x08: ssid (32 bytes, null-terminated)
 *   Offset 0x28: password (64 bytes, null-terminated)
 *   Offset 0x68: Fletcher-16 checksum over bytes 0x00..0x67 (2 bytes)
 *
 * Total: 106 bytes. Fits in one 256B page, stored in one 4KB sector. */
#include "config_storage.h"

#include <string.h>
#include <stdint.h>

/* ── Flash API — provided by mock (x86) or Pico SDK (hardware) ─────── */
#ifdef FLASH_MOCK
#include "hardware/flash.h"
#else
#include "pico/stdlib.h"
#include "hardware/flash.h"
#endif

/* ── Flash read abstraction ─────────────────────────────────────────── */
extern uint8_t *flash_config_base(void);

/* ── Fletcher-16 checksum ───────────────────────────────────────────── */
static uint16_t config_checksum(const config_t *cfg) {
    const uint8_t *p = (const uint8_t *)cfg;
    uint16_t sum1 = 0, sum2 = 0;
    for (size_t i = 0; i < sizeof(config_t) - 2; i++) {
        sum1 = (sum1 + p[i]) % 255;
        sum2 = (sum2 + sum1) % 255;
    }
    return (uint16_t)(sum2 << 8 | sum1);
}

/* ── Helpers ────────────────────────────────────────────────────────── */
static config_t *config_read_raw(void) {
    return (config_t *)flash_config_base();
}

/* ── Public API ─────────────────────────────────────────────────────── */

int config_load(config_t *out) {
    config_t *raw = config_read_raw();
    memcpy(out, raw, sizeof(config_t));
    return 0;
}

int config_save(const config_t *cfg) {
    /* Validate SSID is non-empty */
    if (cfg->ssid[0] == '\0') {
        return -1;
    }

    /* Truncate SSID if needed */
    config_t local = *cfg;
    if (strlen(local.ssid) >= CONFIG_SSID_MAX) {
        local.ssid[CONFIG_SSID_MAX - 1] = '\0';
    }

    /* Compute and store checksum */
    local.checksum = config_checksum(&local);

    /* Erase one sector (4KB) */
    flash_range_erase(CONFIG_OFFSET, FLASH_SECTOR_SIZE);

    /* Program one page (256B) — our config is 106 bytes, fits in one page */
    flash_range_program(CONFIG_OFFSET, (const uint8_t *)&local, sizeof(config_t));

    return 0;
}

int config_erase(void) {
    flash_range_erase(CONFIG_OFFSET, FLASH_SECTOR_SIZE);
    return 0;
}

int config_is_valid(void) {
    config_t *raw = config_read_raw();

    /* Check magic */
    if (memcmp(raw->magic, CONFIG_MAGIC, 4) != 0) {
        return 0;
    }

    /* Check version */
    if (raw->version != CONFIG_VERSION) {
        return 0;
    }

    /* Check checksum */
    uint16_t expected = config_checksum(raw);
    if (expected != raw->checksum) {
        return 0;
    }

    /* Check valid flag */
    if ((raw->flags & CONFIG_FLAG_VALID) == 0) {
        return 0;
    }

    return 1;
}
