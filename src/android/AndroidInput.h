#pragma once

#include <android/input.h>
#include <android/keycodes.h>
#include <memory>
#include <unordered_map>

namespace AndroidBridge
{
	namespace Input
	{
		// Input device types
		enum class DeviceType
		{
			Unknown,
			Touchscreen,
			Gamepad,
			Keyboard,
			QuestController
		};

		// Touch event data
		struct TouchPoint
		{
			int32_t id;
			float x, y;
			float pressure;
			bool active;
		};

		// Gamepad state
		struct GamepadState
		{
			int32_t deviceId;
			DeviceType type;

			// Analog sticks (normalized -1.0 to 1.0)
			float leftStickX, leftStickY;
			float rightStickX, rightStickY;

			// Triggers (0.0 to 1.0)
			float leftTrigger, rightTrigger;

			// Button states (bitfield)
			uint32_t buttons;

			// Quest-specific
			bool isQuestController;
			int32_t controllerIndex; // 0 = left, 1 = right for Quest
		};

		// Input event handlers
		bool HandleTouchEvent(AInputEvent* event);
		bool HandleKeyEvent(AInputEvent* event);
		bool HandleMotionEvent(AInputEvent* event);
		bool HandleGamepadEvent(AInputEvent* event);

		// Device management
		void OnDeviceAdded(int32_t deviceId);
		void OnDeviceRemoved(int32_t deviceId);
		DeviceType GetDeviceType(int32_t deviceId);
		bool IsQuestController(int32_t deviceId);

		// Touch screen management
		void UpdateTouchState(const TouchPoint* points, int32_t pointCount);
		void MapTouchToGamepad(float x, float y, bool pressed);

		// Gamepad mapping
		void UpdateGamepadState(int32_t deviceId, const GamepadState& state);
		bool MapAndroidKeycode(int32_t keycode, int32_t& cemuKey);

		// Quest controller specific
		void HandleQuestControllerInput(int32_t deviceId, AInputEvent* event);
		void MapQuestButtonsToCemu(const GamepadState& state);

		// Input system lifecycle
		void Initialize();
		void Shutdown();
		void Update(); // Called each frame to process pending input

		// Configuration
		void SetTouchscreenEnabled(bool enabled);
		void SetGamepadDeadzone(float deadzone);
		void SetTouchSensitivity(float sensitivity);

		// Input mapping to Cemu's input system
		void ForwardToCemuInput();
	}

	// Button bit flags for gamepad state
	namespace GamepadButtons
	{
		constexpr uint32_t A = 1 << 0;
		constexpr uint32_t B = 1 << 1;
		constexpr uint32_t X = 1 << 2;
		constexpr uint32_t Y = 1 << 3;
		constexpr uint32_t LeftShoulder = 1 << 4;
		constexpr uint32_t RightShoulder = 1 << 5;
		constexpr uint32_t Back = 1 << 6;
		constexpr uint32_t Start = 1 << 7;
		constexpr uint32_t LeftThumb = 1 << 8;
		constexpr uint32_t RightThumb = 1 << 9;
		constexpr uint32_t DpadUp = 1 << 10;
		constexpr uint32_t DpadDown = 1 << 11;
		constexpr uint32_t DpadLeft = 1 << 12;
		constexpr uint32_t DpadRight = 1 << 13;
		constexpr uint32_t Home = 1 << 14;
		constexpr uint32_t Menu = 1 << 15;

		// Quest-specific buttons
		constexpr uint32_t QuestTrigger = 1 << 16;
		constexpr uint32_t QuestGrip = 1 << 17;
		constexpr uint32_t QuestThumbstick = 1 << 18;
		constexpr uint32_t QuestButtonOne = 1 << 19;
		constexpr uint32_t QuestButtonTwo = 1 << 20;
	}
}