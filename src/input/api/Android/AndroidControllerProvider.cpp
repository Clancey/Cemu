#include "input/api/Android/AndroidControllerProvider.h"

#include <android/input.h>
#include <android/keycodes.h>

#include "input/InputManager.h"
#include "input/api/Android/AndroidController.h"
#include "input/api/Controller.h"

AndroidControllerProvider::AndroidControllerState& AndroidControllerProvider::get_or_create_controller_state(const std::string& deviceDescriptor)
{
	if (auto it = m_controllersState.find(deviceDescriptor); it != m_controllersState.end())
		return it->second;
	m_controllersState[deviceDescriptor] = {};
	return m_controllersState[deviceDescriptor];
}

void AndroidControllerProvider::on_key_event(const std::string& deviceDescriptor, [[maybe_unused]] const std::string& deviceName, int nativeKeyCode, bool isPressed)
{
	std::scoped_lock lock(m_controllersMutex);
	auto& controllerState = get_or_create_controller_state(deviceDescriptor);
	controllerState.controllerState.buttons.SetButtonState(nativeKeyCode, isPressed);
}

void AndroidControllerProvider::on_axis_event(const std::string& deviceDescriptor, [[maybe_unused]] const std::string& deviceName, int nativeAxisCode, float value)
{
	std::scoped_lock lock(m_controllersMutex);
	auto& controllerState = get_or_create_controller_state(deviceDescriptor);
	switch (nativeAxisCode)
	{
	case AMOTION_EVENT_AXIS_X:
		controllerState.controllerState.axis.x = value;
		break;
	case AMOTION_EVENT_AXIS_Y:
		controllerState.controllerState.axis.y = value;
		break;
	case AMOTION_EVENT_AXIS_RX:
	case AMOTION_EVENT_AXIS_Z:
		controllerState.controllerState.rotation.x = value;
		break;
	case AMOTION_EVENT_AXIS_RY:
	case AMOTION_EVENT_AXIS_RZ:
		controllerState.controllerState.rotation.y = value;
		break;
	case AMOTION_EVENT_AXIS_LTRIGGER:
		controllerState.controllerState.trigger.x = value;
		break;
	case AMOTION_EVENT_AXIS_RTRIGGER:
		controllerState.controllerState.trigger.y = value;
		break;
	case AMOTION_EVENT_AXIS_HAT_X:
		if (value == 0.0f)
		{
			controllerState.controllerState.buttons.SetButtonState(kButtonRight, false);
			controllerState.controllerState.buttons.SetButtonState(kButtonLeft, false);
			return;
		}
		else if (value > 0.0f)
			controllerState.controllerState.buttons.SetButtonState(kButtonRight, true);
		else
			controllerState.controllerState.buttons.SetButtonState(kButtonLeft, true);
		break;
	case AMOTION_EVENT_AXIS_HAT_Y:
		if (value == 0.0f)
		{
			controllerState.controllerState.buttons.SetButtonState(kButtonUp, false);
			controllerState.controllerState.buttons.SetButtonState(kButtonDown, false);
			return;
		}
		else if (value > 0.0f)
			controllerState.controllerState.buttons.SetButtonState(kButtonDown, true);
		else
			controllerState.controllerState.buttons.SetButtonState(kButtonUp, true);
		break;
	}
}

void AndroidControllerProvider::on_motion_event(const std::string& deviceDescriptor, [[maybe_unused]] const std::string& deviceName, const MotionSample& motionSample, bool hasMotion)
{
	std::scoped_lock lock(m_controllersMutex);
	auto& controllerState = get_or_create_controller_state(deviceDescriptor);
	controllerState.motionSample = motionSample;
	controllerState.hasMotion = hasMotion;
}

void AndroidControllerProvider::on_position_event(const std::string& deviceDescriptor, [[maybe_unused]] const std::string& deviceName, float x, float y, PositionVisibility visibility)
{
	std::scoped_lock lock(m_controllersMutex);
	auto& controllerState = get_or_create_controller_state(deviceDescriptor);
	controllerState.prevPosition = controllerState.position;
	controllerState.position = {x, y};
	controllerState.positionVisibility = visibility;
}

ControllerState AndroidControllerProvider::get_controller_state(const std::string& deviceDescriptor) const
{
	std::scoped_lock lock(m_controllersMutex);
	if (auto it = m_controllersState.find(deviceDescriptor); it != m_controllersState.end())
		return it->second.controllerState;
	return {};
}

bool AndroidControllerProvider::has_motion(const std::string& deviceDescriptor) const
{
	std::scoped_lock lock(m_controllersMutex);
	if (auto it = m_controllersState.find(deviceDescriptor); it != m_controllersState.end())
		return it->second.hasMotion;
	return false;
}

MotionSample AndroidControllerProvider::get_motion_sample(const std::string& deviceDescriptor) const
{
	std::scoped_lock lock(m_controllersMutex);
	if (auto it = m_controllersState.find(deviceDescriptor); it != m_controllersState.end())
		return it->second.motionSample;
	return {};
}

bool AndroidControllerProvider::has_position(const std::string& deviceDescriptor) const
{
	return get_position_visibility(deviceDescriptor) != PositionVisibility::NONE;
}

glm::vec2 AndroidControllerProvider::get_position(const std::string& deviceDescriptor) const
{
	std::scoped_lock lock(m_controllersMutex);
	if (auto it = m_controllersState.find(deviceDescriptor); it != m_controllersState.end())
		return it->second.position;
	return {};
}

glm::vec2 AndroidControllerProvider::get_prev_position(const std::string& deviceDescriptor) const
{
	std::scoped_lock lock(m_controllersMutex);
	if (auto it = m_controllersState.find(deviceDescriptor); it != m_controllersState.end())
		return it->second.prevPosition;
	return {};
}

PositionVisibility AndroidControllerProvider::get_position_visibility(const std::string& deviceDescriptor) const
{
	std::scoped_lock lock(m_controllersMutex);
	if (auto it = m_controllersState.find(deviceDescriptor); it != m_controllersState.end())
		return it->second.positionVisibility;
	return PositionVisibility::NONE;
}
