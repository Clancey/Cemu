#if __ANDROID__

#include "AndroidWindowSystem.h"
#include "AndroidCanvas.h"
#include "gui/interface/WindowSystem.h"
#include "Cafe/HW/Latte/Core/Latte.h"
#include "config/ActiveSettings.h"
#include "config/NetworkSettings.h"
#include "config/CemuConfig.h"
#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "Cafe/CafeSystem.h"
#include "input/HotkeySettings.h"

#include <android/native_window.h>
#include <android/input.h>
#include <android/log.h>
#include <android/keycodes.h>
#include <fmt/format.h>

#define LOG_TAG "Cemu"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace AndroidWindowSystem
{
	WindowSystem::WindowInfo g_window_info{};

	AndroidWindowSystem& AndroidWindowSystem::GetInstance()
	{
		static AndroidWindowSystem instance;
		return instance;
	}

	void AndroidWindowSystem::Initialize(ANativeWindow* window)
	{
		LOGD("AndroidWindowSystem::Initialize called");
		if (m_initialized)
		{
			LOGD("Already initialized, updating window");
			OnWindowChanged(window);
			return;
		}

		m_nativeWindow = window;
		if (m_nativeWindow)
		{
			// Get initial window dimensions
			int32_t width = ANativeWindow_getWidth(m_nativeWindow);
			int32_t height = ANativeWindow_getHeight(m_nativeWindow);
			LOGD("Initial window size: %dx%d", width, height);

			UpdateWindowInfo();
		}

		m_appState = AppState::Resumed;
		m_initialized = true;

		LOGD("AndroidWindowSystem initialized successfully");
	}

	void AndroidWindowSystem::Shutdown()
	{
		LOGD("AndroidWindowSystem::Shutdown called");

		m_mainCanvas.reset();
		m_padCanvas.reset();

		if (m_nativeWindow)
		{
			m_nativeWindow = nullptr;
		}

		m_appState = AppState::Destroyed;
		m_initialized = false;
	}

	void AndroidWindowSystem::OnResume()
	{
		LOGD("AndroidWindowSystem::OnResume called");
		m_appState = AppState::Resumed;
		g_window_info.app_active = true;
		ResumeEmulation();
	}

	void AndroidWindowSystem::OnPause()
	{
		LOGD("AndroidWindowSystem::OnPause called");
		m_appState = AppState::Paused;
		g_window_info.app_active = false;
		PauseEmulation();
	}

	void AndroidWindowSystem::OnDestroy()
	{
		LOGD("AndroidWindowSystem::OnDestroy called");
		Shutdown();
	}

	void AndroidWindowSystem::OnWindowChanged(ANativeWindow* window)
	{
		LOGD("AndroidWindowSystem::OnWindowChanged called");
		if (m_nativeWindow != window)
		{
			m_nativeWindow = window;
			UpdateWindowInfo();

			// Notify canvases about window change
			if (m_mainCanvas)
			{
				m_mainCanvas->OnWindowChanged(window);
			}
			if (m_padCanvas)
			{
				m_padCanvas->OnWindowChanged(window);
			}
		}
	}

	void AndroidWindowSystem::OnWindowResized(int32_t width, int32_t height)
	{
		LOGD("AndroidWindowSystem::OnWindowResized: %dx%d", width, height);
		UpdateWindowInfo();

		// Notify canvases about resize
		if (m_mainCanvas)
		{
			m_mainCanvas->OnResize(width, height);
		}
		if (m_padCanvas)
		{
			m_padCanvas->OnResize(width, height);
		}
	}

	int32_t AndroidWindowSystem::HandleInputEvent(AInputEvent* event)
	{
		int32_t event_type = AInputEvent_getType(event);

		switch (event_type)
		{
		case AINPUT_EVENT_TYPE_KEY:
		{
			int32_t keycode = AKeyEvent_getKeyCode(event);
			int32_t action = AKeyEvent_getAction(event);
			bool pressed = (action == AKEY_EVENT_ACTION_DOWN);

			g_window_info.set_keystate(keycode, pressed);
			return 1; // Event consumed
		}
		case AINPUT_EVENT_TYPE_MOTION:
		{
			// Handle touch input for GamePad if needed
			// For now, just return consumed
			return 1;
		}
		default:
			return 0; // Event not handled
		}
	}

	std::shared_ptr<AndroidCanvas> AndroidWindowSystem::CreateMainCanvas()
	{
		if (!m_mainCanvas)
		{
			m_mainCanvas = std::make_shared<AndroidCanvas>(true);
		}
		return m_mainCanvas;
	}

	std::shared_ptr<AndroidCanvas> AndroidWindowSystem::CreatePadCanvas()
	{
		if (!m_padCanvas)
		{
			m_padCanvas = std::make_shared<AndroidCanvas>(false);
		}
		return m_padCanvas;
	}

	void AndroidWindowSystem::UpdateWindowInfo()
	{
		if (!m_nativeWindow)
			return;

		int32_t width = ANativeWindow_getWidth(m_nativeWindow);
		int32_t height = ANativeWindow_getHeight(m_nativeWindow);

		g_window_info.width = width;
		g_window_info.height = height;
		g_window_info.phys_width = width;
		g_window_info.phys_height = height;
		g_window_info.dpi_scale = 1.0; // Android handles DPI internally

		// Set window handle info for main window
		g_window_info.window_main.backend = WindowSystem::WindowHandleInfo::Backend::Android;
		g_window_info.window_main.surface = m_nativeWindow;
		g_window_info.window_main.display = nullptr; // Not used on Android
		g_window_info.window_main.nativeWindow = m_nativeWindow;

		// Set canvas handle info
		g_window_info.canvas_main.backend = WindowSystem::WindowHandleInfo::Backend::Android;
		g_window_info.canvas_main.surface = m_nativeWindow;
		g_window_info.canvas_main.display = nullptr; // Not used on Android
		g_window_info.canvas_main.nativeWindow = m_nativeWindow;

		// For now, pad window is the same as main window on Android
		g_window_info.pad_open = false; // Typically not used on mobile
		g_window_info.pad_width = 0;
		g_window_info.pad_height = 0;
		g_window_info.phys_pad_width = 0;
		g_window_info.phys_pad_height = 0;
		g_window_info.pad_dpi_scale = 1.0;
	}

	void AndroidWindowSystem::PauseEmulation()
	{
		// Pause emulation when app goes to background
		if (CafeSystem::IsTitleRunning())
		{
			LOGD("Pausing emulation due to app lifecycle");
			// Note: CafeSystem might need pause/resume functionality
		}
	}

	void AndroidWindowSystem::ResumeEmulation()
	{
		// Resume emulation when app comes to foreground
		if (CafeSystem::IsTitleRunning())
		{
			LOGD("Resuming emulation due to app lifecycle");
			// Note: CafeSystem might need pause/resume functionality
		}
	}

	// Global interface functions for compatibility
	void Create()
	{
		LOGD("AndroidWindowSystem::Create() called");
		// On Android, initialization happens when the native window is provided
		// This is a no-op since we initialize in AndroidWindowSystem::Initialize
	}

	void ShowErrorDialog(std::string_view message, std::string_view title, std::optional<WindowSystem::ErrorCategory> errorCategory)
	{
		// Log error to Android logcat
		std::string titleStr = title.empty() ? "Cemu Error" : std::string(title);
		LOGE("%s: %s", titleStr.c_str(), std::string(message).c_str());

		// On Android, we might want to show a toast or use JNI to show a proper dialog
		// For now, just log the error
	}

	WindowSystem::WindowInfo& GetWindowInfo()
	{
		return g_window_info;
	}

	void UpdateWindowTitles(bool isIdle, bool isLoading, double fps)
	{
		// On Android, window titles aren't typically shown
		// Could potentially update notification or activity title via JNI
		LOGD("UpdateWindowTitles: idle=%s loading=%s fps=%.2f",
			  isIdle ? "true" : "false", isLoading ? "true" : "false", fps);
	}

	void GetWindowSize(int& w, int& h)
	{
		w = g_window_info.width;
		h = g_window_info.height;
	}

	void GetPadWindowSize(int& w, int& h)
	{
		if (g_window_info.pad_open)
		{
			w = g_window_info.pad_width;
			h = g_window_info.pad_height;
		}
		else
		{
			w = 0;
			h = 0;
		}
	}

	void GetWindowPhysSize(int& w, int& h)
	{
		w = g_window_info.phys_width;
		h = g_window_info.phys_height;
	}

	void GetPadWindowPhysSize(int& w, int& h)
	{
		if (g_window_info.pad_open)
		{
			w = g_window_info.phys_pad_width;
			h = g_window_info.phys_pad_height;
		}
		else
		{
			w = 0;
			h = 0;
		}
	}

	double GetWindowDPIScale()
	{
		return g_window_info.dpi_scale;
	}

	double GetPadDPIScale()
	{
		return g_window_info.pad_open ? g_window_info.pad_dpi_scale.load() : 1.0;
	}

	bool IsPadWindowOpen()
	{
		return g_window_info.pad_open;
	}

	bool IsKeyDown(uint32 key)
	{
		return g_window_info.get_keystate(key);
	}

	bool IsKeyDown(WindowSystem::PlatformKeyCodes platformKey)
	{
		uint32 key = 0;

		switch (platformKey)
		{
		case WindowSystem::PlatformKeyCodes::LCONTROL:
		case WindowSystem::PlatformKeyCodes::RCONTROL:
			key = AKEYCODE_CTRL_LEFT; // Android doesn't distinguish L/R for some keys
			break;
		case WindowSystem::PlatformKeyCodes::TAB:
			key = AKEYCODE_TAB;
			break;
		case WindowSystem::PlatformKeyCodes::ESCAPE:
			key = AKEYCODE_ESCAPE;
			break;
		default:
			return false;
		}

		return IsKeyDown(key);
	}

	std::string GetKeyCodeName(uint32 button)
	{
		// Android keycode names
		switch (button)
		{
		case AKEYCODE_BACK:
			return "Back";
		case AKEYCODE_MENU:
			return "Menu";
		case AKEYCODE_HOME:
			return "Home";
		case AKEYCODE_VOLUME_UP:
			return "Volume Up";
		case AKEYCODE_VOLUME_DOWN:
			return "Volume Down";
		default:
			return fmt::format("key_{}", button);
		}
	}

	bool InputConfigWindowHasFocus()
	{
		// On Android, assume input config has focus when app is active
		return g_window_info.app_active;
	}

	void NotifyGameLoaded()
	{
		// Could potentially update Android notification or activity state via JNI
		LOGD("Game loaded notification");
	}

	void NotifyGameExited()
	{
		// Could potentially update Android notification or activity state via JNI
		LOGD("Game exited notification");
	}

	void RefreshGameList()
	{
		// On Android, game list refresh might need to notify UI via JNI
		LOGD("Game list refresh requested");
	}

	bool IsFullScreen()
	{
		// Android apps are typically always fullscreen
		return true;
	}

	void CaptureInput(const ControllerState& currentState, const ControllerState& lastState)
	{
		HotkeySettings::CaptureInput(currentState, lastState);
	}

} // namespace AndroidWindowSystem

#endif // __ANDROID__