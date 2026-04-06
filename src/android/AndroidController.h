#pragma once

#include "input/api/Controller.h"
#include "AndroidControllerProvider.h"
#include "AndroidInput.h"

class AndroidController : public Controller<AndroidControllerProvider>
{
public:
	AndroidController(int32_t device_id, AndroidBridge::Input::DeviceType device_type);
	~AndroidController() override = default;

	std::string_view api_name() const override
	{
		return "AndroidController";
	}
	InputAPI::Type api() const override { return InputAPI::AndroidController; }

	bool is_connected() override;
	bool connect() override;

	// Update the internal state from Android input
	void update_state(const AndroidBridge::Input::GamepadState& android_state);

	int32_t get_device_id() const { return m_device_id; }

protected:
	ControllerState raw_state() override;

private:
	int32_t m_device_id;
	AndroidBridge::Input::DeviceType m_device_type;
	mutable std::mutex m_state_mutex;
	AndroidBridge::Input::GamepadState m_current_state{};
	bool m_connected = false;

	// Map Android gamepad buttons to Cemu button constants
	static uint64 map_android_button_to_cemu(uint32_t android_buttons);

	// Helper methods for generating controller identification
	static std::string generate_uuid(int32_t device_id, AndroidBridge::Input::DeviceType device_type);
	static std::string generate_display_name(int32_t device_id, AndroidBridge::Input::DeviceType device_type);
};