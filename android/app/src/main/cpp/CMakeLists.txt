add_library(CemuAndroid SHARED
        AndroidAudio.cpp
        AndroidEmulatedController.cpp
        CompressTitleCallbacks.cpp
        GameTitleLoader.cpp
        Image.cpp
        JNIUtils.cpp
        NativeActiveSettings.cpp
        NativeAccount.cpp
        NativeEmulation.cpp
        NativeGameTitles.cpp
        NativeGraphicPacks.cpp
        NativeInput.cpp
        NativeLib.cpp
        NativeLocalization.cpp
        NativeLogging.cpp
        NativeSettings.cpp
        NativeSwkbd.cpp
        WuaConverter.cpp
        ../../../../src/Common/android/FilesystemAndroid.cpp
)

target_include_directories(CemuAndroid PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../src
)

cemu_use_precompiled_header(CemuAndroid)

target_link_libraries(CemuAndroid PRIVATE
        -landroid
        CemuCommon
        CemuAudio
        CemuComponents
        CemuCafe
        CemuBin
        CemuGui
        CemuInput
        CemuConfig
        ZArchive::zarchive
        stb
        Threads::Threads
)
