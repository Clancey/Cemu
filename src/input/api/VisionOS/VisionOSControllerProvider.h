#pragma once

#include "input/api/ControllerProvider.h"
#include "input/api/ControllerState.h"

class VisionOSControllerProvider : public ControllerProviderBase
{
	friend class VisionOSController;

   public:
	inline static InputAPI::Type kAPIType = InputAPI::VisionOS;
	InputAPI::Type api() const override { return kAPIType; }
	std::vector<std::shared_ptr<ControllerBase>> get_controllers() override { return {}; }

	static void on_key_event(int keyCode, bool isPressed);
	static void on_axis_event(int axisCode, float value);

	// Button constants
	static constexpr int kButtonA = 0x1000;
	static constexpr int kButtonB = 0x1001;
	static constexpr int kButtonX = 0x1002;
	static constexpr int kButtonY = 0x1003;
	static constexpr int kButtonL = 0x1004;
	static constexpr int kButtonR = 0x1005;
	static constexpr int kButtonZL = 0x1006;
	static constexpr int kButtonZR = 0x1007;
	static constexpr int kButtonDpadUp = 0x1008;
	static constexpr int kButtonDpadDown = 0x1009;
	static constexpr int kButtonDpadLeft = 0x1010;
	static constexpr int kButtonDpadRight = 0x1011;
	static constexpr int kButtonPlus = 0x1012;
	static constexpr int kButtonMinus = 0x1013;
	static constexpr int kButtonHome = 0x1014;
	static constexpr int kButtonLStick = 0x1015;
	static constexpr int kButtonRStick = 0x1016;

	// Axis constants
	static constexpr int kAxisLStickX = 0;
	static constexpr int kAxisLStickY = 1;
	static constexpr int kAxisRStickX = 2;
	static constexpr int kAxisRStickY = 3;
	static constexpr int kAxisLTrigger = 4;
	static constexpr int kAxisRTrigger = 5;

   public:
	static ControllerState& get_controller_state();
	static std::mutex s_controllerMutex;
	static ControllerState s_controllerState;
};