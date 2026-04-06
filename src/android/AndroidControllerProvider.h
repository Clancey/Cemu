#pragma once

#include "input/api/ControllerProvider.h"
#include "AndroidInput.h"
#include <memory>

class AndroidControllerProvider : public ControllerProviderBase
{
public:
	AndroidControllerProvider();
	~AndroidControllerProvider();

	inline static InputAPI::Type kAPIType = InputAPI::AndroidController;
	InputAPI::Type api() const override { return kAPIType; }

	std::vector<std::shared_ptr<ControllerBase>> get_controllers() override;

	// Called by AndroidInput when devices are added/removed
	void on_device_added(int32_t device_id);
	void on_device_removed(int32_t device_id);

	// Called by AndroidInput to update controller states
	void update_device_state(int32_t device_id, const AndroidBridge::Input::GamepadState& state);

private:
	mutable std::mutex m_controllers_mutex;
	std::unordered_map<int32_t, std::shared_ptr<class AndroidController>> m_controllers;
};