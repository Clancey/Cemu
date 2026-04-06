// Stubs that need Cemu's precompiled header types
// Separate from VisionOSStubs.cpp to avoid PCH conflicts

#ifdef VISIONOS

#include "Cafe/HW/Latte/Renderer/OpenGL/LatteTextureViewGL.h"
#include "Cafe/HW/Latte/Renderer/OpenGL/OpenGLRenderer.h"

LatteTextureViewGL* LatteTextureViewGL::GetAlternativeView() { return nullptr; }
void OpenGLRenderer::renderstate_updateTextureSettingsGL(
    LatteDecompilerShader*, LatteTextureView*, uint32,
    const Latte::LATTE_SQ_TEX_RESOURCE_WORD4_N, uint32, bool) {}

// NAPI stubs (curl not available)
#include "Cemu/napi/napi_helper.h"

namespace NAPI {
    bool NAPI_MakeAuthInfoFromCurrentAccount(AuthInfo&) { return false; }
    ACTGetNexTokenResult ACT_GetNexToken_WithCache(AuthInfo&, uint64, uint16, uint32) { return {}; }
    ACTGetIndependentTokenResult ACT_GetIndependentToken_WithCache(AuthInfo&, uint64, uint16, std::string_view) { return {}; }
    ACTConvertNnidToPrincipalIdResult ACT_ACTConvertNnidToPrincipalId(AuthInfo&, std::string_view) { return {}; }
}

namespace nlibcurl {
    IOSUModule* GetModule() { return nullptr; }
}

namespace nn::olv {
    void loadOlivePostAndTopicTypes() {}
    void ParseXML_DownloadedPostData(DownloadedPostData&, pugi::xml_node&) {}
    sint32 UploadCommunityData(const UploadCommunityDataParam*) { return -1; }
    sint32 UploadFavoriteToCommunityData(UploadedFavoriteToCommunityData*, const UploadFavoriteToCommunityDataParam*) { return -1; }
}

#endif
