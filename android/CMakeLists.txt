cmake_minimum_required(VERSION 3.21.1)

# Set up vcpkg before project() and before anything else
set(VCPKG_TARGET_TRIPLET arm64-android CACHE STRING "")

# Chain-load the Android NDK toolchain through vcpkg
# The NDK toolchain is passed by Gradle as CMAKE_TOOLCHAIN_FILE
# We need to redirect it: vcpkg becomes the toolchain, NDK gets chain-loaded
if(CMAKE_TOOLCHAIN_FILE AND NOT CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg")
    # Gradle passed the NDK toolchain - save it for chain-loading
    set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_TOOLCHAIN_FILE}" CACHE STRING "")
    # Override with vcpkg toolchain
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../dependencies/vcpkg/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file" FORCE)
elseif(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    # No toolchain passed, set up vcpkg and try to find NDK
    if(CMAKE_ANDROID_NDK)
        set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_ANDROID_NDK}/build/cmake/android.toolchain.cmake" CACHE STRING "")
    elseif(DEFINED ENV{ANDROID_NDK_HOME})
        set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "$ENV{ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake" CACHE STRING "")
    endif()
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../dependencies/vcpkg/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file")
endif()

project(CemuAndroid)

# Disable LTO/IPO - CMake's Clang module adds -fuse-ld=gold for IPO which doesn't exist on Android
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)
set(CMAKE_C_LINK_OPTIONS_IPO "" CACHE STRING "" FORCE)
set(CMAKE_CXX_LINK_OPTIONS_IPO "" CACHE STRING "" FORCE)

# Set Android-specific variables early
set(ANDROID_PLATFORM "android-29")
set(ANDROID_ABI "arm64-v8a")
set(CMAKE_ANDROID_ARCH_ABI "arm64-v8a")
set(ANDROID_PLATFORM_LEVEL 29)

# Ensure we're building for Android
if(NOT ANDROID)
    message(FATAL_ERROR "This CMakeLists.txt is only for Android builds")
endif()

# Force disable unsupported features for Android
set(ENABLE_WXWIDGETS OFF CACHE BOOL "Disable wxWidgets on Android" FORCE)
set(ENABLE_XINPUT OFF CACHE BOOL "Disable XInput on Android" FORCE)
set(ENABLE_DIRECTINPUT OFF CACHE BOOL "Disable DirectInput on Android" FORCE)
set(ENABLE_DIRECTAUDIO OFF CACHE BOOL "Disable DirectAudio on Android" FORCE)
set(ENABLE_XAUDIO OFF CACHE BOOL "Disable XAudio on Android" FORCE)
set(ENABLE_WAYLAND OFF CACHE BOOL "Disable Wayland on Android" FORCE)
set(ENABLE_BLUEZ OFF CACHE BOOL "Disable Bluez on Android" FORCE)
set(ENABLE_FERAL_GAMEMODE OFF CACHE BOOL "Disable GameMode on Android" FORCE)
set(ENABLE_OPENGL OFF CACHE BOOL "Disable OpenGL on Android (use Vulkan)" FORCE)

# Enable supported features for Android/Meta Quest
set(ENABLE_VULKAN ON CACHE BOOL "Enable Vulkan on Android" FORCE)
set(ENABLE_SDL ON CACHE BOOL "Enable SDL on Android" FORCE)
set(ENABLE_CUBEB ON CACHE BOOL "Enable cubeb audio on Android" FORCE)
set(ENABLE_HIDAPI ON CACHE BOOL "Enable HIDAPI on Android" FORCE)
set(ENABLE_DISCORD_RPC OFF CACHE BOOL "Disable Discord RPC on Android" FORCE)

# Enable vcpkg for Android builds
set(ENABLE_VCPKG ON CACHE BOOL "Enable vcpkg for Android build" FORCE)

# Set Android-specific compile definitions
add_compile_definitions(
    ANDROID
    __ANDROID__
    VK_USE_PLATFORM_ANDROID_KHR
    VULKAN_ANDROID
)

# Set Android C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Android-specific compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -Wall -Wextra")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -Wall -Wextra")

# Force lld linker - gold is not available on Android NDK darwin-x86_64
# CMake's Clang.cmake module sets -fuse-ld=gold for IPO/LTO - override it
set(CMAKE_C_LINK_OPTIONS_IPO "-fuse-ld=lld" CACHE STRING "" FORCE)
set(CMAKE_CXX_LINK_OPTIONS_IPO "-fuse-ld=lld" CACHE STRING "" FORCE)

# Explicitly disable SDL for Android
add_compile_definitions(HAS_SDL=0)

# Set release flags for optimization
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG -ffast-math")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O3 -DNDEBUG -ffast-math")

# Add the Android native app glue
find_library(NATIVE_APP_GLUE android_native_app_glue)
if(NOT NATIVE_APP_GLUE)
    # If not found, build it from the NDK
    set(ANDROID_NDK $ENV{ANDROID_NDK_ROOT})
    if(ANDROID_NDK)
        add_library(android_native_app_glue STATIC
            ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c)
        target_include_directories(android_native_app_glue PUBLIC
            ${ANDROID_NDK}/sources/android/native_app_glue)
    endif()
endif()

# Include the main CMakeLists.txt from the parent directory
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_BINARY_DIR}/cemu)

# Strip gold linker flag that may leak from vcpkg ports or dependency configs
string(REPLACE "-fuse-ld=gold" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
string(REPLACE "-fuse-ld=gold" "" CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
# Also override for all link types
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}" CACHE STRING "" FORCE)
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS}" CACHE STRING "" FORCE)

# Create Android native library
if(TARGET CemuBin)
    # CemuBin is already a shared library on Android and includes all our Android sources
    # Just rename it to the expected Android library name
    set_target_properties(CemuBin PROPERTIES
        OUTPUT_NAME cemu
        LINK_FLAGS "-Wl,--export-dynamic"
    )

    # Make sure required Android libraries are linked
    target_link_libraries(CemuBin PRIVATE
        android_native_app_glue
        android
        log
        vulkan
        EGL
        GLESv3
    )

    # Ensure android_native_app_glue is available
    if(NOT TARGET android_native_app_glue)
        find_library(NATIVE_APP_GLUE android_native_app_glue)
        if(NATIVE_APP_GLUE)
            add_library(android_native_app_glue SHARED IMPORTED)
            set_target_properties(android_native_app_glue PROPERTIES IMPORTED_LOCATION ${NATIVE_APP_GLUE})
        endif()
    endif()
endif()