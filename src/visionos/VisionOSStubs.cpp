// Minimal stubs for visionOS — symbols needed at link time
// These are for features not available on visionOS (OpenGL, Vulkan, curl)

#ifdef VISIONOS

// Font data
unsigned char* g_fontawesome_data = nullptr;
int g_fontawesome_size = 0;

int* extractCafeDefaultFont(int* sizeOut) {
    if (sizeOut) *sizeOut = 0;
    return nullptr;
}

// VsyncDriver (Vulkan-specific, Metal has its own timing)
void VsyncDriver_startThread(void(*cbVSync)()) {}

// OpenGL draw stubs
void LatteDraw_cleanupAfterFrame() {}
void LatteDraw_handleSpecialState8_clearAsDepth() {}

// CemuGL function pointers (OpenGL not used on visionOS)
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

#endif
