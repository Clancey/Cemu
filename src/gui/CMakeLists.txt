add_library(CemuGui INTERFACE)
target_include_directories(CemuGui INTERFACE "interface")

if(ENABLE_WXWIDGETS)
    add_subdirectory(wxgui)
    target_link_libraries(CemuGui INTERFACE CemuWxGui)
endif()

if(ANDROID)
    # Android GUI sources
    set(ANDROID_GUI_SOURCES
        android/AndroidWindowSystem.cpp
        android/AndroidWindowSystemImpl.cpp
        android/AndroidCanvas.cpp
    )

    add_library(CemuAndroidGui STATIC ${ANDROID_GUI_SOURCES})

    target_include_directories(CemuAndroidGui PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )

    target_link_libraries(CemuAndroidGui
        android
        log
        glm::glm
        Threads::Threads
    )

    target_compile_definitions(CemuAndroidGui PRIVATE
        __ANDROID__=1
        VK_USE_PLATFORM_ANDROID_KHR=1
    )

    # Use precompiled header from CemuCommon
    cemu_use_precompiled_header(CemuAndroidGui)

    target_link_libraries(CemuGui INTERFACE CemuAndroidGui)
endif()
