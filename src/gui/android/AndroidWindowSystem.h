#pragma once

#if __ANDROID__

#include "gui/interface/WindowSystem.h"
#include <atomic>
#include <memory>
#include "input/api/ControllerState.h"

struct ANativeWindow;
struct AInputEvent;

namespace AndroidWindowSystem
{
	class AndroidCanvas;

	// Android app lifecycle states
	enum class AppState
	{
		Resumed,
		Paused,
		Stopped,
		Destroyed
	};

	class AndroidWindowSystem
	{
	public:
		static AndroidWindowSystem& GetInstance();

		// Android lifecycle management
		void Initialize(ANativeWindow* window);
		void Shutdown();
		void OnResume();
		void OnPause();
		void OnDestroy();
		void OnWindowChanged(ANativeWindow* window);
		void OnWindowResized(int32_t width, int32_t height);

		// Input handling
		int32_t HandleInputEvent(AInputEvent* event);

		// Canvas management
		std::shared_ptr<AndroidCanvas> CreateMainCanvas();
		std::shared_ptr<AndroidCanvas> CreatePadCanvas();

		// Window info accessors
		ANativeWindow* GetNativeWindow() const { return m_nativeWindow; }
		AppState GetAppState() const { return m_appState; }
		bool IsInitialized() const { return m_initialized; }

	private:
		AndroidWindowSystem() = default;
		~AndroidWindowSystem() = default;
		AndroidWindowSystem(const AndroidWindowSystem&) = delete;
		AndroidWindowSystem& operator=(const AndroidWindowSystem&) = delete;

		void UpdateWindowInfo();
		void PauseEmulation();
		void ResumeEmulation();

		ANativeWindow* m_nativeWindow = nullptr;
		std::atomic<AppState> m_appState{AppState::Stopped};
		std::atomic_bool m_initialized{false};

		std::shared_ptr<AndroidCanvas> m_mainCanvas;
		std::shared_ptr<AndroidCanvas> m_padCanvas;
	};

	// Global functions for compatibility with existing window system interface
	void Create();
	void ShowErrorDialog(std::string_view message, std::string_view title = "", std::optional<WindowSystem::ErrorCategory> errorCategory = {});
	WindowSystem::WindowInfo& GetWindowInfo();
	void UpdateWindowTitles(bool isIdle, bool isLoading, double fps);
	void GetWindowSize(int& w, int& h);
	void GetPadWindowSize(int& w, int& h);
	void GetWindowPhysSize(int& w, int& h);
	void GetPadWindowPhysSize(int& w, int& h);
	double GetWindowDPIScale();
	double GetPadDPIScale();
	bool IsPadWindowOpen();
	bool IsKeyDown(uint32 key);
	bool IsKeyDown(WindowSystem::PlatformKeyCodes key);
	std::string GetKeyCodeName(uint32 key);
	bool InputConfigWindowHasFocus();
	void NotifyGameLoaded();
	void NotifyGameExited();
	void RefreshGameList();
	bool IsFullScreen();
	void CaptureInput(const ControllerState& currentState, const ControllerState& lastState);

} // namespace AndroidWindowSystem

#endif // __ANDROID__