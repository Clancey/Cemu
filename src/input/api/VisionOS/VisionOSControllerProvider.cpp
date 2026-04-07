#include "input/api/VisionOS/VisionOSControllerProvider.h"

#include "input/InputManager.h"
#include "input/api/VisionOS/VisionOSController.h"
#include "input/api/Controller.h"

std::mutex VisionOSControllerProvider::s_controllerMutex;
ControllerState VisionOSControllerProvider::s_controllerState{};

ControllerState& VisionOSControllerProvider::get_controller_state()
{
	return s_controllerState;
}

void VisionOSControllerProvider::on_key_event(int keyCode, bool isPressed)
{
	// ControllerButtonState has its own spinlock — no extra mutex needed
	s_controllerState.buttons.SetButtonState(keyCode, isPressed);
}

void VisionOSControllerProvider::on_axis_event(int axisCode, float value)
{
	switch (axisCode)
	{
	case kAxisLStickX:
		s_controllerState.axis.x = value;
		break;
	case kAxisLStickY:
		s_controllerState.axis.y = value;
		break;
	case kAxisRStickX:
		s_controllerState.rotation.x = value;
		break;
	case kAxisRStickY:
		s_controllerState.rotation.y = value;
		break;
	case kAxisLTrigger:
		s_controllerState.trigger.x = value;
		break;
	case kAxisRTrigger:
		s_controllerState.trigger.y = value;
		break;
	}
}