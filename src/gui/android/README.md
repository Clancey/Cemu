# Android Window System for Cemu

This directory contains the Android-specific window system implementation for Cemu, providing platform abstraction that decouples the emulation core from wxWidgets.

## Architecture

### Core Components

1. **AndroidWindowSystem.h/cpp** - Main window system that manages Android lifecycle and provides the same interface as the desktop version
2. **AndroidCanvas.h/cpp** - Android implementation of IRenderCanvas that manages Vulkan surface creation with ANativeWindow
3. **AndroidWindowSystemImpl.cpp** - Bridge that forwards WindowSystem namespace calls to AndroidWindowSystem implementation

### Integration Points

The Android window system integrates with:
- **VulkanRenderer** - Creates Android Vulkan surfaces using `VK_KHR_android_surface`
- **CafeSystem** - Handles emulation lifecycle (pause/resume)
- **Input System** - Processes Android input events
- **Main Application** - Provides lifecycle callbacks from Android framework

## Usage

### Android App Integration

The Android app should call these C++ entry points from `main.cpp`:

```cpp
// Initialize core systems
cemuAndroid_coreInit();

// Initialize window system when surface is available
cemuAndroid_initWindowSystem(nativeWindow);

// Handle lifecycle
cemuAndroid_onResume();
cemuAndroid_onPause(); 
cemuAndroid_onDestroy();

// Handle window changes
cemuAndroid_onWindowChanged(newWindow);
cemuAndroid_onWindowResized(width, height);

// Handle input
cemuAndroid_handleInputEvent(inputEvent);
```

### Window System API

The Android implementation provides the same WindowSystem namespace interface as desktop platforms:

```cpp
// These work identically on Android and desktop
WindowSystem::GetWindowInfo();
WindowSystem::GetWindowSize(w, h);
WindowSystem::IsKeyDown(key);
WindowSystem::UpdateWindowTitles(idle, loading, fps);
```

## Lifecycle Management

### App States

- **Resumed** - App is visible and active, emulation runs
- **Paused** - App is paused or in background, emulation pauses  
- **Stopped** - App is being destroyed, emulation stops
- **Destroyed** - App is fully destroyed, cleanup completed

### Window Lifecycle

1. **APP_CMD_INIT_WINDOW** → Initialize AndroidWindowSystem with ANativeWindow
2. **APP_CMD_TERM_WINDOW** → Notify window destroyed  
3. **Window resize** → Update dimensions and notify renderer
4. **APP_CMD_DESTROY** → Full cleanup

## Vulkan Integration

### Surface Creation

The Android implementation uses `VK_KHR_android_surface` extension:

1. AndroidCanvas creates ANativeWindow handle info
2. VulkanRenderer.CreateFramebufferSurface() detects Android backend
3. VulkanRenderer.CreateAndroidSurface() creates VkSurfaceKHR from ANativeWindow
4. Normal Vulkan rendering proceeds with Android surface

### Required Extensions

- `VK_KHR_surface` (base surface extension)
- `VK_KHR_android_surface` (Android-specific surface creation)

## Build Integration

### CMake Configuration

The Android GUI components are built when `ANDROID` is defined:

```cmake
if(ANDROID)
    add_library(CemuAndroidGui STATIC
        android/AndroidWindowSystem.cpp
        android/AndroidWindowSystemImpl.cpp  
        android/AndroidCanvas.cpp
    )
    
    target_compile_definitions(CemuAndroidGui PRIVATE
        __ANDROID__=1
        VK_USE_PLATFORM_ANDROID_KHR=1
    )
endif()
```

### Conditional Compilation

All Android window system code is wrapped in `#if __ANDROID__` guards, ensuring no conflicts with desktop builds.

## Key Features

### Platform Abstraction
- Same WindowSystem namespace interface as desktop platforms
- Automatic backend detection via WindowHandleInfo::Backend::Android
- Drop-in replacement for wxWidgets window management

### Android Native Integration  
- Full ANativeWindow lifecycle management
- Proper app pause/resume handling
- Android input event processing
- Vulkan surface creation from native window

### Emulation Integration
- Automatic pause on app backgrounding
- Resume on app foregrounding
- Window resize handling for dynamic resolution
- Input forwarding to Cemu input system

## Files Created

- `/Users/clancey/Projects/Godot/Cemu/src/gui/android/AndroidWindowSystem.h` - Main window system interface
- `/Users/clancey/Projects/Godot/Cemu/src/gui/android/AndroidWindowSystem.cpp` - Implementation  
- `/Users/clancey/Projects/Godot/Cemu/src/gui/android/AndroidCanvas.h` - Canvas interface
- `/Users/clancey/Projects/Godot/Cemu/src/gui/android/AndroidCanvas.cpp` - Canvas implementation
- `/Users/clancey/Projects/Godot/Cemu/src/gui/android/AndroidWindowSystemImpl.cpp` - WindowSystem namespace bridge

## Files Modified

- `/Users/clancey/Projects/Godot/Cemu/src/gui/interface/WindowSystem.h` - Added Android backend enum
- `/Users/clancey/Projects/Godot/Cemu/src/Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.cpp` - Fixed Android surface parameter
- `/Users/clancey/Projects/Godot/Cemu/src/main.cpp` - Added Android entry points
- `/Users/clancey/Projects/Godot/Cemu/src/gui/CMakeLists.txt` - Added Android build support
- `/Users/clancey/Projects/Godot/Cemu/src/android/CMakeLists.txt` - Added GUI library link

The Android window system is now ready for integration and provides a complete platform abstraction that maintains compatibility with the existing Cemu codebase while adding native Android support.