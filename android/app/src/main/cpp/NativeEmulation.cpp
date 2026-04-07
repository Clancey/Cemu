#include "WindowSystem.h"
#include <android/log.h>
#include "JNIUtils.h"
#include "AndroidAudio.h"
#include "AndroidEmulatedController.h"
#include "AndroidFilesystemCallbacks.h"
#include "OpenXRManager.h"
#include "Cafe/HW/Latte/Core/LatteOverlay.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include "Cafe/CafeSystem.h"
#include "GameTitleLoader.h"
#include "input/ControllerFactory.h"
#include "input/InputManager.h"
#include "input/api/Android/AndroidController.h"
#include "input/api/Android/AndroidControllerProvider.h"
#include "config/ActiveSettings.h"
#include "Cemu/ncrypto/ncrypto.h"
// #include "OpenXRManager.h" // TODO: re-enable with dynamic loading

// forward declaration from main.cpp
void CemuCommonInit();

namespace NativeEmulation
{
	void initializeAudioDevices()
	{
		auto& config = GetConfig();
		if (!config.tv_device.empty())
			AndroidAudio::createAudioDevice(IAudioAPI::AudioAPI::Cubeb, config.tv_channels, config.tv_volume, true);

		if (!config.pad_device.empty())
			AndroidAudio::createAudioDevice(IAudioAPI::AudioAPI::Cubeb, config.pad_channels, config.pad_volume, false);
	}

	void createCemuDirectories()
	{
		std::wstring mlc = ActiveSettings::GetMlcPath().generic_wstring();

		// create sys/usr folder in mlc01
		const auto sysFolder = fs::path(mlc).append(L"sys");
		fs::create_directories(sysFolder);

		const auto usrFolder = fs::path(mlc).append(L"usr");
		fs::create_directories(usrFolder);
		fs::create_directories(fs::path(usrFolder).append("title/00050000")); // base
		fs::create_directories(fs::path(usrFolder).append("title/0005000c")); // dlc
		fs::create_directories(fs::path(usrFolder).append("title/0005000e")); // update

		// Mii Maker save folders {0x500101004A000, 0x500101004A100, 0x500101004A200},
		fs::create_directories(fs::path(mlc).append(L"usr/save/00050010/1004a000/user/common/db"));
		fs::create_directories(fs::path(mlc).append(L"usr/save/00050010/1004a100/user/common/db"));
		fs::create_directories(fs::path(mlc).append(L"usr/save/00050010/1004a200/user/common/db"));

		// lang files
		auto langDir = fs::path(mlc).append(L"sys/title/0005001b/1005c000/content");
		fs::create_directories(langDir);

		auto langFile = fs::path(langDir).append("language.txt");
		if (!fs::exists(langFile))
		{
			std::ofstream file(langFile);
			if (file.is_open())
			{
				const char* langStrings[] = {"ja", "en", "fr", "de", "it", "es", "zh", "ko", "nl", "pt", "ru", "zh"};
				for (const char* lang : langStrings)
					file << fmt::format(R"("{}",)", lang) << std::endl;

				file.flush();
				file.close();
			}
		}

		auto countryFile = fs::path(langDir).append("country.txt");
		if (!fs::exists(countryFile))
		{
			std::ofstream file(countryFile);
			for (sint32 i = 0; i < 201; i++)
			{
				const char* countryCode = NCrypto::GetCountryAsString(i);
				if (boost::iequals(countryCode, "NN"))
					file << "NULL," << std::endl;
				else
					file << fmt::format(R"("{}",)", countryCode) << std::endl;
			}
			file.flush();
			file.close();
		}

		// cemu directories
		const auto controllerProfileFolder = ActiveSettings::GetConfigPath(L"controllerProfiles").generic_wstring();
		if (!fs::exists(controllerProfileFolder))
			fs::create_directories(controllerProfileFolder);

		const auto memorySearcherFolder = ActiveSettings::GetUserDataPath(L"memorySearcher").generic_wstring();
		if (!fs::exists(memorySearcherFolder))
			fs::create_directories(memorySearcherFolder);
	}

	enum PrepareTitleResult : sint32
	{
		SUCCESSFUL = 0,
		ERROR_GAME_BASE_FILES_NOT_FOUND = 1,
		ERROR_NO_DISC_KEY = 2,
		ERROR_NO_TITLE_TIK = 3,
		ERROR_UNKNOWN = 4,
	};

	std::shared_ptr<ANativeWindow> createANativeWindowFromSurface(JNIEnv* env, jobject surface)
	{
		ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
		return {window, &ANativeWindow_release};
	}

	class TestSurface
	{
	  public:
		TestSurface()
		{
			JNIUtils::ScopedJNIENV env;

			jclass surfaceTextureClass = env->FindClass("android/graphics/SurfaceTexture");
			jmethodID ctorSurfaceTexture = env->GetMethodID(surfaceTextureClass, "<init>", "(I)V");
			jobject localSurfaceTexture = env->NewObject(surfaceTextureClass, ctorSurfaceTexture, 0);

			jclass surfaceClass = env->FindClass("android/view/Surface");
			jmethodID ctorSurface = env->GetMethodID(surfaceClass, "<init>", "(Landroid/graphics/SurfaceTexture;)V");
			jobject localSurface = env->NewObject(surfaceClass, ctorSurface, localSurfaceTexture);

			m_surfaceTexture = env->NewGlobalRef(localSurfaceTexture);
			m_surface = env->NewGlobalRef(localSurface);

			m_window = ANativeWindow_fromSurface(*env, m_surface);
			ANativeWindow_acquire(m_window);

			env->DeleteLocalRef(localSurfaceTexture);
			env->DeleteLocalRef(localSurface);
			env->DeleteLocalRef(surfaceTextureClass);
			env->DeleteLocalRef(surfaceClass);
		}

		~TestSurface()
		{
			JNIUtils::ScopedJNIENV env;

			ANativeWindow_release(m_window);

			jclass surfaceClass = env->FindClass("android/view/Surface");
			jmethodID releaseSurface = env->GetMethodID(surfaceClass, "release", "()V");
			env->CallVoidMethod(m_surface, releaseSurface);
			env->DeleteGlobalRef(m_surface);
			m_surface = nullptr;
			env->DeleteLocalRef(surfaceClass);

			jclass surfaceTextureClass = env->FindClass("android/graphics/SurfaceTexture");
			jmethodID releaseSurfaceTexture = env->GetMethodID(surfaceTextureClass, "release", "()V");
			env->CallVoidMethod(m_surfaceTexture, releaseSurfaceTexture);
			env->DeleteGlobalRef(m_surfaceTexture);
			m_surfaceTexture = nullptr;
			env->DeleteLocalRef(surfaceTextureClass);
		}

		ANativeWindow* getWindow()
		{
			return m_window;
		}

	  private:
		ANativeWindow* m_window;
		jobject m_surface = nullptr;
		jobject m_surfaceTexture = nullptr;
	};

	std::unique_ptr<TestSurface> g_testSurface;
	// std::unique_ptr<OpenXRManager> g_openxrManager; // TODO: re-enable
	std::atomic<bool> g_useOpenXR{false};
} // namespace NativeEmulation

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_setReplaceTVWithPadView([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean swapped)
{
	// Emulate pressing the TAB key for showing DRC instead of TV
	WindowSystem::GetWindowInfo().set_keystate(static_cast<uint32>(WindowSystem::PlatformKeyCodes::TAB), swapped);
}

std::atomic<bool> s_emulationInitialized{false};

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_initializeEmulation([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	FilesystemAndroid::SetFilesystemCallbacks(std::make_shared<AndroidFilesystemCallbacks>());
	GetConfigHandle().SetFilename(ActiveSettings::GetConfigPath("settings.xml").generic_wstring());
	NativeEmulation::createCemuDirectories();
	NetworkConfig::LoadOnce();
	ActiveSettings::Init();
	LatteOverlay_init();
	// Run heavy init on background thread to avoid ANR on Quest
	std::thread([]() {
		CemuCommonInit();
		s_emulationInitialized = true;
	}).detach();
}

static std::atomic<bool> s_rendererInitialized{false};

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_initializeRenderer(JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	__android_log_print(ANDROID_LOG_DEBUG, "Cemu", ">>> initializeRenderer called");
	InitializeGlobalVulkan();
	// On Quest, TestSurface AHardwareBuffer allocation fails.
	// Defer VulkanRenderer creation until setSurface provides a real surface.
	// The renderer will be created lazily in initializeSurface.
	s_rendererInitialized = false;
}

// Global OpenXR manager instance
std::unique_ptr<OpenXRManager> g_openxrManager = nullptr;

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_initializeOpenXR([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	__android_log_print(ANDROID_LOG_DEBUG, "Cemu", ">>> initializeOpenXR called");
	cemuLog_log(LogType::Force, "OpenXR: Initializing with dynamic loading");

	try {
		// Initialize global Vulkan (loads function pointers only)
		if (!InitializeGlobalVulkan()) {
			cemuLog_log(LogType::Force, "OpenXR: Failed to initialize global Vulkan");
			return false;
		}

		// Create OpenXR manager and let it create Vulkan objects via OpenXR
		g_openxrManager = std::make_unique<OpenXRManager>();

		// Initialize OpenXR (this creates Vulkan objects through OpenXR)
		if (!g_openxrManager->Initialize()) {
			cemuLog_log(LogType::Force, "OpenXR: Failed to initialize OpenXR manager");
			g_openxrManager.reset();
			return false;
		}

		// Get OpenXR-created Vulkan objects
		VkInstance vkInstance = g_openxrManager->GetVkInstance();
		VkPhysicalDevice vkPhysicalDevice = g_openxrManager->GetVkPhysicalDevice();
		VkDevice vkDevice = g_openxrManager->GetVkDevice();
		uint32_t queueFamilyIndex = g_openxrManager->GetQueueFamilyIndex();

		cemuLog_log(LogType::Force, "OpenXR: Created Vulkan objects - Instance={:p} PhysDevice={:p} Device={:p} QueueFamily={}",
			(void*)vkInstance, (void*)vkPhysicalDevice, (void*)vkDevice, queueFamilyIndex);

		// TODO: Pass these Vulkan objects to VulkanRenderer
		// For now, just ensure the session is created successfully
		// g_renderer = std::make_unique<VulkanRenderer>(vkInstance, vkPhysicalDevice, vkDevice, queueFamilyIndex);

		// Create OpenXR swapchain (1920x1080 for now, typical TV resolution)
		const uint32_t swapchainWidth = 1920;
		const uint32_t swapchainHeight = 1080;
		const VkFormat swapchainFormat = VK_FORMAT_R8G8B8A8_SRGB;

		if (!g_openxrManager->CreateSwapchain(swapchainWidth, swapchainHeight, swapchainFormat)) {
			cemuLog_log(LogType::Force, "OpenXR: Failed to create swapchain");
			g_openxrManager.reset();
			return false;
		}

		cemuLog_log(LogType::Force, "OpenXR: Initialization successful - starting frame loop");

		// Start OpenXR frame loop on a background thread
		// This keeps the VR session alive by submitting frames
		std::thread([]() {
			__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "OpenXR frame loop started");
			while (g_openxrManager) {
				g_openxrManager->PollEvents();
				if (!g_openxrManager->IsSessionRunning()) {
					std::this_thread::sleep_for(std::chrono::milliseconds(100));
					continue;
				}
				if (g_openxrManager->BeginFrame()) {
					uint32_t imageIndex = g_openxrManager->AcquireSwapchainImage();
					if (imageIndex != UINT32_MAX) {
						// TODO: Blit Cemu's rendered frame to the swapchain image
						// For now, just submit an empty frame (black screen in VR)
						g_openxrManager->ReleaseSwapchainImage();
					}
					// Submit frame as a quad panel in VR space
					XrPosef quadPose = {
						.orientation = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f},
						.position = {.x = 0.0f, .y = 0.0f, .z = -2.0f}
					};
					XrExtent2Df quadSize = {.width = 2.0f, .height = 1.125f};
					g_openxrManager->EndFrame(quadPose, quadSize);
				}
			}
			__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "OpenXR frame loop ended");
		}).detach();

		return true;

	} catch (const std::exception& e) {
		cemuLog_log(LogType::Force, fmt::format("OpenXR: Exception during initialization: {}", e.what()));
		g_openxrManager.reset();
		return false;
	}
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_shutdownOpenXR([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	__android_log_print(ANDROID_LOG_DEBUG, "Cemu", ">>> shutdownOpenXR called");
	if (g_openxrManager) {
		cemuLog_log(LogType::Force, "OpenXR: Shutting down");
		g_openxrManager.reset();
	}
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_isOpenXRActive([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return g_openxrManager && g_openxrManager->IsSessionRunning();
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_updateOpenXRFrame([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	if (!g_openxrManager) {
		return false;
	}

	// Poll OpenXR events to update session state
	g_openxrManager->PollEvents();

	// Return true if session is running and we should continue rendering
	return g_openxrManager->IsSessionRunning();
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_setDPI([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jfloat dpi)
{
	auto& windowInfo = WindowSystem::GetWindowInfo();
	windowInfo.dpi_scale = windowInfo.pad_dpi_scale = dpi;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_clearPadSurface([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	VulkanRenderer::GetInstance()->StopUsingPadAndWait();
	WindowSystem::GetWindowInfo().pad_open = false;
}


extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_supportsLoadingCustomDriver([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return SupportsLoadingCustomDriver();
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_setSurface(JNIEnv* env, [[maybe_unused]] jclass clazz, jobject surface, jboolean is_main_canvas)
{
	__android_log_print(ANDROID_LOG_DEBUG, "Cemu", ">>> setSurface called, isMain=%d surface=%p", (int)is_main_canvas, surface);
	JNIUtils::handleNativeException(env, [&]() {
		auto& windowHandleInfo = is_main_canvas ? WindowSystem::GetWindowInfo().canvas_main : WindowSystem::GetWindowInfo().canvas_pad;
		auto oldWindow = windowHandleInfo.surface.load();
		if (oldWindow != nullptr)
			ANativeWindow_release(static_cast<ANativeWindow*>(oldWindow));
		auto newSurface = ANativeWindow_fromSurface(env, surface);
		ANativeWindow_acquire(newSurface);
		windowHandleInfo.surface = newSurface;
		windowHandleInfo.surface.notify_all();
	});
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_initializeSurface(JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean is_main_canvas)
{
	__android_log_print(ANDROID_LOG_DEBUG, "Cemu", ">>> initializeSurface called, isMain=%d rendererInit=%d", (int)is_main_canvas, s_rendererInitialized ? 1 : 0);
	JNIUtils::handleNativeException(env, [&]() {
		// Create VulkanRenderer lazily using the real surface (Quest compatible)
		// Quest's Gralloc HAL rejects synthetic TestSurface buffers, so we must
		// use the real SurfaceView surface. Wait for it to be set by setSurface().
		if (!s_rendererInitialized && is_main_canvas) {
			auto& surfaceAtomic = WindowSystem::GetWindowInfo().canvas_main.surface;
			__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "initializeSurface: waiting for surface from TextureView...");
			cemuLog_log(LogType::Force, "initializeSurface: waiting for surface from TextureView...");
			// Wait for setSurface to provide a valid surface (called from UI thread)
			// Use atomic wait with timeout
			for (int i = 0; i < 100; i++) { // 10 seconds max
				auto surface = surfaceAtomic.load();
				if (surface) {
					__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "initializeSurface: got surface %p, creating VulkanRenderer", surface);
				cemuLog_log(LogType::Force, "initializeSurface: got surface, creating VulkanRenderer");
					WindowSystem::GetWindowInfo().window_main.surface = surface;
					try {
						g_renderer = std::make_unique<VulkanRenderer>();
						s_rendererInitialized = true;
						__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "VulkanRenderer created successfully!");
				cemuLog_log(LogType::Force, "VulkanRenderer created successfully!");
						break;
					} catch (const std::exception& e) {
						cemuLog_log(LogType::Force, "VulkanRenderer creation failed: {}", e.what());
						break;
					}
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			}
			if (!s_rendererInitialized) {
				__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "initializeSurface: timed out waiting for surface");
			cemuLog_log(LogType::Force, "initializeSurface: timed out waiting for surface");
				return;
			}
		}

		int width, height;
		if (is_main_canvas)
		{
			WindowSystem::GetWindowPhysSize(width, height);
		}
		else
		{
			WindowSystem::GetPadWindowPhysSize(width, height);
			WindowSystem::GetWindowInfo().pad_open = true;
		}

		VulkanRenderer::GetInstance()->InitializeSurface({width, height}, is_main_canvas);
	});
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_setSurfaceSize([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jint width, jint height, jboolean is_main_canvas)
{
	auto& windowInfo = WindowSystem::GetWindowInfo();
	if (is_main_canvas)
	{
		windowInfo.width = windowInfo.phys_width = width;
		windowInfo.height = windowInfo.phys_height = height;
	}
	else
	{
		windowInfo.pad_width = windowInfo.phys_pad_width = width;
		windowInfo.pad_height = windowInfo.phys_pad_height = height;
	}
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_initializeSystems([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	WindowSystem::GetWindowInfo().set_keystatesup();
	NativeEmulation::initializeAudioDevices();
}

extern "C" [[maybe_unused]] JNIEXPORT jint JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_prepareTitle([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jstring launch_path)
{
	// Wait for background init to complete
	while (!s_emulationInitialized.load())
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	fs::path launchPath = JNIUtils::toString(env, launch_path);

	TitleInfo launchTitle{launchPath};

	using enum NativeEmulation::PrepareTitleResult;

	if (launchTitle.IsValid())
	{
		// the title might not be in the TitleList, so we add it as a temporary entry
		CafeTitleList::AddTitleFromPath(launchPath);
		// title is valid, launch from TitleId
		TitleId baseTitleId;
		if (!CafeTitleList::FindBaseTitleId(launchTitle.GetAppTitleId(), baseTitleId))
		{
			return ERROR_GAME_BASE_FILES_NOT_FOUND;
		}
		CafeSystem::PREPARE_STATUS_CODE r = CafeSystem::PrepareForegroundTitle(baseTitleId);
		if (r != CafeSystem::PREPARE_STATUS_CODE::SUCCESS)
		{
			return ERROR_UNKNOWN;
		}
	}
	else // if (launchTitle.GetFormat() == TitleInfo::TitleDataFormat::INVALID_STRUCTURE )
	{
		// title is invalid, if it's an RPX/ELF we can launch it directly
		// otherwise it's an error
		CafeTitleFileType fileType = DetermineCafeSystemFileType(launchPath);
		if (fileType == CafeTitleFileType::RPX || fileType == CafeTitleFileType::ELF)
		{
			CafeSystem::PREPARE_STATUS_CODE r = CafeSystem::PrepareForegroundTitleFromStandaloneRPX(launchPath);
			if (r != CafeSystem::PREPARE_STATUS_CODE::SUCCESS)
			{
				return ERROR_UNKNOWN;
			}
		}
		else if (launchTitle.GetInvalidReason() == TitleInfo::InvalidReason::NO_DISC_KEY)
		{
			return ERROR_NO_DISC_KEY;
		}
		else if (launchTitle.GetInvalidReason() == TitleInfo::InvalidReason::NO_TITLE_TIK)
		{
			return ERROR_NO_TITLE_TIK;
		}
		else
		{
			return ERROR_UNKNOWN;
		}
	}

	return SUCCESSFUL;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_launchTitle([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	CafeSystem::LaunchForegroundTitle();
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_pauseTitle([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	CafeSystem::PauseTitle();
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_resumeTitle([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	CafeSystem::ResumeTitle();
}
