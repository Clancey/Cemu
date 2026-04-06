#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Get the current title ID (if any) - available in both Android and non-Android builds
uint64_t gameMeta_getTitleId();

#ifdef __cplusplus
}
#endif

#ifdef __ANDROID__

// Forward declarations for Android types
struct ANativeWindow;
struct AInputEvent;

#ifdef __cplusplus
extern "C" {
#endif

// Core initialization - staged approach like SSimco
void cemuAndroid_initializeEmulation();
// Legacy function (calls initializeEmulation)
void cemuAndroid_coreInit();

// Window system initialization and management
void cemuAndroid_initWindowSystem(ANativeWindow* window);
void cemuAndroid_onResume();
void cemuAndroid_onPause();
void cemuAndroid_onDestroy();
void cemuAndroid_onWindowChanged(ANativeWindow* window);
void cemuAndroid_onWindowResized(int width, int height);

// Input handling - returns 1 if event was handled, 0 otherwise
int cemuAndroid_handleInputEvent(AInputEvent* event);

// Launch the emulator in LLE mode
void cemuAndroid_launchEmulatorLLE();

// Check if a title is currently running
int cemuAndroid_isTitleRunning();

// Shutdown the currently running title
void cemuAndroid_shutdownTitle();

#ifdef __cplusplus
}
#endif

#endif // __ANDROID__