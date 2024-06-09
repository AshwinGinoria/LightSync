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

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Include PicoLED for WS2812B integration
include("PicoLED/PicoLed.cmake")

# Add executable. Default name is the project name, version 0.1

add_executable(LEDServer src/LEDServer.cpp)

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
)

pico_add_extra_outputs(LEDServer)
