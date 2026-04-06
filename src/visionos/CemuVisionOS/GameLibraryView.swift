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
    }

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
