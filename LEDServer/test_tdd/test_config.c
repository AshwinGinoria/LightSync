/* Config Storage Tests — Phase 1: Persistent WiFi Config */
#include "config_storage.h"
#include "hardware/flash.h"

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
        printf("  FAIL %s:%d: %s (%ld) != %s (%ld)\n", \
            __FILE__, __LINE__, #a, (long)(a), #b, (long)(b)); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define CHECK_STREQ(a, b)  do { \
    if (strcmp((a), (b)) != 0) { \
        printf("  FAIL %s:%d: \"%s\" != \"%s\"\n", \
            __FILE__, __LINE__, (a), (b)); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* ── Fletcher-16 helper (for test verification) ────────────────────── */
static uint16_t fletcher16(const uint8_t *data, size_t len) {
    uint16_t sum1 = 0, sum2 = 0;
    for (size_t i = 0; i < len; i++) {
        sum1 = (sum1 + data[i]) % 255;
        sum2 = (sum2 + sum1) % 255;
    }
    return (uint16_t)(sum2 << 8 | sum1);
}

/* ── Helpers ───────────────────────────────────────────────────────── */
static void reset_flash(void) {
    config_erase();
}

/* ═════════════════════════════════════════════════════════════════════
 * CONFIG STORAGE TESTS
 * ═════════════════════════════════════════════════════════════════════ */

/* C1: struct fits in one flash page */
static void test_config_struct_size(void) {
    TEST("config struct size <= FLASH_PAGE_SIZE");
    CHECK_EQ(sizeof(config_t), 116);
}

/* C2: checksum is deterministic */
static void test_config_checksum_deterministic(void) {
    TEST("config checksum is deterministic");
    config_t cfg1, cfg2;
    memset(&cfg1, 0, sizeof(cfg1));
    memset(&cfg2, 0, sizeof(cfg2));
    memcpy(cfg1.magic, CONFIG_MAGIC, 4);
    memcpy(cfg2.magic, CONFIG_MAGIC, 4);
    cfg1.version = CONFIG_VERSION;
    cfg2.version = CONFIG_VERSION;
    memcpy(cfg1.ssid, "test", 5);
    memcpy(cfg2.ssid, "test", 5);
    /* Set same checksum so fletcher16 has a value to compare */
    cfg1.checksum = cfg2.checksum = 0x1234;
    uint16_t cksum1 = fletcher16((uint8_t *)&cfg1, sizeof(cfg1) - 2);
    uint16_t cksum2 = fletcher16((uint8_t *)&cfg2, sizeof(cfg2) - 2);
    CHECK_EQ(cksum1, cksum2);
}

/* C3: checksum changes when data changes */
static void test_config_checksum_changes_with_data(void) {
    TEST("config checksum changes with data");
    config_t cfg1, cfg2;
    memset(&cfg1, 0, sizeof(cfg1));
    memset(&cfg2, 0, sizeof(cfg2));
    memcpy(cfg1.magic, CONFIG_MAGIC, 4);
    memcpy(cfg2.magic, CONFIG_MAGIC, 4);
    cfg1.version = CONFIG_VERSION;
    cfg2.version = CONFIG_VERSION;
    memcpy(cfg1.ssid, "netA", 5);
    memcpy(cfg2.ssid, "netB", 5);
    CHECK(fletcher16((uint8_t *)&cfg1, 114) != fletcher16((uint8_t *)&cfg2, 114));
}

/* C4: after erase, config is invalid */
static void test_config_is_valid_false_after_erase(void) {
    TEST("config invalid after erase");
    reset_flash();
    config_t cfg;
    CHECK_EQ(config_load(&cfg), 0);
    CHECK(!config_is_valid());
}

/* C5: after save, config is valid */
static void test_config_save_and_is_valid(void) {
    TEST("config valid after save");
    reset_flash();
    config_t cfg;
    memcpy(cfg.magic, CONFIG_MAGIC, 4);
    cfg.version = CONFIG_VERSION;
    cfg.flags = CONFIG_FLAG_VALID;
    memcpy(cfg.ssid, "testnet", 8);
    memcpy(cfg.password, "pass1234", 9);
    cfg.checksum = fletcher16((uint8_t *)&cfg, 114);
    CHECK_EQ(config_save(&cfg), 0);
    CHECK(config_is_valid());
}

/* C6: save + load roundtrip */
static void test_config_save_and_load_roundtrip(void) {
    TEST("config save and load roundtrip");
    reset_flash();
    config_t orig, loaded;
    memcpy(orig.magic, CONFIG_MAGIC, 4);
    orig.version = CONFIG_VERSION;
    orig.flags = CONFIG_FLAG_VALID;
    memcpy(orig.ssid, "roundtrip_net", 14);
    memcpy(orig.password, "secret_pw", 10);
    orig.checksum = fletcher16((uint8_t *)&orig, 114);
    CHECK_EQ(config_save(&orig), 0);
    CHECK_EQ(config_load(&loaded), 0);
    CHECK_STREQ(loaded.ssid, "roundtrip_net");
    CHECK_STREQ(loaded.password, "secret_pw");
}

/* C7: erase clears validity */
static void test_config_erase_clears_valid(void) {
    TEST("config erase clears validity");
    reset_flash();
    config_t cfg;
    memcpy(cfg.magic, CONFIG_MAGIC, 4);
    cfg.version = CONFIG_VERSION;
    cfg.flags = CONFIG_FLAG_VALID;
    memcpy(cfg.ssid, "temp", 5);
    memcpy(cfg.password, "temp", 5);
    cfg.checksum = fletcher16((uint8_t *)&cfg, 114);
    config_save(&cfg);
    CHECK(config_is_valid());
    config_erase();
    CHECK(!config_is_valid());
}

/* C8: bad magic → invalid */
static void test_config_bad_magic_invalid(void) {
    TEST("bad magic makes config invalid");
    reset_flash();
    config_t cfg;
    memcpy(cfg.magic, "XXXX", 4);
    cfg.version = CONFIG_VERSION;
    cfg.flags = CONFIG_FLAG_VALID;
    cfg.checksum = fletcher16((uint8_t *)&cfg, 114);
    config_save(&cfg);
    CHECK(!config_is_valid());
}

/* C9: bad checksum → invalid */
static void test_config_bad_checksum_invalid(void) {
    TEST("bad checksum makes config invalid");
    reset_flash();
    config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.magic, CONFIG_MAGIC, 4);
    cfg.version = CONFIG_VERSION;
    cfg.flags = CONFIG_FLAG_VALID;
    memcpy(cfg.ssid, "badck", 6);
    memcpy(cfg.password, "pass", 5);
    cfg.checksum = fletcher16((uint8_t *)&cfg, sizeof(cfg) - 2);
    config_save(&cfg);

    /* Now corrupt the data in flash directly (bypass config_save which
     * recomputes the checksum). Flip a byte in the SSID. */
    uint8_t *raw = mock_flash_get_base();
    raw[10] = ~raw[10]; /* flip byte in SSID area */

    CHECK(!config_is_valid());
}

/* C10: empty SSID rejected */
static void test_config_empty_ssid_rejected(void) {
    TEST("empty SSID rejected");
    reset_flash();
    config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.magic, CONFIG_MAGIC, 4);
    cfg.version = CONFIG_VERSION;
    cfg.flags = CONFIG_FLAG_VALID;
    cfg.ssid[0] = '\0';
    memcpy(cfg.password, "pass", 5);
    cfg.checksum = fletcher16((uint8_t *)&cfg, 114);
    CHECK_EQ(config_save(&cfg), -1);
    CHECK(!config_is_valid());
}

/* C11: SSID truncated to 31 chars + null */
static void test_config_ssid_truncated(void) {
    TEST("SSID truncated to 31 chars");
    reset_flash();
    config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.magic, CONFIG_MAGIC, 4);
    cfg.version = CONFIG_VERSION;
    cfg.flags = CONFIG_FLAG_VALID;
    /* 40-char SSID — should be truncated to 31+null */
    for (int i = 0; i < 40; i++) cfg.ssid[i] = 'A' + (i % 26);
    cfg.ssid[39] = '\0'; /* original has null at end */
    memcpy(cfg.password, "pass", 5);
    cfg.checksum = fletcher16((uint8_t *)&cfg, 114);
    config_save(&cfg);
    config_t loaded;
    config_load(&loaded);
    CHECK(strlen(loaded.ssid) <= 31);
    CHECK(loaded.ssid[31] == '\0');
}

/* C12: double save — second wins */
static void test_config_double_save_overwrites(void) {
    TEST("double save overwrites");
    reset_flash();
    config_t cfg1, cfg2;
    memcpy(cfg1.magic, CONFIG_MAGIC, 4);
    cfg1.version = CONFIG_VERSION;
    cfg1.flags = CONFIG_FLAG_VALID;
    memcpy(cfg1.ssid, "first", 6);
    memcpy(cfg1.password, "p1", 3);
    cfg1.checksum = fletcher16((uint8_t *)&cfg1, 114);
    config_save(&cfg1);

    memcpy(cfg2.magic, CONFIG_MAGIC, 4);
    cfg2.version = CONFIG_VERSION;
    cfg2.flags = CONFIG_FLAG_VALID;
    memcpy(cfg2.ssid, "second", 7);
    memcpy(cfg2.password, "p2", 3);
    cfg2.checksum = fletcher16((uint8_t *)&cfg2, 114);
    config_save(&cfg2);

    config_t loaded;
    config_load(&loaded);
    CHECK_STREQ(loaded.ssid, "second");
    CHECK_STREQ(loaded.password, "p2");
}

/* C14: v2 config saves and loads effect fields correctly */
static void test_config_v2_effect_fields(void) {
    TEST("v2 config saves and loads effect fields");
    reset_flash();
    config_t orig, loaded;
    memset(&orig, 0, sizeof(orig));
    memcpy(orig.magic, CONFIG_MAGIC, 4);
    orig.version = CONFIG_VERSION;
    orig.flags = CONFIG_FLAG_VALID;
    memcpy(orig.ssid, "v2net", 6);
    memcpy(orig.password, "pw", 3);
    orig.effects_mode = EFFECT_MODE_AUTO;
    orig.effect_id = 3;       /* EFFECT_CHASE */
    orig.speed = 64;
    orig.brightness = 128;
    orig.color_r = 255;
    orig.color_g = 128;
    orig.color_b = 64;
    orig.color2_r = 0;
    orig.color2_g = 0;
    orig.color2_b = 255;
    orig.checksum = 0;
    config_save(&orig);
    config_load(&loaded);
    CHECK_EQ(loaded.effects_mode, EFFECT_MODE_AUTO);
    CHECK_EQ(loaded.effect_id, 3);
    CHECK_EQ(loaded.speed, 64);
    CHECK_EQ(loaded.brightness, 128);
    CHECK_EQ(loaded.color_r, 255);
    CHECK_EQ(loaded.color_g, 128);
    CHECK_EQ(loaded.color_b, 64);
    CHECK_EQ(loaded.color2_r, 0);
    CHECK_EQ(loaded.color2_g, 0);
    CHECK_EQ(loaded.color2_b, 255);
}

/* C15: v1 config is rejected after version bump */
static void test_config_v1_rejected(void) {
    TEST("v1 config rejected after version bump");
    reset_flash();
    config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.magic, CONFIG_MAGIC, 4);
    cfg.version = 0x0001;
    cfg.flags = CONFIG_FLAG_VALID;
    memcpy(cfg.ssid, "old", 4);
    memcpy(cfg.password, "pw", 3);
    cfg.checksum = 0;
    config_save(&cfg);
    CHECK(!config_is_valid());
}

/* C16: effects_mode field roundtrips through save/load */
static void test_config_effects_mode_roundtrip(void) {
    TEST("effects_mode roundtrips through save/load");
    reset_flash();
    config_t orig, loaded;
    memset(&orig, 0, sizeof(orig));
    memcpy(orig.magic, CONFIG_MAGIC, 4);
    orig.version = CONFIG_VERSION;
    orig.flags = CONFIG_FLAG_VALID;
    memcpy(orig.ssid, "mode", 5);
    memcpy(orig.password, "pw", 3);
    orig.effects_mode = EFFECT_MODE_AUTO;
    orig.checksum = 0;
    config_save(&orig);
    config_load(&loaded);
    CHECK_EQ(loaded.effects_mode, EFFECT_MODE_AUTO);
}

/* C13: max-length password (63 chars) */
static void test_config_max_length_password(void) {
    TEST("max-length password (63 chars)");
    reset_flash();
    config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    memcpy(cfg.magic, CONFIG_MAGIC, 4);
    cfg.version = CONFIG_VERSION;
    cfg.flags = CONFIG_FLAG_VALID;
    memcpy(cfg.ssid, "net", 4);
    for (int i = 0; i < 63; i++) cfg.password[i] = 'x';
    cfg.password[63] = '\0';
    cfg.checksum = fletcher16((uint8_t *)&cfg, 114);
    CHECK_EQ(config_save(&cfg), 0);
    config_t loaded;
    config_load(&loaded);
    CHECK_EQ(strlen(loaded.password), 63u);
    CHECK_STREQ(loaded.password, cfg.password);
}

/* ═════════════════════════════════════════════════════════════════════
/* ── Runner ────────────────────────────────────────────────────────── */

int main(void) {
    test_config_struct_size();
    test_config_checksum_deterministic();
    test_config_checksum_changes_with_data();
    test_config_is_valid_false_after_erase();
    test_config_save_and_is_valid();
    test_config_save_and_load_roundtrip();
    test_config_erase_clears_valid();
    test_config_bad_magic_invalid();
    test_config_bad_checksum_invalid();
    test_config_empty_ssid_rejected();
    test_config_ssid_truncated();
    test_config_double_save_overwrites();
    test_config_v2_effect_fields();
    test_config_v1_rejected();
    test_config_effects_mode_roundtrip();
    test_config_max_length_password();

    printf("\n%d tests run, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
