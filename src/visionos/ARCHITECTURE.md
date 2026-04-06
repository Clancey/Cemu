# Cemu visionOS Port -- Architecture Design Document

## Overview

This document describes the architecture for porting Cemu to visionOS (Apple Vision Pro). The initial target is a flat 2D window mode where the emulator's Metal-rendered output is displayed in a standard visionOS window. A future phase will add immersive VR rendering via CompositorServices.

Cemu is a C++20 codebase with an existing Metal rendering backend (using metal-cpp headers and Objective-C++ `.mm` files for layer management). The macOS port uses wxWidgets for its GUI and AppKit for Metal layer hosting. The visionOS port replaces both with SwiftUI and UIKit.

---

## 1. App Entry Point and Shell Architecture

### 1.1 SwiftUI App Lifecycle

visionOS apps use the SwiftUI App protocol. There is no `main()` entry point exposed to the developer; the `@main` attribute handles it. The app shell is pure Swift.

```swift
// CemuVisionApp.swift
import SwiftUI

@main
struct CemuVisionApp: App {
    @State private var emulatorCore = EmulatorCore()

    var body: some Scene {
        WindowGroup {
            EmulatorView(core: emulatorCore)
        }
        .defaultSize(width: 1280, height: 720)
    }
}
```

Key points:
- `WindowGroup` creates a standard 2D window. This is the correct container for flat mode -- no `ImmersiveSpace` needed yet.
- `.defaultSize(width:height:)` sets the initial window dimensions in points. On visionOS, 1 point = 1 point at the system's rendering scale; the system handles pixel density.
- The window behaves like a floating panel in the user's space.

### 1.2 Hosting the Metal Rendering Surface

The emulator's Metal output needs a `CAMetalLayer` to draw into. On visionOS (which uses UIKit, not AppKit), we wrap a UIView-backed Metal surface using `UIViewRepresentable`.

```swift
// MetalEmulatorView.swift
import SwiftUI
import MetalKit

struct MetalEmulatorView: UIViewRepresentable {
    let core: EmulatorCore

    func makeUIView(context: Context) -> MetalHostView {
        let view = MetalHostView()
        // Pass the CAMetalLayer pointer to C++ after the view is laid out
        return view
    }

    func updateUIView(_ uiView: MetalHostView, context: Context) {
        // Handle resize if needed
    }

    func makeCoordinator() -> Coordinator {
        Coordinator(core: core)
    }

    class Coordinator {
        let core: EmulatorCore
        init(core: EmulatorCore) { self.core = core }
    }
}
```

The backing UIView subclass provides the CAMetalLayer:

```objc
// MetalHostView.h (Objective-C++ for direct Metal/C++ interop)
#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>

@interface MetalHostView : UIView
@property (nonatomic, readonly) CAMetalLayer* metalLayer;
- (void)configureLayerWithDevice:(id<MTLDevice>)device;
@end
```

```objc
// MetalHostView.mm
@implementation MetalHostView

+ (Class)layerClass {
    return [CAMetalLayer class];
}

- (CAMetalLayer*)metalLayer {
    return (CAMetalLayer*)self.layer;
}

- (void)configureLayerWithDevice:(id<MTLDevice>)device {
    CAMetalLayer* layer = self.metalLayer;
    layer.device = device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = YES;
    layer.contentsScale = self.traitCollection.displayScale;
}

- (void)layoutSubviews {
    [super layoutSubviews];
    // Notify C++ of resize
    CGSize drawableSize = self.bounds.size;
    CGFloat scale = self.traitCollection.displayScale;
    drawableSize.width *= scale;
    drawableSize.height *= scale;
    self.metalLayer.drawableSize = drawableSize;
}

@end
```

### 1.3 Swift to C++ Bridge

There are two viable bridging strategies. The recommended approach for this project is the **Objective-C++ bridge** pattern, which is already used by the macOS Metal backend.

**Option A: Objective-C++ Bridge (Recommended)**

Create an Objective-C++ wrapper that Swift calls via a bridging header. This is the proven pattern already used in Cemu's macOS port.

```objc
// CemuBridge.h (exposed to Swift via bridging header)
#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>

@interface CemuBridge : NSObject

+ (instancetype)shared;

/// Initialize the emulator subsystems (called once at app start)
- (BOOL)initializeWithError:(NSError**)error;

/// Provide the CAMetalLayer for the main emulator display
- (void)setMainDisplayLayer:(CAMetalLayer*)layer
                      width:(int)width
                     height:(int)height;

/// Provide the CAMetalLayer for the GamePad display (if using dual view)
- (void)setPadDisplayLayer:(CAMetalLayer*)layer
                     width:(int)width
                    height:(int)height;

/// Load and launch a game from the given file path
- (BOOL)loadGameAtPath:(NSString*)path error:(NSError**)error;

/// Resize notification from the UI
- (void)resizeMainDisplay:(int)width height:(int)height;

/// Pause / resume emulation
- (void)pauseEmulation;
- (void)resumeEmulation;
- (void)stopEmulation;

/// Check emulation state
@property (nonatomic, readonly) BOOL isRunning;

@end
```

```objc
// CemuBridge.mm
#include "CemuBridge.h"
#include "Cafe/CafeSystem.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalRenderer.h"
#include "gui/interface/WindowSystem.h"
// ... other Cemu headers

@implementation CemuBridge

+ (instancetype)shared {
    static CemuBridge* instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{ instance = [[self alloc] init]; });
    return instance;
}

- (void)setMainDisplayLayer:(CAMetalLayer*)layer width:(int)width height:(int)height {
    // Store the layer handle in WindowSystem so MetalLayerHandle can find it
    auto& windowInfo = WindowSystem::GetWindowInfo();
    windowInfo.canvas_main.backend = WindowSystem::WindowHandleInfo::Backend::Cocoa;
    windowInfo.canvas_main.surface = (__bridge void*)layer;
    windowInfo.width = width;
    windowInfo.height = height;
}

// ... implementation continues
@end
```

**Option B: Swift/C++ Interop (Swift 5.9+)**

Xcode 15+ supports direct Swift/C++ interop. This eliminates the Objective-C layer but requires careful header management. It is viable for new code but may be premature for wrapping the large existing Cemu C++ surface. Consider for future phases.

### 1.4 Emulator Thread Model

The emulation must NOT run on the main thread. The main thread is owned by UIKit/SwiftUI for UI rendering.

```
Main Thread:     SwiftUI / UIKit event loop
                      |
                      v
Emulator Thread: CafeSystem::LaunchForegroundTitle() -- runs CPU emulation
                      |
                      v
GPU Thread:      Latte GPU emulation, calls MetalRenderer to issue Metal commands
                      |
                      v
Metal Layer:     Presents drawables to the CAMetalLayer on the visionOS window
```

This matches the existing macOS threading model. The key difference is that visionOS window management is handled by SwiftUI rather than wxWidgets.

---

## 2. Rendering Strategy for 2D Window Mode

### 2.1 Architecture

For flat 2D window mode, the rendering path is:

1. SwiftUI `WindowGroup` creates a standard visionOS window.
2. Inside that window, a `UIViewRepresentable` hosts a `UIView` subclass whose `layerClass` is `CAMetalLayer`.
3. The C++ `MetalLayerHandle` is given a pointer to this `CAMetalLayer` (via the bridge).
4. `MetalRenderer` draws frames to the layer using the existing metal-cpp pipeline.
5. The layer presents drawables normally via `MTL::CommandBuffer::presentDrawable()`.

**CompositorServices is NOT needed** for this mode. CompositorServices (`cp_layer_renderer`) is only required for fully immersive rendering where you supply per-eye content. A standard window with a CAMetalLayer is the correct approach for flat 2D content.

### 2.2 What Changes from the macOS Metal Backend

The existing Metal renderer code (`MetalRenderer.cpp`, `MetalLayerHandle.cpp`, pipeline caches, etc.) is almost entirely platform-agnostic -- it uses metal-cpp, not Objective-C Metal APIs directly. The platform-specific code is confined to:

| Component | macOS (Current) | visionOS (New) |
|-----------|-----------------|----------------|
| View class | `MetalView : NSView` (`MetalView.mm`) | `MetalHostView : UIView` (new `.mm` file) |
| Layer creation | `CreateMetalLayer()` takes `NSView*`, creates child `NSView` with `CAMetalLayer` | `CreateMetalLayer()` takes `UIView*` (or receives `CAMetalLayer*` directly) |
| Scale factor | `NSView.convertRectToBacking:` | `UIView.traitCollection.displayScale` (or `UIScreen` equivalent) |
| Window handle | `NSView*` passed via `WindowSystem::WindowHandleInfo.surface` | `CAMetalLayer*` passed directly (no need for an `NSView*` intermediate on visionOS) |
| Display link | `CVDisplayLink` (macOS) | `CADisplayLink` (UIKit-based) |
| Resize handling | `NSView` resize notification | `UIView.layoutSubviews` override |
| Window management | wxWidgets / `NSWindow` | SwiftUI `WindowGroup` / `UIWindow` (system managed) |
| AppKit headers | `#import <Cocoa/Cocoa.h>` | `#import <UIKit/UIKit.h>` |

### 2.3 Adapting MetalLayer.mm and MetalView.mm

The cleanest approach is to create visionOS-specific versions alongside the macOS originals:

**File: `src/visionos/MetalViewVisionOS.h`**
```objc
#pragma once
#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>

@interface MetalViewVisionOS : UIView
@end
```

**File: `src/visionos/MetalViewVisionOS.mm`**
```objc
#import "MetalViewVisionOS.h"

@implementation MetalViewVisionOS

+ (Class)layerClass {
    return [CAMetalLayer class];
}

- (void)layoutSubviews {
    [super layoutSubviews];
    CGFloat scale = self.traitCollection.displayScale;
    CAMetalLayer* metalLayer = (CAMetalLayer*)self.layer;
    metalLayer.contentsScale = scale;
    CGSize size = self.bounds.size;
    metalLayer.drawableSize = CGSizeMake(size.width * scale, size.height * scale);
}

@end
```

**File: `src/visionos/MetalLayerVisionOS.mm`**
```objc
#import "MetalViewVisionOS.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalLayer.h"

void* CreateMetalLayer(void* handle, float& scaleX, float& scaleY) {
    // On visionOS, handle is a UIView*
    UIView* parentView = (__bridge UIView*)handle;

    MetalViewVisionOS* childView = [[MetalViewVisionOS alloc]
        initWithFrame:parentView.bounds];
    childView.autoresizingMask = UIViewAutoresizingFlexibleWidth |
                                  UIViewAutoresizingFlexibleHeight;
    [parentView addSubview:childView];

    CGFloat scale = parentView.traitCollection.displayScale;
    scaleX = (float)scale;
    scaleY = (float)scale;

    return (__bridge void*)childView.layer;
}
```

Alternatively, pass the `CAMetalLayer*` directly from Swift, bypassing `CreateMetalLayer()` entirely. This avoids the `NSView*`/`UIView*` abstraction:

```objc
// In CemuBridge.mm -- direct layer injection
- (void)setMainDisplayLayer:(CAMetalLayer*)layer width:(int)w height:(int)h {
    // Wrap the Objective-C CAMetalLayer* as a metal-cpp CA::MetalLayer*
    CA::MetalLayer* cppLayer = (__bridge CA::MetalLayer*)layer;
    // Feed it directly to MetalLayerHandle or a new initialization path
}
```

### 2.4 Display Link on visionOS

macOS uses `CVDisplayLink`. visionOS uses `CADisplayLink` (the UIKit variant):

```swift
// In the Swift layer or Objective-C++:
let displayLink = CADisplayLink(target: self, selector: #selector(displayLinkFired))
displayLink.preferredFrameRateRange = CAFrameRateRange(
    minimum: 30, maximum: 90, preferred: 90  // Vision Pro supports up to 90Hz
)
displayLink.add(to: .main, forMode: .common)
```

However, Cemu's Metal renderer does not rely on a display link for frame pacing on macOS -- it uses `presentDrawable` on the command buffer, which blocks until the next vsync. This same approach works on visionOS. A display link is optional and only needed if you want to drive the frame loop from the display side rather than the emulator side.

### 2.5 Metal API Differences on visionOS

The Apple Vision Pro has an M2 chip. The Metal feature set is:

- **GPU Family**: Apple 8 (MTL::GPUFamilyApple8), Metal 3
- **Mesh shaders**: Supported
- **Ray tracing**: Supported
- **Unified memory**: Yes (always; `hasUnifiedMemory` returns `true`)
- **Framebuffer fetch**: Supported (Apple GPU family 2+)
- **Texture format support**: All Apple Silicon formats

Notable restrictions:
- **No `MTL::CopyAllDevices()`**: visionOS only has one GPU. Use `MTL::CreateSystemDefaultDevice()` only. The existing code in `MetalRenderer()` constructor already falls back to this, but the `CopyAllDevices` path will not compile on visionOS.
- **No discrete GPU selection**: The device selection UI in Cemu config is not applicable.
- **`StorageModeManaged` is not available**: Apple Silicon uses unified memory, so the code already uses `StorageModeShared` when `hasUnifiedMemory` is true. This path is correct for visionOS.
- **Pixel format `Depth24Unorm_Stencil8`**: Not supported on Apple Silicon. The existing code already checks this via `depth24Stencil8PixelFormatSupported()`.

### 2.6 Rendering Pipeline Summary

```
SwiftUI WindowGroup
    |
    +-- EmulatorView (SwiftUI)
         |
         +-- MetalEmulatorView (UIViewRepresentable)
              |
              +-- MetalHostView (UIView, layerClass = CAMetalLayer)
                   |
                   +-- CAMetalLayer <-- MetalLayerHandle.m_layer
                        |
                        +-- MetalRenderer draws here via metal-cpp
                             |
                             +-- presentDrawable() on MTL::CommandBuffer
```

---

## 3. Input on visionOS

### 3.1 Eye Tracking and Pinch Gestures (UI Navigation)

visionOS uses an indirect input model: the user looks at a target and pinches to select. For Cemu's UI (game library browser, settings), standard SwiftUI controls handle this automatically. No special code is needed for menus and lists -- SwiftUI's built-in hover effects and tap gestures work with eye tracking.

For the emulator display view itself:
- The system sends standard gesture events. Use SwiftUI gesture modifiers on the `MetalEmulatorView`.
- Tap gestures on the emulator view can be used for Wii U GamePad touch input (see 3.3).

```swift
MetalEmulatorView(core: emulatorCore)
    .onTapGesture { location in
        // Forward to Wii U GamePad touch
        emulatorCore.handleTouch(at: location)
    }
    .gesture(
        DragGesture()
            .onChanged { value in
                emulatorCore.handleTouchDrag(at: value.location)
            }
            .onEnded { _ in
                emulatorCore.handleTouchEnd()
            }
    )
```

### 3.2 Bluetooth Game Controller Support

The `GCController` framework (Game Controller framework) works on visionOS and is the primary way to handle physical controllers.

Supported controllers:
- Xbox Wireless Controller (Bluetooth)
- PlayStation DualSense / DualShock 4
- MFi controllers
- Nintendo Pro Controller (via Bluetooth, limited support)

```swift
import GameController

class GameControllerManager: ObservableObject {
    @Published var connectedController: GCController?

    init() {
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(controllerConnected),
            name: .GCControllerDidConnect,
            object: nil
        )
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(controllerDisconnected),
            name: .GCControllerDidDisconnect,
            object: nil
        )
        GCController.startWirelessControllerDiscovery {}
    }

    @objc func controllerConnected(_ notification: Notification) {
        guard let controller = notification.object as? GCController else { return }
        connectedController = controller
        setupControllerMapping(controller)
    }

    func setupControllerMapping(_ controller: GCController) {
        guard let extendedGamepad = controller.extendedGamepad else { return }

        extendedGamepad.valueChangedHandler = { [weak self] gamepad, element in
            // Map GCController inputs to Wii U controller state
            // Forward to Cemu's InputManager via the bridge
        }
    }
}
```

Cemu already has `InputManager` and controller abstraction. The visionOS port needs a new input provider that wraps `GCController` and feeds into the existing `ControllerState` structure. The Android port's `AndroidInput.h` is a useful reference for how to add a new platform input backend.

### 3.3 Wii U GamePad Touchscreen Emulation

The Wii U GamePad has a resistive touchscreen. On visionOS, this maps to:

**Option A: Direct touch on a second window (Recommended)**

Open a second SwiftUI window for the GamePad display. The user can look at it and use pinch-drag gestures to simulate touch.

```swift
WindowGroup(id: "gamepad") {
    GamePadView(core: emulatorCore)
}
.defaultSize(width: 854, height: 480)
```

**Option B: Indirect pointer**

When the emulator view is focused, map the system's spatial pointer position to GamePad touch coordinates. This works but is less intuitive.

For either option, the touch coordinates are translated to the Wii U GamePad's resolution (854x480) and forwarded to `vpad` input handling via the bridge.

---

## 4. File Access and Storage

### 4.1 App Sandbox

visionOS apps run in a strict sandbox (same as iOS). There is no open filesystem access.

**Loading ROMs/Games:**
- Use `UIDocumentPickerViewController` (wrapped in `UIViewControllerRepresentable`) or the SwiftUI `.fileImporter()` modifier.
- The user selects a folder containing the game. The app receives a security-scoped URL.
- The security-scoped bookmark must be persisted so the app can access the folder on subsequent launches without re-prompting.

```swift
.fileImporter(
    isPresented: $showFilePicker,
    allowedContentTypes: [.folder],
    allowsMultipleSelection: false
) { result in
    switch result {
    case .success(let urls):
        guard let url = urls.first else { return }
        // Start security-scoped access
        guard url.startAccessingSecurityScopedResource() else { return }
        // Persist the bookmark
        let bookmarkData = try? url.bookmarkData(
            options: .minimalBookmark,
            includingResourceValuesForKeys: nil,
            relativeTo: nil
        )
        // Store bookmarkData in UserDefaults or a database
        // Pass the path to Cemu for loading
        emulatorCore.loadGame(at: url.path)
    case .failure(let error):
        print("File picker error: \(error)")
    }
}
```

### 4.2 Storage Locations

| Data | Location | Notes |
|------|----------|-------|
| App config (settings.xml) | `Application Support/` via `FileManager.default.urls(for: .applicationSupportDirectory)` | Survives app updates |
| Shader cache | `Application Support/shaderCache/` | Can be large (hundreds of MB); consider `Caches/` if regenerable |
| Save data (MLC) | `Application Support/mlc01/` | Critical user data |
| Graphic packs | `Application Support/graphicPacks/` | Bundled with app or downloaded |
| keys.txt | `Application Support/` | User must provide via file picker |
| Temporary files | `NSTemporaryDirectory()` | System may purge |

The `ActiveSettings` / path configuration in Cemu needs a visionOS-specific initialization that sets these paths:

```objc
// In CemuBridge.mm during initialization
- (void)initializePaths {
    NSString* appSupport = NSSearchPathForDirectoriesInDomains(
        NSApplicationSupportDirectory, NSUserDomainMask, YES
    ).firstObject;

    NSString* cemuBase = [appSupport stringByAppendingPathComponent:@"Cemu"];
    [[NSFileManager defaultManager] createDirectoryAtPath:cemuBase
                              withIntermediateDirectories:YES
                                               attributes:nil
                                                    error:nil];

    // Set Cemu's path configuration
    // ActiveSettings::SetPath(cemuBase.UTF8String);
    // ActiveSettings::SetMlcPath((cemuBase + "/mlc01").UTF8String);
    // etc.
}
```

### 4.3 iCloud Sync Considerations

For a future enhancement, save data (`mlc01/`) could be synced via iCloud using `NSFileManager.ubiquityContainerURL`. This would allow save transfer between a future iPadOS port and the visionOS version. Not needed for initial port.

---

## 5. Build Configuration

### 5.1 Xcode Project Structure

The visionOS app requires a native Xcode project (not CMake-generated) because:
- SwiftUI `@main` app entry point requires Swift compilation with the visionOS SDK.
- The app target must be a visionOS Application, not a command-line tool.
- Storyboard/launch screen and Info.plist configuration are required.

**Recommended structure:**

```
Cemu.xcodeproj/
  |
  +-- Targets:
  |     +-- CemuVision (visionOS Application)
  |     |     Sources: src/visionos/*.swift, src/visionos/*.mm
  |     |     Links: libCemuCore.a
  |     |
  |     +-- CemuCore (Static Library, visionOS)
  |           Sources: src/Cafe/**, src/Common/**, src/util/**, etc.
  |           Excludes: src/gui/wxgui/**, all Vulkan/OpenGL renderer files
  |           Compiler: C++20, Objective-C++
  |
  +-- CemuVision-Bridging-Header.h
        Imports: CemuBridge.h
```

Alternatively, use CMake to build `libCemuCore.a` for the `xros` platform (CMake 3.28+ has preliminary visionOS support via `-DCMAKE_SYSTEM_NAME=visionOS`), then link that static library from the Xcode-managed Swift app target.

### 5.2 SDK and Deployment Requirements

| Requirement | Value |
|-------------|-------|
| Xcode version | 16.0+ (visionOS 2.0 SDK) or 15.2+ (visionOS 1.0 SDK) |
| visionOS SDK | visionOS 1.0+ (initial), visionOS 2.0 for enhanced features |
| Minimum deployment target | visionOS 1.0 (equivalent to iOS 17 frameworks) |
| Swift version | 5.9+ |
| C++ standard | C++20 (already configured in CMakeLists.txt) |
| Metal | Metal 3 (Apple GPU Family 8) |
| Architecture | arm64 only |

### 5.3 Linking the C++ Static Library

The `CemuCore` static library contains all the emulation code. The Swift app links against it.

**Key build settings for CemuCore (static lib target):**
```
SDKROOT = xros
SUPPORTED_PLATFORMS = xros xrsimulator
ARCHS = arm64
CLANG_CXX_LANGUAGE_STANDARD = c++20
CLANG_ENABLE_OBJC_ARC = YES
HEADER_SEARCH_PATHS = $(SRCROOT)/src $(SRCROOT)/dependencies/...
OTHER_CPLUSPLUSFLAGS = -DTARGET_VISIONOS=1
```

**Key build settings for CemuVision (app target):**
```
SDKROOT = xros
SUPPORTED_PLATFORMS = xros xrsimulator
SWIFT_OBJC_BRIDGING_HEADER = src/visionos/CemuVision-Bridging-Header.h
OTHER_LDFLAGS = -lc++ -lstdc++
```

### 5.4 Conditional Compilation

Use a preprocessor macro to gate platform-specific code:

```cpp
// In C++/Objective-C++ code:
#if TARGET_OS_VISION
    // visionOS-specific code (UIKit, no NSView, no wxWidgets)
#elif TARGET_OS_OSX
    // macOS-specific code (AppKit, wxWidgets)
#elif TARGET_OS_IOS
    // iOS-specific code (if future iPad port)
#endif
```

`TARGET_OS_VISION` is defined by the visionOS SDK in `<TargetConditionals.h>` (Xcode 15.2+).

### 5.5 Dependencies to Exclude on visionOS

The following macOS dependencies are not available or not needed:

| Dependency | Status on visionOS | Action |
|------------|-------------------|--------|
| wxWidgets | Not available | Exclude entirely; SwiftUI replaces it |
| Vulkan / MoltenVK | Not needed | Exclude; Metal is the only renderer |
| OpenGL | Not available | Exclude |
| SDL2 | Not available on visionOS | Exclude; use GCController for input |
| CVDisplayLink | macOS only | Replace with CADisplayLink or omit |
| Cubeb (audio) | Needs testing | May work; fallback to AVAudioEngine |

### 5.6 Dependencies to Include

| Dependency | Notes |
|------------|-------|
| metal-cpp | Already used; works on visionOS (same Metal headers) |
| boost | Header-only parts work; filesystem may need config |
| fmt | Works |
| zlib / zstd | Available on visionOS |
| pugixml | Works (header-only) |
| imgui | Metal backend works; needs UIKit input backend for visionOS |

---

## 6. Key Differences from the macOS Metal Backend -- Summary

### 6.1 MetalLayer.mm / MetalView.mm

**Current macOS code** (`MetalLayer.mm`):
```objc
NSView* view = (NSView*)handle;
MetalView* childView = [[MetalView alloc] initWithFrame:view.bounds];
childView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
childView.wantsLayer = YES;
[view addSubview:childView];
```

**visionOS replacement:**
```objc
UIView* view = (__bridge UIView*)handle;
MetalViewVisionOS* childView = [[MetalViewVisionOS alloc] initWithFrame:view.bounds];
childView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
[view addSubview:childView];
```

Key API differences:
- `NSView` -> `UIView`
- `NSViewWidthSizable` -> `UIViewAutoresizingFlexibleWidth`
- `wantsLayer = YES` is not needed (UIView always has a layer)
- `convertRectToBacking:` -> `traitCollection.displayScale`
- `makeBackingLayer` -> `layerClass` class method (already the UIKit pattern)
- `layer:shouldInheritContentsScale:fromWindow:` -> handle in `traitCollectionDidChange:` or `layoutSubviews`

### 6.2 MetalRenderer Constructor -- Device Selection

```cpp
// macOS: MTL::CopyAllDevices() to enumerate GPUs
// visionOS: Only MTL::CreateSystemDefaultDevice() is available
// The existing fallback path handles this correctly.
```

Wrap the `CopyAllDevices` call:
```cpp
#if !TARGET_OS_VISION
    NS_STACK_SCOPED auto devices = MTL::CopyAllDevices();
    // ... enumerate
#endif
if (!m_device)
    m_device = MTL::CreateSystemDefaultDevice();
```

### 6.3 Display Link

macOS uses `CVDisplayLink` (Core Video). visionOS uses `CADisplayLink` (Core Animation / UIKit).

```objc
// macOS:
CVDisplayLinkCreateWithActiveCGDisplays(&displayLink);
CVDisplayLinkSetOutputCallback(displayLink, callback, context);
CVDisplayLinkStart(displayLink);

// visionOS:
CADisplayLink* link = [CADisplayLink displayLinkWithTarget:self selector:@selector(render)];
link.preferredFrameRateRange = CAFrameRateRangeMake(30, 90, 90);
[link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSDefaultRunLoopMode];
```

As noted in section 2.4, Cemu's Metal renderer does not currently use a display link for frame pacing, so this may not be needed for the initial port.

### 6.4 Window System Abstraction

The `WindowSystem` namespace needs a visionOS backend. The Android port provides a good reference for adding a new platform:

```cpp
// New: WindowSystem::WindowHandleInfo::Backend::VisionOS
// Or reuse Backend::Cocoa since both are Apple platforms using Metal
```

The `WindowInfo` structure's atomic dimensions need to be updated when the SwiftUI window resizes. This is driven from the Swift side via the bridge.

### 6.5 Audio

The macOS port uses Cubeb for audio output. On visionOS, the options are:
- **AVAudioEngine** with a source node (push model) -- recommended for visionOS.
- **Audio Unit** (lower level, more control).
- Cubeb may work if it has a CoreAudio backend that compiles for visionOS, but this is unverified.

The audio subsystem in Cemu has an `IAudioAPI` abstraction. A new `AVAudioEngineAPI` implementation would be the cleanest approach.

---

## 7. Phase 2: Immersive VR Mode (Future)

This section is a brief outline for future planning. It is NOT part of the initial port.

### 7.1 CompositorServices

For immersive rendering (stereo, head-tracked), visionOS uses `CompositorServices`:

```swift
ImmersiveSpace(id: "emulator-vr") {
    CompositorLayer(configuration: ContentStageConfiguration()) { layerRenderer in
        // Called once; set up the Metal rendering loop
        startVRRenderLoop(layerRenderer)
    }
}
```

The `LayerRenderer` provides per-frame timing, head pose, and drawable textures for left/right eyes. The emulator's output would be rendered to a virtual screen quad in 3D space.

### 7.2 Stereoscopic Rendering

The emulator output is mono (2D). For VR mode, the same texture is projected onto a virtual flat screen in front of the user. No actual stereoscopic rendering of the game content is needed -- just placing a 2D quad in 3D space.

### 7.3 ARKit Integration

ARKit on visionOS provides hand tracking and world anchoring. Potential uses:
- Hand tracking for Wii Remote-style motion controls.
- Anchoring the emulator screen to a surface in the room.

---

## 8. Implementation Roadmap

### Phase 1: Flat Window Mode (MVP)

1. **Create Xcode project** with visionOS app target and CemuCore static library target.
2. **Implement `MetalViewVisionOS.mm`** -- UIView subclass with CAMetalLayer.
3. **Implement `CemuBridge.mm`** -- Objective-C++ bridge between Swift and C++.
4. **Implement SwiftUI app shell** -- `CemuVisionApp.swift`, `EmulatorView.swift`, `MetalEmulatorView.swift`.
5. **Adapt `MetalLayerHandle`** -- Accept CAMetalLayer directly or via UIView.
6. **Stub out wxWidgets** -- Replace all wxWidgets GUI dependencies with no-ops or the bridge.
7. **Add visionOS path configuration** -- Sandbox-aware file paths.
8. **Add GCController input backend** -- Map to Cemu's `ControllerState`.
9. **Add audio backend** -- AVAudioEngine implementation of `IAudioAPI`.
10. **Test with a game** -- Verify Metal rendering, input, and audio.

### Phase 2: Polish

- GamePad second window.
- File picker for ROM loading with bookmarks.
- Settings UI in SwiftUI.
- Performance profiling and optimization.

### Phase 3: Immersive VR Mode

- CompositorServices integration.
- Virtual screen placement in 3D space.
- Hand tracking exploration.

---

## Appendix A: File Inventory for visionOS Port

New files to create:

```
src/visionos/
    ARCHITECTURE.md              -- This document
    CemuVisionApp.swift          -- @main SwiftUI app entry point
    EmulatorView.swift           -- Main emulator screen SwiftUI view
    MetalEmulatorView.swift      -- UIViewRepresentable wrapping MetalHostView
    GamePadView.swift            -- Secondary window for GamePad display
    GameControllerManager.swift  -- GCController wrapper
    FileBrowserView.swift        -- ROM loading UI with file picker
    SettingsView.swift           -- Emulator settings UI
    EmulatorCore.swift           -- Swift-side state management (@Observable)
    CemuBridge.h                 -- Objective-C bridge header
    CemuBridge.mm                -- Objective-C++ bridge implementation
    MetalViewVisionOS.h          -- UIView subclass header
    MetalViewVisionOS.mm         -- UIView subclass implementation
    MetalLayerVisionOS.mm        -- CreateMetalLayer() for visionOS
    AudioEngineVisionOS.h        -- AVAudioEngine audio backend header
    AudioEngineVisionOS.mm       -- AVAudioEngine audio backend implementation
    WindowSystemVisionOS.h       -- WindowSystem implementation header
    WindowSystemVisionOS.mm      -- WindowSystem implementation
    CemuVision-Bridging-Header.h -- Swift/ObjC bridging header
```

Files to modify (with `#if TARGET_OS_VISION` guards):

```
src/Cafe/HW/Latte/Renderer/Metal/MetalRenderer.cpp  -- Guard CopyAllDevices()
src/Cafe/HW/Latte/Renderer/Metal/MetalLayerHandle.cpp -- Platform layer creation
src/gui/interface/WindowSystem.h  -- Add VisionOS backend enum
```

## Appendix B: Minimum Viable Bridge Header

```objc
// CemuVision-Bridging-Header.h
#import "CemuBridge.h"
```

This single import exposes the entire C++ emulator to Swift through the `CemuBridge` interface.
