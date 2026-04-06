project(cemu_visionos)

# visionOS-specific source files
set(VISIONOS_SOURCES
    CemuBridge/CemuBridge.mm
    CemuBridge/MetalLayerVisionOS.mm
    CemuBridge/MetalHostView.mm
    VisionOSStubs.cpp
    VisionOSStubsPCH.cpp
)

# Create visionOS native support library
add_library(cemu_visionos STATIC ${VISIONOS_SOURCES})

# Include directories for visionOS headers
target_include_directories(cemu_visionos PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/dependencies/metal-cpp
)

# Link required frameworks for visionOS
target_link_libraries(cemu_visionos PUBLIC
    "-framework Foundation"
    "-framework Metal"
    "-framework MetalKit"
    "-framework QuartzCore"
    "-framework UIKit"
)

# Link to Cemu core libraries
target_link_libraries(cemu_visionos PUBLIC
    CemuCommon
    CemuCafe
    CemuUtil
)

# Set compile options
target_compile_options(cemu_visionos PRIVATE
    -fobjc-arc  # Enable automatic reference counting
)