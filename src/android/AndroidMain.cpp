#include "AndroidMain.h"
#include "AndroidStorage.h"
#include "AndroidInput.h"

// Cemu includes
#include "config/CemuConfig.h"
#include "config/ActiveSettings.h"
#include "config/LaunchSettings.h"
#include "Cafe/CafeSystem.h"
#include "Cafe/TitleList/TitleList.h"
#include "Cafe/TitleList/SaveList.h"
#include "gui/android/AndroidWindowSystem.h"
#include "CemuAndroid.h"
#include "util/crypto/aes128.h"
#include "util/helpers/helpers.h"
#include "audio/IAudioAPI.h"
#include "audio/IAudioInputAPI.h"
#include "input/InputManager.h"
#include "Cafe/GraphicPack/GraphicPack2.h"
#include "Common/ExceptionHandler/ExceptionHandler.h"
#include "Common/cpu_features.h"

#include <android/log.h>
#include <android/asset_manager.h>
#include <android/configuration.h>
#include <sys/system_properties.h>
#include <future>
#include <chrono>

#define LOG_TAG "CemuAndroid"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace AndroidBridge
{
	// Global state
	AndroidAppState g_androidState;

	// Forward declarations
	void HandleAppCmd(android_app* app, int32_t cmd);
	int32_t HandleInputEvent(android_app* app, AInputEvent* event);
	void EmulationThreadFunc();
	void CemuAndroidInit();

	std::string GetAndroidVersion()
	{
		char versionStr[PROP_VALUE_MAX];
		int len = __system_property_get("ro.build.version.release", versionStr);
		if (len > 0)
		{
			return std::string(versionStr);
		}
		return "Unknown";
	}

	bool IsQuestDevice()
	{
		char manufacturer[PROP_VALUE_MAX];
		char model[PROP_VALUE_MAX];

		int mfrLen = __system_property_get("ro.product.manufacturer", manufacturer);
		int modelLen = __system_property_get("ro.product.model", model);

		if (mfrLen > 0 && modelLen > 0)
		{
			std::string mfrStr(manufacturer);
			std::string modelStr(model);

			// Check for Meta/Oculus devices
			return (mfrStr.find("Oculus") != std::string::npos ||
			        mfrStr.find("Meta") != std::string::npos ||
			        modelStr.find("Quest") != std::string::npos);
		}
		return false;
	}

	void InitializeAndroidPaths()
	{
		LOGD("Initializing Android paths");

		if (!g_androidState.app || !g_androidState.app->activity)
		{
			LOGE("Cannot initialize paths - no Android app or activity");
			return;
		}

		// Initialize storage system
		Storage::Initialize(g_androidState.app->activity);

		// Get paths for Cemu
		g_androidState.internalStoragePath = Storage::GetInternalStoragePath(g_androidState.app->activity);
		g_androidState.externalStoragePath = Storage::GetExternalStoragePath(g_androidState.app->activity);
		g_androidState.cemuDataPath = Storage::GetCemuDataPath();
		g_androidState.mlcPath = Storage::GetMlcPath();
		g_androidState.shaderCachePath = Storage::GetShaderCachePath();

		LOGD("Cemu data path: %s", g_androidState.cemuDataPath.c_str());
		LOGD("MLC path: %s", g_androidState.mlcPath.c_str());
		LOGD("Shader cache path: %s", g_androidState.shaderCachePath.c_str());
	}

	void InitializeEmulation()
	{
		LOGD("Initializing Cemu emulation system");

		try
		{
			CemuAndroidInit();
			LOGD("Cemu initialization completed successfully");
		}
		catch (const std::exception& e)
		{
			LOGE("Failed to initialize Cemu: %s", e.what());
			g_androidState.emulationState = EmulationState::Stopped;
		}
	}

	void StartEmulationThread()
	{
		if (g_androidState.emulationState != EmulationState::Stopped)
		{
			LOGD("Emulation thread already running");
			return;
		}

		LOGD("Starting emulation thread");
		g_androidState.emulationState = EmulationState::Running;
		g_androidState.emulationThread = std::thread(EmulationThreadFunc);
	}

	void StopEmulationThread()
	{
		LOGD("Stopping emulation thread");

		{
			std::lock_guard<std::mutex> lock(g_androidState.stateMutex);
			g_androidState.emulationState = EmulationState::Stopped;
		}
		g_androidState.stateCondition.notify_all();

		if (g_androidState.emulationThread.joinable())
		{
			g_androidState.emulationThread.join();
		}

		LOGD("Emulation thread stopped");
	}

	void PauseEmulation()
	{
		if (g_androidState.emulationState == EmulationState::Running)
		{
			LOGD("Pausing emulation");
			{
				std::lock_guard<std::mutex> lock(g_androidState.stateMutex);
				g_androidState.emulationState = EmulationState::Paused;
			}
			g_androidState.stateCondition.notify_all();
		}
	}

	void ResumeEmulation()
	{
		if (g_androidState.emulationState == EmulationState::Paused)
		{
			LOGD("Resuming emulation");
			{
				std::lock_guard<std::mutex> lock(g_androidState.stateMutex);
				g_androidState.emulationState = EmulationState::Running;
			}
			g_androidState.stateCondition.notify_all();
		}
	}

	void OnWindowInit(ANativeWindow* window)
	{
		LOGD("Window initialized");

		g_androidState.window = window;
		g_androidState.windowWidth = ANativeWindow_getWidth(window);
		g_androidState.windowHeight = ANativeWindow_getHeight(window);
		g_androidState.windowReady = true;

		LOGD("Window size: %dx%d", g_androidState.windowWidth, g_androidState.windowHeight);

		// Initialize the existing Android window system using the C API
		cemuAndroid_initWindowSystem(window);

		// Start emulation if we have focus
		if (g_androidState.hasFocus)
		{
			StartEmulationThread();
		}
	}

	void OnWindowTerminate()
	{
		LOGD("Window terminated");

		// Pause emulation when window is destroyed
		PauseEmulation();

		// Notify Android window system that window is gone
		cemuAndroid_onWindowChanged(nullptr);

		g_androidState.window = nullptr;
		g_androidState.windowReady = false;
		g_androidState.windowWidth = 0;
		g_androidState.windowHeight = 0;
	}

	void OnWindowResize(int32_t width, int32_t height)
	{
		LOGD("Window resized to %dx%d", width, height);

		g_androidState.windowWidth = width;
		g_androidState.windowHeight = height;

		// Notify Android window system of resize
		cemuAndroid_onWindowResized(width, height);
	}

	void OnGainedFocus()
	{
		LOGD("Gained focus");
		g_androidState.hasFocus = true;

		// Notify Android window system
		cemuAndroid_onResume();

		// Resume emulation if window is ready
		if (g_androidState.windowReady)
		{
			if (g_androidState.emulationState == EmulationState::Stopped)
			{
				StartEmulationThread();
			}
			else if (g_androidState.emulationState == EmulationState::Paused)
			{
				ResumeEmulation();
			}
		}
	}

	void OnLostFocus()
	{
		LOGD("Lost focus");
		g_androidState.hasFocus = false;

		// Notify Android window system
		cemuAndroid_onPause();

		// Pause emulation when losing focus
		PauseEmulation();
	}

	void OnPause()
	{
		LOGD("App paused");
		cemuAndroid_onPause();
		PauseEmulation();
	}

	void OnResume()
	{
		LOGD("App resumed");
		cemuAndroid_onResume();

		// Resume only if we have focus and window
		if (g_androidState.hasFocus && g_androidState.windowReady)
		{
			ResumeEmulation();
		}
	}

	void OnDestroy()
	{
		LOGD("App destroyed");

		// Stop emulation
		StopEmulationThread();

		// Notify Android window system
		cemuAndroid_onDestroy();

		// Cleanup systems
		Input::Shutdown();
		Storage::Shutdown();

		// Shutdown Cemu systems
		CafeSystem::Shutdown();
	}

	void HandleAppCmd(android_app* app, int32_t cmd)
	{
		switch (cmd)
		{
		case APP_CMD_INIT_WINDOW:
			if (app->window != nullptr)
			{
				OnWindowInit(app->window);
			}
			break;

		case APP_CMD_TERM_WINDOW:
			OnWindowTerminate();
			break;

		case APP_CMD_GAINED_FOCUS:
			OnGainedFocus();
			break;

		case APP_CMD_LOST_FOCUS:
			OnLostFocus();
			break;

		case APP_CMD_PAUSE:
			OnPause();
			break;

		case APP_CMD_RESUME:
			OnResume();
			break;

		case APP_CMD_DESTROY:
			OnDestroy();
			break;

		case APP_CMD_CONFIG_CHANGED:
			// Handle configuration changes (orientation, etc.)
			LOGD("Configuration changed");
			break;

		case APP_CMD_LOW_MEMORY:
			LOGD("Low memory warning");
			// Could trigger shader cache cleanup or other memory optimizations
			break;

		default:
			LOGD("Unhandled app command: %d", cmd);
			break;
		}
	}

	int32_t HandleInputEvent(android_app* app, AInputEvent* event)
	{
		// Let the existing Android window system handle input
		return cemuAndroid_handleInputEvent(event);
	}

	void EmulationThreadFunc()
	{
		LOGD("Emulation thread started");

		// Wait for proper initialization
		std::unique_lock<std::mutex> lock(g_androidState.stateMutex);
		g_androidState.stateCondition.wait(lock, []() {
			return g_androidState.emulationState != EmulationState::Stopped &&
				   g_androidState.windowReady;
		});

		if (g_androidState.emulationState == EmulationState::Stopped)
		{
			LOGD("Emulation thread exiting early - stopped before start");
			return;
		}

		lock.unlock();

		// Launch emulator LLE - this will block until emulation ends
		LOGD("Starting Cemu LLE emulation");
		cemuAndroid_launchEmulatorLLE();

		LOGD("Emulation thread exiting - LLE emulation ended");

		// Update state to stopped
		{
			std::lock_guard<std::mutex> guard(g_androidState.stateMutex);
			g_androidState.emulationState = EmulationState::Stopped;
		}
	}

	void CemuAndroidInit()
	{
		LOGD("Starting Cemu initialization for Android");

		// Set up Android-specific paths first, before core init
		std::set<fs::path> failedWriteAccess;
		fs::path execPath = "/system/bin/app_process"; // Dummy executable path for Android
		fs::path userData = _utf8ToPath(g_androidState.cemuDataPath);
		fs::path configPath = _utf8ToPath(Storage::GetConfigPath());
		fs::path cachePath = _utf8ToPath(g_androidState.shaderCachePath);
		fs::path dataPath = userData; // Use same as user data

		ActiveSettings::SetPaths(false, execPath, userData, configPath, cachePath, dataPath, failedWriteAccess);
		ActiveSettings::Init();

		// Use the existing Android core initialization
		cemuAndroid_coreInit();

		LOGD("Cemu Android initialization completed");
	}

} // namespace AndroidBridge

// Global Android native app entry point (must be in global namespace for NDK)
void android_main(android_app* app)
{
	using namespace AndroidBridge;

	LOGI("Cemu for Android starting");
	LOGI("Android version: %s", GetAndroidVersion().c_str());
	LOGI("Quest device: %s", IsQuestDevice() ? "Yes" : "No");

	// Initialize global state
	g_androidState.app = app;
	g_androidState.assetManager = app->activity->assetManager;

	// Set app callbacks
	app->onAppCmd = HandleAppCmd;
	app->onInputEvent = HandleInputEvent;

	// Initialize Android-specific systems
	InitializeAndroidPaths();
	Input::Initialize();

	// Initialize emulation system
	InitializeEmulation();

	LOGI("Cemu Android initialization complete, entering main loop");

	// Main message loop
	int ident;
	int events;
	android_poll_source* source;

	while (true)
	{
		// Poll for Android events
		while ((ident = ALooper_pollAll(
			g_androidState.emulationState == EmulationState::Running ? 0 : -1,
			nullptr, &events, (void**)&source)) >= 0)
		{
			// Process this event
			if (source != nullptr)
			{
				source->process(app, source);
			}

			// Update input system to forward events to Cemu
			if (g_androidState.emulationState == EmulationState::Running)
			{
				AndroidBridge::Input::Update();
			}

			// Check if we are exiting
			if (app->destroyRequested != 0)
			{
				LOGI("App destroy requested, exiting");
				return;
			}
		}

		// If we're running emulation, do a small sleep to prevent busy loop
		if (g_androidState.emulationState == EmulationState::Running)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}
}