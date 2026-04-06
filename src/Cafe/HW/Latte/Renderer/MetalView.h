#pragma once

#if __APPLE__
#include <TargetConditionals.h>
#endif

#if TARGET_OS_VISION

// On visionOS, the Metal view is a UIView subclass provided by the
// visionOS app shell (MetalHostView).  This header is not used.

#else

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

@interface MetalView : NSView
@end

#endif // TARGET_OS_VISION
