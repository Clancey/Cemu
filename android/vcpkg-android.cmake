# Custom vcpkg configuration for Cemu Android builds
# This file sets up vcpkg for cross-compilation to Android arm64

# Set the target triplet for Android arm64
set(VCPKG_TARGET_TRIPLET arm64-android CACHE STRING "")

# Set CMake system information for Android
set(VCPKG_CMAKE_SYSTEM_NAME Android)
set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

# Android-specific settings
set(ANDROID_ABI "arm64-v8a")
set(ANDROID_PLATFORM "android-29")
set(CMAKE_ANDROID_ARCH_ABI "arm64-v8a")
set(ANDROID_PLATFORM_LEVEL 29)

# Set NDK path from environment variable if available
if(DEFINED ENV{ANDROID_NDK_HOME})
    set(CMAKE_ANDROID_NDK $ENV{ANDROID_NDK_HOME})
elseif(DEFINED ENV{ANDROID_NDK_ROOT})
    set(CMAKE_ANDROID_NDK $ENV{ANDROID_NDK_ROOT})
endif()

# Set Android toolchain
if(CMAKE_ANDROID_NDK)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_ANDROID_NDK}/build/cmake/android.toolchain.cmake")
endif()

# Ensure vcpkg finds the right toolchain
set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)

message(STATUS "vcpkg Android configuration loaded")
message(STATUS "  Target triplet: ${VCPKG_TARGET_TRIPLET}")
message(STATUS "  Android ABI: ${ANDROID_ABI}")
message(STATUS "  Android NDK: ${CMAKE_ANDROID_NDK}")