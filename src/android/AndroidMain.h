#pragma once

#include <android/native_activity.h>
#include <android/asset_manager.h>
#include <android/input.h>
#include <android/window.h>
#include <android_native_app_glue.h>

#include <string>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace AndroidBridge
{
	// Emulation states
	enum class EmulationState
	{
		Stopped,
		Running,
		Paused
	};

	// Global Android application state
	struct AndroidAppState
	{
		// Android app data
		android_app* app = nullptr;
		ANativeWindow* window = nullptr;
		AAssetManager* assetManager = nullptr;

		// Storage paths
		std::string internalStoragePath;
		std::string externalStoragePath;
		std::string cemuDataPath;
		std::string mlcPath;
		std::string shaderCachePath;

		// Window/rendering state
		int32_t windowWidth = 0;
		int32_t windowHeight = 0;
		bool windowReady = false;
		bool hasFocus = false;
		bool vulkanInitialized = false;

		// Emulation state
		std::atomic<EmulationState> emulationState{EmulationState::Stopped};
		std::thread emulationThread;
		std::mutex stateMutex;
		std::condition_variable stateCondition;

		// Input state
		bool touchSupported = true;
		std::mutex inputMutex;
	};

	// Global app state instance
	extern AndroidAppState g_androidState;

	// Core Android functions
	void InitializeAndroidPaths();
	void InitializeEmulation();
	void InitializeVulkanRenderer();
	void StartEmulationThread();
	void StopEmulationThread();
	void PauseEmulation();
	void ResumeEmulation();

	// Check if core initialization is complete
	bool IsCoreInitDone();

	// Window management
	void OnWindowInit(ANativeWindow* window);
	void OnWindowTerminate();
	void OnWindowResize(int32_t width, int32_t height);

	// Focus/lifecycle management
	void OnGainedFocus();
	void OnLostFocus();
	void OnPause();
	void OnResume();
	void OnDestroy();

	// Input handling
	int32_t HandleInputEvent(android_app* app, AInputEvent* event);

	// Utility functions
	std::string GetAndroidVersion();
	bool IsQuestDevice();

	// Path helpers (from AndroidStorage)
	std::string GetInternalStoragePath();
	std::string GetExternalStoragePath();
	std::string GetCemuConfigPath();
	std::string GetShaderCachePath();
	std::string GetMlcPath();
}