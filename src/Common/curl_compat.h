#pragma once

// Platform-specific curl availability
// curl is available on: Windows, Linux, macOS, Android (via vcpkg)
// curl is NOT available on: visionOS (pipe2 missing in libc)

#if defined(VISIONOS) || (defined(__APPLE__) && __has_include(<TargetConditionals.h>))
#include <TargetConditionals.h>
#if TARGET_OS_VISION
#define CEMU_HAS_CURL 0
#else
#define CEMU_HAS_CURL 1
#endif
#else
#define CEMU_HAS_CURL 1
#endif

#if CEMU_HAS_CURL
#include <curl/curl.h>
#endif
