#pragma once

#include "input/api/ControllerProvider.h"
#include "input/api/ControllerState.h"
#include <atomic>

// Controller emulation mode
enum class VisionOSControllerMode
{
	ProController,      // Single Pro Controller (URCC)
	WiimoteNunchuck,    // Wiimote (right hand) + Nunchuck (left hand)
};

// Motion data from GCMotion
struct VisionOSMotionState
{
	// Gravity vector (accelerometer reference)
	float gravityX = 0.0f, gravityY = -1.0f, gravityZ = 0.0f;
	// User acceleration (excluding gravity)
	float userAccX = 0.0f, userAccY = 0.0f, userAccZ = 0.0f;
	// Rotation rate (gyroscope)
	float rotRateX = 0.0f, rotRateY = 0.0f, rotRateZ = 0.0f;
	// Attitude quaternion
	float attX = 0.0f, attY = 0.0f, attZ = 0.0f, attW = 1.0f;
};

class VisionOSControllerProvider : public ControllerProviderBase
{
	friend class VisionOSController;

   public:
	inline static InputAPI::Type kAPIType = InputAPI::VisionOS;
	InputAPI::Type api() const override { return kAPIType; }
	std::vector<std::shared_ptr<ControllerBase>> get_controllers() override { return {}; }

	static void on_key_event(int keyCode, bool isPressed);
	static void on_axis_event(int axisCode, float value);
	static void on_motion_event(float gx, float gy, float gz,
	                            float uax, float uay, float uaz,
	                            float rrx, float rry, float rrz,
	                            float aqx, float aqy, float aqz, float aqw);

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
	static VisionOSMotionState& get_motion_state();
	static VisionOSControllerMode get_mode();
	static void set_mode(VisionOSControllerMode mode);

	static std::mutex s_controllerMutex;
	static ControllerState s_controllerState;
	static VisionOSMotionState s_motionState;
	static std::atomic<VisionOSControllerMode> s_controllerMode;
};