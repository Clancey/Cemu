import SwiftUI

/// Main entry point for the Cemu visionOS application.
///
/// The app presents a single ``WindowGroup`` containing the emulator display
/// at a default size appropriate for Wii U output (854x480 scaled to 1280x720).
/// Scene phase changes are observed so that emulation can be paused when the
/// app moves to the background and resumed when it returns.
@main
struct CemuApp: App {

    /// Shared emulator lifecycle manager, injected into the view hierarchy.
    @State private var emulatorCore = EmulatorCore()

    /// Tracks the visionOS scene phase for pause / resume.
    @Environment(\.scenePhase) private var scenePhase

    var body: some Scene {
        WindowGroup {
            EmulatorView(core: emulatorCore)
        }
        .defaultSize(width: 1280, height: 720)
        .onChange(of: scenePhase) { _, newPhase in
            handleScenePhase(newPhase)
        }
    }

    // MARK: - Private

    private func handleScenePhase(_ phase: ScenePhase) {
        switch phase {
        case .active:
            emulatorCore.resume()
        case .inactive, .background:
            emulatorCore.pause()
        @unknown default:
            break
        }
    }
}
