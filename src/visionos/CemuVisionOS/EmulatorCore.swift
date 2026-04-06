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

    /// Whether a game is actively running.
    var isRunning: Bool { bridge.isRunning }

    /// Human-readable title of the running game.
    var currentTitleName: String? { bridge.currentTitleName }

    /// Current emulation state.
    var emulationState: CemuEmulationState { bridge.emulationState }

    // MARK: - Private

    private let bridge = CemuBridge.shared()
    private let logger = Logger(subsystem: "org.cemu.CemuVision", category: "EmulatorCore")

    // MARK: - Lifecycle

    /// Perform one-time initialisation of Cemu subsystems and storage paths.
    func initialize() {
        guard !isInitialized else { return }

        // Configure sandbox-aware paths before core init.
        configurePaths()

        var error: NSError?
        let success = bridge.initialize(withError: &error)
        if success {
            isInitialized = true
            logger.info("Cemu core initialised")
        } else {
            let message = error?.localizedDescription ?? "Unknown error"
            lastError = message
            logger.error("Cemu core init failed: \(message)")
        }
    }

    // MARK: - Display

    /// Provide the main TV CAMetalLayer to the C++ renderer.
    func setMainDisplayLayer(_ layer: CAMetalLayer, width: Int, height: Int) {
        bridge.setMainDisplayLayer(layer, width: Int32(width), height: Int32(height))
    }

    /// Notify the C++ side that the main display was resized.
    func resizeMainDisplay(width: Int, height: Int) {
        bridge.resizeMainDisplayWidth(Int32(width), height: Int32(height))
    }

    // MARK: - Game Loading

    /// Load and launch a game at the given filesystem path.
    func loadGame(at path: String) {
        lastError = nil
        var error: NSError?
        let success = bridge.loadGame(atPath: path, error: &error)
        if !success {
            let message = error?.localizedDescription ?? "Failed to load game"
            lastError = message
            logger.error("Load game failed: \(message)")
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
        bridge.handleGamePadTouch(atX: x, y: y)
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

        bridge.setStoragePaths(withConfig: configPath, cache: cachePath, data: dataPath)
    }
}
