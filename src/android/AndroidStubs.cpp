// Stub implementations for desktop-only symbols that are referenced
// but not available on Android. These allow linking while the
// actual functionality is disabled via #ifdef guards.

#ifdef __ANDROID__

#include <cstdint>

// ImGui OpenGL stubs (Android uses Vulkan, not OpenGL)
struct ImDrawData;
extern "C" {
    bool ImGui_ImplOpenGL3_Init(const char*) { return false; }
    void ImGui_ImplOpenGL3_NewFrame() {}
    void ImGui_ImplOpenGL3_RenderDrawData(ImDrawData*) {}
    void ImGui_ImplOpenGL3_DestroyFontsTexture() {}
}

// Font data stubs (embedded fonts not available on Android yet)
extern "C" {
    unsigned char* g_fontawesome_data = nullptr;
    int g_fontawesome_size = 0;
}

int* extractCafeDefaultFont(int* sizeOut) {
    if (sizeOut) *sizeOut = 0;
    return nullptr;
}

// ELF symbol table stubs (not needed on Android)
class ELFSymbolTable {
public:
    ELFSymbolTable() {}
    ~ELFSymbolTable() {}
    bool OffsetToSymbol(unsigned long offset, unsigned long& symbolOffset) const {
        return false;
    }
};

// Network stubs (curl-based networking disabled on Android)
enum class NetworkService : int {};

namespace NAPI {
    struct IDBE_RawResult { bool isValid = false; };
    IDBE_RawResult IDBE_RequestRawEncrypted(NetworkService, unsigned long) { return {}; }
}

namespace nlibcurl {
    struct IOSUModule;
    IOSUModule* GetModule() { return nullptr; }
}

// nn::olv (Olive/Miiverse) stubs - online service no longer active
namespace nn::olv {
    bool g_IsInitialized = false;
    bool g_IsOfflineDBMode = false;
    bool g_IsOnlineMode = false;
    int g_ReportTypes = 0;

    struct InitializeParam {};
    struct DownloadedPostData {};

    unsigned int Initialize(InitializeParam*) { return 0; }
    bool IsInitialized() { return false; }
    void loadOlivePostAndTopicTypes() {}
}

namespace pugi { class xml_node; }
namespace nn::olv {
    void ParseXML_DownloadedPostData(DownloadedPostData&, pugi::xml_node&) {}
}

#endif // __ANDROID__
