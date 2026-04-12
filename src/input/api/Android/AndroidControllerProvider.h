#pragma once

#include "input/api/ControllerProvider.h"
#include "input/api/ControllerState.h"
#include "input/motion/MotionSample.h"

class AndroidControllerProvider : public ControllerProviderBase
{
    friend class AndroidController;

   public:
    struct AndroidControllerState
    {
        ControllerState controllerState{};
        MotionSample motionSample{};
        bool hasMotion = false;
        glm::vec2 position{};
        glm::vec2 prevPosition{};
        PositionVisibility positionVisibility = PositionVisibility::NONE;
    };

    inline static InputAPI::Type kAPIType = InputAPI::Android;
    InputAPI::Type api() const override { return kAPIType; }
    std::vector<std::shared_ptr<ControllerBase>> get_controllers() override { return {}; }
    void on_key_event(const std::string& deviceDescriptor, const std::string& deviceName, int nativeKeyCode, bool isPressed);
    void on_axis_event(const std::string& deviceDescriptor, const std::string& deviceName, int nativeAxisCode, float value);
    void on_motion_event(const std::string& deviceDescriptor, const std::string& deviceName, const MotionSample& motionSample, bool hasMotion);
    void on_position_event(const std::string& deviceDescriptor, const std::string& deviceName, float x, float y, PositionVisibility visibility);

   private:
    AndroidControllerState& get_or_create_controller_state(const std::string& deviceDescriptor);
    ControllerState get_controller_state(const std::string& deviceDescriptor) const;
    bool has_motion(const std::string& deviceDescriptor) const;
    MotionSample get_motion_sample(const std::string& deviceDescriptor) const;
    bool has_position(const std::string& deviceDescriptor) const;
    glm::vec2 get_position(const std::string& deviceDescriptor) const;
    glm::vec2 get_prev_position(const std::string& deviceDescriptor) const;
    PositionVisibility get_position_visibility(const std::string& deviceDescriptor) const;

    mutable std::mutex m_controllersMutex;
    std::unordered_map<std::string, AndroidControllerState> m_controllersState;
};
