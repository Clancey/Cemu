# Cemu Android Porting Notes

This document describes the modifications made to port Cemu's core emulation functionality to Android.

## Summary of Changes

### Files Modified

1. **`src/main.cpp`**
   - Wrapped wxWidgets/desktop-specific includes in `#ifndef __ANDROID__` guards
   - Added Android-specific includes (`android/log.h`, `CemuAndroid.h`)
   - Extracted core initialization into `CemuCoreInit()` function
   - Wrapped desktop entry points (`main()`, `wWinMain()`) in `#ifndef __ANDROID__` guards  
   - Added Android-callable C functions for core functionality

2. **`src/mainLLE.cpp`**
   - Wrapped WindowSystem includes and calls in `#ifndef __ANDROID__` guards
   - Modified `mainEmulatorLLE()` to work without GUI on Android

3. **`src/Cafe/CafeSystem.cpp`**
   - Wrapped WindowSystem includes and calls in `#ifndef __ANDROID__` guards
   - Added fallback logging for Android when error dialogs aren't available

4. **`src/Cemu/Logging/CemuLogging.cpp`**
   - Added Android logcat support using `__android_log_print`
   - Maintained existing file-based logging for desktop

### Files Created

1. **`src/CemuAndroid.h`** - Header defining Android-callable functions
2. **`src/AndroidMain.cpp.example`** - Example Android native main implementation
3. **`src/CMakeLists_android.txt.example`** - Example CMake configuration for Android

## Android API Functions

The following C functions are exported for use by Android native code:

### Core Functions
- `cemuAndroid_coreInit()` - Initialize Cemu core (call first)
- `cemuAndroid_launchEmulatorLLE()` - Launch emulator in LLE mode
- `cemuAndroid_isTitleRunning()` - Check if a title is running
- `cemuAndroid_shutdownTitle()` - Shutdown current title
- `gameMeta_getTitleId()` - Get current title ID

### Window/Lifecycle Management
- `cemuAndroid_initWindowSystem(ANativeWindow*)` - Initialize window system
- `cemuAndroid_onResume()` - Handle app resume
- `cemuAndroid_onPause()` - Handle app pause  
- `cemuAndroid_onDestroy()` - Handle app destroy
- `cemuAndroid_onWindowChanged(ANativeWindow*)` - Handle window changes
- `cemuAndroid_onWindowResized(int, int)` - Handle window resize

### Input
- `cemuAndroid_handleInputEvent(AInputEvent*)` - Handle input events

## Key Design Decisions

1. **Minimal Changes**: Changes were kept minimal to avoid breaking desktop functionality
2. **Preprocessor Guards**: Used `#ifndef __ANDROID__` to exclude desktop-only code
3. **Core Extraction**: Extracted initialization into `CemuCoreInit()` for reuse
4. **C API**: Provided C-compatible API for easy JNI integration
5. **Logging**: Added Android logcat support while maintaining file logging

## Dependencies Removed/Guarded

- **wxWidgets**: All wxWidgets dependencies guarded for Android
- **WindowSystem**: Desktop window management guarded
- **X11**: XInitThreads() call guarded (Linux desktop only)

## Usage Example

```c
// Initialize Cemu core
cemuAndroid_coreInit();

// Set up window system
cemuAndroid_initWindowSystem(native_window);

// Launch emulator
cemuAndroid_launchEmulatorLLE();

// Check status
if (cemuAndroid_isTitleRunning()) {
    // Game is running
}
```

## Next Steps for Full Android Port

1. **Graphics**: Ensure Vulkan/OpenGL ES rendering works on Android
2. **Audio**: Verify audio subsystem works with Android audio APIs  
3. **Input**: Implement touch input mapping and gamepad support
4. **Storage**: Handle Android storage permissions and paths
5. **JNI Wrapper**: Create Java/Kotlin wrapper for the C API
6. **UI**: Create Android UI for game selection, settings, etc.
7. **Performance**: Optimize for mobile performance constraints

## Build Configuration

Use the provided `CMakeLists_android.txt.example` as a starting point for Android CMake configuration. Key requirements:

- Android NDK with API level 21+
- Link against `log`, `android`, and `native_app_glue` libraries
- Define `__ANDROID__` preprocessor macro
- Configure proper toolchain for ARM64/ARMv7

## Testing

The core modifications maintain desktop functionality while enabling Android compilation. Test both desktop and Android builds to ensure no regressions.