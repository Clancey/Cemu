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
		// Always initialize TV audio on Android
		if (config.tv_device.empty())
			config.tv_device = L"Default";
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

// Helper: submit empty OpenXR frame to prevent Quest from killing us during long init
void SubmitOpenXRKeepaliveFrame();

// Global OpenXR manager instance
std::unique_ptr<OpenXRManager> g_openxrManager = nullptr;

// Submits an empty OpenXR frame with zero composition layers
// Thread-safe alongside Vulkan init (no Vulkan commands used)
void SubmitOpenXRKeepaliveFrame()
{
	if (!g_openxrManager) return;
	g_openxrManager->PollEvents();
	if (!g_openxrManager->IsSessionRunning()) return;
	{ g_openxrManager->PollEvents(); if (g_openxrManager->BeginFrame()) { auto idx = g_openxrManager->AcquireSwapchainImage(); if (idx != UINT32_MAX) g_openxrManager->ReleaseSwapchainImage(); XrPosef p={{0,0,0,1},{0,0,-2}}; XrExtent2Df s={2.0f,1.125f}; g_openxrManager->EndFrame(p,s); } }
}

// Store Activity reference for OpenXR
static jobject s_openxrActivity = nullptr;

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_initializeOpenXR(JNIEnv* env, [[maybe_unused]] jclass clazz, jobject activity)
{
	// Store global ref to Activity for OpenXR
	if (s_openxrActivity) env->DeleteGlobalRef(s_openxrActivity);
	s_openxrActivity = env->NewGlobalRef(activity);
	__android_log_print(ANDROID_LOG_DEBUG, "Cemu", ">>> initializeOpenXR called");
	cemuLog_log(LogType::Force, "OpenXR: Initializing with dynamic loading");

	try {
		// Initialize global Vulkan (loads function pointers only)
		if (!InitializeGlobalVulkan()) {
			cemuLog_log(LogType::Force, "OpenXR: Failed to initialize global Vulkan");
			return false;
		}

		// Create OpenXR manager — Phase 1: Vulkan objects only, NO session
		g_openxrManager = std::make_unique<OpenXRManager>();

		if (!g_openxrManager->InitializeVulkanOnly(s_openxrActivity)) {
			cemuLog_log(LogType::Force, "OpenXR: Failed to create Vulkan objects");
			g_openxrManager.reset();
			return false;
		}

		cemuLog_log(LogType::Force, "OpenXR: Vulkan objects created (session deferred until renderer ready)");
		cemuLog_log(LogType::Force, "OpenXR: Instance={:p} PhysDevice={:p} Device={:p} QueueFamily={}",
			(void*)g_openxrManager->GetVkInstance(), (void*)g_openxrManager->GetVkPhysicalDevice(),
			(void*)g_openxrManager->GetVkDevice(), g_openxrManager->GetQueueFamilyIndex());

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

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_initializeRendererForVR(JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	__android_log_print(ANDROID_LOG_DEBUG, "Cemu", ">>> initializeRendererForVR called");

	if (!g_openxrManager) {
		__android_log_print(ANDROID_LOG_ERROR, "Cemu", "initializeRendererForVR: OpenXR not initialized");
		return;
	}

	// Wait for emulation init to complete
	while (!s_emulationInitialized.load())
		std::this_thread::sleep_for(std::chrono::milliseconds(100));

	// Load global Vulkan function pointers
	InitializeGlobalVulkan();
	__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "initializeRendererForVR: creating VulkanRenderer (NO session yet — no time pressure)");

	try {
	// Create VulkanRenderer using OpenXR's Vulkan objects
	// NO OpenXR session exists yet, so Quest won't kill us for not submitting frames
	g_renderer = std::make_unique<VulkanRenderer>(
		g_openxrManager->GetVkInstance(),
		g_openxrManager->GetVkPhysicalDevice(),
		g_openxrManager->GetVkDevice(),
		g_openxrManager->GetQueueFamilyIndex()
	);
	s_rendererInitialized = true;
	} catch (const std::exception& e) {
		__android_log_print(ANDROID_LOG_ERROR, "Cemu", "VulkanRenderer creation FAILED: %s", e.what());
		return;
	}

	__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "VulkanRenderer created!");

	// Set up window info
	auto& windowInfo = WindowSystem::GetWindowInfo();
	windowInfo.width = windowInfo.phys_width = 1920;
	windowInfo.height = windowInfo.phys_height = 1080;

	// Create a temporary render pass for Initialize() — needed for ImGui/pipeline cache
	// Use format that matches what we'll use for OpenXR swapchain
	{
		std::vector<VkImage> dummyImages;
		VulkanRenderer::GetInstance()->InitializeSurfaceFromOpenXR(dummyImages, 1920, 1080, VK_FORMAT_R8G8B8A8_SRGB);
	}

	// Skip Initialize() entirely — will be called after session begins
	__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "VR: Init deferred until session begins");

	// Start OpenXR session (creates swapchain)
	if (!g_openxrManager->StartSession()) {
		__android_log_print(ANDROID_LOG_ERROR, "Cemu", "Failed to start OpenXR session");
		return;
	}

	// Wire OpenXR swapchain to VulkanRenderer
	auto swapImages = g_openxrManager->GetSwapchainImages();
	uint32_t swapW, swapH;
	g_openxrManager->GetSwapchainSize(swapW, swapH);
	VulkanRenderer::GetInstance()->InitializeSurfaceFromOpenXR(swapImages, swapW, swapH, VK_FORMAT_R8G8B8A8_SRGB);

	// Set up OpenXR callbacks on the swapchain info
	auto& chainInfo = VulkanRenderer::GetInstance()->GetChainInfo(true);
	chainInfo.SetOpenXRCallbacks(
		g_openxrManager.get(),
		[](void* m) { static_cast<OpenXRManager*>(m)->PollEvents(); },
		[](void* m) -> bool { return static_cast<OpenXRManager*>(m)->IsSessionRunning(); },
		[](void* m) -> bool { return static_cast<OpenXRManager*>(m)->BeginFrame(); },
		[](void* m) -> uint32_t { return static_cast<OpenXRManager*>(m)->AcquireSwapchainImage(); },
		[](void* m) { static_cast<OpenXRManager*>(m)->ReleaseSwapchainImage(); },
		[](void* m) {
			XrPosef pose = {{0,0,0,1},{0,0,-2}};
			XrExtent2Df size = {2.0f, 1.125f};
			static_cast<OpenXRManager*>(m)->EndFrame(pose, size);
		}
	);
	__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "VR: OpenXR callbacks wired to swapchain");

	// Poll events to begin session
	for (int i = 0; i < 20; i++) {
		g_openxrManager->PollEvents();
		if (g_openxrManager->IsSessionRunning()) break;
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "VR: Session running=%d", g_openxrManager->IsSessionRunning() ? 1 : 0);

	// Keepalive: submit empty frames until game starts rendering
	std::thread([]() {
		__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "VR keepalive started (until game renders)");
		while (g_openxrManager && g_openxrManager->IsSessionRunning()) {
			auto* renderer = VulkanRenderer::GetInstance();
			if (renderer && renderer->GetChainInfoPtr(true) != nullptr) {
				auto& chain = renderer->GetChainInfo(true);
				if (chain.swapchainImageIndex != (uint32)-1) {
					__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "VR keepalive: game took over");
					break;
				}
			}
			{ g_openxrManager->PollEvents(); if (g_openxrManager->BeginFrame()) { auto idx = g_openxrManager->AcquireSwapchainImage(); if (idx != UINT32_MAX) g_openxrManager->ReleaseSwapchainImage(); XrPosef p={{0,0,0,1},{0,0,-2}}; XrExtent2Df s={2.0f,1.125f}; g_openxrManager->EndFrame(p,s); } }
		}
		__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "VR keepalive ended");
	}).detach();
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

extern "C" JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_pollOpenXRInput(JNIEnv* env, jclass clazz)
{
	if (!g_openxrManager) {
		return;
	}

	// Always poll events to advance session state
	g_openxrManager->PollEvents();

	static int logCounter = 0;
	if (!g_openxrManager->IsSessionRunning()) {
		if (logCounter++ % 60 == 0) {
			__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "pollOpenXRInput: session not running yet (poll %d)", logCounter);
		}
		return;
	}

	// Poll input actions
	g_openxrManager->PollInput();

	// Get current input state
	auto inputState = g_openxrManager->GetInputState();

	// Get Android controller provider
	auto apiProvider = InputManager::instance().get_api_provider(InputAPI::Android);
	auto androidControllerProvider = dynamic_cast<AndroidControllerProvider*>(apiProvider.get());
	if (!androidControllerProvider) {
		return;
	}

	const std::string descriptor = "openxr_quest_controller";
	const std::string name = "Quest Controller";

	// Map buttons to Android keycodes
	static const int buttonKeyCodes[] = {
		96,  // AKEYCODE_BUTTON_A (Right A)
		97,  // AKEYCODE_BUTTON_B (Right B)
		99,  // AKEYCODE_BUTTON_X (Left X)
		100, // AKEYCODE_BUTTON_Y (Left Y)
		108, // AKEYCODE_BUTTON_START (Menu)
		106, // AKEYCODE_BUTTON_THUMBL (Left thumbstick click)
		107, // AKEYCODE_BUTTON_THUMBR (Right thumbstick click)
	};

	static bool previousButtonStates[7] = {false};

	// Send button events only on state changes
	for (int i = 0; i < 7; ++i) {
		if (inputState.buttons[i] != previousButtonStates[i]) {
			__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "OpenXR button %d (keycode %d) = %d", i, buttonKeyCodes[i], inputState.buttons[i] ? 1 : 0);
			androidControllerProvider->on_key_event(descriptor, name, buttonKeyCodes[i], inputState.buttons[i]);
			previousButtonStates[i] = inputState.buttons[i];
		}
	}

	// Handle grip buttons as digital buttons (when grip > 0.5)
	static bool previousGripL = false;
	static bool previousGripR = false;

	bool currentGripL = inputState.gripL > 0.5f;
	bool currentGripR = inputState.gripR > 0.5f;

	if (currentGripL != previousGripL) {
		androidControllerProvider->on_key_event(descriptor, name, 102, currentGripL); // AKEYCODE_BUTTON_L1
		previousGripL = currentGripL;
	}

	if (currentGripR != previousGripR) {
		androidControllerProvider->on_key_event(descriptor, name, 103, currentGripR); // AKEYCODE_BUTTON_R1
		previousGripR = currentGripR;
	}

	// Send axis events (always send, as small changes matter for analog inputs)
	androidControllerProvider->on_axis_event(descriptor, name, 17, inputState.triggerL);  // AMOTION_EVENT_AXIS_LTRIGGER
	androidControllerProvider->on_axis_event(descriptor, name, 18, inputState.triggerR);  // AMOTION_EVENT_AXIS_RTRIGGER
	androidControllerProvider->on_axis_event(descriptor, name, 0, inputState.thumbstickLX);  // AMOTION_EVENT_AXIS_X
	androidControllerProvider->on_axis_event(descriptor, name, 1, -inputState.thumbstickLY); // AMOTION_EVENT_AXIS_Y (invert Y)
	androidControllerProvider->on_axis_event(descriptor, name, 12, inputState.thumbstickRX); // AMOTION_EVENT_AXIS_RX
	androidControllerProvider->on_axis_event(descriptor, name, 13, -inputState.thumbstickRY); // AMOTION_EVENT_AXIS_RY (invert Y)
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
