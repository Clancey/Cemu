import SwiftUI
import UniformTypeIdentifiers

/// A sheet-presented view that displays discovered game titles and allows the
/// user to add new games via the system file picker.
struct GameLibraryView: View {

    /// The shared emulator state.
    let core: EmulatorCore

    @Environment(\.dismiss) private var dismiss

    /// Titles discovered from persisted bookmarks.
    @State private var games: [GameEntry] = []

    /// Controls presentation of the file importer for adding games.
    @State private var showAddPicker = false

    var body: some View {
        NavigationStack {
            Group {
                if games.isEmpty {
                    emptyState
                } else {
                    gameList
                }
            }
            .navigationTitle("Game Library")
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Done") { dismiss() }
                }
                ToolbarItem(placement: .primaryAction) {
                    Button {
                        showAddPicker = true
                    } label: {
                        Label("Add Game", systemImage: "plus")
                    }
                }
            }
            .fileImporter(
                isPresented: $showAddPicker,
                allowedContentTypes: [.folder, .data],
                allowsMultipleSelection: false
            ) { result in
                handleAddResult(result)
            }
            .onAppear {
                loadBookmarkedGames()
            }
        }
    }

    // MARK: - Subviews

    private var emptyState: some View {
        ContentUnavailableView {
            Label("No Games", systemImage: "gamecontroller")
        } description: {
            Text("Tap the + button to add a Wii U game folder or file.")
        } actions: {
            Button {
                showAddPicker = true
            } label: {
                Text("Add Game")
            }
            .buttonStyle(.borderedProminent)
        }
    }

    private var gameList: some View {
        List {
            ForEach(games) { game in
                Button {
                    launchGame(game)
                } label: {
                    GameRow(game: game)
                }
            }
            .onDelete(perform: deleteGames)
        }
    }

    // MARK: - Actions

    private func launchGame(_ game: GameEntry) {
        #if DEBUG && targetEnvironment(simulator)
        // On simulator, paths are direct host filesystem paths — no bookmark needed
        if game.bookmarkData.isEmpty {
            core.loadGame(at: game.path)
            dismiss()
            return
        }
        #endif

        // Resolve the bookmark to a URL.
        var isStale = false
        guard let url = try? URL(
            resolvingBookmarkData: game.bookmarkData,
            bookmarkDataIsStale: &isStale
        ) else {
            return
        }

        guard url.startAccessingSecurityScopedResource() else { return }

        core.loadGame(at: url.path)
        dismiss()
    }

    private func handleAddResult(_ result: Result<[URL], Error>) {
        guard case .success(let urls) = result, let url = urls.first else { return }
        guard url.startAccessingSecurityScopedResource() else { return }

        // Create and persist a bookmark.
        guard let bookmarkData = try? url.bookmarkData(
            options: .minimalBookmark,
            includingResourceValuesForKeys: nil,
            relativeTo: nil
        ) else {
            return
        }

        let entry = GameEntry(
            name: url.lastPathComponent,
            path: url.path,
            bookmarkData: bookmarkData
        )
        games.append(entry)
        saveBookmarkedGames()
    }

    private func deleteGames(at offsets: IndexSet) {
        games.remove(atOffsets: offsets)
        saveBookmarkedGames()
    }

    // MARK: - Persistence

    private static let bookmarksKey = "GameLibraryBookmarks"

    private func loadBookmarkedGames() {
        guard let stored = UserDefaults.standard.array(forKey: Self.bookmarksKey)
                as? [[String: Any]] else {
            #if DEBUG && targetEnvironment(simulator)
            games = scanDebugGamePath()
            #endif
            return
        }

        games = stored.compactMap { dict in
            guard let name = dict["name"] as? String,
                  let path = dict["path"] as? String,
                  let data = dict["bookmark"] as? Data else {
                return nil
            }
            return GameEntry(name: name, path: path, bookmarkData: data)
        }

        #if DEBUG && targetEnvironment(simulator)
        // Append games from the debug path that aren't already bookmarked
        let existingPaths = Set(games.map(\.path))
        let debugGames = scanDebugGamePath().filter { !existingPaths.contains($0.path) }
        games.append(contentsOf: debugGames)
        #endif
    }

    #if DEBUG && targetEnvironment(simulator)
    /// Scan a well-known host path for game files when running in the simulator.
    private func scanDebugGamePath() -> [GameEntry] {
        let debugPath = "/Users/clancey/Documents/Games/WiiU"
        let fm = FileManager.default
        guard fm.fileExists(atPath: debugPath) else { return [] }

        let gameExtensions: Set<String> = ["rpx", "wud", "wux", "wua", "iso", "nsp"]
        var entries: [GameEntry] = []

        // Scan top-level items
        guard let items = try? fm.contentsOfDirectory(atPath: debugPath) else { return [] }
        for item in items {
            let fullPath = (debugPath as NSString).appendingPathComponent(item)
            var isDir: ObjCBool = false
            fm.fileExists(atPath: fullPath, isDirectory: &isDir)

            if isDir.boolValue {
                // Check for code/<title>.rpx inside the folder
                let codePath = (fullPath as NSString).appendingPathComponent("code")
                if let codeContents = try? fm.contentsOfDirectory(atPath: codePath) {
                    for file in codeContents where file.hasSuffix(".rpx") {
                        let rpxPath = (codePath as NSString).appendingPathComponent(file)
                        entries.append(GameEntry(
                            name: item,
                            path: rpxPath,
                            bookmarkData: Data()  // No bookmark needed on simulator
                        ))
                        break
                    }
                }
            } else {
                let ext = (item as NSString).pathExtension.lowercased()
                if gameExtensions.contains(ext) {
                    entries.append(GameEntry(
                        name: (item as NSString).deletingPathExtension,
                        path: fullPath,
                        bookmarkData: Data()
                    ))
                }
            }
        }

        return entries
    }
    #endif

    private func saveBookmarkedGames() {
        let stored: [[String: Any]] = games.map { entry in
            [
                "name": entry.name,
                "path": entry.path,
                "bookmark": entry.bookmarkData
            ]
        }
        UserDefaults.standard.set(stored, forKey: Self.bookmarksKey)
    }
}

// MARK: - GameEntry

/// Lightweight model representing a bookmarked game title.
struct GameEntry: Identifiable {
    let id = UUID()
    let name: String
    let path: String
    let bookmarkData: Data
}

// MARK: - GameRow

/// A single row in the game library list.
private struct GameRow: View {
    let game: GameEntry

    var body: some View {
        HStack(spacing: 16) {
            // Placeholder artwork.
            RoundedRectangle(cornerRadius: 8)
                .fill(.quaternary)
                .frame(width: 80, height: 80)
                .overlay {
                    Image(systemName: "gamecontroller.fill")
                        .font(.title2)
                        .foregroundStyle(.secondary)
                }

            VStack(alignment: .leading, spacing: 4) {
                Text(game.name)
                    .font(.headline)
                Text(game.path)
                    .font(.caption)
                    .foregroundStyle(.secondary)
                    .lineLimit(1)
                    .truncationMode(.middle)
            }

            Spacer()
        }
        .padding(.vertical, 4)
    }
}
