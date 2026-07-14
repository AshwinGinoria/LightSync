/* Logger implementation - static buffer, no dynamic allocation.
 *
 * Uses a 256-byte static buffer for formatted output. If a message
 * exceeds the buffer it is truncated.
 *
 * Under FLASH_MOCK (native test builds), all functions are no-ops
 * since the header macros are already no-ops.
 *
 * UART output: no critical section — printf is only called from the
 * main loop (no concurrent callers in bare-metal firmware).
 */
#include "logger.h"

/* Module name table */
const char *log_module_names[] = {
    [MOD_BOOT]      = "BOOT",
    [MOD_HTTPD]     = "HTTPD",
    [MOD_UDP]       = "UDP",
    [MOD_DDP]       = "DDP",
    [MOD_MUSIC]     = "MUSIC",
    [MOD_DNS]       = "DNS",
    [MOD_DHCP]      = "DHCP",
    [MOD_EFFECTS]   = "EFFECTS",
    [MOD_CONFIG]    = "CONFIG",
    [MOD_LED]       = "LED",
    [MOD_MDNS]      = "MDNS",
    [MOD_MEM]       = "MEM",
    [MOD_MAIN]      = "MAIN",
    [MOD_APP]       = "APP",
};

/* FLASH_MOCK stubs - no-op functions for test builds */
#ifdef FLASH_MOCK

void logger_init(void) {}
void logger_set_level(uint8_t level) { (void)level; }
uint8_t logger_get_level(void) { return 0; }
void logger_emit(uint8_t level, log_module_t mod, const char *fmt, ...) {
    (void)level; (void)mod; (void)fmt;
}
void memory_heartbeat_init(void) {}
void memory_heartbeat_report(void) {}
void stack_watermark_init(void) {}
void stack_watermark_report(void) {}
bool dwt_init(void) { return false; }
uint32_t dwt_read_cycles(void) { return 0; }
uint32_t dwt_get_cpu_load_pct(void) { return 0; }
void dwt_reset_sample(void) {}
void dwt_sample_window(uint32_t total_cycles, uint32_t idle_cycles) {
    (void)total_cycles; (void)idle_cycles;
}

#else

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "pico/stdlib.h"

/* Static state */
static volatile uint8_t g_log_level = LOG_LEVEL;
static uint8_t g_buffer[256];

/* Memory heartbeat state - bare-metal has no dynamic allocator,
 * so we track heartbeat count only (no free-heap tracking). */
static volatile uint32_t g_heartbeat_count = 0;

/* Public API */

void logger_init(void) {
    /* g_log_level is already LOG_LEVEL at file-scope init; this is
     * retained for clarity and any restart scenarios. */
    stack_watermark_init();
}

void logger_set_level(uint8_t level) {
    g_log_level = level;
}

uint8_t logger_get_level(void) {
    return g_log_level;
}

static const char *level_to_string(uint8_t level) {
    switch (level) {
        case LEVEL_EMERG:  return "EMERG";
        case LEVEL_ALERT:  return "ALERT";
        case LEVEL_CRIT:   return "CRIT";
        case LEVEL_ERROR:  return "ERROR";
        case LEVEL_WARN:   return "WARN";
        case LEVEL_INFO:   return "INFO";
        case LEVEL_DEBUG:  return "DEBUG";
        case LEVEL_TRACE:  return "TRACE";
        default:           return "???";
    }
}

void logger_emit(uint8_t level, log_module_t mod, const char *fmt, ...) {
    /* Runtime filter - compile-time guard already eliminates calls
     * below LOG_LEVEL, but runtime changes via logger_set_level()
     * still need this check. */
    if (level > g_log_level) {
        return;
    }

    /* Timestamp in milliseconds from RP2040 tick. */
    uint64_t us = time_us_64();
    uint32_t ms = (uint32_t)(us / 1000);

    /* Module name */
    const char *mod_name = "???";
    if (mod < MOD_COUNT && log_module_names[mod] != NULL) {
        mod_name = log_module_names[mod];
    }

    /* Format message into static buffer */
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf((char *)g_buffer, sizeof(g_buffer), fmt, args);
    va_end(args);

    if (n < 0) {
        n = 0;
    }
    if ((size_t)n >= sizeof(g_buffer)) {
        n = (int)(sizeof(g_buffer) - 1);
    }

    /* UART output — no critical section needed: printf is only called
     * from the main loop (no concurrent callers in bare-metal firmware).
     * Keeping interrupts enabled is important for WiFi coexistence. */
    printf("[%u] [%s] [%s] %.*s\n",
           ms,
           level_to_string(level),
           mod_name,
           n, g_buffer);
}

/* Memory heartbeat */

void memory_heartbeat_init(void) {
    g_heartbeat_count = 0;
}

void memory_heartbeat_report(void) {
    g_heartbeat_count++;
    LOG_INFO(MOD_MEM, "heartbeat #%u", g_heartbeat_count);
}

/* Note: g_heartbeat_count is a diagnostic uptime counter, not a memory
 * allocator tracker. On bare-metal with no dynamic allocation, there is
 * no free-heap to report. It overflows after ~49.7 days at 100/sec. */

/* Stack watermarking - measure max stack usage at runtime.
 *
 * Technique: fill the stack region with a known pattern (0xBE) at boot.
 * The stack grows downward from __StackTop toward __StackBottom.
 * Periodically scan from __StackBottom upward to find the first byte
 * that is NOT 0xBE. This is the lowest address the stack has reached.
 * Max stack used = __StackTop - lowest_touched_address.
 *
 * The stack region is the .stack_dummy section (~1KB), reserved by
 * the linker between __StackTop and __StackBottom. */

/* Stack watermark state - volatile so compiler doesn't cache across
 * report calls. */
static volatile uint32_t g_stack_max_used = 0;
static uint32_t g_stack_total = 0;

/* External linker symbols - defined in the Pico SDK linker script.
 * __StackTop: initial SP (top of SCRATCH_Y, used as stack)
 * __StackBottom: bottom of the .stack_dummy region (lowest stack address) */
extern char __StackTop;
extern char __StackBottom;

void stack_watermark_init(void) {
    /* Fill the stack region with the watermark pattern.
     * The stack grows downward from __StackTop toward __StackBottom.
     * We fill the entire stack region so we can detect how far
     * the stack has grown by scanning from __StackBottom upward. */
    uintptr_t stack_top = (uintptr_t)&__StackTop;
    uintptr_t stack_bottom = (uintptr_t)&__StackBottom;
    size_t region_size = stack_top - stack_bottom;

    /* Fill with 0xBE pattern - easy to spot in debug */
    volatile uint8_t *p = (volatile uint8_t *)stack_bottom;
    for (size_t i = 0; i < region_size; i++) {
        p[i] = (uint8_t)0xBE;
    }

    /* Record totals for reporting */
    g_stack_max_used = 0;
    g_stack_total = (uint32_t)region_size;
}

void stack_watermark_report(void) {
    /* Scan from __StackBottom upward to find the first non-0xBE byte.
     * This is the lowest address the stack has reached.
     * If no non-0xBE byte is found, the stack has never grown.
     *
     * __StackTop is the initial SP — excluded from the scan since
     * startup code may have written to that address (not stack usage).
     */
    uintptr_t stack_top = (uintptr_t)&__StackTop;
    uintptr_t lowest = stack_top; /* default: no stack growth detected */

    volatile const uint8_t *p = (volatile const uint8_t *)&__StackBottom;
    while (p < (volatile const uint8_t *)&__StackTop) {
        if (*p != (uint8_t)0xBE) {
            /* Found a byte that was written to - stack reached here */
            lowest = (uintptr_t)p;
            break;
        }
        p++;
    }

    /* Calculate max stack used: distance from __StackTop to lowest point */
    g_stack_max_used = (uint32_t)(stack_top - lowest);

    LOG_INFO(MOD_MEM,
             "stack watermark: max_used=%u total=%u (%.1f%%)",
             g_stack_max_used,
             g_stack_total,
             g_stack_total > 0 ? (float)g_stack_max_used * 100.0f / (float)g_stack_total : 0.0f);
}

/* DWT cycle counter - CPU profiling via Data Watchpoint and Trace unit.
 *
 * The RP2040 Cortex-M0+ has a DWT peripheral with a 32-bit cycle counter
 * that increments every core clock cycle (133 MHz). We enable it at boot
 * and read it to measure CPU utilization.
 *
 * CoreDebug->DEMCR bit 24 (TRCENA) enables the trace subsystem.
 * DWT->CTRL bit 0 (CYCCNTENA) enables the cycle counter.
 *
 * During WFI, the core clock stops — DWT does not increment.
 * This is how we measure idle time: by using a non-blocking WFI loop
 * and comparing DWT cycles to wall time. */

/* DWT and CoreDebug register addresses (ARM CMSIS standard) */
#define DWT_CTRL        (*(volatile uint32_t *)0xE0001000U)
#define DWT_CYCCNT      (*(volatile uint32_t *)0xE0001004U)
#define CoreDebug_DEMCR (*(volatile uint32_t *)0xE000EDFCU)
#define DEMCR_TRCENA    (1U << 24)
#define DWT_CTRL_CYCCNTENA (1U << 0)

/* DWT state
 * Accumulate cycle samples periodically. Every DWT_SAMPLE_WINDOW_COUNT
 * windows (default 100 = ~1s), the accumulated totals are reported
 * and then zeroed to avoid uint32_t wrap at ~32s. */
#define DWT_SAMPLE_WINDOW_COUNT 100 /* ~1 second at 10ms windows */

static bool g_dwt_enabled = false;
static volatile uint32_t g_cpu_idle_cycles = 0; /* accumulated idle cycles */
static volatile uint32_t g_cpu_total_cycles = 0; /* accumulated total cycles */
static volatile uint32_t g_sample_count = 0;    /* windows accumulated */

bool dwt_init(void) {
    /* Enable CoreDebug (TRCENA) first, then enable DWT CYCCNT */
    CoreDebug_DEMCR |= DEMCR_TRCENA;
    if (!(CoreDebug_DEMCR & DEMCR_TRCENA)) {
        return false; /* DWT not available */
    }
    DWT_CTRL |= DWT_CTRL_CYCCNTENA;
    DWT_CYCCNT = 0; /* reset counter */
    g_dwt_enabled = true;
    return true;
}

uint32_t dwt_read_cycles(void) {
    return g_dwt_enabled ? DWT_CYCCNT : 0;
}

uint32_t dwt_get_cpu_load_pct(void) {
    /* CPU load = (total_cycles - idle_cycles) / total_cycles * 100.
     * Idle cycles are accumulated by the main loop when it calls
     * dwt_sample_window() during WFI.
     *
     * Both g_cpu_total_cycles and g_cpu_idle_cycles are zeroed together
     * in dwt_sample_window()'s auto-reset, so they wrap simultaneously
     * and the subtraction cannot underflow. */
    if (g_cpu_total_cycles == 0) {
        return 0;
    }
    uint32_t active = g_cpu_total_cycles - g_cpu_idle_cycles;
    if (active > g_cpu_total_cycles) {
        active = g_cpu_total_cycles; /* wrap protection */
    }
    return (active * 100) / g_cpu_total_cycles;
}

void dwt_reset_sample(void) {
    /* One-time reset of accumulated cycle counters at startup.
     * This is NOT meant to be called per-interval — dwt_sample_window()
     * auto-resets after DWT_SAMPLE_WINDOW_COUNT windows. Calling this
     * repeatedly would zero accumulated totals prematurely. */
    g_cpu_total_cycles = 0;
    g_cpu_idle_cycles = 0;
}

void dwt_sample_window(uint32_t total_cycles, uint32_t idle_cycles) {
    /* Call after each measurement window to accumulate totals.
     * total_cycles = wall-clock cycles in the window (e.g. 1,330,000 for 10ms).
     * idle_cycles = cycles spent in WFI (core stopped, DWT not counting).
     *
     * Every DWT_SAMPLE_WINDOW_COUNT windows (~1s), the accumulated totals
     * are reported via dwt_get_cpu_load_pct() and then zeroed to avoid
     * uint32_t wrap at ~32s (2^32 / 133MHz). */
    g_cpu_idle_cycles += idle_cycles;
    g_cpu_total_cycles += total_cycles;
    g_sample_count++;

    /* Periodic reset to avoid uint32_t overflow (~32s at 133MHz).
     * Use a fresh window size for the next accumulation period. */
    if (g_sample_count >= DWT_SAMPLE_WINDOW_COUNT) {
        g_sample_count = 0;
        g_cpu_idle_cycles = 0;
        g_cpu_total_cycles = 0;
    }
}

#endif /* FLASH_MOCK */
