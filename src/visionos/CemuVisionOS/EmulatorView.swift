import SwiftUI

/// Primary view for the emulator window.
///
/// Hosts the Metal rendering surface via ``MetalEmulatorView`` and provides
/// a toolbar with play/pause controls and game selection through the system
/// file importer.
struct EmulatorView: View {

    /// The shared emulator state manager.
    let core: EmulatorCore

    /// Controls presentation of the system file-picker for game selection.
    @State private var showFilePicker = false

    /// Controls presentation of the game library sheet.
    @State private var showGameLibrary = false

    /// Toggle on-screen controls visibility.
    @State private var showControls = true

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()

            // Metal rendering surface with pointer tracking overlay
            MetalEmulatorView(core: core)
                .aspectRatio(854.0 / 480.0, contentMode: .fit)
                .overlay {
                    // Track gaze/pointer position for Wiimote pointing
                    GeometryReader { geo in
                        Color.clear
                            .contentShape(Rectangle())
                            .onContinuousHover { phase in
                                switch phase {
                                case .active(let location):
                                    // Convert view coordinates to Wiimote DPD space (0-1024, 0-768)
                                    let x = Float(location.x / geo.size.width) * 1024.0
                                    let y = Float(location.y / geo.size.height) * 768.0
                                    CemuBridge.shared().onControllerAxisEvent(10, value: x)
                                    CemuBridge.shared().onControllerAxisEvent(11, value: y)
                                case .ended:
                                    break
                                }
                            }
                    }
                }

            // On-screen controls — hidden when a physical controller is connected
            if core.isRunning && showControls && core.inputManager.connectedController == nil {
                VStack {
                    Spacer()
                    onScreenControls
                        .padding(.bottom, 20)
                }
            }
        }
        .toolbar(core.isRunning ? .hidden : .visible, for: .bottomOrnament)
        .toolbar {
            toolbarContent
        }
        .fileImporter(
            isPresented: $showFilePicker,
            allowedContentTypes: [.folder, .data],
            allowsMultipleSelection: false
        ) { result in
            handleFileImportResult(result)
        }
        .sheet(isPresented: $showGameLibrary) {
            GameLibraryView(core: core)
        }
        .onAppear {
            core.initialize()
        }
        .alert("Error",
               isPresented: .constant(core.lastError != nil),
               actions: {
                   Button("OK") { }
               },
               message: {
                   Text(core.lastError ?? "")
               })
    }

    // MARK: - Subviews

    /// Shown when no game is running.
    private var placeholderView: some View {
        VStack(spacing: 24) {
            Image(systemName: "gamecontroller.fill")
                .font(.system(size: 72))
                .foregroundStyle(.secondary)

            Text("No Game Running")
                .font(.title)
                .foregroundStyle(.secondary)

            Text("Select a game from the library or open a file to begin.")
                .font(.body)
                .foregroundStyle(.tertiary)
                .multilineTextAlignment(.center)
                .frame(maxWidth: 400)

            HStack(spacing: 16) {
                Button {
                    showGameLibrary = true
                } label: {
                    Label("Game Library", systemImage: "square.grid.2x2")
                }
                .buttonStyle(.bordered)

                Button {
                    showFilePicker = true
                } label: {
                    Label("Open File", systemImage: "folder")
                }
                .buttonStyle(.borderedProminent)
            }
        }
        .padding()
    }

    // MARK: - Toolbar

    @ToolbarContentBuilder
    private var toolbarContent: some ToolbarContent {
        ToolbarItemGroup(placement: .bottomOrnament) {
            if !core.isRunning {
                Button {
                    showGameLibrary = true
                } label: {
                    Label("Library", systemImage: "square.grid.2x2")
                }
                .disabled(!core.isInitialized)

                Button {
                    showFilePicker = true
                } label: {
                    Label("Open", systemImage: "folder")
                }
                .disabled(!core.isInitialized)
            }

            if core.isRunning {
                if core.emulationState == .paused {
                    Button {
                        core.resume()
                    } label: {
                        Label("Resume", systemImage: "play.fill")
                    }
                } else {
                    Button {
                        core.pause()
                    } label: {
                        Label("Pause", systemImage: "pause.fill")
                    }
                }

                Button {
                    core.stop()
                } label: {
                    Label("Stop", systemImage: "stop.fill")
                }
            }
        }
    }

    // MARK: - File Import

    private func handleFileImportResult(_ result: Result<[URL], Error>) {
        switch result {
        case .success(let urls):
            guard let url = urls.first else { return }

            // Persist a bookmark so the file can be re-opened on next launch.
            persistBookmark(for: url)

            // Use URL-based loader which handles security scope
            core.loadGame(from: url)

            // Note: stopAccessingSecurityScopedResource will be called when
            // emulation ends and the URL is no longer needed.

        case .failure:
            break
        }
    }

    /// Save a security-scoped bookmark for later access.
    private func persistBookmark(for url: URL) {
        guard let bookmarkData = try? url.bookmarkData(
            options: .minimalBookmark,
            includingResourceValuesForKeys: nil,
            relativeTo: nil
        ) else {
            return
        }

        var bookmarks = UserDefaults.standard.dictionary(forKey: "GameBookmarks")
            as? [String: Data] ?? [:]
        bookmarks[url.lastPathComponent] = bookmarkData
        UserDefaults.standard.set(bookmarks, forKey: "GameBookmarks")
    }

    // MARK: - On-Screen Controls

    private var onScreenControls: some View {
        HStack(spacing: 30) {
            // D-pad
            VStack(spacing: 2) {
                gameButton("▲", vkey: 0x1008)
                HStack(spacing: 2) {
                    gameButton("◀", vkey: 0x1010)
                    Color.clear.frame(width: 44, height: 44)
                    gameButton("▶", vkey: 0x1011)
                }
                gameButton("▼", vkey: 0x1009)
            }

            // Face buttons
            VStack(spacing: 2) {
                gameButton("X", vkey: 0x1002)
                HStack(spacing: 2) {
                    gameButton("Y", vkey: 0x1003)
                    Color.clear.frame(width: 44, height: 44)
                    gameButton("A", vkey: 0x1000)
                }
                gameButton("B", vkey: 0x1001)
            }

            // Shoulders + menu
            VStack(spacing: 8) {
                HStack(spacing: 8) {
                    gameButton("L", vkey: 0x1004)
                    gameButton("R", vkey: 0x1005)
                }
                HStack(spacing: 8) {
                    gameButton("ZL", vkey: 0x1006)
                    gameButton("ZR", vkey: 0x1007)
                }
                HStack(spacing: 8) {
                    gameButton("+", vkey: 0x1012)
                    gameButton("−", vkey: 0x1013)
                }
            }
        }
        .padding()
        .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 16))
    }

    /// A single on-screen button that sends key down on press, key up on release.
    private func gameButton(_ label: String, vkey: UInt32) -> some View {
        Text(label)
            .font(.system(size: 14, weight: .bold, design: .monospaced))
            .frame(width: 44, height: 44)
            .background(.quaternary, in: RoundedRectangle(cornerRadius: 8))
            .simultaneousGesture(
                DragGesture(minimumDistance: 0)
                    .onChanged { _ in
                        CemuBridge.shared().onControllerButtonEvent(vkey, pressed: true)
                    }
                    .onEnded { _ in
                        CemuBridge.shared().onControllerButtonEvent(vkey, pressed: false)
                    }
            )
    }

    // MARK: - Keyboard Input

    /// Map keyboard presses to virtual key codes for the emulator.
    private func handleKeyPress(_ keyPress: KeyPress) -> KeyPress.Result {
        let bridge = CemuBridge.shared()
        let pressed = keyPress.phase == .down

        let vkey: UInt32? = switch keyPress.key {
        case .init("j"): 0x1000 as UInt32 // A
        case .init("k"): 0x1001 as UInt32 // B
        case .init("i"): 0x1002 as UInt32 // X
        case .init("u"): 0x1003 as UInt32 // Y
        case .init("q"): 0x1004 as UInt32 // L
        case .init("e"): 0x1005 as UInt32 // R
        case .init("z"): 0x1006 as UInt32 // ZL
        case .init("c"): 0x1007 as UInt32 // ZR
        case .init("w"): 0x1008 as UInt32 // DPad Up
        case .init("s"): 0x1009 as UInt32 // DPad Down
        case .init("a"): 0x1010 as UInt32 // DPad Left
        case .init("d"): 0x1011 as UInt32 // DPad Right
        case .return:    0x1012 as UInt32 // Plus/Start
        case .escape:    0x1014 as UInt32 // Home
        default: nil
        }

        // Arrow keys
        let arrowVkey: UInt32? = switch keyPress.key {
        case .upArrow:    0x1008 as UInt32
        case .downArrow:  0x1009 as UInt32
        case .leftArrow:  0x1010 as UInt32
        case .rightArrow: 0x1011 as UInt32
        default: nil
        }

        if let k = vkey ?? arrowVkey {
            bridge.onControllerButtonEvent(k, pressed: pressed)
            return .handled
        }
        return .ignored
    }
}
