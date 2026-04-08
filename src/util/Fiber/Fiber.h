#pragma once

#if TARGET_OS_VISION
#include "FiberPThread.h"
#elif defined(__ANDROID__)
#include "FiberFContext.h"
#elif _WIN32
#include "FiberWin.h"
#else
#include "FiberUContext.h"
#endif