#pragma once

#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>

NS_ASSUME_NONNULL_BEGIN

/// Emulation lifecycle states exposed to the Swift UI layer.
typedef NS_ENUM(NSInteger, CemuEmulationState) {
    CemuEmulationStateStopped = 0,
    CemuEmulationStateLoading,
    CemuEmulationStateRunning,
    CemuEmulationStatePaused,
};

/// Objective-C++ bridge between the SwiftUI app shell and the Cemu C++ core.
///
/// All public methods are safe to call from the main thread unless documented
/// otherwise.  Heavy work (emulation, rendering) is dispatched to background
/// threads internally.
@interface CemuBridge : NSObject

/// Singleton accessor.
+ (instancetype)shared;

// MARK: - Initialization

/// Perform one-time subsystem initialization (config, crypto, audio, etc.).
/// Must be called before any other method.
/// @param error On failure, populated with a localised description.
/// @return YES on success.
- (BOOL)initializeWithError:(NSError * _Nullable * _Nullable)error;

/// Configure sandbox-aware storage paths.
/// Call after -initializeWithError: and before loading a game.
/// @param configPath  Path to Application Support/Cemu (config + mlc).
/// @param cachePath   Path for shader caches.
/// @param dataPath    Path for read-only bundled data (graphic packs, etc.).
- (void)setStoragePathsWithConfig:(NSString *)configPath
                            cache:(NSString *)cachePath
                             data:(NSString *)dataPath;

// MARK: - Display

/// Provide the CAMetalLayer for the main TV display.
/// The bridge retains the layer for the lifetime of emulation.
- (void)setMainDisplayLayer:(CAMetalLayer *)layer
                      width:(int)width
                     height:(int)height;

/// Provide the CAMetalLayer for the secondary GamePad display.
- (void)setPadDisplayLayer:(CAMetalLayer *)layer
                     width:(int)width
                    height:(int)height;

/// Notify the renderer that the main display was resized.
- (void)resizeMainDisplayWidth:(int)width height:(int)height;

/// Notify the renderer that the pad display was resized.
- (void)resizePadDisplayWidth:(int)width height:(int)height;

// MARK: - Emulation Control

/// Load and launch a title from a file path (RPX, WUD, WUX, etc.).
/// Emulation starts on a background thread.
/// @param path  Absolute filesystem path to the title.
/// @param error On failure, populated with a description.
/// @return YES if the title was prepared and launch was initiated.
- (BOOL)loadGameAtPath:(NSString *)path error:(NSError * _Nullable * _Nullable)error;

/// Pause a running title.
- (void)pauseEmulation;

/// Resume a paused title.
- (void)resumeEmulation;

/// Stop the current title and tear down emulation state.
- (void)stopEmulation;

// MARK: - State

/// Current emulation lifecycle state.
@property (nonatomic, readonly) CemuEmulationState emulationState;

/// Whether a title is actively running (not stopped).
@property (nonatomic, readonly) BOOL isRunning;

/// Human-readable name of the running title, or nil.
@property (nonatomic, readonly, nullable) NSString *currentTitleName;

/// Current title ID, or 0 if nothing is loaded.
@property (nonatomic, readonly) uint64_t currentTitleId;

// MARK: - Input Forwarding

/// Forward a touch-down / touch-move event on the GamePad display.
/// Coordinates are in the Wii U GamePad resolution space (854x480).
- (void)handleGamePadTouchAtX:(float)x y:(float)y;

/// Forward a touch-up event on the GamePad display.
- (void)handleGamePadTouchEnd;

@end

NS_ASSUME_NONNULL_END
