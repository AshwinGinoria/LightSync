cmake_minimum_required(VERSION 3.16)
project(SyncClient LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10.0.17134.0) # Minimum for Windows Graphics Capture
set(CMAKE_WINDOWS_KITS_VERSION "10.0.22000.0") # Your preferred SDK version

# Find OpenCV
find_package(OpenCV REQUIRED)

# Find required Qt modules
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# Collect effect source files from /effects
file(GLOB EFFECTS_SRC effects/*.cpp)

# Define the executable and all its sources
add_executable(SyncLights
    client.cpp
    effect_manager.cpp
    effect_manager_ui.cpp
    led_strip.cpp
    logger.cpp
    udp_server.cpp
    ${EFFECTS_SRC}
    app_icon.rc
)

# Include folders
target_include_directories(SyncLights PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/effects
    # Ensure Windows SDK headers for Graphics Capture
    $ENV{WindowsSDKDir}Include/$ENV{WindowsSDKVersion}um
    $ENV{WindowsSDKDir}Include/$ENV{WindowsSDKVersion}shared
    $ENV{WindowsSDKDir}Include/$ENV{WindowsSDKVersion}winrt
)

# Compile definitions
target_compile_definitions(SyncLights PRIVATE
    WINRT_LEAN_AND_MEAN
    NOMINMAX # Avoid conflicts with Windows macros
)

# Link libraries
target_link_libraries(SyncLights PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ${OpenCV_LIBS}
    d3d11
    dxgi
    dxguid
    windowsapp
    runtimeobject
)

# Set WIN32 executable property
set_target_properties(SyncLights PROPERTIES WIN32_EXECUTABLE TRUE)

# Optional: Enable C++/WinRT if used
# target_compile_options(SyncLights PRIVATE /ZW)
# find_program(CPPWINRT cppwinrt)
# if (CPPWINRT)
#     execute_process(COMMAND ${CPPWINRT} -input sdk -output include)
# endif()