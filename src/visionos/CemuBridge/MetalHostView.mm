#import "MetalHostView.h"
#import "CemuBridge.h"
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

- (BOOL)canBecomeFirstResponder
{
    return YES;
}

- (void)didMoveToWindow
{
    [super didMoveToWindow];
    if (self.window) {
        [self becomeFirstResponder];
    }
}

// Map UIKeyboardHIDUsage → virtual key code (matches InputManager.swift VKey)
static uint32_t vkeyForKey(UIKey *key)
{
    switch (key.keyCode) {
        case UIKeyboardHIDUsageKeyboardJ:             return 0x1000; // A
        case UIKeyboardHIDUsageKeyboardK:             return 0x1001; // B
        case UIKeyboardHIDUsageKeyboardI:             return 0x1002; // X
        case UIKeyboardHIDUsageKeyboardU:             return 0x1003; // Y
        case UIKeyboardHIDUsageKeyboardQ:             return 0x1004; // L
        case UIKeyboardHIDUsageKeyboardE:             return 0x1005; // R
        case UIKeyboardHIDUsageKeyboardZ:             return 0x1006; // ZL
        case UIKeyboardHIDUsageKeyboardC:             return 0x1007; // ZR
        case UIKeyboardHIDUsageKeyboardUpArrow:       return 0x1008; // DPad Up
        case UIKeyboardHIDUsageKeyboardDownArrow:     return 0x1009; // DPad Down
        case UIKeyboardHIDUsageKeyboardLeftArrow:     return 0x1010; // DPad Left
        case UIKeyboardHIDUsageKeyboardRightArrow:    return 0x1011; // DPad Right
        case UIKeyboardHIDUsageKeyboardReturnOrEnter: return 0x1012; // Plus/Start
        case UIKeyboardHIDUsageKeyboardDeleteOrBackspace: return 0x1013; // Minus
        case UIKeyboardHIDUsageKeyboardEscape:        return 0x1014; // Home
        case UIKeyboardHIDUsageKeyboardW:             return 0x1008; // DPad Up
        case UIKeyboardHIDUsageKeyboardS:             return 0x1009; // DPad Down
        case UIKeyboardHIDUsageKeyboardA:             return 0x1010; // DPad Left
        case UIKeyboardHIDUsageKeyboardD:             return 0x1011; // DPad Right
        default: return UINT32_MAX;
    }
}

- (void)pressesBegan:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event
{
    BOOL handled = NO;
    for (UIPress *press in presses) {
        if (press.key) {
            uint32_t vkey = vkeyForKey(press.key);
            if (vkey != UINT32_MAX) {
                [[CemuBridge shared] setKeyState:vkey pressed:YES];
                handled = YES;
            }
        }
    }
    if (!handled) [super pressesBegan:presses withEvent:event];
}

- (void)pressesEnded:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event
{
    BOOL handled = NO;
    for (UIPress *press in presses) {
        if (press.key) {
            uint32_t vkey = vkeyForKey(press.key);
            if (vkey != UINT32_MAX) {
                [[CemuBridge shared] setKeyState:vkey pressed:NO];
                handled = YES;
            }
        }
    }
    if (!handled) [super pressesEnded:presses withEvent:event];
}

- (void)pressesCancelled:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event
{
    for (UIPress *press in presses) {
        if (press.key) {
            uint32_t vkey = vkeyForKey(press.key);
            if (vkey != UINT32_MAX) {
                [[CemuBridge shared] setKeyState:vkey pressed:NO];
            }
        }
    }
    [super pressesCancelled:presses withEvent:event];
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
