#if __ANDROID__

// This file provides the WindowSystem namespace implementations for Android
// It acts as a bridge between the generic WindowSystem interface and AndroidWindowSystem

#include "gui/interface/WindowSystem.h"
#include "AndroidWindowSystem.h"

// Implement WindowSystem namespace functions by forwarding to AndroidWindowSystem
namespace WindowSystem
{
    void Create()
    {
        AndroidWindowSystem::Create();
    }

    void ShowErrorDialog(std::string_view message, std::string_view title, std::optional<WindowSystem::ErrorCategory> errorCategory)
    {
        AndroidWindowSystem::ShowErrorDialog(message, title, errorCategory);
    }

    WindowSystem::WindowInfo& GetWindowInfo()
    {
        return AndroidWindowSystem::GetWindowInfo();
    }

    void UpdateWindowTitles(bool isIdle, bool isLoading, double fps)
    {
        AndroidWindowSystem::UpdateWindowTitles(isIdle, isLoading, fps);
    }

    void GetWindowSize(int& w, int& h)
    {
        AndroidWindowSystem::GetWindowSize(w, h);
    }

    void GetPadWindowSize(int& w, int& h)
    {
        AndroidWindowSystem::GetPadWindowSize(w, h);
    }

    void GetWindowPhysSize(int& w, int& h)
    {
        AndroidWindowSystem::GetWindowPhysSize(w, h);
    }

    void GetPadWindowPhysSize(int& w, int& h)
    {
        AndroidWindowSystem::GetPadWindowPhysSize(w, h);
    }

    double GetWindowDPIScale()
    {
        return AndroidWindowSystem::GetWindowDPIScale();
    }

    double GetPadDPIScale()
    {
        return AndroidWindowSystem::GetPadDPIScale();
    }

    bool IsPadWindowOpen()
    {
        return AndroidWindowSystem::IsPadWindowOpen();
    }

    bool IsKeyDown(uint32 key)
    {
        return AndroidWindowSystem::IsKeyDown(key);
    }

    bool IsKeyDown(PlatformKeyCodes key)
    {
        return AndroidWindowSystem::IsKeyDown(key);
    }

    std::string GetKeyCodeName(uint32 key)
    {
        return AndroidWindowSystem::GetKeyCodeName(key);
    }

    bool InputConfigWindowHasFocus()
    {
        return AndroidWindowSystem::InputConfigWindowHasFocus();
    }

    void NotifyGameLoaded()
    {
        AndroidWindowSystem::NotifyGameLoaded();
    }

    void NotifyGameExited()
    {
        AndroidWindowSystem::NotifyGameExited();
    }

    void RefreshGameList()
    {
        AndroidWindowSystem::RefreshGameList();
    }

    bool IsFullScreen()
    {
        return AndroidWindowSystem::IsFullScreen();
    }

    void CaptureInput(const ControllerState& currentState, const ControllerState& lastState)
    {
        AndroidWindowSystem::CaptureInput(currentState, lastState);
    }

} // namespace WindowSystem

#endif // __ANDROID__