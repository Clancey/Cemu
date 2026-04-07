#pragma once

#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>

@class MetalHostView;

/// Delegate protocol for MetalHostView layout notifications.
@protocol MetalHostViewDelegate <NSObject>
@optional
- (void)metalHostViewDidLayout:(nonnull MetalHostView *)view;
@end

/// UIView subclass whose backing layer is a CAMetalLayer.
@interface MetalHostView : UIView

/// The underlying CAMetalLayer (convenience accessor).
@property (nonatomic, readonly, nonnull) CAMetalLayer *metalLayer;

/// Delegate for layout notifications.
@property (nonatomic, weak, nullable) id<MetalHostViewDelegate> delegate;

/// Configure the layer's Metal device and pixel format.
- (void)configureWithDevice:(nonnull id<MTLDevice>)device;

@end
