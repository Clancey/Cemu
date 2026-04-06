#pragma once

#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>

/// UIView subclass whose backing layer is a CAMetalLayer.
///
/// This is the visionOS equivalent of the macOS `MetalView` (NSView subclass)
/// used by the existing desktop Metal backend.  The CAMetalLayer is configured
/// for BGRA8Unorm output with framebufferOnly optimisation.
@interface MetalHostView : UIView

/// The underlying CAMetalLayer (convenience accessor).
@property (nonatomic, readonly, nonnull) CAMetalLayer *metalLayer;

/// Configure the layer's Metal device and pixel format.
/// Call once after the view is inserted into the view hierarchy.
- (void)configureWithDevice:(nonnull id<MTLDevice>)device;

@end
