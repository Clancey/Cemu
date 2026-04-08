#include "input/api/VisionOS/VisionOSControllerProvider.h"

#include "input/InputManager.h"
#include "input/api/VisionOS/VisionOSController.h"
#include "input/api/Controller.h"

std::mutex VisionOSControllerProvider::s_controllerMutex;
ControllerState VisionOSControllerProvider::s_controllerState{};
VisionOSMotionState VisionOSControllerProvider::s_motionState{};
std::atomic<VisionOSControllerMode> VisionOSControllerProvider::s_controllerMode{VisionOSControllerMode::ProController};
std::atomic<float> VisionOSControllerProvider::s_pointingX{512.0f};
std::atomic<float> VisionOSControllerProvider::s_pointingY{384.0f};
std::atomic<bool> VisionOSControllerProvider::s_pointingValid{false};

ControllerState& VisionOSControllerProvider::get_controller_state()
{
	return s_controllerState;
}

VisionOSMotionState& VisionOSControllerProvider::get_motion_state()
{
	return s_motionState;
}

VisionOSControllerMode VisionOSControllerProvider::get_mode()
{
	return s_controllerMode.load();
}

void VisionOSControllerProvider::set_mode(VisionOSControllerMode mode)
{
	s_controllerMode.store(mode);
}

void VisionOSControllerProvider::on_key_event(int keyCode, bool isPressed)
{
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
	case kAxisPointingX:
		s_pointingX.store(value);
		s_pointingValid.store(true);
		break;
	case kAxisPointingY:
		s_pointingY.store(value);
		break;
	}
}

void VisionOSControllerProvider::on_motion_event(float gx, float gy, float gz,
                                                  float uax, float uay, float uaz,
                                                  float rrx, float rry, float rrz,
                                                  float aqx, float aqy, float aqz, float aqw)
{
	s_motionState.gravityX = gx;
	s_motionState.gravityY = gy;
	s_motionState.gravityZ = gz;
	s_motionState.userAccX = uax;
	s_motionState.userAccY = uay;
	s_motionState.userAccZ = uaz;
	s_motionState.rotRateX = rrx;
	s_motionState.rotRateY = rry;
	s_motionState.rotRateZ = rrz;
	s_motionState.attX = aqx;
	s_motionState.attY = aqy;
	s_motionState.attZ = aqz;
	s_motionState.attW = aqw;
}