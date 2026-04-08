import Foundation
import Observation
import QuartzCore
import os

/// Swift-side state manager that wraps ``CemuBridge`` for use with SwiftUI.
///
/// All mutations to published properties happen on the main actor so that
/// SwiftUI views can observe them safely.
@Observable
@MainActor
final class EmulatorCore {

    // MARK: - Published State

    /// Whether the C++ subsystems have been initialised.
    private(set) var isInitialized = false

    /// Descriptive error surfaced to the UI, or nil.
    private(set) var lastError: String?

    /// Whether a game is actively running (polled from C++ state).
    private(set) var isRunning: Bool = false

    /// Human-readable title of the running game.
    var currentTitleName: String? { bridge.currentTitleName }

    /// Current emulation state.
    var emulationState: CemuEmulationState { bridge.emulationState }

    // MARK: - Private

    private let bridge = CemuBridge.shared()
    private let logger = Logger(subsystem: "org.cemu.CemuVision", category: "EmulatorCore")

    /// Input manager for game controllers and keyboard.
    let inputManager = InputManager()

    // MARK: - Lifecycle

    /// Perform one-time initialisation of Cemu subsystems and storage paths.
    func initialize() {
        guard !isInitialized else { return }

        // Disable Metal validation to avoid abort() on unsupported features (simulator)
        setenv("MTL_DEBUG_LAYER", "0", 1)
        setenv("METAL_ERROR_MODE", "0", 1)

        // Configure sandbox-aware paths before core init.
        configurePaths()

        do {
            try bridge.initialize()
            isInitialized = true
            logger.info("Cemu core initialised")
        } catch {
            let message = error.localizedDescription
            lastError = message
            logger.error("Cemu core init failed: \(message)")
            return
        }

        #if DEBUG && targetEnvironment(simulator)
        autoLaunchDebugGame()
        #endif
    }

    #if DEBUG && targetEnvironment(simulator)
    /// Path to auto-launch in debug simulator builds (set during init, launched once layer is ready).
    private(set) var pendingDebugGamePath: String?

    /// In debug simulator builds, find BOTW and queue it for auto-launch.
    private func autoLaunchDebugGame() {
        let basePath = "/Users/clancey/Documents/Games/WiiU"
        let gameDirs = [
            "The Legend of Zelda Breath of the Wild [Game] [00050000101c9400]",
        ]
        let fm = FileManager.default
        for dir in gameDirs {
            let gameDirPath = "\(basePath)/\(dir)"
            // Check if this is a valid game directory (has code/ and meta/)
            if fm.fileExists(atPath: "\(gameDirPath)/code") && fm.fileExists(atPath: "\(gameDirPath)/meta") {
                logger.info("Debug game queued for auto-launch: \(gameDirPath)")
                pendingDebugGamePath = gameDirPath
                return
            }
            // Fallback: check for RPX directly
            let codePath = "\(gameDirPath)/code"
            if let files = try? fm.contentsOfDirectory(atPath: codePath) {
                for file in files where file.hasSuffix(".rpx") {
                    let rpxPath = "\(codePath)/\(file)"
                    logger.info("Debug game queued for auto-launch (RPX): \(rpxPath)")
                    pendingDebugGamePath = rpxPath
                    return
                }
            }
        }
        logger.info("No debug game found for auto-launch")
    }

    /// Called after the Metal display layer is attached to actually launch the queued game.
    func launchPendingDebugGameIfNeeded() {
        guard let path = pendingDebugGamePath else { return }
        pendingDebugGamePath = nil
        logger.info("Auto-launching debug game now (Metal layer ready): \(path)")
        // Delay to let the renderer fully initialize after layer attachment
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) { [weak self] in
            self?.loadGame(at: path)
            // Auto-press A to get past title screen on simulator
            // (GCController not available on visionOS simulator)
            let bridge = CemuBridge.shared()
            var pressCount = 0
            Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { timer in
                pressCount += 1
                bridge.onControllerButtonEvent(0x1000, pressed: pressCount % 2 == 1)
                if pressCount > 120 {
                    bridge.onControllerButtonEvent(0x1000, pressed: false)
                    timer.invalidate()
                }
            }
        }
    }
    #endif

    // MARK: - Display

    /// Provide the main TV CAMetalLayer to the C++ renderer.
    func setMainDisplayLayer(_ layer: CAMetalLayer, width: Int, height: Int) {
        bridge.setMainDisplay(layer, width: Int32(width), height: Int32(height))
        #if DEBUG && targetEnvironment(simulator)
        launchPendingDebugGameIfNeeded()
        #endif
    }

    /// Notify the C++ side that the main display was resized.
    func resizeMainDisplay(width: Int, height: Int) {
        bridge.resizeMainDisplayWidth(Int32(width), height: Int32(height))
    }

    // MARK: - Game Loading

    /// Load and launch a game at the given filesystem path.
    func loadGame(at path: String) {
        lastError = nil
        do {
            try bridge.loadGame(atPath: path)
            isRunning = bridge.isRunning
            startStatePolling()
        } catch {
            let message = error.localizedDescription
            lastError = message
            logger.error("Load game failed: \(message)")
        }
    }

    /// Retained security-scoped URL to keep access alive during emulation.
    private var activeSecurityScopedURL: URL?

    /// Load a game from a security-scoped URL (file picker / bookmark).
    func loadGame(from url: URL) {
        // Start accessing security-scoped resource and KEEP it alive
        let gained = url.startAccessingSecurityScopedResource()
        if gained {
            activeSecurityScopedURL = url
        }
        loadGame(at: url.path)
    }

    private var stateTimer: Timer?

    private func startStatePolling() {
        stateTimer?.invalidate()
        stateTimer = Timer.scheduledTimer(withTimeInterval: 0.5, repeats: true) { [weak self] _ in
            Task { @MainActor in
                guard let self else { return }
                let running = self.bridge.isRunning
                if self.isRunning != running {
                    self.isRunning = running
                }
            }
        }
    }

    // MARK: - Emulation Control

    /// Pause emulation (e.g. when the scene becomes inactive).
    func pause() {
        bridge.pauseEmulation()
    }

    /// Resume emulation (e.g. when the scene becomes active).
    func resume() {
        bridge.resumeEmulation()
    }

    /// Stop the current title entirely.
    func stop() {
        bridge.stopEmulation()
    }

    // MARK: - Input

    /// Forward a GamePad touch event.
    func handleGamePadTouch(x: Float, y: Float) {
        bridge.handleGamePadTouchAt(x: x, y: y)
    }

    /// Forward a GamePad touch-up event.
    func handleGamePadTouchEnd() {
        bridge.handleGamePadTouchEnd()
    }

    // MARK: - Private Helpers

    private func configurePaths() {
        let fm = FileManager.default
        guard let appSupport = fm.urls(for: .applicationSupportDirectory,
                                       in: .userDomainMask).first else {
            logger.error("Unable to locate Application Support directory")
            return
        }

        let configPath = appSupport.appendingPathComponent("Cemu").path
        let cachePath  = fm.urls(for: .cachesDirectory,
                                 in: .userDomainMask).first?
            .appendingPathComponent("Cemu").path ?? configPath

        // Bundled data (graphic packs, etc.) lives inside the app bundle.
        let dataPath = Bundle.main.resourcePath ?? configPath

        bridge.setStoragePathsWithConfig(configPath, cache: cachePath, data: dataPath)

        // Create a Games folder in Documents for users to add ROMs via Files app
        createGamesFolder()
    }

    /// The app's Documents/Games folder — visible in Files app via UIFileSharingEnabled.
    static var gamesFolderURL: URL {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first!
            .appendingPathComponent("Games")
    }

    private func createGamesFolder() {
        let fm = FileManager.default
        let gamesDir = Self.gamesFolderURL
        if !fm.fileExists(atPath: gamesDir.path) {
            try? fm.createDirectory(at: gamesDir, withIntermediateDirectories: true)
            logger.info("Created Games folder at \(gamesDir.path)")
        }
    }
}
