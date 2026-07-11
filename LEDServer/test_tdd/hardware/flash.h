/* Mock flash for x86 testing — simulates RP2040 flash_range_erase/program.
 * Backed by a static buffer initialized to 0xFF (erased state). */
#ifndef FLASH_MOCK_H
#define FLASH_MOCK_H

#include <stdint.h>
#include <stddef.h>

#define FLASH_SECTOR_SIZE 4096
#define FLASH_PAGE_SIZE   256

/* Real Pico SDK signatures — we provide our own implementations. */
void flash_range_erase(uint32_t offset, size_t count);
void flash_range_program(uint32_t offset, const uint8_t *data, size_t count);

/* Test helpers */
uint8_t *mock_flash_get_base(void);

/* Production code helper: get base address of flash sector for reading back */
static inline uint8_t *flash_sector_get_base(void) {
    return mock_flash_get_base();
}
int mock_flash_get_erase_count(void);
int mock_flash_get_program_count(void);

#endif /* FLASH_MOCK_H */
