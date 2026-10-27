cmake_minimum_required(VERSION 3.24)
project(TitanStorageHub VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler definitions and flags
if (WIN32)
    add_compile_definitions(NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

if (MSVC)
    add_compile_options(/W4 /permissive- /Zc:preprocessor)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-variable -Wno-unused-but-set-variable)
    if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        add_compile_options(-Wno-packed-not-aligned)
    endif()
    if (WIN32)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
    endif()
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Standalone TitanStorageHub Core Interface Library
add_library(titanhub_core INTERFACE)
target_include_directories(titanhub_core INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Test Suite Executable (Linux & Windows)
add_executable(titanstoragehub_tests tests/test_hub.cpp)
target_link_libraries(titanstoragehub_tests PRIVATE titanhub_core)

if (WIN32)
    # Win32 Modern Desktop GUI Control Center
    add_executable(titanstoragehub WIN32 src/app/titanstoragehub.cpp)
    target_link_libraries(titanstoragehub PRIVATE titanhub_core user32 gdi32 gdiplus comctl32 dwmapi uxtheme shell32 advapi32)

    # Standalone Installer Packager
    add_executable(titanstoragehub_packager src/tools/titanstoragehub_packager.cpp)
    target_link_libraries(titanstoragehub_packager PRIVATE titanhub_core)

    # Standalone 1-Click GUI Installer
    add_executable(titanstoragehub_setup WIN32 src/installer/titanstoragehub_setup.cpp)
    target_link_libraries(titanstoragehub_setup PRIVATE titanhub_core user32 gdi32 gdiplus comctl32 dwmapi uxtheme ole32 oleaut32 shell32 advapi32)
    add_dependencies(titanstoragehub_setup titanstoragehub titanstoragehub_packager)

    set(WINFSP_MSI "${CMAKE_CURRENT_SOURCE_DIR}/redist/winfsp-2.1.25156.msi")
    set(PACKAGER_EXTRA_ARGS "")
    if (EXISTS "${WINFSP_MSI}")
        set(PACKAGER_EXTRA_ARGS "${WINFSP_MSI}")
    endif()

    add_custom_command(
        TARGET titanstoragehub_setup POST_BUILD
        COMMAND titanstoragehub_packager $<TARGET_FILE:titanstoragehub_setup>
                $<TARGET_FILE:titanstoragehub>
                ${CMAKE_CURRENT_SOURCE_DIR}/assets/titanstoragehub_hero.jpg
                ${PACKAGER_EXTRA_ARGS}
        COMMENT "Embedding Titan Storage Hub application, WinFsp MSI, and assets into standalone titanstoragehub_setup.exe..."
    )
endif()

# ============================================================================
# CPack Packaging Configuration
# ============================================================================
set(CPACK_PACKAGE_NAME "TitanStorageHub")
set(CPACK_PACKAGE_VENDOR "MicaNT-Kernel")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Unified Drive Control Center for ZFS, ReFS, NTFS, and Btrfs on Windows")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

install(TARGETS titanstoragehub_tests RUNTIME DESTINATION bin)
if (WIN32)
    install(TARGETS titanstoragehub titanstoragehub_setup RUNTIME DESTINATION bin)
    install(DIRECTORY assets/ DESTINATION assets)
    install(DIRECTORY redist/ DESTINATION redist OPTIONAL)
endif()

set(CPACK_GENERATOR "ZIP")
include(CPack)

# CTest registration
enable_testing()
add_test(NAME TitanHubCoreTests COMMAND titanstoragehub_tests)
