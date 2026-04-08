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
        // Local games (from Documents/Games) have empty bookmark data — load directly
        if game.bookmarkData.isEmpty {
            core.loadGame(at: game.path)
            dismiss()
            return
        }

        // External games — resolve bookmark to get current path
        var isStale = false
        guard let url = try? URL(
            resolvingBookmarkData: game.bookmarkData,
            bookmarkDataIsStale: &isStale
        ) else {
            return
        }

        core.loadGame(from: url)
        dismiss()
    }

    private func handleAddResult(_ result: Result<[URL], Error>) {
        guard case .success(let urls) = result, let url = urls.first else { return }
        guard url.startAccessingSecurityScopedResource() else { return }

        let fm = FileManager.default
        var isDir: ObjCBool = false
        fm.fileExists(atPath: url.path, isDirectory: &isDir)

        if isDir.boolValue {
            // Check if this is a single game folder (has code/ and meta/)
            let hasCode = fm.fileExists(atPath: url.appendingPathComponent("code").path)
            let hasMeta = fm.fileExists(atPath: url.appendingPathComponent("meta").path)

            if hasCode && hasMeta {
                // Single game directory — add it directly
                addGameEntry(url: url)
            } else {
                // Parent folder — scan for game directories inside
                scanAndAddGames(from: url)
            }
        } else {
            // Single file (RPX, WUD, etc.) — add directly
            addGameEntry(url: url)
        }

        saveBookmarkedGames()
    }

    private func addGameEntry(url: URL) {
        guard let bookmarkData = try? url.bookmarkData(
            options: .minimalBookmark,
            includingResourceValuesForKeys: nil,
            relativeTo: nil
        ) else { return }

        // Clean up display name
        var name = url.lastPathComponent
        name = name.replacingOccurrences(of: #"\s*\[(Game|Update|DLC)\].*"#, with: "", options: .regularExpression)
            .trimmingCharacters(in: .whitespaces)
        if name.isEmpty { name = url.lastPathComponent }

        // Don't add duplicates
        if games.contains(where: { $0.path == url.path }) { return }

        games.append(GameEntry(name: name, path: url.path, bookmarkData: bookmarkData))
    }

    /// Scan the app's local Documents/Games folder for game titles.
    private func scanLocalGamesFolder() -> [GameEntry] {
        let gamesDir = EmulatorCore.gamesFolderURL
        let fm = FileManager.default
        guard fm.fileExists(atPath: gamesDir.path) else { return [] }
        guard let items = try? fm.contentsOfDirectory(atPath: gamesDir.path) else { return [] }

        var entries: [GameEntry] = []
        for item in items {
            let itemURL = gamesDir.appendingPathComponent(item)
            var isDir: ObjCBool = false
            fm.fileExists(atPath: itemURL.path, isDirectory: &isDir)

            guard isDir.boolValue else {
                let ext = (item as NSString).pathExtension.lowercased()
                if ["wud", "wux", "wua", "iso"].contains(ext) {
                    let name = (item as NSString).deletingPathExtension
                    entries.append(GameEntry(name: name, path: itemURL.path, bookmarkData: Data()))
                }
                continue
            }

            // Skip Update and DLC folders
            if item.contains("[Update]") || item.contains("[DLC]") { continue }

            // Check for game directory (code/ and meta/)
            let hasCode = fm.fileExists(atPath: itemURL.appendingPathComponent("code").path)
            let hasMeta = fm.fileExists(atPath: itemURL.appendingPathComponent("meta").path)
            if hasCode || hasMeta {
                var name = item
                name = name.replacingOccurrences(of: #"\s*\[(Game|Update|DLC)\].*"#, with: "", options: .regularExpression)
                    .trimmingCharacters(in: .whitespaces)
                if name.isEmpty { name = item }
                entries.append(GameEntry(name: name, path: itemURL.path, bookmarkData: Data()))
            }
        }
        return entries
    }

    private func scanAndAddGames(from folderURL: URL) {
        let fm = FileManager.default
        guard let items = try? fm.contentsOfDirectory(atPath: folderURL.path) else { return }

        for item in items {
            let itemURL = folderURL.appendingPathComponent(item)
            var isDir: ObjCBool = false
            fm.fileExists(atPath: itemURL.path, isDirectory: &isDir)

            guard isDir.boolValue else {
                // Check for game files (WUD, WUX, etc.)
                let ext = (item as NSString).pathExtension.lowercased()
                if ["wud", "wux", "wua", "iso"].contains(ext) {
                    addGameEntry(url: itemURL)
                }
                continue
            }

            // Skip Update and DLC folders
            if item.contains("[Update]") || item.contains("[DLC]") { continue }

            // Check if this subdirectory is a game (has code/ and meta/)
            let hasCode = fm.fileExists(atPath: itemURL.appendingPathComponent("code").path)
            let hasMeta = fm.fileExists(atPath: itemURL.appendingPathComponent("meta").path)
            if hasCode && hasMeta {
                addGameEntry(url: itemURL)
            }
        }
    }

    private func deleteGames(at offsets: IndexSet) {
        games.remove(atOffsets: offsets)
        saveBookmarkedGames()
    }

    // MARK: - Persistence

    private static let bookmarksKey = "GameLibraryBookmarks"

    private func loadBookmarkedGames() {
        // Always scan the local Documents/Games folder fresh (no persistence needed)
        games = scanLocalGamesFolder()

        // Deduplicate by game name (paths change between launches due to GUID)
        let existingNames = Set(games.map(\.name))

        // Load bookmarked games (external paths only — skip if name already found locally)
        if let stored = UserDefaults.standard.array(forKey: Self.bookmarksKey) as? [[String: Any]] {
            for dict in stored {
                guard let name = dict["name"] as? String,
                      let data = dict["bookmark"] as? Data,
                      !data.isEmpty,
                      !existingNames.contains(name) else { continue }

                // Resolve bookmark to get current path
                var isStale = false
                if let url = try? URL(resolvingBookmarkData: data, bookmarkDataIsStale: &isStale) {
                    games.append(GameEntry(name: name, path: url.path, bookmarkData: data))
                }
            }
        }

        #if DEBUG && targetEnvironment(simulator)
        let existingNames2 = Set(games.map(\.name))
        let debugGames = scanDebugGamePath().filter { !existingNames2.contains($0.name) }
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
                // Check for a valid game directory (with code/ and meta/ or code/*.rpx)
                let codePath = (fullPath as NSString).appendingPathComponent("code")
                let metaPath = (fullPath as NSString).appendingPathComponent("meta")
                // Skip Update and DLC folders — only show [Game] entries
                if item.contains("[Update]") || item.contains("[DLC]") {
                    continue
                }
                if fm.fileExists(atPath: codePath) && fm.fileExists(atPath: metaPath) {
                    // Full game directory — pass the directory path (not the RPX)
                    // Extract a clean name from the folder name
                    let cleanName = item
                        .replacingOccurrences(of: #"\s*\[Game\].*"#, with: "", options: .regularExpression)
                        .trimmingCharacters(in: .whitespaces)
                    entries.append(GameEntry(
                        name: cleanName.isEmpty ? item : cleanName,
                        path: fullPath,
                        bookmarkData: Data()
                    ))
                } else if let codeContents = try? fm.contentsOfDirectory(atPath: codePath) {
                    for file in codeContents where file.hasSuffix(".rpx") {
                        let rpxPath = (codePath as NSString).appendingPathComponent(file)
                        entries.append(GameEntry(
                            name: item,
                            path: rpxPath,
                            bookmarkData: Data()
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
        // Only save games that have bookmark data (external paths).
        // Local Documents/Games entries have empty bookmarks and are scanned fresh each launch.
        let stored: [[String: Any]] = games
            .filter { !$0.bookmarkData.isEmpty }
            .map { entry in
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
