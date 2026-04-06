#pragma once

#ifdef __ANDROID__
#include "FiberFContext.h"
#elif _WIN32
#include "FiberWin.h"
#else
#include "FiberUContext.h"
#endif