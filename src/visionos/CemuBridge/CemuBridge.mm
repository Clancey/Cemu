#import "CemuBridge.h"

#include "Cafe/CafeSystem.h"
#include "Cafe/TitleList/TitleList.h"
#include "Cafe/TitleList/SaveList.h"
#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "Cafe/HW/Latte/Renderer/Metal/MetalRenderer.h"
#include "gui/interface/WindowSystem.h"
#include "config/ActiveSettings.h"
#include "config/CemuConfig.h"
#include "config/LaunchSettings.h"
#include "Common/ExceptionHandler/ExceptionHandler.h"
#include "input/InputManager.h"
#include "audio/IAudioAPI.h"
#include "audio/IAudioInputAPI.h"
#include "Cafe/GraphicPack/GraphicPack2.h"
#include "util/crypto/aes128.h"

#include <fmt/format.h>
#include <os/log.h>
#include <thread>

// Defined in main.cpp -- performs core subsystem init.
extern void CemuCoreInit();

static os_log_t cemuLog() {
    static os_log_t log = os_log_create("org.cemu.CemuVision", "Bridge");
    return log;
}

// ---------------------------------------------------------------------------
// MARK: - VisionOS WindowSystem Implementation
// ---------------------------------------------------------------------------

// This translation unit also provides the WindowSystem namespace
// implementation for visionOS, following the same pattern as the Android port.

namespace {
    WindowSystem::WindowInfo g_visionos_window_info{};
}

namespace WindowSystem
{
    void Create()
    {
        // No-op on visionOS.  Initialization is driven from the SwiftUI side
        // through CemuBridge.
    }

    void ShowErrorDialog(std::string_view message, std::string_view title,
                         std::optional<ErrorCategory> /*errorCategory*/)
    {
        NSString *nsTitle = title.empty()
            ? @"Cemu Error"
            : [[NSString alloc] initWithBytes:title.data()
                                       length:title.size()
                                     encoding:NSUTF8StringEncoding];
        NSString *nsMessage = [[NSString alloc] initWithBytes:message.data()
                                                       length:message.size()
                                                     encoding:NSUTF8StringEncoding];
        os_log_error(cemuLog(), "Error [%{public}@]: %{public}@", nsTitle, nsMessage);
    }

    WindowInfo& GetWindowInfo()
    {
        return g_visionos_window_info;
    }

    void UpdateWindowTitles(bool isIdle, bool isLoading, double fps)
    {
        // visionOS window titles are managed by SwiftUI.  Nothing to do.
    }

    void GetWindowSize(int& w, int& h)
    {
        w = g_visionos_window_info.width;
        h = g_visionos_window_info.height;
    }

    void GetPadWindowSize(int& w, int& h)
    {
        if (g_visionos_window_info.pad_open) {
            w = g_visionos_window_info.pad_width;
            h = g_visionos_window_info.pad_height;
        } else {
            w = 0;
            h = 0;
        }
    }

    void GetWindowPhysSize(int& w, int& h)
    {
        w = g_visionos_window_info.phys_width;
        h = g_visionos_window_info.phys_height;
    }

    void GetPadWindowPhysSize(int& w, int& h)
    {
        if (g_visionos_window_info.pad_open) {
            w = g_visionos_window_info.phys_pad_width;
            h = g_visionos_window_info.phys_pad_height;
        } else {
            w = 0;
            h = 0;
        }
    }

    double GetWindowDPIScale()
    {
        return g_visionos_window_info.dpi_scale;
    }

    double GetPadDPIScale()
    {
        return g_visionos_window_info.pad_open
            ? g_visionos_window_info.pad_dpi_scale.load()
            : 1.0;
    }

    bool IsPadWindowOpen()
    {
        return g_visionos_window_info.pad_open;
    }

    bool IsKeyDown(uint32 /*key*/)
    {
        return false;
    }

    bool IsKeyDown(PlatformKeyCodes /*key*/)
    {
        return false;
    }

    std::string GetKeyCodeName(uint32 key)
    {
        return fmt::format("key_{}", key);
    }

    bool InputConfigWindowHasFocus()
    {
        return g_visionos_window_info.app_active;
    }

    void NotifyGameLoaded()
    {
        os_log_info(cemuLog(), "Game loaded");
    }

    void NotifyGameExited()
    {
        os_log_info(cemuLog(), "Game exited");
    }

    void RefreshGameList()
    {
        os_log_info(cemuLog(), "Game list refresh requested");
    }

    bool IsFullScreen()
    {
        return false;
    }

    void CaptureInput(const ControllerState& /*currentState*/,
                      const ControllerState& /*lastState*/)
    {
        // Input is handled via GCController on visionOS.
    }
}

// ---------------------------------------------------------------------------
// MARK: - CemuBridge Implementation
// ---------------------------------------------------------------------------

@implementation CemuBridge {
    BOOL _initialized;
    CemuEmulationState _emulationState;
    std::thread _emulationThread;
}

+ (instancetype)shared
{
    static CemuBridge *instance = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        instance = [[CemuBridge alloc] init];
    });
    return instance;
}

- (instancetype)init
{
    self = [super init];
    if (self) {
        _initialized = NO;
        _emulationState = CemuEmulationStateStopped;
    }
    return self;
}

// MARK: - Initialization

- (BOOL)initializeWithError:(NSError **)error
{
    if (_initialized)
        return YES;

    @try {
        CemuCoreInit();
        _initialized = YES;
        os_log_info(cemuLog(), "Cemu core initialized successfully");
        return YES;
    }
    @catch (NSException *exception) {
        if (error) {
            *error = [NSError errorWithDomain:@"org.cemu.CemuVision"
                                         code:-1
                                     userInfo:@{
                NSLocalizedDescriptionKey:
                    [NSString stringWithFormat:@"Core init failed: %@", exception.reason]
            }];
        }
        os_log_error(cemuLog(), "Core init exception: %{public}@", exception.reason);
        return NO;
    }
}

- (void)setStoragePathsWithConfig:(NSString *)configPath
                            cache:(NSString *)cachePath
                             data:(NSString *)dataPath
{
    NSFileManager *fm = [NSFileManager defaultManager];
    // Ensure directories exist.
    for (NSString *dir in @[configPath, cachePath, dataPath]) {
        if (![fm fileExistsAtPath:dir]) {
            [fm createDirectoryAtPath:dir
          withIntermediateDirectories:YES
                           attributes:nil
                                error:nil];
        }
    }

    // Also create the mlc01 subdirectory inside configPath.
    NSString *mlcPath = [configPath stringByAppendingPathComponent:@"mlc01"];
    [fm createDirectoryAtPath:mlcPath
  withIntermediateDirectories:YES
                   attributes:nil
                        error:nil];

    std::set<fs::path> failedPaths;
    fs::path execPath = fs::path([[[NSBundle mainBundle] executablePath] UTF8String]);
    ActiveSettings::SetPaths(
        false, // not portable
        execPath,
        fs::path([configPath UTF8String]),
        fs::path([configPath UTF8String]),
        fs::path([cachePath UTF8String]),
        fs::path([dataPath UTF8String]),
        failedPaths
    );

    if (!failedPaths.empty()) {
        for (auto& p : failedPaths) {
            os_log_error(cemuLog(), "Failed write access to: %{public}s", p.c_str());
        }
    }

    os_log_info(cemuLog(), "Storage paths configured: config=%{public}s cache=%{public}s data=%{public}s",
                [configPath UTF8String], [cachePath UTF8String], [dataPath UTF8String]);
}

// MARK: - Display

- (void)setMainDisplayLayer:(CAMetalLayer *)layer width:(int)width height:(int)height
{
    auto& windowInfo = WindowSystem::GetWindowInfo();

    windowInfo.canvas_main.backend = WindowSystem::WindowHandleInfo::Backend::Cocoa;
    // On visionOS we pass the CAMetalLayer pointer directly.  The visionOS
    // variant of CreateMetalLayer() (MetalLayerVisionOS.mm) knows to treat
    // it as a layer rather than a UIView.
    windowInfo.canvas_main.surface = (__bridge void *)layer;

    windowInfo.window_main.backend = WindowSystem::WindowHandleInfo::Backend::Cocoa;
    windowInfo.window_main.surface = (__bridge void *)layer;

    windowInfo.width = width;
    windowInfo.height = height;
    windowInfo.phys_width = width;
    windowInfo.phys_height = height;
    windowInfo.dpi_scale = 1.0; // visionOS points are handled by the system.
    windowInfo.app_active = true;

    os_log_info(cemuLog(), "Main display layer set: %dx%d", width, height);
}

- (void)setPadDisplayLayer:(CAMetalLayer *)layer width:(int)width height:(int)height
{
    auto& windowInfo = WindowSystem::GetWindowInfo();

    windowInfo.canvas_pad.backend = WindowSystem::WindowHandleInfo::Backend::Cocoa;
    windowInfo.canvas_pad.surface = (__bridge void *)layer;

    windowInfo.window_pad.backend = WindowSystem::WindowHandleInfo::Backend::Cocoa;
    windowInfo.window_pad.surface = (__bridge void *)layer;

    windowInfo.pad_open = true;
    windowInfo.pad_width = width;
    windowInfo.pad_height = height;
    windowInfo.phys_pad_width = width;
    windowInfo.phys_pad_height = height;
    windowInfo.pad_dpi_scale = 1.0;

    os_log_info(cemuLog(), "Pad display layer set: %dx%d", width, height);
}

- (void)resizeMainDisplayWidth:(int)width height:(int)height
{
    auto& windowInfo = WindowSystem::GetWindowInfo();
    windowInfo.width = width;
    windowInfo.height = height;
    windowInfo.phys_width = width;
    windowInfo.phys_height = height;

    if (g_renderer && g_renderer->GetType() == RendererAPI::Metal)
        static_cast<MetalRenderer*>(g_renderer.get())->ResizeLayer({width, height}, true);
}

- (void)resizePadDisplayWidth:(int)width height:(int)height
{
    auto& windowInfo = WindowSystem::GetWindowInfo();
    windowInfo.pad_width = width;
    windowInfo.pad_height = height;
    windowInfo.phys_pad_width = width;
    windowInfo.phys_pad_height = height;

    if (g_renderer && g_renderer->GetType() == RendererAPI::Metal)
        static_cast<MetalRenderer*>(g_renderer.get())->ResizeLayer({width, height}, false);
}

// MARK: - Emulation Control

- (BOOL)loadGameAtPath:(NSString *)path error:(NSError **)error
{
    if (!_initialized) {
        if (error) {
            *error = [NSError errorWithDomain:@"org.cemu.CemuVision"
                                         code:-2
                                     userInfo:@{
                NSLocalizedDescriptionKey: @"CemuBridge not initialized. Call initializeWithError: first."
            }];
        }
        return NO;
    }

    _emulationState = CemuEmulationStateLoading;
    os_log_info(cemuLog(), "Loading game: %{public}@", path);

    fs::path gamePath([path UTF8String]);
    auto status = CafeSystem::PrepareForegroundTitleFromStandaloneRPX(gamePath);
    if (status != CafeSystem::PREPARE_STATUS_CODE::SUCCESS) {
        // Attempt as a title ID path (WUD/WUX).
        // For standalone RPX that failed, report an error.
        _emulationState = CemuEmulationStateStopped;
        if (error) {
            *error = [NSError errorWithDomain:@"org.cemu.CemuVision"
                                         code:-3
                                     userInfo:@{
                NSLocalizedDescriptionKey:
                    [NSString stringWithFormat:@"Failed to prepare title at path: %@", path]
            }];
        }
        os_log_error(cemuLog(), "Failed to prepare title: %{public}@", path);
        return NO;
    }

    // Launch on a background thread -- this blocks until the title exits.
    _emulationState = CemuEmulationStateRunning;
    if (_emulationThread.joinable()) {
        _emulationThread.join();
    }
    _emulationThread = std::thread([self] {
        os_log_info(cemuLog(), "Emulation thread started");
        CafeSystem::LaunchForegroundTitle();
        os_log_info(cemuLog(), "Emulation thread finished");
        dispatch_async(dispatch_get_main_queue(), ^{
            self->_emulationState = CemuEmulationStateStopped;
        });
    });

    return YES;
}

- (void)pauseEmulation
{
    if (_emulationState != CemuEmulationStateRunning)
        return;

    _emulationState = CemuEmulationStatePaused;
    os_log_info(cemuLog(), "Emulation paused");
    // Cemu does not currently expose a formal pause API at the CafeSystem
    // level.  When one is added, call it here.  For now we rely on the
    // GPU thread checking the state flag.
}

- (void)resumeEmulation
{
    if (_emulationState != CemuEmulationStatePaused)
        return;

    _emulationState = CemuEmulationStateRunning;
    os_log_info(cemuLog(), "Emulation resumed");
}

- (void)stopEmulation
{
    if (_emulationState == CemuEmulationStateStopped)
        return;

    os_log_info(cemuLog(), "Stopping emulation");
    CafeSystem::ShutdownTitle();

    if (_emulationThread.joinable()) {
        _emulationThread.join();
    }
    _emulationState = CemuEmulationStateStopped;
    os_log_info(cemuLog(), "Emulation stopped");
}

// MARK: - State

- (BOOL)isRunning
{
    return CafeSystem::IsTitleRunning();
}

- (NSString *)currentTitleName
{
    if (!CafeSystem::IsTitleRunning())
        return nil;
    std::string name = CafeSystem::GetForegroundTitleName();
    if (name.empty())
        return nil;
    return [NSString stringWithUTF8String:name.c_str()];
}

- (uint64_t)currentTitleId
{
    return CafeSystem::GetForegroundTitleId();
}

// MARK: - Input

- (void)handleGamePadTouchAtX:(float)x y:(float)y
{
    // Forward to VPAD touch input.
    // The coordinates should be in Wii U GamePad space (854x480).
    // TODO: Wire into Cemu's VPAD touch input system once a C-callable
    // API is exposed.  The touch state is written into the VPAD ring buffer
    // by the input provider; a visionOS-specific provider needs to be
    // registered with InputManager.
    os_log_debug(cemuLog(), "GamePad touch: (%.1f, %.1f)", x, y);
}

- (void)handleGamePadTouchEnd
{
    os_log_debug(cemuLog(), "GamePad touch ended");
}

@end
