#import "CemuBridge.h"

// Include precompiled header first to get all basic definitions
#include "Common/precompiled.h"

#include "Cafe/CafeSystem.h"
#include "Cafe/TitleList/TitleList.h"
#include "Cafe/TitleList/TitleInfo.h"
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
#include "input/api/Keyboard/KeyboardControllerProvider.h"
#ifdef VISIONOS
#include "input/api/VisionOS/VisionOSControllerProvider.h"
#endif
#include "audio/IAudioAPI.h"
#include "audio/IAudioInputAPI.h"
#include "Cafe/GraphicPack/GraphicPack2.h"
#include "util/crypto/aes128.h"

#include <fmt/format.h>
#include <os/log.h>
#include <thread>
#include <fstream>

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

    bool IsKeyDown(uint32 key)
    {
        bool state = g_visionos_window_info.get_keystate(key);
        if (state) {
            static int logCount = 0;
            if (logCount++ < 20)
                cemuLog_log(LogType::Force, "visionOS: IsKeyDown({}) = true", key);
        }
        return state;
    }

    bool IsKeyDown(PlatformKeyCodes key)
    {
        return g_visionos_window_info.get_keystate((uint32)key);
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
        // Profile must exist BEFORE CemuCoreInit calls InputManager::load()
        [self setupDefaultControllerProfile];
        CemuCoreInit();

        // Verify controller was loaded
        auto vpad = InputManager::instance().get_vpad_controller(0);
        if (vpad) {
            cemuLog_log(LogType::Force, "visionOS: VPAD controller loaded OK, has {} controllers", vpad->get_controllers().size());
        } else {
            cemuLog_log(LogType::Force, "visionOS: WARNING - VPAD controller is NULL!");
        }

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

    // Create the Metal renderer if not already created.
    if (!g_renderer) {
        cemuLog_log(LogType::Force, "visionOS: Creating Metal renderer");
        g_renderer = std::make_unique<MetalRenderer>();
        cemuLog_log(LogType::Force, "visionOS: Metal renderer created, g_renderer={}", (void*)g_renderer.get());
    }

    // Initialize/resize the layer in the renderer.
    auto* metalRenderer = MetalRenderer::GetInstance();
    if (metalRenderer) {
        cemuLog_log(LogType::Force, "visionOS: InitializeLayer {}x{}", width, height);
        metalRenderer->InitializeLayer({width, height}, true);
    } else {
        cemuLog_log(LogType::Force, "visionOS: WARNING - MetalRenderer::GetInstance() returned null");
    }

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
    CafeSystem::PREPARE_STATUS_CODE status = CafeSystem::PREPARE_STATUS_CODE::UNABLE_TO_MOUNT;

    // Try loading as a title (directory with meta/code/content, WUD, WUX, etc.)
    TitleInfo launchTitle{gamePath};
    os_log_info(cemuLog(), "TitleInfo valid=%d path=%{public}s", launchTitle.IsValid(), gamePath.generic_string().c_str());
    if (launchTitle.IsValid())
    {
        os_log_info(cemuLog(), "Loading valid title, appTitleId=%{public}016llx", launchTitle.GetAppTitleId());
        CafeTitleList::AddTitleFromPath(gamePath);
        fs::path parentPath = gamePath.parent_path();
        if (!parentPath.empty())
        {
            os_log_info(cemuLog(), "Scanning parent: %{public}s", parentPath.generic_string().c_str());
            CafeTitleList::AddScanPath(parentPath);
            CafeTitleList::Refresh();
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        TitleId baseTitleId;
        if (CafeTitleList::FindBaseTitleId(launchTitle.GetAppTitleId(), baseTitleId))
        {
            os_log_info(cemuLog(), "Found base title ID: %{public}016llx", baseTitleId);
            status = CafeSystem::PrepareForegroundTitle(baseTitleId);
            os_log_info(cemuLog(), "PrepareForegroundTitle status=%d", (int)status);

            // If mount failed, the game files may be on a File Provider path
            // that C++ can't access. Log the error for now.
            if (status != CafeSystem::PREPARE_STATUS_CODE::SUCCESS) {
                os_log_error(cemuLog(), "Mount failed (status=%d). Game files may need to be in the app's local storage. "
                    "File Provider / iCloud paths are not directly accessible.", (int)status);
            }
        }
        else
        {
            os_log_info(cemuLog(), "Could not find base title, trying standalone RPX");
            // Try finding RPX in code/ directory
            fs::path codePath = gamePath / "code";
            if (fs::exists(codePath)) {
                for (auto& entry : fs::directory_iterator(codePath)) {
                    if (entry.path().extension() == ".rpx") {
                        os_log_info(cemuLog(), "Trying RPX: %{public}s", entry.path().generic_string().c_str());
                        status = CafeSystem::PrepareForegroundTitleFromStandaloneRPX(entry.path());
                        os_log_info(cemuLog(), "StandaloneRPX status=%d", (int)status);
                        break;
                    }
                }
            }
        }
    }
    else if (fs::is_directory(gamePath))
    {
        // TitleInfo didn't recognize it, but it's a directory — try scanning and finding RPX
        os_log_info(cemuLog(), "TitleInfo invalid, scanning directory for RPX");
        fs::path codePath = gamePath / "code";
        bool codeExists = fs::exists(codePath);
        os_log_info(cemuLog(), "code/ exists=%d at %{public}s", codeExists, codePath.generic_string().c_str());
        if (codeExists)
        {
            for (auto& entry : fs::directory_iterator(codePath))
            {
                os_log_info(cemuLog(), "Found file: %{public}s", entry.path().filename().c_str());
                if (entry.path().extension() == ".rpx")
                {
                    os_log_info(cemuLog(), "Found RPX: %{public}s", entry.path().generic_string().c_str());
                    // Add parent as scan path for update/DLC discovery
                    CafeTitleList::AddScanPath(gamePath.parent_path());
                    CafeTitleList::AddTitleFromPath(gamePath);
                    CafeTitleList::Refresh();
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    // Try standalone RPX
                    status = CafeSystem::PrepareForegroundTitleFromStandaloneRPX(entry.path());
                    break;
                }
            }
        }
    }
    else
    {
        // Not a valid title structure — try as standalone RPX/ELF
        cemuLog_log(LogType::Force, "visionOS: Loading as standalone RPX");
        status = CafeSystem::PrepareForegroundTitleFromStandaloneRPX(gamePath);
    }

    if (status != CafeSystem::PREPARE_STATUS_CODE::SUCCESS) {
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

    // Launch emulation — LaunchForegroundTitle() starts scheduler threads
    // and returns immediately. We keep a thread alive to monitor for exit.
    _emulationState = CemuEmulationStateRunning;
    if (_emulationThread.joinable()) {
        _emulationThread.join();
    }
    _emulationThread = std::thread([self] {
        os_log_info(cemuLog(), "Emulation thread started");
        CafeSystem::LaunchForegroundTitle();
        os_log_info(cemuLog(), "Emulation launched, scheduler running");
        // Wait until the title stops running
        while (CafeSystem::IsTitleRunning()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        os_log_info(cemuLog(), "Emulation thread finished — title exited");
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

- (void)setupDefaultControllerProfile
{
    // Write a default VisionOS Controller → VPAD mapping if none exists
    fs::path profileDir = ActiveSettings::GetConfigPath("controllerProfiles");
    fs::path profilePath = profileDir / "controller0.xml";

    // Always recreate to pick up fixes (TODO: version check instead)
    if (fs::exists(profilePath))
        fs::remove(profilePath);

    std::error_code ec;
    fs::create_directories(profileDir, ec);

    // mapping = VPADController::ButtonId enum, button = VisionOS button constants
    // VPAD ButtonId: A=1, B=2, X=3, Y=4, L=5, R=6, ZL=7, ZR=8, Plus=9, Minus=10, Home=11,
    //               Up=12, Down=13, Left=14, Right=15, StickL=16, StickR=17,
    //               StickL_Up=18, StickL_Down=19, StickL_Left=20, StickL_Right=21,
    //               StickR_Up=22, StickR_Down=23, StickR_Left=24, StickR_Right=25
    // VisionOS button codes (defined in VisionOSControllerProvider.h):
    // A=0x1000, B=0x1001, X=0x1002, Y=0x1003, L=0x1004, R=0x1005, ZL=0x1006, ZR=0x1007
    // DpadUp=0x1008, DpadDown=0x1009, DpadLeft=0x1010, DpadRight=0x1011
    // Plus=0x1012, Minus=0x1013, Home=0x1014, LStick=0x1015, RStick=0x1016
    std::string xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<emulated_controller>
  <type>Wii U GamePad</type>
  <controller>
    <api>VisionOS</api>
    <uuid>0</uuid>
    <display_name>visionOS Controller</display_name>
    <mappings>
      <entry><mapping>1</mapping><button>4096</button></entry>
      <entry><mapping>2</mapping><button>4097</button></entry>
      <entry><mapping>3</mapping><button>4098</button></entry>
      <entry><mapping>4</mapping><button>4099</button></entry>
      <entry><mapping>5</mapping><button>4100</button></entry>
      <entry><mapping>6</mapping><button>4101</button></entry>
      <entry><mapping>7</mapping><button>4102</button></entry>
      <entry><mapping>8</mapping><button>4103</button></entry>
      <entry><mapping>9</mapping><button>4114</button></entry>
      <entry><mapping>10</mapping><button>4115</button></entry>
      <entry><mapping>11</mapping><button>4116</button></entry>
      <entry><mapping>12</mapping><button>4104</button></entry>
      <entry><mapping>13</mapping><button>4105</button></entry>
      <entry><mapping>14</mapping><button>4112</button></entry>
      <entry><mapping>15</mapping><button>4113</button></entry>
      <entry><mapping>16</mapping><button>4117</button></entry>
      <entry><mapping>17</mapping><button>4118</button></entry>
    </mappings>
  </controller>
</emulated_controller>
)";

    std::ofstream file(profilePath);
    if (file.is_open()) {
        file << xml;
        cemuLog_log(LogType::Force, "visionOS: Created default VisionOS controller profile at {}", profilePath.generic_string());
    }
}

- (void)setKeyState:(uint32_t)keyCode pressed:(BOOL)pressed
{
    cemuLog_log(LogType::Force, "visionOS: setKeyState {} = {}", keyCode, pressed ? "DOWN" : "UP");
    WindowSystem::GetWindowInfo().set_keystate(keyCode, pressed);
}

- (void)releaseAllKeys
{
    WindowSystem::GetWindowInfo().set_keystatesup();
}

// MARK: - Controller Input

- (void)onControllerButtonEvent:(uint32_t)buttonCode pressed:(BOOL)pressed
{
#ifdef VISIONOS
    VisionOSControllerProvider::on_key_event(buttonCode, pressed);
#endif
}

- (void)onControllerAxisEvent:(uint32_t)axisCode value:(float)value
{
#ifdef VISIONOS
    VisionOSControllerProvider::on_axis_event(axisCode, value);
#endif
}

- (void)onControllerMotionWithGravityX:(float)gx gravityY:(float)gy gravityZ:(float)gz
                      userAccelerationX:(float)uax userAccelerationY:(float)uay userAccelerationZ:(float)uaz
                         rotationRateX:(float)rrx rotationRateY:(float)rry rotationRateZ:(float)rrz
                             attitudeX:(float)aqx attitudeY:(float)aqy attitudeZ:(float)aqz attitudeW:(float)aqw
{
#ifdef VISIONOS
    VisionOSControllerProvider::on_motion_event(gx, gy, gz, uax, uay, uaz, rrx, rry, rrz, aqx, aqy, aqz, aqw);
#endif
}

- (void)setControllerMode:(uint32_t)mode
{
#ifdef VISIONOS
    VisionOSControllerProvider::set_mode(mode == 0 ? VisionOSControllerMode::ProController : VisionOSControllerMode::WiimoteNunchuck);
#endif
}

@end
