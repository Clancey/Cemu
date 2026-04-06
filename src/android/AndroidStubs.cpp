// Stub implementations for desktop-only symbols that are referenced
// but not available on Android. These allow linking while the
// actual functionality is disabled via #ifdef guards.
//
// This file uses precompiled headers (PCH) so all Cemu types are available.

#ifdef __ANDROID__

// Forward declarations for types not included in PCH
struct ImDrawData;
enum class NetworkService : uint32;

// Forward declarations for NAPI types
enum class NAPI_RESULT { SUCCESS, FAILED };
enum class ACT_ERROR_CODE { NONE };

namespace NAPI {
    struct AuthInfo;
    struct _NAPI_CommonResultACT {
        NAPI_RESULT apiError = NAPI_RESULT::FAILED;
        ACT_ERROR_CODE serviceError = ACT_ERROR_CODE::NONE;
    };
    struct ACTGetNexTokenResult : public _NAPI_CommonResultACT {};
    struct ACTGetIndependentTokenResult : public _NAPI_CommonResultACT {};
    struct ACTConvertNnidToPrincipalIdResult : public _NAPI_CommonResultACT {};
    struct IDBEIconDataV0 {};
    struct NAPI_VersionListVersion_Result { bool isValid = false; };
    struct NAPI_VersionList_Result { bool isValid = false; };
}

// Forward declarations for nn::olv types
namespace nn::olv {
    struct DiscoveryResultStorage {};
    struct PortalAppParam {};
    struct InitializeParam {};
    struct UploadCommunityDataParam {};
    struct UploadedCommunityData {};
    struct DownloadedPostData {};
    struct DownloadedCommunityData {};
    struct DownloadCommunityDataListParam {};
    struct UploadFavoriteToCommunityDataParam {};
    struct UploadedFavoriteToCommunityData {};
    const sint32 OLV_RESULT_FAILED_REQUEST = -1;
}

namespace pugi {
    class xml_node {};
}

namespace nlibcurl {
    struct IOSUModule {};
}

// ELF symbol table stubs (not needed on Android)
class ELFSymbolTable {
public:
    ELFSymbolTable();
    ~ELFSymbolTable();
    std::string_view OffsetToSymbol(uint64, uint64&) const;
};

ELFSymbolTable::ELFSymbolTable() {}
ELFSymbolTable::~ELFSymbolTable() {}
std::string_view ELFSymbolTable::OffsetToSymbol(uint64, uint64&) const { return {}; }

// ImGui OpenGL stubs (Android uses Vulkan, not OpenGL)
bool ImGui_ImplOpenGL3_Init(const char*) { return false; }
void ImGui_ImplOpenGL3_NewFrame() {}
void ImGui_ImplOpenGL3_RenderDrawData(ImDrawData*) {}
void ImGui_ImplOpenGL3_DestroyFontsTexture() {}

// Font data stubs (embedded fonts not available on Android)
extern "C" {
    unsigned char* g_fontawesome_data = nullptr;
    int g_fontawesome_size = 0;
}

int* extractCafeDefaultFont(int* sizeOut) {
    if (sizeOut) *sizeOut = 0;
    return nullptr;
}

// NAPI stubs (curl-based networking disabled on Android)
namespace NAPI {
    bool NAPI_MakeAuthInfoFromCurrentAccount(AuthInfo&) { return false; }

    ACTGetNexTokenResult ACT_GetNexToken_WithCache(AuthInfo&, uint64, uint16, uint32) {
        ACTGetNexTokenResult result;
        result.apiError = NAPI_RESULT::FAILED;
        return result;
    }

    ACTGetIndependentTokenResult ACT_GetIndependentToken_WithCache(AuthInfo&, uint64, uint16, std::string_view) {
        ACTGetIndependentTokenResult result;
        result.apiError = NAPI_RESULT::FAILED;
        return result;
    }

    ACTConvertNnidToPrincipalIdResult ACT_ACTConvertNnidToPrincipalId(AuthInfo&, std::string_view) {
        ACTConvertNnidToPrincipalIdResult result;
        result.apiError = NAPI_RESULT::FAILED;
        return result;
    }

    std::optional<IDBEIconDataV0> IDBE_Request(NetworkService, uint64) {
        return std::nullopt;
    }

    std::vector<uint8> IDBE_RequestRawEncrypted(NetworkService, uint64) {
        return {};
    }

    NAPI_VersionListVersion_Result TAG_GetVersionListVersion(AuthInfo&) {
        NAPI_VersionListVersion_Result result;
        result.isValid = false;
        return result;
    }

    NAPI_VersionList_Result TAG_GetVersionList(AuthInfo&, std::string_view, uint32) {
        NAPI_VersionList_Result result;
        result.isValid = false;
        return result;
    }
}

// nlibcurl stubs
namespace nlibcurl {
    IOSUModule* GetModule() {
        return nullptr;
    }
}

// nn::olv (Olive/Miiverse) stubs - online service no longer active
namespace nn::olv {
    DiscoveryResultStorage g_DiscoveryResults{};
    uint32 g_ReportTypes = 0;
    bool g_IsInitialized = false;
    bool g_IsOnlineMode = false;
    bool g_IsOfflineDBMode = false;

    sint32 InitializePortalApp(PortalAppParam*, InitializeParam*) {
        return OLV_RESULT_FAILED_REQUEST;
    }

    uint32 Initialize(InitializeParam*) {
        return OLV_RESULT_FAILED_REQUEST;
    }

    bool IsInitialized() {
        return g_IsInitialized;
    }

    void loadOlivePostAndTopicTypes() {
        // do nothing
    }

    void ParseXML_DownloadedPostData(DownloadedPostData&, pugi::xml_node&) {
        // do nothing
    }

    namespace Report {
        uint32 GetReportTypes() {
            return g_ReportTypes;
        }

        void SetReportTypes(uint32 reportTypes) {
            g_ReportTypes = reportTypes;
        }
    }

    sint32 UploadCommunityData(UploadCommunityDataParam const*) {
        return OLV_RESULT_FAILED_REQUEST;
    }

    sint32 UploadCommunityData(UploadedCommunityData*, UploadCommunityDataParam const*) {
        return OLV_RESULT_FAILED_REQUEST;
    }

    sint32 DownloadCommunityDataList(DownloadedCommunityData*, uint32*, uint32, DownloadCommunityDataListParam const*) {
        return OLV_RESULT_FAILED_REQUEST;
    }

    sint32 UploadFavoriteToCommunityData(UploadFavoriteToCommunityDataParam const*) {
        return OLV_RESULT_FAILED_REQUEST;
    }

    sint32 UploadFavoriteToCommunityData(UploadedFavoriteToCommunityData*, UploadFavoriteToCommunityDataParam const*) {
        return OLV_RESULT_FAILED_REQUEST;
    }
}

#endif // __ANDROID__