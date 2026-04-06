project(cemuMain)

option(CEMU_CXX_FLAGS "Additional flags used for compiling Cemu source code")
if(CEMU_CXX_FLAGS)
	add_compile_options(${CEMU_CXX_FLAGS})
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
	message( FATAL_ERROR "Pointers are not 64bit" )
endif()

if(MSVC)
	add_compile_definitions(WIN32_LEAN_AND_MEAN CURL_STATICLIB)
elseif(UNIX)
	if(APPLE)
		add_compile_definitions(
			_XOPEN_SOURCE
			VK_USE_PLATFORM_MACOS_MVK
			VK_USE_PLATFORM_METAL_EXT
		)
	elseif(ANDROID)
		add_compile_definitions(
			ANDROID
			__ANDROID__
			VK_USE_PLATFORM_ANDROID_KHR
			_POSIX_C_SOURCE=200809L
			_GNU_SOURCE
		)
	elseif(CMAKE_SYSTEM_NAME STREQUAL "visionOS" OR VISIONOS)
		add_compile_definitions(
			TARGET_OS_VISION=1
			VK_USE_PLATFORM_MACOS_MVK
			VK_USE_PLATFORM_METAL_EXT
			_XOPEN_SOURCE
		)
	else()
		add_compile_definitions(
			VK_USE_PLATFORM_XLIB_KHR # legacy. Do we need to support XLIB surfaces?
			VK_USE_PLATFORM_XCB_KHR
		)
		if (ENABLE_WAYLAND)
			add_compile_definitions(VK_USE_PLATFORM_WAYLAND_KHR)
		endif()
	endif()
	# warnings
	if(CMAKE_C_COMPILER_ID STREQUAL "Clang")
		add_compile_options(-Wno-ambiguous-reversed-operator)
	endif()

    add_compile_options(-Wno-multichar -Wno-invalid-offsetof -Wno-switch -Wno-ignored-attributes -Wno-deprecated-enum-enum-conversion)
endif()

add_compile_definitions(VK_NO_PROTOTYPES)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

# on msvc we can reuse the PCH from CemuCommon, on other platforms it's not straight forward to reuse the PCH so we opt out of it
function(cemu_use_precompiled_header target_name)
	if(CMAKE_SYSTEM_NAME STREQUAL "visionOS" OR VISIONOS)
		# PCH REUSE_FROM breaks on visionOS cross-compilation with non-Xcode generators
		# due to ObjCXX PCH path resolution issues — use per-target PCH instead
		target_precompile_headers(${target_name} PRIVATE
			"$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_SOURCE_DIR}/src/Common/precompiled.h>")
		return()
	endif()
	# Always reuse from CemuCommon to avoid path issues
	target_precompile_headers(${target_name} REUSE_FROM CemuCommon)
endfunction()

add_subdirectory(Common)
add_subdirectory(gui)
add_subdirectory(Cafe)
add_subdirectory(Cemu)
add_subdirectory(config)
add_subdirectory(input)
add_subdirectory(audio)
add_subdirectory(util)
add_subdirectory(imgui)
add_subdirectory(resource)

# Android-specific sources
if(ANDROID)
	add_subdirectory(android)
endif()

# visionOS-specific sources
if(CMAKE_SYSTEM_NAME STREQUAL "visionOS" OR VISIONOS)
	add_subdirectory(visionos)
endif()

if(ANDROID)
	# For Android, create a STATIC library — JNI wrapper (CemuAndroid) is the SHARED lib
	add_library(CemuBin STATIC
		main.cpp
		mainLLE.cpp
	)
	# Link our Android native sources
	target_link_libraries(CemuBin PRIVATE cemu_android)
elseif(CMAKE_SYSTEM_NAME STREQUAL "visionOS" OR VISIONOS)
	# For visionOS, create a shared library instead of executable
	add_library(CemuBin SHARED
		main.cpp
		mainLLE.cpp
	)
	# Link our visionOS native sources — use force_load to prevent dead-stripping
	# of Obj-C classes (CemuBridge, MetalHostView) that the Swift app needs
	target_link_options(CemuBin PRIVATE
		"LINKER:-force_load,$<TARGET_FILE:cemu_visionos>"
	)
	target_link_libraries(CemuBin PRIVATE cemu_visionos)

	# Fix install names: rewrite absolute homebrew paths to @rpath/ so the
	# app finds dependency dylibs in its Frameworks/ bundle directory
	add_custom_command(TARGET CemuBin POST_BUILD
		COMMAND install_name_tool
			-change "/opt/homebrew/opt/glslang/lib/libglslang.16.dylib" "@rpath/libglslang.dylib"
			-change "/opt/homebrew/opt/spirv-tools/lib/libSPIRV-Tools-opt.dylib" "@rpath/libSPIRV-Tools-opt.dylib"
			-change "/opt/homebrew/opt/spirv-tools/lib/libSPIRV-Tools.dylib" "@rpath/libSPIRV-Tools.dylib"
			"$<TARGET_FILE:CemuBin>"
		COMMENT "Fixing dylib install names for visionOS bundle"
	)
else()
	add_executable(CemuBin
		main.cpp
		mainLLE.cpp
	)
endif()

cemu_use_precompiled_header(CemuBin)

if(MSVC AND MSVC_VERSION EQUAL 1940)
	# workaround for an msvc issue on VS 17.10 where generated ILK files are too large
	# see https://developercommunity.visualstudio.com/t/After-updating-to-VS-1710-the-size-of-/10665511
	set_target_properties(CemuBin PROPERTIES LINK_FLAGS "/INCREMENTAL:NO")
endif()

if(WIN32)
	target_sources(CemuBin PRIVATE
	resource/cemu.rc
	../dist/windows/cemu.manifest
	)
endif()

set_property(TARGET CemuBin PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set_property(TARGET CemuBin PROPERTY WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>)
set(OUTPUT_NAME "Cemu_$<LOWER_CASE:$<CONFIG>>")

if (MACOS_BUNDLE)
	set_property(TARGET CemuBin PROPERTY MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/resource/MacOSXBundleInfo.plist.in")

	set(RESOURCE_FILES "${CMAKE_SOURCE_DIR}/src/resource/cemu.icns")
	target_sources(CemuBin PRIVATE "${RESOURCE_FILES}")

	set(MACOSX_BUNDLE_ICON_FILE "cemu.icns")
	set(MACOSX_BUNDLE_GUI_IDENTIFIER "info.cemu.Cemu")
	set(MACOSX_BUNDLE_BUNDLE_NAME "Cemu")
	set(MACOSX_BUNDLE_SHORT_VERSION_STRING "${EMULATOR_VERSION_MAJOR}.${EMULATOR_VERSION_MINOR}.${EMULATOR_VERSION_PATCH}")
	set(MACOSX_BUNDLE_BUNDLE_VERSION "${EMULATOR_VERSION_MAJOR}.${EMULATOR_VERSION_MINOR}.${EMULATOR_VERSION_PATCH}")
	set(MACOSX_BUNDLE_COPYRIGHT "Copyright © 2024 Cemu Project")

	set(MACOSX_BUNDLE_CATEGORY "public.app-category.games")
 	set(MACOSX_MINIMUM_SYSTEM_VERSION "13.4")
  	set(MACOSX_BUNDLE_TYPE_EXTENSION "wua")

	set_target_properties(CemuBin PROPERTIES
		MACOSX_BUNDLE true
		RESOURCE "${RESOURCE_FILES}"
	)

	set(FOLDERS gameProfiles resources)
	foreach(folder ${FOLDERS})
		add_custom_command (TARGET CemuBin POST_BUILD
			COMMAND ${CMAKE_COMMAND} ARGS -E copy_directory "${CMAKE_SOURCE_DIR}/bin/${folder}" "${CMAKE_SOURCE_DIR}/bin/${OUTPUT_NAME}.app/Contents/SharedSupport/${folder}")
	endforeach(folder)

	if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	    set(LIBUSB_PATH "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/debug/lib/libusb-1.0.0.dylib")
	else()
	    set(LIBUSB_PATH "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/lib/libusb-1.0.0.dylib")
	endif()

	if (EXISTS "/usr/local/lib/libMoltenVK.dylib")
		set(MOLTENVK_PATH "/usr/local/lib/libMoltenVK.dylib")
	elseif (EXISTS "/opt/homebrew/lib/libMoltenVK.dylib")
		set(MOLTENVK_PATH "/opt/homebrew/lib/libMoltenVK.dylib")
	else()
		message(FATAL_ERROR "failed to find libMoltenVK.dylib")
	endif ()

	add_custom_command (TARGET CemuBin POST_BUILD
		COMMAND ${CMAKE_COMMAND} ARGS -E copy "${MOLTENVK_PATH}" "${CMAKE_SOURCE_DIR}/bin/${OUTPUT_NAME}.app/Contents/Frameworks/libMoltenVK.dylib"
		COMMAND ${CMAKE_COMMAND} ARGS -E copy "${LIBUSB_PATH}" "${CMAKE_SOURCE_DIR}/bin/${OUTPUT_NAME}.app/Contents/Frameworks/libusb-1.0.0.dylib"
		COMMAND ${CMAKE_COMMAND} ARGS -E copy "${CMAKE_SOURCE_DIR}/src/resource/update.sh" "${CMAKE_SOURCE_DIR}/bin/${OUTPUT_NAME}.app/Contents/MacOS/update.sh"
		COMMAND bash -c "install_name_tool -add_rpath @executable_path/../Frameworks ${CMAKE_SOURCE_DIR}/bin/${OUTPUT_NAME}.app/Contents/MacOS/${OUTPUT_NAME}"
		COMMAND bash -c "install_name_tool -change ${LIBUSB_PATH} @executable_path/../Frameworks/libusb-1.0.0.dylib ${CMAKE_SOURCE_DIR}/bin/${OUTPUT_NAME}.app/Contents/MacOS/${OUTPUT_NAME}")
else()
    if(APPLE AND NOT (CMAKE_SYSTEM_NAME STREQUAL "visionOS" OR VISIONOS))
        find_library(MOLTENVK_LIBRARY
            NAMES MoltenVK moltenvk libMoltenVK.dylib
            PATHS /usr/local/lib /opt/homebrew/lib
        )
        if(MOLTENVK_LIBRARY)
            message(STATUS "Found MoltenVK: ${MOLTENVK_LIBRARY}")
            target_link_libraries(CemuBin PRIVATE ${MOLTENVK_LIBRARY})
        else()
            message(WARNING "libMoltenVK.dylib not found")
        endif()
        set_target_properties(CemuBin PROPERTIES
            BUILD_WITH_INSTALL_RPATH TRUE
            INSTALL_RPATH "/usr/local/lib;/opt/homebrew/lib"
        )
    endif()
endif()

set_target_properties(CemuBin PROPERTIES
	# multi-configuration generators will add a config subdirectory to RUNTIME_OUTPUT_DIRECTORY if no generator expression is used
	# to get the same behavior everywhere we append an empty generator expression
	RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../bin/$<1:>"
	OUTPUT_NAME "${OUTPUT_NAME}"
)

target_link_libraries(CemuBin PRIVATE
	CemuAudio
	CemuCafe
	CemuCommon
	CemuComponents
	CemuConfig
	CemuGui
	CemuInput
	CemuUtil
)

if(NOT ANDROID AND NOT (CMAKE_SYSTEM_NAME STREQUAL "visionOS" OR VISIONOS))
	target_link_libraries(CemuBin PRIVATE SDL2::SDL2)
endif()

if(UNIX AND NOT APPLE)
	# due to nasm output some linkers will make stack executable
	# cemu does not require this so we explicity disable it
	target_link_options(CemuBin PRIVATE -z noexecstack)
	# some residual debug info from boost/discord-rpc is normally included
	# most likely not helpful in debugging problems with cemu code
	target_link_options(CemuBin PRIVATE "$<$<CONFIG:Release>:-Xlinker;--strip-debug>")
endif()

if (BSD)
	target_link_libraries(CemuBin PRIVATE execinfo SPIRV-Tools SPIRV-Tools-opt)
endif()

# JNI wrapper for Android — creates the CemuAndroid shared library
if(ANDROID)
	add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../android/app/src/main/cpp ${CMAKE_CURRENT_BINARY_DIR}/android_jni)
endif()
