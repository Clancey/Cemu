# Cemu Android Build

This directory contains the Android NDK build configuration for the Cemu Wii U emulator, specifically optimized for Meta Quest devices.

## Prerequisites

1. **Android Studio** with the following components:
   - Android SDK (API level 29+)
   - Android NDK (version 26.1.10909125 or later)
   - CMake (3.22.1 or later)

2. **Dependencies**: The Android build uses the system libraries and disables desktop-only features.

## Setup

1. Copy `local.properties.template` to `local.properties`
2. Edit `local.properties` to set the correct SDK and NDK paths
3. Ensure you have the required Android SDK and NDK versions installed

## Building

### Using Android Studio

1. Open the `android` directory in Android Studio
2. Let Android Studio sync the project
3. Build the project using "Build > Make Project"

### Using Command Line

```bash
cd android
./gradlew assembleRelease
```

## Configuration

The Android build is configured with the following settings:

### Enabled Features
- **Vulkan rendering** (required for Meta Quest)
- **SDL input/audio**
- **cubeb audio backend**
- **HIDAPI for controllers**

### Disabled Features
- **wxWidgets** (desktop UI framework)
- **OpenGL** (Quest uses Vulkan only)
- **DirectInput/XInput** (Windows-only)
- **DirectAudio/XAudio** (Windows-only)
- **Wayland/X11** (Linux desktop-only)
- **Bluez** (Linux Bluetooth)
- **Discord RPC**
- **vcpkg** (using system dependencies instead)

## Target Device

- **Platform**: Android API level 29+
- **Architecture**: arm64-v8a only
- **Primary Target**: Meta Quest devices (Quest, Quest 2, Quest Pro, Quest 3)

## Architecture

The Android build creates a shared library (`libcemu.so`) that:

1. Uses `android_native_app_glue` for the native activity lifecycle
2. Calls the existing Cemu `main()` function
3. Links against Android system libraries (Vulkan, EGL, etc.)

## File Structure

- `CMakeLists.txt` - Android-specific CMake configuration
- `app/build.gradle.kts` - Android app build configuration
- `app/src/main/AndroidManifest.xml` - App manifest with Quest-specific settings
- `app/src/main/cpp/android_main.cpp` - Native activity entry point
- `build.gradle.kts` - Root Gradle build file
- `settings.gradle.kts` - Gradle project settings
- `gradle.properties` - Android/NDK build properties

## Notes

- The build system automatically detects Android and configures appropriate settings
- Vulkan is treated as a system library on Android (no dlopen needed)
- The main CMakeLists.txt has been updated with Android platform detection
- Meta Quest-specific permissions and features are declared in the manifest