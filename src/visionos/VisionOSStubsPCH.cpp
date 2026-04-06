// Stubs for visionOS — provide link-time symbols for disabled features
// This file has PCH applied so it can reference Cemu types

#ifdef VISIONOS

#include "Cafe/IOSU/nn/iosu_nn_service.h"
#include "Cemu/napi/napi.h"
#include <pugixml.hpp>

// NAPI stubs
namespace NAPI {
    bool NAPI_MakeAuthInfoFromCurrentAccount(AuthInfo&) { return false; }
    ACTGetNexTokenResult ACT_GetNexToken_WithCache(AuthInfo&, uint64, uint16, uint32) { return {}; }
    ACTGetIndependentTokenResult ACT_GetIndependentToken_WithCache(AuthInfo&, uint64, uint16, std::string_view) { return {}; }
    ACTConvertNnidToPrincipalIdResult ACT_ACTConvertNnidToPrincipalId(AuthInfo&, std::string_view) { return {}; }
}

namespace nlibcurl {
    IOSUModule* GetModule() { return nullptr; }
}

// nn_olv stubs — Miiverse service
#include "Cafe/OS/libs/nn_olv/nn_olv_DownloadCommunityTypes.h"
#include "Cafe/OS/libs/nn_olv/nn_olv_UploadCommunityTypes.h"
#include "Cafe/OS/libs/nn_olv/nn_olv_UploadFavoriteTypes.h"
#include "Cafe/OS/libs/nn_olv/nn_olv_PostTypes.h"

namespace nn::olv {
    void loadOlivePostAndTopicTypes() {}
    bool ParseXML_DownloadedPostData(DownloadedPostData& data, pugi::xml_node& node) { return false; }
    sint32 UploadCommunityData(const UploadCommunityDataParam* param) { return -1; }
    sint32 UploadFavoriteToCommunityData(UploadedFavoriteToCommunityData* out, const UploadFavoriteToCommunityDataParam* param) { return -1; }
}

#endif
