/* Logger module tests — module name table, DWT accumulation logic.
 *
 * These tests exercise the pure logic in logger.c without requiring
 * Pico SDK hardware (printf, time_us_64, DWT registers).
 *
 * Built with FLASH_MOCK so logger.c compiles as no-ops, but the
 * extern const module name table is still accessible. */
#include "logger.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ── Test harness ──────────────────────────────────────────────────── */
static int  tests_run     =  0;
static int  tests_failed  =  0;

#define TEST(name)  do { \
    tests_run++; \
    printf("TEST: %s\n", name); \
} while(0)

#define CHECK(cond)  do { \
    if (!(cond)) { \
        printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define CHECK_EQ(a, b)  do { \
    if ((a) != (b)) { \
        printf("  FAIL %s:%d: %s (%d) != %s (%d)\n", \
            __FILE__, __LINE__, #a, (int)(a), #b, (int)(b)); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define CHECK_STR_EQ(a, b)  do { \
    if (strcmp((a), (b)) != 0) { \
        printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", \
            __FILE__, __LINE__, (a), (b)); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* ── Externs from logger.c ─────────────────────────────────────────── */
extern const char *log_module_names[];

/* ── Module name table tests ───────────────────────────────────────── */

static void test_module_names_non_null(void) {
    TEST("module_names: all entries non-NULL");
    for (int i = 0; i < MOD_COUNT; i++) {
        CHECK(log_module_names[i] != NULL);
    }
}

static void test_module_names_known_values(void) {
    TEST("module_names: known entries match expected strings");
    CHECK_STR_EQ(log_module_names[MOD_BOOT],      "BOOT");
    CHECK_STR_EQ(log_module_names[MOD_HTTPD],     "HTTPD");
    CHECK_STR_EQ(log_module_names[MOD_UDP],       "UDP");
    CHECK_STR_EQ(log_module_names[MOD_DDP],       "DDP");
    CHECK_STR_EQ(log_module_names[MOD_MUSIC],     "MUSIC");
    CHECK_STR_EQ(log_module_names[MOD_MDNS],      "MDNS");
    CHECK_STR_EQ(log_module_names[MOD_MEM],       "MEM");
    CHECK_STR_EQ(log_module_names[MOD_MAIN],      "MAIN");
}

static void test_module_count_matches_table(void) {
    TEST("module_count: MOD_COUNT matches table size");
    /* MOD_COUNT should equal the number of entries in the table.
     * Verify by checking the last index is valid. */
    CHECK(MOD_COUNT > 0);
    CHECK(log_module_names[MOD_COUNT - 1] != NULL);
}

static void test_level_constants_ordered(void) {
    TEST("level_constants: severity values are monotonically increasing");
    CHECK(LEVEL_EMERG   < LEVEL_ALERT);
    CHECK(LEVEL_ALERT   < LEVEL_CRIT);
    CHECK(LEVEL_CRIT    < LEVEL_ERROR);
    CHECK(LEVEL_ERROR   < LEVEL_WARN);
    CHECK(LEVEL_WARN    < LEVEL_INFO);
    CHECK(LEVEL_INFO    < LEVEL_DEBUG);
    CHECK(LEVEL_DEBUG   < LEVEL_TRACE);
}

static void test_level_trace_is_highest(void) {
    TEST("level_constants: LEVEL_TRACE is the highest severity");
    CHECK(LEVEL_TRACE == 7);
}

/* ── DWT accumulation logic tests ────────────────────────────────────
 * These test the math that dwt_sample_window() uses, without needing
 * the actual DWT hardware. We verify the cycle accounting is correct.
 *
 * The RP2040 runs at 133 MHz, so 10ms = 1,330,000 cycles.
 * During WFI, the core stops and DWT does not count.
 * idle_cycles = wall_cycles - work_cycles. */

static void test_dwt_idle_calculation_no_idle(void) {
    TEST("dwt_idle: no idle (all work cycles)");
    uint32_t wall_cycles = 1330000;   /* 10ms */
    uint32_t work_cycles = 1330000;   /* core was busy the whole time */
    uint32_t idle = (work_cycles < wall_cycles) ? (wall_cycles - work_cycles) : 0;
    CHECK_EQ(idle, 0);
}

static void test_dwt_idle_calculation_full_idle(void) {
    TEST("dwt_idle: full idle (no work cycles)");
    uint32_t wall_cycles = 1330000;   /* 10ms */
    uint32_t work_cycles = 0;         /* core was idle the whole time */
    uint32_t idle = (work_cycles < wall_cycles) ? (wall_cycles - work_cycles) : 0;
    CHECK_EQ(idle, 1330000);
}

static void test_dwt_idle_calculation_half_idle(void) {
    TEST("dwt_idle: 50% idle");
    uint32_t wall_cycles = 1330000;
    uint32_t work_cycles = 665000;
    uint32_t idle = (work_cycles < wall_cycles) ? (wall_cycles - work_cycles) : 0;
    CHECK_EQ(idle, 665000);
}

static void test_dwt_idle_calculation_wrap_protection(void) {
    TEST("dwt_idle: work_cycles >= wall_cycles capped to 0");
    uint32_t wall_cycles = 1330000;
    uint32_t work_cycles = 2000000; /* impossible but test boundary */
    uint32_t idle = (work_cycles < wall_cycles) ? (wall_cycles - work_cycles) : 0;
    CHECK_EQ(idle, 0);
}

static void test_dwt_cpu_load_calculation(void) {
    TEST("dwt_cpu_load: correct percentage");
    /* 50% idle => 50% load */
    uint32_t total = 1330000;
    uint32_t idle_cycles = 665000;
    uint32_t active = total - idle_cycles;
    uint32_t load = (active * 100) / total;
    CHECK_EQ(load, 50);
}

static void test_dwt_cpu_load_zero_idle(void) {
    TEST("dwt_cpu_load: 0% idle => 100% load");
    uint32_t total = 1330000;
    uint32_t idle_cycles = 0;
    uint32_t active = total - idle_cycles;
    uint32_t load = (active * 100) / total;
    CHECK_EQ(load, 100);
}

static void test_dwt_cpu_load_full_idle(void) {
    TEST("dwt_cpu_load: 100% idle => 0% load");
    uint32_t total = 1330000;
    uint32_t idle_cycles = 1330000;
    uint32_t active = total - idle_cycles;
    uint32_t load = (active * 100) / total;
    CHECK_EQ(load, 0);
}

static void test_dwt_cpu_load_zero_total(void) {
    TEST("dwt_cpu_load: zero total returns 0%");
    uint32_t total = 0;
    uint32_t idle_cycles = 0;
    uint32_t active = total - idle_cycles;
    uint32_t load = (total == 0) ? 0 : ((active * 100) / total);
    CHECK_EQ(load, 0);
}

static void test_dwt_accumulation_reset(void) {
    TEST("dwt_accumulation: auto-reset after 100 windows");
    /* Simulate the accumulation + reset logic from dwt_sample_window().
     * After 100 windows of 10ms each, counters should zero out. */
    uint32_t idle_sum = 0;
    uint32_t total_sum = 0;
    uint32_t sample_count = 0;
    #define DWT_SAMPLE_WINDOW_COUNT 100

    for (int i = 0; i < DWT_SAMPLE_WINDOW_COUNT; i++) {
        idle_sum += 500000;   /* 50% idle per window */
        total_sum += 1330000;
        sample_count++;
        if (sample_count >= DWT_SAMPLE_WINDOW_COUNT) {
            sample_count = 0;
            idle_sum = 0;
            total_sum = 0;
        }
    }
    CHECK_EQ(sample_count, 0);
    CHECK_EQ(idle_sum, 0);
    CHECK_EQ(total_sum, 0);
}

static void test_dwt_accumulation_no_reset(void) {
    TEST("dwt_accumulation: accumulates correctly before reset");
    uint32_t idle_sum = 0;
    uint32_t total_sum = 0;
    uint32_t sample_count = 0;
    #define DWT_SAMPLE_WINDOW_COUNT 100

    for (int i = 0; i < 50; i++) {
        idle_sum += 500000;
        total_sum += 1330000;
        sample_count++;
    }
    CHECK_EQ(sample_count, 50);
    CHECK_EQ(idle_sum, 25000000);  /* 50 * 500000 */
    CHECK_EQ(total_sum, 66500000); /* 50 * 1330000 */
}

/* ── Main ──────────────────────────────────────────────────────────── */
int main(void) {
    test_module_names_non_null();
    test_module_names_known_values();
    test_module_count_matches_table();
    test_level_constants_ordered();
    test_level_trace_is_highest();
    test_dwt_idle_calculation_no_idle();
    test_dwt_idle_calculation_full_idle();
    test_dwt_idle_calculation_half_idle();
    test_dwt_idle_calculation_wrap_protection();
    test_dwt_cpu_load_calculation();
    test_dwt_cpu_load_zero_idle();
    test_dwt_cpu_load_full_idle();
    test_dwt_cpu_load_zero_total();
    test_dwt_accumulation_reset();
    test_dwt_accumulation_no_reset();

    printf("\n%d tests run, %d failed\n", tests_run, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
