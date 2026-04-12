#include "WindowSystem.h"
#include <android/input.h>
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
#include <chrono>
#include <mutex>
#include <thread>
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
static std::atomic<bool> s_openxrMappingsApplied{false};

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
bool SubmitOpenXRKeepaliveFrame();

// Global: set when the app first presents a real VR frame
std::atomic<bool> g_vrGameRendering{false};

// Global OpenXR manager instance
std::unique_ptr<OpenXRManager> g_openxrManager = nullptr;

namespace
{
	constexpr auto kOpenXRKeepaliveIdleThreshold = std::chrono::milliseconds(250);
	constexpr auto kOpenXRKeepalivePollInterval = std::chrono::milliseconds(50);
	constexpr auto kOpenXRKeepalivePostSubmitDelay = std::chrono::milliseconds(100);

	std::mutex g_openxrFrameLoopMutex;
	std::thread g_openxrKeepaliveThread;
	std::atomic<bool> g_openxrKeepaliveStop{false};
	std::atomic<int64_t> g_openxrLastPresentedFrameNs{0};

	int64_t GetOpenXRSteadyClockNs()
	{
		return std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now().time_since_epoch())
			.count();
	}

	void NoteOpenXRFramePresented()
	{
		g_openxrLastPresentedFrameNs.store(GetOpenXRSteadyClockNs());
	}

	void StopOpenXRKeepaliveThread()
	{
		g_openxrKeepaliveStop.store(true);
		if (g_openxrKeepaliveThread.joinable())
			g_openxrKeepaliveThread.join();
	}

	void StartOpenXRKeepaliveThread()
	{
		StopOpenXRKeepaliveThread();
		g_openxrKeepaliveStop.store(false);
		NoteOpenXRFramePresented();
		g_openxrKeepaliveThread = std::thread([]() {
			const auto idleThresholdNs = std::chrono::duration_cast<std::chrono::nanoseconds>(kOpenXRKeepaliveIdleThreshold).count();
			while (!g_openxrKeepaliveStop.load()) {
				auto* manager = g_openxrManager.get();
				if (!manager || !manager->IsSessionRunning()) {
					std::this_thread::sleep_for(kOpenXRKeepalivePollInterval);
					continue;
				}

				const auto idleNs = GetOpenXRSteadyClockNs() - g_openxrLastPresentedFrameNs.load();
				if (idleNs < idleThresholdNs) {
					std::this_thread::sleep_for(kOpenXRKeepalivePollInterval);
					continue;
				}

				std::unique_lock<std::mutex> frameLock(g_openxrFrameLoopMutex, std::try_to_lock);
				if (!frameLock.owns_lock()) {
					std::this_thread::sleep_for(kOpenXRKeepalivePollInterval);
					continue;
				}

				manager = g_openxrManager.get();
				if (!manager || !manager->IsSessionRunning()) {
					std::this_thread::sleep_for(kOpenXRKeepalivePollInterval);
					continue;
				}

				const auto lockedIdleNs = GetOpenXRSteadyClockNs() - g_openxrLastPresentedFrameNs.load();
				if (lockedIdleNs < idleThresholdNs) {
					std::this_thread::sleep_for(kOpenXRKeepalivePollInterval);
					continue;
				}

				__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "VR keepalive: submit idle frame after %lld ms", static_cast<long long>(lockedIdleNs / 1000000));
				if (SubmitOpenXRKeepaliveFrame())
					NoteOpenXRFramePresented();
				std::this_thread::sleep_for(kOpenXRKeepalivePostSubmitDelay);
			}
		});
	}
}

// Submits an empty OpenXR frame with zero composition layers
// Thread-safe alongside Vulkan init (no Vulkan commands used)
bool SubmitOpenXRKeepaliveFrame()
{
	if (!g_openxrManager)
		return false;
	g_openxrManager->PollEvents();
	if (!g_openxrManager->IsSessionRunning())
		return false;

	g_openxrManager->PollEvents();
	if (!g_openxrManager->BeginFrame())
		return false;

	auto idx = g_openxrManager->AcquireSwapchainImage();
	if (idx == UINT32_MAX)
		return g_openxrManager->CancelFrame();

	g_openxrManager->ReleaseSwapchainImage();
	XrPosef pose = {{0,0,0,1},{0,0,-2}};
	XrExtent2Df size = {2.0f, 1.125f};
	return g_openxrManager->EndFrame(pose, size);
}

// Store Activity reference for OpenXR
static jobject s_openxrActivity = nullptr;

namespace
{
	constexpr const char* kOpenXRControllerDescriptor = "openxr_quest_controller";
	constexpr const char* kOpenXRControllerName = "Quest Controller";
	constexpr const char* kOpenXRSecondaryMotionDescriptor = "openxr_quest_controller_left_motion";
	constexpr const char* kOpenXRSecondaryMotionName = "Quest Left Motion";

	struct OpenXRForwardState
	{
		bool buttons[7]{};
		bool gripL = false;
		bool gripR = false;
	};

	OpenXRForwardState g_openxrForwardState{};

	void ForwardOpenXRInputState(const OpenXRManager::ControllerInputState& inputState);

	void ResetOpenXRForwardState()
	{
		g_openxrForwardState = {};
	}

	bool BeginOpenXRRenderFrame(OpenXRManager* manager)
	{
		g_openxrFrameLoopMutex.lock();
		if (!manager->BeginFrame()) {
			g_openxrFrameLoopMutex.unlock();
			return false;
		}

		ForwardOpenXRInputState(manager->GetInputState());
		return true;
	}

	uint32_t AcquireOpenXRRenderFrameImage(OpenXRManager* manager)
	{
		const auto imageIndex = manager->AcquireSwapchainImage();
		if (imageIndex != UINT32_MAX)
			return imageIndex;

		manager->CancelFrame();
		g_openxrFrameLoopMutex.unlock();
		return UINT32_MAX;
	}

	void EndOpenXRRenderFrame(OpenXRManager* manager)
	{
		XrPosef pose = {{0,0,0,1},{0,0,-2}};
		XrExtent2Df size = {2.0f, 1.125f};
		manager->EndFrame(pose, size);
		NoteOpenXRFramePresented();
		g_openxrFrameLoopMutex.unlock();
	}

	AndroidControllerProvider* GetAndroidControllerProvider()
	{
		auto apiProvider = InputManager::instance().get_api_provider(InputAPI::Android);
		return dynamic_cast<AndroidControllerProvider*>(apiProvider.get());
	}

	PositionVisibility ToPositionVisibility(int visibility)
	{
		switch (visibility) {
		case 1:
			return PositionVisibility::FULL;
		case 2:
			return PositionVisibility::PARTIAL;
		default:
			return PositionVisibility::NONE;
		}
	}

	MotionSample ToMotionSample(const OpenXRManager::ControllerInputState::MotionState& motionState)
	{
		float acceleration[3] = {
			motionState.acceleration[0],
			motionState.acceleration[1],
			motionState.acceleration[2],
		};
		float gyro[3] = {
			motionState.gyro[0],
			motionState.gyro[1],
			motionState.gyro[2],
		};
		float orientation[3] = {
			motionState.orientation[0],
			motionState.orientation[1],
			motionState.orientation[2],
		};
		float quaternion[4] = {
			motionState.quaternion[0],
			motionState.quaternion[1],
			motionState.quaternion[2],
			motionState.quaternion[3],
		};
		return MotionSample(acceleration, MotionSample::calculateAccAcceleration(acceleration, acceleration), gyro, orientation, quaternion);
	}

	void EnsureOpenXRWiimoteMappings()
	{
		for (size_t index = 0; index < InputManager::kMaxController; ++index) {
			auto emulatedController = InputManager::instance().get_controller(index);
			if (!emulatedController || emulatedController->type() != EmulatedController::Type::Wiimote) {
				continue;
			}

			ControllerPtr questController;
			ControllerPtr secondaryMotionController;
			for (const auto& controller : emulatedController->get_controllers()) {
				if (controller->api() == InputAPI::Android && controller->uuid() == kOpenXRControllerDescriptor) {
					questController = controller;
				}
				if (controller->api() == InputAPI::Android && controller->uuid() == kOpenXRSecondaryMotionDescriptor) {
					secondaryMotionController = controller;
				}
			}

			if (!questController) {
				questController = ControllerFactory::CreateController(InputAPI::Android, kOpenXRControllerDescriptor, kOpenXRControllerName);
				if (questController)
					emulatedController->add_controller(questController);

				auto* wiimoteController = static_cast<WiimoteController*>(emulatedController.get());
				if (wiimoteController->get_device_type() == kWAPDevCore) {
					wiimoteController->set_device_type(kWAPDevFreestyle);
				}
			}

			if (!secondaryMotionController) {
				secondaryMotionController = ControllerFactory::CreateController(InputAPI::Android, kOpenXRSecondaryMotionDescriptor, kOpenXRSecondaryMotionName);
				if (secondaryMotionController)
					emulatedController->add_controller(secondaryMotionController);
			}

			if (questController)
				emulatedController->set_default_mapping(questController);
			try {
				InputManager::instance().save(index);
			} catch (const std::exception& e) {
				cemuLog_log(LogType::Force, "OpenXR: Failed to save Wiimote profile {}: {}", index, e.what());
				__android_log_print(ANDROID_LOG_ERROR, "Cemu", "OpenXR: Failed to save Wiimote profile %zu: %s", index, e.what());
			}
		}
	}

	void EnsureOpenXRVPADMappings()
	{
		auto attachQuestController = [](const EmulatedControllerPtr& emulatedController) -> bool
		{
			if (!emulatedController || emulatedController->type() != EmulatedController::Type::VPAD) {
				return false;
			}

			__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "OpenXR: Inspecting VPAD profile %zu controllers=%zu",
				emulatedController->player_index(), emulatedController->get_controllers().size());
			for (const auto& controller : emulatedController->get_controllers()) {
				__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "OpenXR: VPAD profile %zu controller api=%d uuid=%s name=%s",
					emulatedController->player_index(),
					static_cast<int>(controller->api()),
					controller->uuid().c_str(),
					controller->display_name().c_str());
			}

			ControllerPtr questController;
			for (const auto& controller : emulatedController->get_controllers()) {
				if (controller->api() == InputAPI::Android && controller->uuid() == kOpenXRControllerDescriptor) {
					questController = controller;
					break;
				}
			}

			if (!questController && emulatedController->get_controllers().empty()) {
				questController = ControllerFactory::CreateController(InputAPI::Android, kOpenXRControllerDescriptor, kOpenXRControllerName);
				if (questController) {
					emulatedController->add_controller(questController);
					__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "OpenXR: Attached Quest controller to existing VPAD profile %zu", emulatedController->player_index());
				} else {
					__android_log_print(ANDROID_LOG_ERROR, "Cemu", "OpenXR: Failed to create Quest controller for VPAD profile %zu",
						emulatedController->player_index());
				}
			}

			if (!questController) {
				__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "OpenXR: Leaving VPAD profile %zu unchanged (controllers=%zu, questAttached=%d)",
					emulatedController->player_index(), emulatedController->get_controllers().size(), 0);
				return !emulatedController->get_controllers().empty();
			}

			const bool mappingUpdated = emulatedController->set_default_mapping(questController);
			const bool saveResult = InputManager::instance().save(emulatedController->player_index());
			__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "OpenXR: VPAD profile %zu mappingUpdated=%d saveResult=%d controllers=%zu",
				emulatedController->player_index(), mappingUpdated ? 1 : 0, saveResult ? 1 : 0, emulatedController->get_controllers().size());
			try {
				if (!saveResult) {
					cemuLog_log(LogType::Force, "OpenXR: Failed to save VPAD profile {}", emulatedController->player_index());
				}
			} catch (const std::exception& e) {
				cemuLog_log(LogType::Force, "OpenXR: Failed to save VPAD profile {}: {}", emulatedController->player_index(), e.what());
				__android_log_print(ANDROID_LOG_ERROR, "Cemu", "OpenXR: Failed to save VPAD profile %zu: %s",
					emulatedController->player_index(), e.what());
			}
			return true;
		};

		bool hasConfiguredVPAD = false;
		for (size_t index = 0; index < InputManager::kMaxVPADControllers; ++index) {
			auto vpadController = InputManager::instance().get_vpad_controller(index);
			if (!vpadController) {
				continue;
			}

			hasConfiguredVPAD = true;
			if (attachQuestController(vpadController)) {
				return;
			}
		}

		if (hasConfiguredVPAD) {
			__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "OpenXR: Existing VPAD profiles found, auto-create skipped");
			return;
		}

		size_t freeProfileIndex = InputManager::kMaxController;
		for (size_t index = 0; index < InputManager::kMaxController; ++index) {
			if (!InputManager::instance().get_controller(index)) {
				freeProfileIndex = index;
				break;
			}
		}

		if (freeProfileIndex == InputManager::kMaxController) {
			return;
		}

		auto questController = ControllerFactory::CreateController(InputAPI::Android, kOpenXRControllerDescriptor, kOpenXRControllerName);
		if (!questController) {
			__android_log_print(ANDROID_LOG_ERROR, "Cemu", "OpenXR: Failed to create Quest controller for new VPAD profile");
			return;
		}

		auto emulatedController = InputManager::instance().set_controller(freeProfileIndex, EmulatedController::Type::VPAD, questController);
		if (!emulatedController) {
			__android_log_print(ANDROID_LOG_ERROR, "Cemu", "OpenXR: Failed to create new VPAD profile %zu", freeProfileIndex);
			return;
		}

		const bool mappingUpdated = emulatedController->set_default_mapping(questController);
		const bool saveResult = InputManager::instance().save(freeProfileIndex);
		__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "OpenXR: New VPAD profile %zu mappingUpdated=%d saveResult=%d controllers=%zu",
			freeProfileIndex, mappingUpdated ? 1 : 0, saveResult ? 1 : 0, emulatedController->get_controllers().size());
		try {
			if (!saveResult) {
				cemuLog_log(LogType::Force, "OpenXR: Failed to save auto-created VPAD profile {}", freeProfileIndex);
			}
		} catch (const std::exception& e) {
			cemuLog_log(LogType::Force, "OpenXR: Failed to save auto-created VPAD profile {}: {}", freeProfileIndex, e.what());
			__android_log_print(ANDROID_LOG_ERROR, "Cemu", "OpenXR: Failed to save auto-created VPAD profile %zu: %s",
				freeProfileIndex, e.what());
		}
		cemuLog_log(LogType::Force, "OpenXR: Auto-configured Quest controller as VPAD on profile {}", freeProfileIndex);
		__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "OpenXR: Auto-configured Quest controller as VPAD on profile %zu", freeProfileIndex);
	}

	void EnsureOpenXRControllerMappingsIfReady()
	{
		if (!g_openxrManager) {
			return;
		}

		if (!s_emulationInitialized.load()) {
			cemuLog_log(LogType::Force, "OpenXR: Deferring controller auto-mapping until Cemu init completes");
			__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "OpenXR: Deferring controller auto-mapping until Cemu init completes");
			return;
		}

		bool expected = false;
		if (!s_openxrMappingsApplied.compare_exchange_strong(expected, true)) {
			return;
		}

		cemuLog_log(LogType::Force, "OpenXR: Applying controller auto-mapping after Cemu init");
		__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "OpenXR: Applying controller auto-mapping after Cemu init");

		try {
			EnsureOpenXRWiimoteMappings();
		} catch (const std::exception& e) {
			cemuLog_log(LogType::Force, "OpenXR: Wiimote auto-mapping failed (non-fatal): {}", e.what());
			__android_log_print(ANDROID_LOG_ERROR, "Cemu", "OpenXR: Wiimote auto-mapping failed (non-fatal): %s", e.what());
		}

		try {
			EnsureOpenXRVPADMappings();
		} catch (const std::exception& e) {
			cemuLog_log(LogType::Force, "OpenXR: VPAD auto-mapping failed (non-fatal): {}", e.what());
			__android_log_print(ANDROID_LOG_ERROR, "Cemu", "OpenXR: VPAD auto-mapping failed (non-fatal): %s", e.what());
		}
	}

	void ForwardOpenXRInputState(const OpenXRManager::ControllerInputState& inputState)
	{
		auto* androidControllerProvider = GetAndroidControllerProvider();
		if (!androidControllerProvider) {
			return;
		}

		static const int buttonKeyCodes[] = {
			AKEYCODE_BUTTON_A,      // Right A
			AKEYCODE_BUTTON_B,      // Right B
			AKEYCODE_BUTTON_X,      // Left X
			AKEYCODE_BUTTON_Y,      // Left Y
			AKEYCODE_BUTTON_START,  // Menu
			AKEYCODE_BUTTON_THUMBL, // Left thumbstick click
			AKEYCODE_BUTTON_THUMBR, // Right thumbstick click
		};

		for (size_t i = 0; i < (sizeof(buttonKeyCodes) / sizeof(buttonKeyCodes[0])); ++i) {
			if (inputState.buttons[i] != g_openxrForwardState.buttons[i]) {
				__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "OpenXR button[%zu] key=%d pressed=%d",
					i, buttonKeyCodes[i], inputState.buttons[i] ? 1 : 0);
				androidControllerProvider->on_key_event(kOpenXRControllerDescriptor, kOpenXRControllerName, buttonKeyCodes[i], inputState.buttons[i]);
				g_openxrForwardState.buttons[i] = inputState.buttons[i];
			}
		}

		const bool currentGripL = inputState.gripL > 0.5f;
		const bool currentGripR = inputState.gripR > 0.5f;
		if (currentGripL != g_openxrForwardState.gripL) {
			__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "OpenXR gripL pressed=%d", currentGripL ? 1 : 0);
			androidControllerProvider->on_key_event(kOpenXRControllerDescriptor, kOpenXRControllerName, AKEYCODE_BUTTON_L1, currentGripL);
			g_openxrForwardState.gripL = currentGripL;
		}
		if (currentGripR != g_openxrForwardState.gripR) {
			__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "OpenXR gripR pressed=%d", currentGripR ? 1 : 0);
			androidControllerProvider->on_key_event(kOpenXRControllerDescriptor, kOpenXRControllerName, AKEYCODE_BUTTON_R1, currentGripR);
			g_openxrForwardState.gripR = currentGripR;
		}

		static int s_lastPointerVisibility = -1;
		if (inputState.pointerVisibility != s_lastPointerVisibility) {
			__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "OpenXR pointer visibility=%d x=%.3f y=%.3f",
				inputState.pointerVisibility, inputState.pointerX, inputState.pointerY);
			s_lastPointerVisibility = inputState.pointerVisibility;
		}

		androidControllerProvider->on_axis_event(kOpenXRControllerDescriptor, kOpenXRControllerName, AMOTION_EVENT_AXIS_LTRIGGER, inputState.triggerL);
		androidControllerProvider->on_axis_event(kOpenXRControllerDescriptor, kOpenXRControllerName, AMOTION_EVENT_AXIS_RTRIGGER, inputState.triggerR);
		androidControllerProvider->on_axis_event(kOpenXRControllerDescriptor, kOpenXRControllerName, AMOTION_EVENT_AXIS_X, inputState.thumbstickLX);
		androidControllerProvider->on_axis_event(kOpenXRControllerDescriptor, kOpenXRControllerName, AMOTION_EVENT_AXIS_Y, -inputState.thumbstickLY);
		androidControllerProvider->on_axis_event(kOpenXRControllerDescriptor, kOpenXRControllerName, AMOTION_EVENT_AXIS_RX, inputState.thumbstickRX);
		androidControllerProvider->on_axis_event(kOpenXRControllerDescriptor, kOpenXRControllerName, AMOTION_EVENT_AXIS_RY, -inputState.thumbstickRY);
		androidControllerProvider->on_position_event(
			kOpenXRControllerDescriptor,
			kOpenXRControllerName,
			inputState.pointerX,
			inputState.pointerY,
			ToPositionVisibility(inputState.pointerVisibility));
		androidControllerProvider->on_motion_event(
			kOpenXRControllerDescriptor,
			kOpenXRControllerName,
			ToMotionSample(inputState.motionR),
			inputState.motionR.valid);
		androidControllerProvider->on_motion_event(
			kOpenXRSecondaryMotionDescriptor,
			kOpenXRSecondaryMotionName,
			ToMotionSample(inputState.motionL),
			inputState.motionL.valid);
	}
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_initializeOpenXR(JNIEnv* env, [[maybe_unused]] jclass clazz, jobject activity, jboolean defer_session_start)
{
	// Store global ref to Activity for OpenXR
	if (s_openxrActivity) env->DeleteGlobalRef(s_openxrActivity);
	s_openxrActivity = env->NewGlobalRef(activity);
	s_openxrMappingsApplied.store(false);
	ResetOpenXRForwardState();
	__android_log_print(ANDROID_LOG_DEBUG, "Cemu", ">>> initializeOpenXR called (deferSessionStart=%d)", defer_session_start ? 1 : 0);
	cemuLog_log(LogType::Force, "OpenXR: Initializing with dynamic loading (deferSessionStart={})", defer_session_start ? 1 : 0);

	try {
		// Initialize global Vulkan (loads function pointers only)
		if (!InitializeGlobalVulkan()) {
			cemuLog_log(LogType::Force, "OpenXR: Failed to initialize global Vulkan");
			return false;
		}

		g_openxrManager = std::make_unique<OpenXRManager>();

		if (defer_session_start) {
			if (!g_openxrManager->InitializeVulkanOnly(s_openxrActivity)) {
				cemuLog_log(LogType::Force, "OpenXR: Failed to create deferred VR Vulkan objects");
				g_openxrManager.reset();
				return false;
			}

			cemuLog_log(LogType::Force, "OpenXR: Vulkan objects created (session deferred until renderer ready)");
			cemuLog_log(LogType::Force, "OpenXR: Instance={:p} PhysDevice={:p} Device={:p} QueueFamily={}",
				(void*)g_openxrManager->GetVkInstance(), (void*)g_openxrManager->GetVkPhysicalDevice(),
				(void*)g_openxrManager->GetVkDevice(), g_openxrManager->GetQueueFamilyIndex());
		} else {
			if (!g_openxrManager->Initialize(s_openxrActivity)) {
				cemuLog_log(LogType::Force, "OpenXR: Failed to initialize input session");
				g_openxrManager.reset();
				return false;
			}

			cemuLog_log(LogType::Force, "OpenXR: Input session created and ready for polling");
		}

		EnsureOpenXRControllerMappingsIfReady();

		return true;

	} catch (const std::exception& e) {
		cemuLog_log(LogType::Force, fmt::format("OpenXR: Exception during initialization: {}", e.what()));
		__android_log_print(ANDROID_LOG_ERROR, "Cemu", "OpenXR: Exception during initialization: %s", e.what());
		s_openxrMappingsApplied.store(false);
		g_openxrManager.reset();
		return false;
	}
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_shutdownOpenXR([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	__android_log_print(ANDROID_LOG_DEBUG, "Cemu", ">>> shutdownOpenXR called");
	ForwardOpenXRInputState({});
	ResetOpenXRForwardState();
	s_openxrMappingsApplied.store(false);
	StopOpenXRKeepaliveThread();
	g_vrGameRendering.store(false);
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

	EnsureOpenXRControllerMappingsIfReady();

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

	auto& windowInfo = WindowSystem::GetWindowInfo();
	windowInfo.width = windowInfo.phys_width = 1920;
	windowInfo.height = windowInfo.phys_height = 1080;

	// NO OpenXR session yet — game will load without VR frame deadline
	// Session starts on first SwapBuffer call (deferred start)

	// Create a placeholder OpenXR-backed surface so the Latte thread can run the
	// normal renderer initialization path against a valid render pass later.
	{
		std::vector<VkImage> dummyImages;
		VulkanRenderer::GetInstance()->InitializeSurfaceFromOpenXR(dummyImages, 1920, 1080, VK_FORMAT_R8G8B8A8_SRGB);
	}

	// Set up OpenXR callbacks with DEFERRED session start
	// Session begins on first SwapBuffer call — no timeout during loading
	{
		auto& ci = VulkanRenderer::GetInstance()->GetChainInfo(true);
		ci.SetOpenXRCallbacks(
			g_openxrManager.get(),
			[](void* m) { static_cast<OpenXRManager*>(m)->PollEvents(); },
			[](void* m) -> bool { return static_cast<OpenXRManager*>(m)->IsSessionRunning(); },
			[](void* m) -> bool {
				auto* mgr = static_cast<OpenXRManager*>(m);
				return BeginOpenXRRenderFrame(mgr);
			},
			[](void* m) -> uint32_t { return AcquireOpenXRRenderFrameImage(static_cast<OpenXRManager*>(m)); },
			[](void* m) { static_cast<OpenXRManager*>(m)->ReleaseSwapchainImage(); },
			[](void* m) { EndOpenXRRenderFrame(static_cast<OpenXRManager*>(m)); },
			// Deferred start: called on first SwapBuffer
			[](void* m) -> bool {
				auto* mgr = static_cast<OpenXRManager*>(m);
				__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "VR: Starting session (deferred)...");
				if (!mgr->StartSession()) return false;
				// Wire real swapchain images
				auto images = mgr->GetSwapchainImages();
				uint32_t w, h;
				mgr->GetSwapchainSize(w, h);
				VulkanRenderer::GetInstance()->InitializeSurfaceFromOpenXR(images, w, h, VK_FORMAT_R8G8B8A8_SRGB);
				// Re-set callbacks on new swapchain (without start callback this time)
				auto& chain = VulkanRenderer::GetInstance()->GetChainInfo(true);
				chain.SetOpenXRCallbacks(
					mgr,
					[](void* m2) { static_cast<OpenXRManager*>(m2)->PollEvents(); },
					[](void* m2) -> bool { return static_cast<OpenXRManager*>(m2)->IsSessionRunning(); },
					[](void* m2) -> bool {
						return BeginOpenXRRenderFrame(static_cast<OpenXRManager*>(m2));
					},
					[](void* m2) -> uint32_t { return AcquireOpenXRRenderFrameImage(static_cast<OpenXRManager*>(m2)); },
					[](void* m2) { static_cast<OpenXRManager*>(m2)->ReleaseSwapchainImage(); },
					[](void* m2) { EndOpenXRRenderFrame(static_cast<OpenXRManager*>(m2)); }
				);
				// Poll events to begin session
				for (int i = 0; i < 20; i++) {
					mgr->PollEvents();
					if (mgr->IsSessionRunning()) break;
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
				}
				if (mgr->IsSessionRunning())
					StartOpenXRKeepaliveThread();
				__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "VR: Session started! running=%d", mgr->IsSessionRunning() ? 1 : 0);
				return mgr->IsSessionRunning();
			}
		);
	}
	__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "VR: Ready! Session deferred until first SwapBuffer");

	__android_log_print(ANDROID_LOG_DEBUG, "Cemu", "VR: Fully ready! session=%d", g_openxrManager->IsSessionRunning() ? 1 : 0);
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
	if (!g_openxrManager->PollInput()) {
		return;
	}

	ForwardOpenXRInputState(g_openxrManager->GetInputState());
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

	EnsureOpenXRControllerMappingsIfReady();

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
