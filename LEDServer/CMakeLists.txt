# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NEVER EDIT THE NEXT LINES for Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
   set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(PICO_SDK_PATH ${USERHOME}/.pico-sdk/)
if(WIN32)
    set(pico-sdk-tools_DIR ${USERHOME}/.pico-sdk/tools)
    include(${pico-sdk-tools_DIR}/pico-sdk-tools-config.cmake)
    include(${pico-sdk-tools_DIR}/pico-sdk-tools-config-version.cmake)
endif()
# ====================================================================================
set(PICO_BOARD pico_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.4.0")
  message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.4.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

project(LEDServer C CXX ASM)

set(PICO_CXX_ENABLE_EXCEPTIONS 1)
set(PICO_CXX_ENABLE_RTTI 1)
set(PICO_CYW43_ARCH_DEBUG_ENABLED  1)

# === DEBUG: Trace malloc to find exact OOM allocation point ===
add_compile_definitions(PICO_DEBUG_MALLOC=1)

# Define lwIP options before SDK init so lwIP library targets pick them up.
# LWIP_NUM_NETIF_CLIENT_DATA must be ≥1 for mDNS (netif_alloc_client_data_id).
add_compile_definitions(LWIP_NUM_NETIF_CLIENT_DATA=1)

# Logging configuration
add_compile_definitions(LOG_LEVEL=6)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Include PicoLED for WS2812B integration
include("PicoLED/PicoLed.cmake")

# === Main firmware target ===
add_executable(LEDServer
  src/LEDServer.cpp
  src/mdns_service.c
  src/protocol_ddp.c
  src/effects_engine.c
  src/music_sync.c
  src/led_engine.c
  src/boot_flow.c
  src/flash_abstraction.c
  src/config_storage.c
  src/captive_dns.c
  src/httpd.c
  src/dhcpserver.c
  src/provisioning_http.c
  src/settings_http.c
  src/logger.c
)

pico_set_program_name(LEDServer "LEDServer")
pico_set_program_version(LEDServer "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(LEDServer 1)
pico_enable_stdio_usb(LEDServer 1)

# Add the standard include files to the build
target_include_directories(LEDServer PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}
  ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts or any other standard includes, if required
)

# Add the standard library to the build
target_link_libraries(LEDServer
  pico_cyw43_arch_lwip_poll
  pico_stdlib
  PicoLed
  pico_lwip_mdns
)

pico_add_extra_outputs(LEDServer)
