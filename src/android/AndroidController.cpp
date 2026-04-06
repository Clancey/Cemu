#include "AndroidController.h"
#include "input/api/Controller.h"
#include <android/log.h>
#include <sstream>

#define LOG_TAG "AndroidController"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

AndroidController::AndroidController(int32_t device_id, AndroidBridge::Input::DeviceType device_type)
	: Controller(generate_uuid(device_id, device_type), generate_display_name(device_id, device_type))
	, m_device_id(device_id)
	, m_device_type(device_type)
{
	LOGD("Created AndroidController for device %d, type %d", device_id, static_cast<int>(device_type));
}

std::string AndroidController::generate_uuid(int32_t device_id, AndroidBridge::Input::DeviceType device_type)
{
	std::ostringstream ss;
	ss << "android_" << device_id << "_" << static_cast<int>(device_type);
	return ss.str();
}

std::string AndroidController::generate_display_name(int32_t device_id, AndroidBridge::Input::DeviceType device_type)
{
	switch (device_type)
	{
	case AndroidBridge::Input::DeviceType::QuestController:
		return device_id % 2 == 0 ? "Quest Left Controller" : "Quest Right Controller";
	case AndroidBridge::Input::DeviceType::Gamepad:
		return "Android Gamepad " + std::to_string(device_id);
	case AndroidBridge::Input::DeviceType::Touchscreen:
		return "Android Touchscreen";
	case AndroidBridge::Input::DeviceType::Keyboard:
		return "Android Keyboard";
	default:
		return "Android Input Device " + std::to_string(device_id);
	}
}

bool AndroidController::is_connected()
{
	std::lock_guard<std::mutex> lock(m_state_mutex);
	return m_connected;
}

bool AndroidController::connect()
{
	std::lock_guard<std::mutex> lock(m_state_mutex);
	m_connected = true;
	return true;
}

void AndroidController::update_state(const AndroidBridge::Input::GamepadState& android_state)
{
	std::lock_guard<std::mutex> lock(m_state_mutex);
	m_current_state = android_state;
	m_connected = true;
}

ControllerState AndroidController::raw_state()
{
	std::lock_guard<std::mutex> lock(m_state_mutex);

	ControllerState state{};

	if (!m_connected)
	{
		return state;
	}

	// Map analog sticks - Cemu uses glm::vec2 with range [-1, 1]
	state.axis.x = m_current_state.leftStickX;
	state.axis.y = -m_current_state.leftStickY; // Invert Y axis to match Cemu convention

	state.rotation.x = m_current_state.rightStickX;
	state.rotation.y = -m_current_state.rightStickY; // Invert Y axis

	// Map triggers - Cemu uses range [0, 1]
	state.trigger.x = m_current_state.leftTrigger;
	state.trigger.y = m_current_state.rightTrigger;

	// Map buttons
	uint64 cemu_buttons = map_android_button_to_cemu(m_current_state.buttons);

	// Set button states in Cemu's format
	for (int i = 0; i < 32; ++i)
	{
		if (cemu_buttons & (1ULL << i))
		{
			state.buttons.SetButtonState(i, true);
		}
	}

	return state;
}

uint64 AndroidController::map_android_button_to_cemu(uint32_t android_buttons)
{
	uint64 cemu_buttons = 0;

	using namespace AndroidBridge::GamepadButtons;

	// Map face buttons
	if (android_buttons & A) cemu_buttons |= (1ULL << Buttons2::kButton0); // A
	if (android_buttons & B) cemu_buttons |= (1ULL << Buttons2::kButton1); // B
	if (android_buttons & X) cemu_buttons |= (1ULL << Buttons2::kButton2); // X
	if (android_buttons & Y) cemu_buttons |= (1ULL << Buttons2::kButton3); // Y

	// Map shoulder buttons
	if (android_buttons & LeftShoulder) cemu_buttons |= (1ULL << Buttons2::kButton4); // L
	if (android_buttons & RightShoulder) cemu_buttons |= (1ULL << Buttons2::kButton5); // R

	// Map triggers (as buttons when pressed)
	if (android_buttons & (1 << 6)) cemu_buttons |= (1ULL << Buttons2::kButtonZL); // ZL
	if (android_buttons & (1 << 7)) cemu_buttons |= (1ULL << Buttons2::kButtonZR); // ZR

	// Map system buttons
	if (android_buttons & Back) cemu_buttons |= (1ULL << Buttons2::kButton6); // Select/Back
	if (android_buttons & Start) cemu_buttons |= (1ULL << Buttons2::kButton7); // Start
	if (android_buttons & Home) cemu_buttons |= (1ULL << Buttons2::kButton8); // Home

	// Map thumbstick buttons
	if (android_buttons & LeftThumb) cemu_buttons |= (1ULL << Buttons2::kButton9); // Left stick click
	if (android_buttons & RightThumb) cemu_buttons |= (1ULL << Buttons2::kButton10); // Right stick click

	// Map D-pad
	if (android_buttons & DpadUp) cemu_buttons |= (1ULL << Buttons2::kButtonUp);
	if (android_buttons & DpadDown) cemu_buttons |= (1ULL << Buttons2::kButtonDown);
	if (android_buttons & DpadLeft) cemu_buttons |= (1ULL << Buttons2::kButtonLeft);
	if (android_buttons & DpadRight) cemu_buttons |= (1ULL << Buttons2::kButtonRight);

	// Quest-specific buttons mapped to additional slots
	if (android_buttons & QuestTrigger) cemu_buttons |= (1ULL << Buttons2::kButton11);
	if (android_buttons & QuestGrip) cemu_buttons |= (1ULL << Buttons2::kButton12);
	if (android_buttons & QuestThumbstick) cemu_buttons |= (1ULL << Buttons2::kButton13);
	if (android_buttons & QuestButtonOne) cemu_buttons |= (1ULL << Buttons2::kButton14);
	if (android_buttons & QuestButtonTwo) cemu_buttons |= (1ULL << Buttons2::kButton15);

	return cemu_buttons;
}