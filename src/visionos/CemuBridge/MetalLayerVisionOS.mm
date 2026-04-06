/// visionOS replacement for src/Cafe/HW/Latte/Renderer/Metal/MetalLayer.mm.
///
/// On macOS the function receives an NSView* handle, creates a child
/// MetalView (NSView subclass with CAMetalLayer), and returns the layer.
///
/// On visionOS the handle is already a CAMetalLayer* provided directly by the
/// Swift/UIKit layer through CemuBridge.  We simply return it and report a
/// 1:1 scale (the layer's drawableSize is configured by MetalHostView).

#if TARGET_OS_VISION

#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>
#include "Cafe/HW/Latte/Renderer/Metal/MetalLayer.h"

void* CreateMetalLayer(void* handle, float& scaleX, float& scaleY)
{
    // On visionOS the bridge passes a CAMetalLayer* directly.
    // Scale is already baked into the layer's drawableSize by MetalHostView.
    scaleX = 1.0f;
    scaleY = 1.0f;
    return handle;
}

#endif // TARGET_OS_VISION
