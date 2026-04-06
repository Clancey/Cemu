cmake_minimum_required(VERSION 3.21.1)

option(ENABLE_VCPKG "Enable the vcpkg package manager" ON)
option(MACOS_BUNDLE "The executable when built on macOS will be created as an application bundle" OFF)
option(ALLOW_PORTABLE "Allow Cemu to be run in portable mode" ON)

# used by CI script to set version:
set(EMULATOR_VERSION_MAJOR "0" CACHE STRING "")
set(EMULATOR_VERSION_MINOR "0" CACHE STRING "")
set(EMULATOR_VERSION_PATCH "0" CACHE STRING "")

set(THREADS_PREFER_PTHREAD_FLAG true) # needs to be set before vcpkg

# visionOS threading workaround
if(CMAKE_SYSTEM_NAME STREQUAL "visionOS" OR VISIONOS)
    set(CMAKE_THREAD_LIBS_INIT "")
    set(CMAKE_HAVE_THREADS_LIBRARY TRUE)
    set(CMAKE_USE_WIN32_THREADS_INIT FALSE)
    set(CMAKE_USE_PTHREADS_INIT TRUE)
    set(Threads_FOUND TRUE)
endif()

execute_process(
		COMMAND git log --format=%h -1
		WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
		OUTPUT_VARIABLE GIT_HASH
		OUTPUT_STRIP_TRAILING_WHITESPACE
)
add_definitions(-DEMULATOR_HASH=${GIT_HASH})

if (ENABLE_VCPKG)
	# check if vcpkg is shallow and unshallow it if necessary
	execute_process(
	COMMAND git rev-parse --is-shallow-repository
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/dependencies/vcpkg
	OUTPUT_VARIABLE is_vcpkg_shallow
	OUTPUT_STRIP_TRAILING_WHITESPACE
    )

	if(is_vcpkg_shallow STREQUAL "true")
		message(STATUS "vcpkg is shallow. Unshallowing it now...")
		execute_process(
			COMMAND git fetch --unshallow
			WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/dependencies/vcpkg"
			RESULT_VARIABLE result
			OUTPUT_VARIABLE output
		)
	endif()

	if(ANDROID)
		# Set Android-specific vcpkg triplet
		set(VCPKG_TARGET_TRIPLET arm64-android)
		# Use Android-specific overlay ports if they exist, otherwise empty
		if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/dependencies/vcpkg_overlay_ports_android")
			set(VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_LIST_DIR}/dependencies/vcpkg_overlay_ports_android")
		else()
			set(VCPKG_OVERLAY_PORTS "")
		endif()
	elseif(CMAKE_SYSTEM_NAME STREQUAL "visionOS" OR VISIONOS)
		# Set visionOS-specific vcpkg triplet (try arm64-osx first, fallback to generic if needed)
		set(VCPKG_TARGET_TRIPLET arm64-osx)
		set(VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_LIST_DIR}/dependencies/vcpkg_overlay_ports_mac;${CMAKE_CURRENT_LIST_DIR}/dependencies/vcpkg_overlay_ports")
	elseif(UNIX AND NOT APPLE)
		set(VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_LIST_DIR}/dependencies/vcpkg_overlay_ports_linux;${CMAKE_CURRENT_LIST_DIR}/dependencies/vcpkg_overlay_ports")
	elseif(APPLE)
		set(VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_LIST_DIR}/dependencies/vcpkg_overlay_ports_mac;${CMAKE_CURRENT_LIST_DIR}/dependencies/vcpkg_overlay_ports")
	else()
		set(VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_LIST_DIR}/dependencies/vcpkg_overlay_ports_win;${CMAKE_CURRENT_LIST_DIR}/dependencies/vcpkg_overlay_ports")
	endif()
	set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/dependencies/vcpkg/scripts/buildsystems/vcpkg.cmake"
		CACHE STRING "Vcpkg toolchain file")
	# Set this so that all the various find_package() calls don't need an explicit
	# CONFIG option
	set(CMAKE_FIND_PACKAGE_PREFER_CONFIG TRUE)
	if (WIN32)
		set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
	endif()
endif()

project(Cemu VERSION 2.0.0)

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_definitions($<$<CONFIG:Debug>:CEMU_DEBUG_ASSERT>) # if build type is debug, set CEMU_DEBUG_ASSERT

add_definitions(-DEMULATOR_VERSION_MAJOR=${EMULATOR_VERSION_MAJOR})
add_definitions(-DEMULATOR_VERSION_MINOR=${EMULATOR_VERSION_MINOR})
add_definitions(-DEMULATOR_VERSION_PATCH=${EMULATOR_VERSION_PATCH})

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# enable link time optimization for release builds
# Note: On Android, IPO is disabled because CMake adds -fuse-ld=gold which is unavailable
if(NOT ANDROID)
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
	set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO ON)
endif()

if (MSVC)
	set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT CemuBin)
	# floating point model: precise, fiber safe optimizations
	add_compile_options(/EHsc /fp:precise)
	if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
		# Speeds up static linking (especially helpful in incremental compilation)
		if((CMAKE_LINKER MATCHES ".*lld-link.*") AND (CMAKE_AR MATCHES ".*llvm-lib.*"))
			set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY STATIC_LIBRARY_OPTIONS /llvmlibthin)
		endif()
  	else()
	  add_compile_options(/GT)
	endif()
	# enable additional optimization flags for release builds
	add_compile_options($<$<CONFIG:Release,RelWithDebInfo>:/Oi>) # enable intrinsic functions
	add_compile_options($<$<CONFIG:Release,RelWithDebInfo>:/Ot>) # favor speed
endif()

if (APPLE)
    enable_language(OBJC OBJCXX)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.4")
endif()

if (UNIX AND NOT APPLE AND NOT ANDROID)
	option(ENABLE_WAYLAND "Build with Wayland support" ON)
	option(ENABLE_FERAL_GAMEMODE "Enables Feral Interactive GameMode Support" ON)
	option(ENABLE_BLUEZ "Build with Bluez support" ON)
endif()

# Android-specific configuration
if (ANDROID)
	# Force disable desktop-only features
	set(ENABLE_WAYLAND OFF CACHE BOOL "Disable Wayland on Android" FORCE)
	set(ENABLE_FERAL_GAMEMODE OFF CACHE BOOL "Disable GameMode on Android" FORCE)
	set(ENABLE_BLUEZ OFF CACHE BOOL "Disable Bluez on Android" FORCE)
	set(ENABLE_WXWIDGETS OFF CACHE BOOL "Disable wxWidgets on Android" FORCE)
	set(ENABLE_OPENGL OFF CACHE BOOL "Disable OpenGL on Android (use Vulkan)" FORCE)
	set(ENABLE_DISCORD_RPC OFF CACHE BOOL "Disable Discord RPC on Android" FORCE)

	# Force enable supported features
	set(ENABLE_VULKAN ON CACHE BOOL "Enable Vulkan on Android" FORCE)

	# Disable features that need system packages not available via NDK
	# Input/audio handled through Android APIs directly
	set(ENABLE_SDL OFF CACHE BOOL "Disable SDL on Android (using native input)" FORCE)
	set(ENABLE_CUBEB OFF CACHE BOOL "Disable cubeb on Android (using native audio)" FORCE)
	set(ENABLE_HIDAPI OFF CACHE BOOL "Disable HIDAPI on Android" FORCE)
endif()

# visionOS-specific configuration
if (CMAKE_SYSTEM_NAME STREQUAL "visionOS" OR VISIONOS)
	# Force disable desktop-only features
	set(ENABLE_WAYLAND OFF CACHE BOOL "Disable Wayland on visionOS" FORCE)
	set(ENABLE_FERAL_GAMEMODE OFF CACHE BOOL "Disable GameMode on visionOS" FORCE)
	set(ENABLE_BLUEZ OFF CACHE BOOL "Disable Bluez on visionOS" FORCE)
	set(ENABLE_WXWIDGETS OFF CACHE BOOL "Disable wxWidgets on visionOS" FORCE)
	set(ENABLE_OPENGL OFF CACHE BOOL "Disable OpenGL on visionOS (Metal only)" FORCE)
	set(ENABLE_DISCORD_RPC OFF CACHE BOOL "Disable Discord RPC on visionOS" FORCE)
	set(ENABLE_VULKAN OFF CACHE BOOL "Disable Vulkan on visionOS (Metal only)" FORCE)

	# Force enable supported features
	set(ENABLE_METAL ON CACHE BOOL "Enable Metal on visionOS" FORCE)

	# Disable input/audio APIs not available on visionOS
	set(ENABLE_SDL OFF CACHE BOOL "Disable SDL on visionOS (using native input)" FORCE)
	set(ENABLE_CUBEB OFF CACHE BOOL "Disable cubeb on visionOS (using native audio)" FORCE)
	set(ENABLE_HIDAPI OFF CACHE BOOL "Disable HIDAPI on visionOS" FORCE)
	set(ENABLE_XINPUT OFF CACHE BOOL "Disable XInput on visionOS" FORCE)
	set(ENABLE_DIRECTINPUT OFF CACHE BOOL "Disable DirectInput on visionOS" FORCE)
endif()

if (APPLE)
    set(ENABLE_METAL_DEFAULT ON)
else()
    set(ENABLE_METAL_DEFAULT OFF)
endif()

option(ENABLE_OPENGL "Enables the OpenGL backend" ON)
option(ENABLE_VULKAN "Enables the Vulkan backend" ON)
option(ENABLE_METAL "Enables the Metal backend" ${ENABLE_METAL_DEFAULT})
option(ENABLE_DISCORD_RPC "Enables the Discord Rich Presence feature" ON)

if (ENABLE_METAL AND NOT APPLE)
    message(FATAL_ERROR "Metal backend is only supported on Apple platforms")
endif()

# input backends
if (WIN32)
	option(ENABLE_XINPUT "Enables the usage of XInput" ON)
	option(ENABLE_DIRECTINPUT "Enables the usage of DirectInput" ON)
	add_compile_definitions(HAS_DIRECTINPUT)
endif()

# Android input configuration
if (ANDROID)
	set(ENABLE_XINPUT OFF CACHE BOOL "Disable XInput on Android" FORCE)
	set(ENABLE_DIRECTINPUT OFF CACHE BOOL "Disable DirectInput on Android" FORCE)
endif()

option(ENABLE_HIDAPI "Build with HIDAPI" ON)
option(ENABLE_SDL "Enables the SDLController backend" ON)

# audio backends
if (WIN32)
	option(ENABLE_DIRECTAUDIO "Enables the directaudio backend" ON)
	option(ENABLE_XAUDIO "Enables the xaudio backend" ON)
endif()

# Android audio configuration
if (ANDROID)
	set(ENABLE_DIRECTAUDIO OFF CACHE BOOL "Disable DirectAudio on Android" FORCE)
	set(ENABLE_XAUDIO OFF CACHE BOOL "Disable XAudio on Android" FORCE)
endif()
option(ENABLE_CUBEB "Enabled cubeb backend" ON)

option(ENABLE_WXWIDGETS "Build with wxWidgets UI (Currently required)" ON)

find_package(Threads REQUIRED)

# Core dependencies needed by all platforms including Android
# Use vcpkg to find dependencies for all platforms, including Android
find_package(Boost COMPONENTS program_options filesystem nowide QUIET)
find_package(glm QUIET)
find_package(fmt 9 QUIET)
find_package(pugixml QUIET)
find_package(RapidJSON QUIET)
find_package(libzip QUIET)
find_package(glslang QUIET)
find_package(ZLIB QUIET)
find_package(zstd MODULE QUIET)
find_package(OpenSSL COMPONENTS Crypto SSL QUIET)
find_package(PNG QUIET)

if(NOT ANDROID)
	# Desktop and visionOS builds use vcpkg or system packages
	find_package(Boost COMPONENTS program_options filesystem nowide REQUIRED)
	find_package(glm REQUIRED)
	find_package(fmt 9 REQUIRED)
	find_package(pugixml REQUIRED)
	find_package(RapidJSON REQUIRED)
	find_package(libzip REQUIRED)
	find_package(glslang REQUIRED)
	find_package(ZLIB REQUIRED)
	find_package(zstd MODULE REQUIRED) # MODULE so that zstd::zstd is available
	find_package(OpenSSL COMPONENTS Crypto SSL REQUIRED)
	find_package(PNG REQUIRED)
endif()

# Platform-specific dependencies
if(NOT ANDROID AND NOT (CMAKE_SYSTEM_NAME STREQUAL "visionOS" OR VISIONOS))
	# Desktop-only dependencies
	find_package(SDL2 REQUIRED)
	find_package(CURL REQUIRED)
elseif(ANDROID)
	# Android-specific dependency handling
	message(STATUS "Android build: using core dependencies, skipping desktop-only packages")
elseif(CMAKE_SYSTEM_NAME STREQUAL "visionOS" OR VISIONOS)
	# visionOS-specific dependency handling
	message(STATUS "visionOS build: using core dependencies, skipping desktop-only packages")
	# For now, disable CURL on visionOS (uses native networking)
	# find_package(CURL REQUIRED)
endif()

# glslang versions older than 11.11.0 define targets without a namespace
if (NOT TARGET glslang::SPIRV AND TARGET SPIRV)
	add_library(glslang::SPIRV ALIAS SPIRV)
endif()

if (UNIX AND NOT APPLE AND NOT ANDROID)
	find_package(X11 REQUIRED)
	if (ENABLE_WAYLAND)
		find_package(Wayland REQUIRED Client)
		find_package(WaylandScanner REQUIRED)
		find_package(WaylandProtocols 1.15 REQUIRED)

		ecm_add_wayland_client_protocol(WAYLAND_PROTOCOL_SRCS
			PROTOCOL "${WaylandProtocols_DATADIR}/stable/viewporter/viewporter.xml"
			BASENAME viewporter)
		add_library(CemuWaylandProtocols STATIC ${WAYLAND_PROTOCOL_SRCS})
		target_include_directories(CemuWaylandProtocols PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
		target_include_directories(CemuWaylandProtocols PRIVATE ${Wayland_INCLUDE_DIRS})
		add_compile_definitions(HAS_WAYLAND)
	endif()
	find_package(GTK3 REQUIRED)

	if(ENABLE_BLUEZ)
		find_package(bluez REQUIRED)
		set(SUPPORTS_WIIMOTE ON)
		add_compile_definitions(HAS_BLUEZ)
	endif()

endif()

if (ENABLE_VULKAN)
	include_directories("dependencies/Vulkan-Headers/include")
endif()

if (ENABLE_OPENGL)
	find_package(OpenGL REQUIRED)
endif()

if (ENABLE_METAL)
    include_directories(${CMAKE_SOURCE_DIR}/dependencies/metal-cpp)

    add_definitions(-DENABLE_METAL=1)
endif()

if (ENABLE_DISCORD_RPC)
	add_compile_definitions(ENABLE_DISCORD_RPC)
endif()

if (ENABLE_HIDAPI)
	find_package(hidapi REQUIRED)
	set(SUPPORTS_WIIMOTE ON)
	add_compile_definitions(HAS_HIDAPI)
endif ()

if(UNIX AND NOT APPLE AND NOT ANDROID)
	if(ENABLE_FERAL_GAMEMODE)
		add_compile_definitions(ENABLE_FERAL_GAMEMODE)
		add_subdirectory(dependencies/gamemode EXCLUDE_FROM_ALL)
		target_include_directories(gamemode INTERFACE ./dependencies/gamemode/lib)
	endif()
endif()

# Android-specific Vulkan setup
if (ANDROID AND ENABLE_VULKAN)
	# On Android, Vulkan is a system library
	find_library(VULKAN_LIB vulkan)
	if (VULKAN_LIB)
		add_library(vulkan SHARED IMPORTED)
		set_target_properties(vulkan PROPERTIES IMPORTED_LOCATION ${VULKAN_LIB})
	else()
		message(FATAL_ERROR "Vulkan library not found on Android")
	endif()
endif()

if (ENABLE_WXWIDGETS AND NOT ANDROID AND NOT (CMAKE_SYSTEM_NAME STREQUAL "visionOS" OR VISIONOS))
	find_package(wxWidgets 3.3 REQUIRED COMPONENTS base core gl propgrid xrc)
endif()

if (ENABLE_CUBEB)
	if (NOT ENABLE_VCPKG)
	    find_package(cubeb)
	endif()
	if (NOT cubeb_FOUND)
		option(BUILD_TESTS "" OFF)
		option(BUILD_TOOLS "" OFF)
		option(BUNDLE_SPEEX "" OFF)
		set(USE_WINMM OFF CACHE BOOL "")
		add_subdirectory("dependencies/cubeb" EXCLUDE_FROM_ALL SYSTEM)
		set_property(TARGET cubeb PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
		add_library(cubeb::cubeb ALIAS cubeb)
	endif()
	add_compile_definitions("HAS_CUBEB=1")
endif()

add_subdirectory("dependencies/ih264d" EXCLUDE_FROM_ALL)

if (CMAKE_OSX_ARCHITECTURES)
	set(CEMU_ARCHITECTURE ${CMAKE_OSX_ARCHITECTURES})
else()
	set(CEMU_ARCHITECTURE ${CMAKE_SYSTEM_PROCESSOR})
endif()
if(CEMU_ARCHITECTURE MATCHES "(aarch64)|(AARCH64)|(arm64)|(ARM64)")
	add_subdirectory("dependencies/xbyak_aarch64" EXCLUDE_FROM_ALL)
endif()

find_package(ZArchive)
if (NOT ZArchive_FOUND)
	add_subdirectory("dependencies/ZArchive" EXCLUDE_FROM_ALL)
endif()

add_subdirectory(src)
