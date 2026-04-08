#pragma once

#if TARGET_OS_VISION
// visionOS (device AND simulator): custom ARM64 assembly fiber
#include "FiberPThread.h"
#elif defined(__ANDROID__)
#include "FiberFContext.h"
#elif _WIN32
#include "FiberWin.h"
#else
#include "FiberUContext.h"
#endif