cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(SelectScreen VERSION 1.0.0)

add_library(${PROJECT_NAME} SHARED
    src/main.cpp
    src/ScreenManager.cpp
)

if (WIN32)
    target_sources(${PROJECT_NAME} PRIVATE src/platform/WindowsScreen.cpp)
    target_compile_definitions(${PROJECT_NAME} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
elseif (APPLE)
    enable_language(OBJCXX)
    target_sources(${PROJECT_NAME} PRIVATE src/platform/MacScreen.mm)
    target_link_libraries(${PROJECT_NAME} "-framework Cocoa" "-framework CoreGraphics")
elseif (UNIX)
    target_sources(${PROJECT_NAME} PRIVATE src/platform/LinuxScreen.cpp)
    find_package(X11 REQUIRED)
    target_include_directories(${PROJECT_NAME} PRIVATE ${X11_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} ${X11_LIBRARIES} Xrandr)
endif()

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "GEODE_SDK environment variable is not set. Run 'geode sdk install' or set GEODE_SDK.")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)
setup_geode_mod(${PROJECT_NAME})
