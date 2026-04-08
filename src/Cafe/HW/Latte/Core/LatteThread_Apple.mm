// Autorelease pool helpers for Metal on background threads
#import <Foundation/Foundation.h>

static thread_local NSAutoreleasePool* s_lattePool = nil;

extern "C" void LatteThread_CreateAutoreleasePool()
{
    s_lattePool = [[NSAutoreleasePool alloc] init];
}

extern "C" void LatteThread_DrainAutoreleasePool()
{
    if (s_lattePool) {
        [s_lattePool drain];
        s_lattePool = [[NSAutoreleasePool alloc] init];
    }
}
