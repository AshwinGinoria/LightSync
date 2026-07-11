/* Flash abstraction: on x86 tests, reads from mock buffer; on Pico, reads
 * from actual flash memory at CONFIG_OFFSET. */
#ifndef FLASH_ABSTRACTION_H
#define FLASH_ABSTRACTION_H

#include <stdint.h>
#include "config_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Return pointer to the start of the config flash sector.
 * On x86 (TEST builds): returns mock_flash buffer.
 * On Pico: returns the actual flash memory address. */
uint8_t *flash_config_base(void);

#ifdef __cplusplus
}
#endif

#endif /* FLASH_ABSTRACTION_H */
