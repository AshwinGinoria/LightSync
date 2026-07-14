cmake_minimum_required(VERSION 3.13)
project(ledserver_tdd C)
set(CMAKE_C_STANDARD 11)

# Override global LOG_LEVEL=LOG_LEVEL_DEBUG from main CMakeLists.txt
# (LOG_LEVEL_DEBUG isn't defined in test build context; use numeric value 6 = LEVEL_DEBUG)
add_compile_definitions(LOG_LEVEL=6)

# Source directory — resolve relative to this file's parent (LEDServer/)
# Works both on host and inside Docker (where CWD is the same tree)
get_filename_component(LED_SERVER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set(LED_SERVER_SRC "${LED_SERVER_DIR}/src")

# ── Effects Engine Tests ─────────────────────────────────────────────
add_executable(ledserver_tdd
    ${LED_SERVER_SRC}/led_engine.c
    ${LED_SERVER_SRC}/effects_engine.c
    test_effects.c
)
target_include_directories(ledserver_tdd PRIVATE ${LED_SERVER_SRC})

# ── DDP Protocol Tests ──────────────────────────────────────────────
add_executable(test_ddp
    ${LED_SERVER_SRC}/protocol_ddp.c
    ${LED_SERVER_SRC}/led_engine.c
    ${LED_SERVER_SRC}/effects_engine.c
    test_ddp.c
    led_strip_stub.c
)
target_include_directories(test_ddp PRIVATE
    ${LED_SERVER_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ── Music Sync Tests ────────────────────────────────────────────────
add_executable(test_music
    ${LED_SERVER_SRC}/music_sync.c
    ${LED_SERVER_SRC}/led_engine.c
    ${LED_SERVER_SRC}/effects_engine.c
    test_music.c
    led_strip_stub.c
)
target_include_directories(test_music PRIVATE
    ${LED_SERVER_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ── mDNS Tests ──────────────────────────────────────────────────────
add_executable(test_mdns
    ${LED_SERVER_SRC}/mdns_service.c
    test_mdns.c
)
target_include_directories(test_mdns PRIVATE
    ${LED_SERVER_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ── Config Storage Tests (Phase 1) ──────────────────────────────────
add_executable(test_config
    ${LED_SERVER_SRC}/config_storage.c
    ${LED_SERVER_SRC}/flash_abstraction.c
    hardware/flash.c
    test_config.c
)
target_include_directories(test_config PRIVATE
    ${LED_SERVER_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(test_config PRIVATE FLASH_MOCK)

target_compile_definitions(test_ddp PRIVATE FLASH_MOCK)
target_compile_definitions(test_music PRIVATE FLASH_MOCK)
target_compile_definitions(test_mdns PRIVATE FLASH_MOCK)

# ── Captive DNS Tests (Phase 2) ─────────────────────────────────────
add_executable(test_dns
    ${LED_SERVER_SRC}/captive_dns.c
    test_dns.c
)
target_include_directories(test_dns PRIVATE
    ${LED_SERVER_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ── HTTP Provisioning Tests (Phase 3) ───────────────────────────────
add_executable(test_provisioning
    ${LED_SERVER_SRC}/provisioning_http.c
    test_provisioning.c
)
target_include_directories(test_provisioning PRIVATE
    ${LED_SERVER_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ── Boot Flow Tests (Phase 4) ───────────────────────────────────────
add_executable(test_boot_flow
    ${LED_SERVER_SRC}/boot_flow.c
    ${LED_SERVER_SRC}/config_storage.c
    ${LED_SERVER_SRC}/flash_abstraction.c
    ${LED_SERVER_SRC}/logger.c
    hardware/flash.c
    test_boot_flow.c
)
target_include_directories(test_boot_flow PRIVATE
    ${LED_SERVER_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(test_boot_flow PRIVATE FLASH_MOCK)

# ── Settings HTTP Tests (Phase 3: form parser + HTML generator) ─────
add_executable(test_settings
    test_settings.c
    ${LED_SERVER_SRC}/settings_http.c
)
target_include_directories(test_settings PRIVATE
    ${LED_SERVER_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# ── HTTPD Tests (Phase 5: captive portal server) ────────────────────
add_executable(test_httpd
    ${LED_SERVER_SRC}/httpd.c
    ${LED_SERVER_SRC}/provisioning_http.c
    ${LED_SERVER_SRC}/config_storage.c
    ${LED_SERVER_SRC}/flash_abstraction.c
    ${LED_SERVER_SRC}/effects_engine.c
    ${LED_SERVER_SRC}/led_engine.c
    ${LED_SERVER_SRC}/settings_http.c
    ${LED_SERVER_SRC}/logger.c
    hardware/flash.c
    led_strip_stub.c
    lwip/tcp.c
    test_httpd.c
)
target_include_directories(test_httpd PRIVATE
    ${LED_SERVER_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(test_httpd PRIVATE FLASH_MOCK)

# ── Integration Tests (full provisioning flow) ──────────────────────
add_executable(test_integration
    ${LED_SERVER_SRC}/httpd.c
    ${LED_SERVER_SRC}/captive_dns.c
    ${LED_SERVER_SRC}/provisioning_http.c
    ${LED_SERVER_SRC}/config_storage.c
    ${LED_SERVER_SRC}/flash_abstraction.c
    ${LED_SERVER_SRC}/boot_flow.c
    ${LED_SERVER_SRC}/settings_http.c
    ${LED_SERVER_SRC}/logger.c
    hardware/flash.c
    lwip/tcp.c
    lwip/udp.c
    test_integration.c
)
target_include_directories(test_integration PRIVATE
    ${LED_SERVER_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(test_integration PRIVATE FLASH_MOCK)
