// Stub implementations for desktop-only symbols that are referenced
// but not available on Android. These allow linking while the
// actual functionality is disabled via #ifdef guards.
//
// This file uses precompiled headers (PCH) so all Cemu types are available.

#ifdef __ANDROID__

// Forward declarations for types not included in PCH
struct ImDrawData;
enum class NetworkService : uint32;


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

// CemuGL OpenGL function stubs (Android is Vulkan-only)
namespace CemuGL {
    void (*glAttachShader)(unsigned int, unsigned int) = nullptr;
    void (*glCompileShader)(unsigned int) = nullptr;
    unsigned int (*glCreateProgram)() = nullptr;
    unsigned int (*glCreateShader)(unsigned int) = nullptr;
    void (*glGetProgramInfoLog)(unsigned int, int, int*, char*) = nullptr;
    void (*glGetProgramiv)(unsigned int, unsigned int, int*) = nullptr;
    void (*glGetShaderInfoLog)(unsigned int, int, int*, char*) = nullptr;
    void (*glGetShaderiv)(unsigned int, unsigned int, int*) = nullptr;
    int (*glGetUniformLocation)(unsigned int, const char*) = nullptr;
    void (*glLinkProgram)(unsigned int) = nullptr;
    void (*glShaderSource)(unsigned int, int, const char* const*, const int*) = nullptr;
}

// OpenGL renderer stubs
void LatteDraw_cleanupAfterFrame() {}
void LatteDraw_handleSpecialState8_clearAsDepth() {}

// OpenGL renderer/texture stubs - use actual headers for correct mangling
#include "Cafe/HW/Latte/Renderer/OpenGL/LatteTextureViewGL.h"
#include "Cafe/HW/Latte/Renderer/OpenGL/OpenGLRenderer.h"

LatteTextureViewGL* LatteTextureViewGL::GetAlternativeView() { return nullptr; }

void OpenGLRenderer::renderstate_updateTextureSettingsGL(
    LatteDecompilerShader* shaderContext, LatteTextureView* _hostTextureView,
    uint32 hostTextureUnit, const Latte::LATTE_SQ_TEX_RESOURCE_WORD4_N texUnitWord4,
    uint32 texUnitIndex, bool isDepthSampler) {}

// OpenGL shader cache stubs
#include "Cafe/HW/Latte/Renderer/OpenGL/RendererShaderGL.h"
void RendererShaderGL::ShaderCacheLoading_begin(uint64) {}
void RendererShaderGL::ShaderCacheLoading_end() {}
void RendererShaderGL::ShaderCacheLoading_Close() {}

// Font data stubs (embedded fonts not available on Android)
extern "C" {
    unsigned char* g_fontawesome_data = nullptr;
    int g_fontawesome_size = 0;
}

int* extractCafeDefaultFont(int* sizeOut) {
    if (sizeOut) *sizeOut = 0;
    return nullptr;
}


#endif // __ANDROID__