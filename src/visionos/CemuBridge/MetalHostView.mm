#import "MetalHostView.h"
#include <os/log.h>

static os_log_t metalViewLog() {
    static os_log_t log = os_log_create("org.cemu.CemuVision", "MetalHostView");
    return log;
}

@implementation MetalHostView

+ (Class)layerClass
{
    return [CAMetalLayer class];
}

- (CAMetalLayer *)metalLayer
{
    return (CAMetalLayer *)self.layer;
}

- (instancetype)initWithFrame:(CGRect)frame
{
    self = [super initWithFrame:frame];
    if (self) {
        self.backgroundColor = [UIColor blackColor];
        // Set default pixel format; will be overridden by -configureWithDevice:.
        self.metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        self.metalLayer.framebufferOnly = YES;
    }
    return self;
}

- (void)configureWithDevice:(id<MTLDevice>)device
{
    CAMetalLayer *layer = self.metalLayer;
    layer.device = device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = YES;

    // visionOS uses displayScale from the trait collection.
    CGFloat scale = self.traitCollection.displayScale;
    if (scale < 1.0)
        scale = 1.0; // Safety fallback.
    layer.contentsScale = scale;

    CGSize drawableSize = self.bounds.size;
    drawableSize.width  *= scale;
    drawableSize.height *= scale;
    layer.drawableSize = drawableSize;

    os_log_info(metalViewLog(),
                "Configured: device=%{public}s scale=%.1f drawable=%.0fx%.0f",
                [[device name] UTF8String], scale,
                drawableSize.width, drawableSize.height);
}

- (void)layoutSubviews
{
    [super layoutSubviews];

    CAMetalLayer *layer = self.metalLayer;
    CGFloat scale = self.traitCollection.displayScale;
    if (scale < 1.0)
        scale = 1.0;
    layer.contentsScale = scale;

    CGSize drawableSize = self.bounds.size;
    drawableSize.width  *= scale;
    drawableSize.height *= scale;
    layer.drawableSize = drawableSize;

    // Notify the delegate (Swift side) that layout happened so it can provide the layer.
    if ([self.delegate respondsToSelector:@selector(metalHostViewDidLayout:)]) {
        [self.delegate metalHostViewDidLayout:self];
    }
}

@end
