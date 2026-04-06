#pragma once

#import <QuartzCore/CAMetalLayer.h>

#if TARGET_OS_VISION
#import <UIKit/UIKit.h>
@interface MetalView : UIView
@end
#else
#import <Cocoa/Cocoa.h>
@interface MetalView : NSView
@end
#endif
