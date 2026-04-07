#include "input/api/VisionOS/VisionOSController.h"
#include "Cemu/Logging/CemuLogging.h"

VisionOSController::VisionOSController()
	: Controller<VisionOSControllerProvider>("0", "visionOS Controller") {}

ControllerState VisionOSController::raw_state()
{
	auto& state = VisionOSControllerProvider::get_controller_state();
	static int logCount = 0;
	if (logCount++ % 5000 == 0) {
		cemuLog_log(LogType::Force, "VisionOSController::raw_state() poll #{}", logCount);
	}
	if (state.buttons.GetButtonState(VisionOSControllerProvider::kButtonA)) {
		cemuLog_log(LogType::Force, "VisionOSController::raw_state() - A BUTTON DETECTED!");
	}
	return state;
}

std::string VisionOSController::get_button_name(uint64 button) const
{
	switch (button)
	{
		case VisionOSControllerProvider::kButtonA: return "Button A";
		case VisionOSControllerProvider::kButtonB: return "Button B";
		case VisionOSControllerProvider::kButtonX: return "Button X";
		case VisionOSControllerProvider::kButtonY: return "Button Y";
		case VisionOSControllerProvider::kButtonL: return "Button L";
		case VisionOSControllerProvider::kButtonR: return "Button R";
		case VisionOSControllerProvider::kButtonZL: return "Button ZL";
		case VisionOSControllerProvider::kButtonZR: return "Button ZR";
		case VisionOSControllerProvider::kButtonDpadUp: return "D-Pad Up";
		case VisionOSControllerProvider::kButtonDpadDown: return "D-Pad Down";
		case VisionOSControllerProvider::kButtonDpadLeft: return "D-Pad Left";
		case VisionOSControllerProvider::kButtonDpadRight: return "D-Pad Right";
		case VisionOSControllerProvider::kButtonPlus: return "Button Plus";
		case VisionOSControllerProvider::kButtonMinus: return "Button Minus";
		case VisionOSControllerProvider::kButtonHome: return "Button Home";
		case VisionOSControllerProvider::kButtonLStick: return "Left Stick";
		case VisionOSControllerProvider::kButtonRStick: return "Right Stick";
	}
	return base_type::get_button_name(button);
}