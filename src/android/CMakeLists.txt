# CMakeLists.txt for Cemu Android native sources
# Only compiled when ANDROID is defined

if(ANDROID)
    message(STATUS "Building Cemu Android native sources")

    # Android-specific source files
    set(ANDROID_SOURCES
        AndroidMain.cpp
        AndroidMain.h
        AndroidStorage.cpp
        AndroidStorage.h
        AndroidInput.cpp
        AndroidInput.h
    )

    # Create Android library
    add_library(cemu_android STATIC ${ANDROID_SOURCES})

    # Include directories
    target_include_directories(cemu_android PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
    )

    # Add NDK include if available
    if(ANDROID_NDK AND EXISTS "${ANDROID_NDK}/sources/android/native_app_glue")
        target_include_directories(cemu_android PRIVATE
            ${ANDROID_NDK}/sources/android/native_app_glue
        )
    endif()

    # Required Android libraries
    # When building with the Android toolchain, these will be found
    # For testing purposes, create dummy variables
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Android")
        # We're simulating Android build for testing
        set(ANDROID_LIB "android")
        set(LOG_LIB "log")
        set(VULKAN_LIB "vulkan")
    else()
        find_library(ANDROID_LIB android)
        find_library(LOG_LIB log)
        find_library(VULKAN_LIB vulkan)
    endif()

    # Link required libraries
    target_link_libraries(cemu_android
        # Core Cemu libraries we need for our implementations
        CemuCommon

        # Android-specific libraries
        ${ANDROID_LIB}
        ${LOG_LIB}
        ${VULKAN_LIB}
        native_app_glue
    )

    # Add native_app_glue source
    if(ANDROID_NDK AND EXISTS "${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c")
        add_library(native_app_glue STATIC
            ${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c
        )

        target_include_directories(native_app_glue PUBLIC
            ${ANDROID_NDK}/sources/android/native_app_glue
        )

        target_link_libraries(native_app_glue ${LOG_LIB} ${ANDROID_LIB})
    else()
        # Create a dummy native_app_glue for testing
        add_library(native_app_glue INTERFACE)
        message(STATUS "Creating dummy native_app_glue for testing (real one needs Android NDK)")
    endif()

    # Link native_app_glue to our Android library
    target_link_libraries(cemu_android native_app_glue)

    # Android-specific compiler definitions
    target_compile_definitions(cemu_android PRIVATE
        ANDROID=1
        __ANDROID__=1
        VK_USE_PLATFORM_ANDROID_KHR=1
        HAS_VULKAN=1
    )

    # Android-specific compiler options
    target_compile_options(cemu_android PRIVATE
        -fPIC
        -frtti
        -fexceptions
        -Wno-unused-parameter
        -Wno-unused-variable
    )

    # Set Android-specific properties
    set_target_properties(cemu_android PROPERTIES
        ANDROID_STL "c++_shared"
        ANDROID_CPP_FEATURES "rtti exceptions"
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
    )

    # Export symbols for JNI
    set_target_properties(cemu_android PROPERTIES
        LINK_FLAGS "-Wl,--export-dynamic"
    )

    # Install targets for Android packaging
    install(TARGETS cemu_android native_app_glue
        LIBRARY DESTINATION lib/${ANDROID_ABI}
        ARCHIVE DESTINATION lib/${ANDROID_ABI}
    )

    message(STATUS "Cemu Android native build configured for ABI: ${ANDROID_ABI}")

else()
    message(STATUS "Skipping Cemu Android native sources (not building for Android)")
endif()