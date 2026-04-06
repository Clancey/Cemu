#include "AndroidControllerProvider.h"
#include "AndroidController.h"
#include <android/log.h>

#define LOG_TAG "AndroidControllerProvider"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

AndroidControllerProvider::AndroidControllerProvider()
{
	LOGD("AndroidControllerProvider initialized");
}

AndroidControllerProvider::~AndroidControllerProvider()
{
	LOGD("AndroidControllerProvider destroyed");
}

std::vector<std::shared_ptr<ControllerBase>> AndroidControllerProvider::get_controllers()
{
	std::lock_guard<std::mutex> lock(m_controllers_mutex);

	std::vector<std::shared_ptr<ControllerBase>> result;
	result.reserve(m_controllers.size());

	for (const auto& [device_id, controller] : m_controllers)
	{
		result.push_back(controller);
	}

	LOGD("get_controllers() returning %zu controllers", result.size());
	return result;
}

void AndroidControllerProvider::on_device_added(int32_t device_id)
{
	std::lock_guard<std::mutex> lock(m_controllers_mutex);

	// Check if controller already exists
	if (m_controllers.find(device_id) != m_controllers.end())
	{
		LOGD("Device %d already exists", device_id);
		return;
	}

	// Create new Android controller
	AndroidBridge::Input::DeviceType device_type = AndroidBridge::Input::GetDeviceType(device_id);
	auto controller = std::make_shared<AndroidController>(device_id, device_type);

	m_controllers[device_id] = controller;

	LOGD("Added Android controller for device %d", device_id);
}

void AndroidControllerProvider::on_device_removed(int32_t device_id)
{
	std::lock_guard<std::mutex> lock(m_controllers_mutex);

	auto it = m_controllers.find(device_id);
	if (it != m_controllers.end())
	{
		m_controllers.erase(it);
		LOGD("Removed Android controller for device %d", device_id);
	}
}

void AndroidControllerProvider::update_device_state(int32_t device_id, const AndroidBridge::Input::GamepadState& state)
{
	std::lock_guard<std::mutex> lock(m_controllers_mutex);

	auto it = m_controllers.find(device_id);
	if (it != m_controllers.end())
	{
		it->second->update_state(state);
	}
}