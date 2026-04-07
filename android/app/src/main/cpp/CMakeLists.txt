set(JNI_DIR ${CMAKE_CURRENT_LIST_DIR})
set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/../../../../../src)

add_library(cemu SHARED
        ${JNI_DIR}/AndroidAudio.cpp
        ${JNI_DIR}/AndroidEmulatedController.cpp
        ${JNI_DIR}/CompressTitleCallbacks.cpp
        ${JNI_DIR}/GameTitleLoader.cpp
        ${JNI_DIR}/Image.cpp
        ${JNI_DIR}/JNIUtils.cpp
        ${JNI_DIR}/NativeActiveSettings.cpp
        ${JNI_DIR}/NativeAccount.cpp
        ${JNI_DIR}/NativeEmulation.cpp
        ${JNI_DIR}/NativeGameTitles.cpp
        ${JNI_DIR}/NativeGraphicPacks.cpp
        ${JNI_DIR}/NativeInput.cpp
        ${JNI_DIR}/NativeLib.cpp
        ${JNI_DIR}/NativeLocalization.cpp
        ${JNI_DIR}/NativeLogging.cpp
        ${JNI_DIR}/NativeSettings.cpp
        ${JNI_DIR}/NativeSwkbd.cpp
        ${JNI_DIR}/WuaConverter.cpp
        # OpenXR files temporarily disabled — need dynamic loading
        # ${JNI_DIR}/OpenXRManager.cpp
        # ${JNI_DIR}/SwapchainInfoXR.cpp
        ${SRC_DIR}/Common/android/FilesystemAndroid.cpp
)

target_include_directories(cemu PRIVATE
        ${JNI_DIR}
        ${SRC_DIR}
        /Users/clancey/Projects/godot_openxr_vendors/thirdparty/openxr/include
)

cemu_use_precompiled_header(cemu)

# OpenXR: headers only at compile time, loaded dynamically at runtime via dlopen
# This allows libcemu.so to load without OpenXR in the main (2D) process

target_link_libraries(cemu PRIVATE
        -landroid
        CemuCommon
        CemuAudio
        CemuComponents
        CemuCafe
        CemuBin
        CemuGui
        CemuInput
        CemuConfig
        CemuUtil
        ZArchive::zarchive
        stb
        Threads::Threads
)
