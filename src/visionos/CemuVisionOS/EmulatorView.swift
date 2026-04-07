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

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()

            // Always show the Metal view
            MetalEmulatorView(core: core)
                .aspectRatio(854.0 / 480.0, contentMode: .fit)
        }
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

            Divider()

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

            // Start security-scoped access for sandboxed file access.
            guard url.startAccessingSecurityScopedResource() else {
                return
            }

            // Persist a bookmark so the file can be re-opened on next launch.
            persistBookmark(for: url)

            core.loadGame(at: url.path)

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
}
