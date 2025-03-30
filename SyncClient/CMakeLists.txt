cmake_minimum_required(VERSION 3.16)
project(SyncClient LANGUAGES CXX)

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find OpenCV (assuming you have OpenCV installed and available)
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
)

# Link Qt libraries
target_link_libraries(SyncLights PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ${OpenCV_LIBS}
    dxgi
    d3d11
    dxguid
)

# set_target_properties(SyncLights PROPERTIES WIN32_EXECUTABLE TRUE)