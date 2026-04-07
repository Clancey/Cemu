#include <boost/container/small_vector.hpp>

#include "input/api/Keyboard/KeyboardController.h"
#include "WindowSystem.h"

KeyboardController::KeyboardController()
	: base_type("keyboard", "Keyboard")
{
	
}

std::string KeyboardController::get_button_name(uint64 button) const
{
	return WindowSystem::GetKeyCodeName(button);
}

ControllerState KeyboardController::raw_state()
{
	ControllerState result{};

	if (WindowSystem::GetWindowInfo().debugger_focused)
		return result;

	boost::container::small_vector<uint32, 16> pressedKeys;
	WindowSystem::GetWindowInfo().iter_keystates([&pressedKeys](const std::pair<const uint32, bool>& keyState) { if (keyState.second) pressedKeys.emplace_back(keyState.first); });

	// Log every 1000th poll to verify polling is happening
	static int pollCount = 0;
	if (pollCount++ % 5000 == 0) {
		cemuLog_log(LogType::Force, "KeyboardController::raw_state poll #{}, pressedKeys={}, windowInfo={}", pollCount, pressedKeys.size(), (void*)&WindowSystem::GetWindowInfo());
	}
	if (!pressedKeys.empty()) {
		cemuLog_log(LogType::Force, "KeyboardController::raw_state: {} keys pressed, first={}", pressedKeys.size(), pressedKeys[0]);
	}

	result.buttons.SetPressedButtons(pressedKeys);
	return result;
}
