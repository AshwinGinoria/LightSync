/* Mock flash implementation backed by static memory.
 * Simulates the full 2MB flash of a Pico W, so offsets like
 * CONFIG_OFFSET (= 2MB - 4KB) land correctly. */
#include "hardware/flash.h"
#include "config_storage.h"
#include <string.h>

#define MOCK_FLASH_SIZE PICO_FLASH_SIZE_BYTES

static uint8_t mock_flash[MOCK_FLASH_SIZE];
static int erase_count = 0;
static int program_count = 0;

__attribute__((constructor))
static void flash_mock_init(void) {
    memset(mock_flash, 0xFF, sizeof(mock_flash));
}

void flash_range_erase(uint32_t offset, size_t count) {
    if (offset + count > MOCK_FLASH_SIZE) return;
    memset(&mock_flash[offset], 0xFF, count);
    erase_count++;
}

void flash_range_program(uint32_t offset, const uint8_t *data, size_t count) {
    if (offset + count > MOCK_FLASH_SIZE) return;
    memcpy(&mock_flash[offset], data, count);
    program_count++;
}

uint8_t *mock_flash_get_base(void) {
    return &mock_flash[CONFIG_OFFSET];
}

int mock_flash_get_erase_count(void) {
    return erase_count;
}

int mock_flash_get_program_count(void) {
    return program_count;
}
