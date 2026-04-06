/*
 * This file redirects to the actual android_main implementation.
 * The real implementation is in src/android/AndroidMain.cpp
 */

// Forward declare the real android_main from AndroidMain.cpp
struct android_app;
extern void android_main(android_app* app);

// This ensures the symbol is exported for Android Studio/Gradle
extern "C" void android_main(android_app* app);