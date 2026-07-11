/* Flash abstraction layer.
 *
 * On x86 (TEST builds with FLASH_MOCK): includes mock declarations and
 * links against hardware/flash.c which provides the implementations.
 *
 * On Pico: includes the real Pico SDK hardware/flash.h which provides
 * the real flash_range_erase/program at runtime. */
#include "flash_abstraction.h"
#include "config_storage.h"

#ifdef FLASH_MOCK
#include "hardware/flash.h"

uint8_t *flash_config_base(void) {
    return mock_flash_get_base();
}
#else
/* Pico SDK target — real flash APIs provided by SDK at link time. */
#include "pico/stdlib.h"

uint8_t *flash_config_base(void) {
    /* Flash memory is mapped at base address + offset.
     * On RP2040, XIP_SWO base is 0x10000000. */
    return (uint8_t *)(XIP_BASE + CONFIG_OFFSET);
}
#endif
