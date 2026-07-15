/* Logger - compile-time filtered, module-tagged logging for LEDServer.
 *
 * Format: [timestamp_ms] [LEVEL] [MODULE] message
 *
 * Compile-time filtering: defining LOG_LEVEL below a severity level
 * eliminates all lower-priority log calls from the binary entirely.
 *
 * FLASH_MOCK guard (native test builds): all macros expand to nothing.
 */
#ifndef LOGGER_H
#define LOGGER_H

#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>

/* Compile-time log level - set before including logger.h.
 * Default: LEVEL_DEBUG (everything except TRACE).
 *
 * Filtering: LOG_LEVEL >= LEVEL_X means emit level X.
 * Higher LOG_LEVEL = more verbose. */
#ifndef LOG_LEVEL
#define LOG_LEVEL LEVEL_DEBUG
#endif

/* Severity levels */
#define LEVEL_EMERG   0
#define LEVEL_ALERT   1
#define LEVEL_CRIT    2
#define LEVEL_ERROR   3
#define LEVEL_WARN    4
#define LEVEL_INFO    5
#define LEVEL_DEBUG   6
#define LEVEL_TRACE   7

/* Sanity check: LOG_LEVEL must be within the valid range.
 * Catches configuration errors (e.g. LOG_LEVEL=0 means only EMERG). */
#ifdef __cplusplus
#define _LOG_STATIC_ASSERT(expr, msg) static_assert(expr, msg)
#else
#define _LOG_STATIC_ASSERT(expr, msg) _Static_assert(expr, msg)
#endif

_LOG_STATIC_ASSERT(LEVEL_EMERG <= LOG_LEVEL && LOG_LEVEL <= LEVEL_TRACE,
    "LOG_LEVEL must be between LEVEL_EMERG (0) and LEVEL_TRACE (7)");

/* Module identifiers */
typedef enum {
    MOD_BOOT      = 0,
    MOD_HTTPD     = 1,
    MOD_UDP       = 2,
    MOD_DDP       = 3,
    MOD_MUSIC     = 4,
    MOD_DNS       = 5,
    MOD_DHCP      = 6,
    MOD_EFFECTS   = 7,
    MOD_CONFIG    = 8,
    MOD_LED       = 9,
    MOD_MDNS      = 10,
    MOD_MEM       = 11,
    MOD_MAIN      = 12,
    MOD_APP       = 13,
    MOD_COUNT     = 14,
} log_module_t;

/* Module name table (defined in logger.c) */
extern const char *log_module_names[];

/* Level name table (defined in logger.c) */
extern const char *log_level_names[];

/* FLASH_MOCK guard - test builds get zero-op macros */
#ifdef FLASH_MOCK

#define LOG_EMERG(mod, ...)  ((void)0)
#define LOG_ALERT(mod, ...)  ((void)0)
#define LOG_CRIT(mod, ...)   ((void)0)
#define LOG_ERROR(mod, ...)  ((void)0)
#define LOG_WARN(mod, ...)   ((void)0)
#define LOG_INFO(mod, ...)   ((void)0)
#define LOG_DEBUG(mod, ...)  ((void)0)
#define LOG_TRACE(mod, ...)  ((void)0)

/* Compile-time filtering - only emit code when level meets LOG_LEVEL */

#else /* !FLASH_MOCK: compile-time filtering */

#if LOG_LEVEL >= LEVEL_EMERG
#define LOG_EMERG(mod, ...)  do { \
    logger_emit(LEVEL_EMERG, mod, __VA_ARGS__); \
} while (0)
#else
#define LOG_EMERG(mod, ...)  ((void)0)
#endif

#if LOG_LEVEL >= LEVEL_ALERT
#define LOG_ALERT(mod, ...)  do { \
    logger_emit(LEVEL_ALERT, mod, __VA_ARGS__); \
} while (0)
#else
#define LOG_ALERT(mod, ...)  ((void)0)
#endif

#if LOG_LEVEL >= LEVEL_CRIT
#define LOG_CRIT(mod, ...)  do { \
    logger_emit(LEVEL_CRIT, mod, __VA_ARGS__); \
} while (0)
#else
#define LOG_CRIT(mod, ...)  ((void)0)
#endif

#if LOG_LEVEL >= LEVEL_ERROR
#define LOG_ERROR(mod, ...)  do { \
    logger_emit(LEVEL_ERROR, mod, __VA_ARGS__); \
} while (0)
#else
#define LOG_ERROR(mod, ...)  ((void)0)
#endif

#if LOG_LEVEL >= LEVEL_WARN
#define LOG_WARN(mod, ...)  do { \
    logger_emit(LEVEL_WARN, mod, __VA_ARGS__); \
} while (0)
#else
#define LOG_WARN(mod, ...)  ((void)0)
#endif

#if LOG_LEVEL >= LEVEL_INFO
#define LOG_INFO(mod, ...)  do { \
    logger_emit(LEVEL_INFO, mod, __VA_ARGS__); \
} while (0)
#else
#define LOG_INFO(mod, ...)  ((void)0)
#endif

#if LOG_LEVEL >= LEVEL_DEBUG
#define LOG_DEBUG(mod, ...)  do { \
    logger_emit(LEVEL_DEBUG, mod, __VA_ARGS__); \
} while (0)
#else
#define LOG_DEBUG(mod, ...)  ((void)0)
#endif

#if LOG_LEVEL >= LEVEL_TRACE
#define LOG_TRACE(mod, ...)  do { \
    logger_emit(LEVEL_TRACE, mod, __VA_ARGS__); \
} while (0)
#else
#define LOG_TRACE(mod, ...)  ((void)0)
#endif
#endif /* FLASH_MOCK */

/* Public API */
#ifdef __cplusplus
extern "C" {
#endif

/* Initialise logger - call once at startup. */
void logger_init(void);

/* Change the minimum severity level at runtime. */
void logger_set_level(uint8_t level);

/* Get the current minimum severity level. */
uint8_t logger_get_level(void);

/* Core emit function - called by LOG_* macros. */
void logger_emit(uint8_t level, log_module_t mod, const char *fmt, ...);

/* Memory heartbeat - tracks heartbeat count for uptime diagnostics.
 * On bare-metal with no dynamic allocation, there is no free-heap to
 * track; this is a counter only. */
void memory_heartbeat_init(void);
void memory_heartbeat_report(void);

/* Stack watermarking - measure max stack usage at runtime. */
void stack_watermark_init(void);
void stack_watermark_report(void);

/* DWT cycle counter - CPU profiling via Data Watchpoint and Trace unit. */
bool dwt_init(void);
uint32_t dwt_read_cycles(void);
uint32_t dwt_get_cpu_load_pct(void);
void dwt_sample_window(uint32_t total_cycles, uint32_t idle_cycles);

#ifdef __cplusplus
}
#endif

#endif /* LOGGER_H */
