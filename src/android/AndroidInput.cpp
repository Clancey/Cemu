#include "AndroidInput.h"
#include "AndroidMain.h"
#include "AndroidControllerProvider.h"
#include "input/InputManager.h"

#include <android/log.h>
#include <vector>
#include <algorithm>
#include <cmath>

#define LOG_TAG "CemuAndroidInput"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace AndroidBridge
{
	namespace Input
	{
		// Input state
		static std::vector<TouchPoint> s_touchPoints;
		static std::unordered_map<int32_t, GamepadState> s_gamepads;
		static bool s_touchscreenEnabled = true;
		static float s_gamepadDeadzone = 0.15f;
		static float s_touchSensitivity = 1.0f;
		static std::mutex s_inputMutex;

		// Android keycode to Cemu mapping
		static const std::unordered_map<int32_t, int32_t> s_keycodeMap = {
			{AKEYCODE_DPAD_UP, 0x26},       // VK_UP
			{AKEYCODE_DPAD_DOWN, 0x28},     // VK_DOWN
			{AKEYCODE_DPAD_LEFT, 0x25},     // VK_LEFT
			{AKEYCODE_DPAD_RIGHT, 0x27},    // VK_RIGHT
			{AKEYCODE_BUTTON_A, 0x58},      // X key (A button)
			{AKEYCODE_BUTTON_B, 0x5A},      // Z key (B button)
			{AKEYCODE_BUTTON_X, 0x53},      // S key (X button)
			{AKEYCODE_BUTTON_Y, 0x41},      // A key (Y button)
			{AKEYCODE_BUTTON_L1, 0x51},     // Q key (L1)
			{AKEYCODE_BUTTON_R1, 0x45},     // E key (R1)
			{AKEYCODE_BUTTON_START, 0x0D},  // Enter (Start)
			{AKEYCODE_BUTTON_SELECT, 0x20}, // Space (Select)
			{AKEYCODE_BACK, 0x1B},          // ESC (Back)
			{AKEYCODE_MENU, 0x09},          // Tab (Menu)
		};

		bool HandleTouchEvent(AInputEvent* event)
		{
			if (!s_touchscreenEnabled) return false;

			std::lock_guard<std::mutex> lock(s_inputMutex);

			int32_t action = AMotionEvent_getAction(event);
			int32_t pointerIndex = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK) >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
			int32_t pointerId = AMotionEvent_getPointerId(event, pointerIndex);

			float x = AMotionEvent_getX(event, pointerIndex);
			float y = AMotionEvent_getY(event, pointerIndex);
			float pressure = AMotionEvent_getPressure(event, pointerIndex);

			// Normalize coordinates to 0-1 range
			if (g_androidState.windowWidth > 0 && g_androidState.windowHeight > 0)
			{
				x /= g_androidState.windowWidth;
				y /= g_androidState.windowHeight;
			}

			int32_t maskedAction = action & AMOTION_EVENT_ACTION_MASK;

			switch (maskedAction)
			{
			case AMOTION_EVENT_ACTION_DOWN:
			case AMOTION_EVENT_ACTION_POINTER_DOWN:
			{
				TouchPoint point = {pointerId, x, y, pressure, true};
				auto it = std::find_if(s_touchPoints.begin(), s_touchPoints.end(),
					[pointerId](const TouchPoint& p) { return p.id == pointerId; });

				if (it != s_touchPoints.end())
				{
					*it = point;
				}
				else
				{
					s_touchPoints.push_back(point);
				}

				// Map first touch to gamepad for basic navigation
				if (s_touchPoints.size() == 1)
				{
					MapTouchToGamepad(x, y, true);
				}

				LOGD("Touch down: id=%d, x=%.2f, y=%.2f", pointerId, x, y);
				break;
			}
			case AMOTION_EVENT_ACTION_MOVE:
			{
				int32_t pointerCount = AMotionEvent_getPointerCount(event);
				for (int32_t i = 0; i < pointerCount; i++)
				{
					int32_t id = AMotionEvent_getPointerId(event, i);
					float px = AMotionEvent_getX(event, i);
					float py = AMotionEvent_getY(event, i);
					float pp = AMotionEvent_getPressure(event, i);

					if (g_androidState.windowWidth > 0 && g_androidState.windowHeight > 0)
					{
						px /= g_androidState.windowWidth;
						py /= g_androidState.windowHeight;
					}

					auto it = std::find_if(s_touchPoints.begin(), s_touchPoints.end(),
						[id](const TouchPoint& p) { return p.id == id; });

					if (it != s_touchPoints.end())
					{
						it->x = px;
						it->y = py;
						it->pressure = pp;
					}
				}
				break;
			}
			case AMOTION_EVENT_ACTION_UP:
			case AMOTION_EVENT_ACTION_POINTER_UP:
			{
				auto it = std::find_if(s_touchPoints.begin(), s_touchPoints.end(),
					[pointerId](const TouchPoint& p) { return p.id == pointerId; });

				if (it != s_touchPoints.end())
				{
					it->active = false;
					s_touchPoints.erase(it);
				}

				// Unmap touch from gamepad
				if (s_touchPoints.empty())
				{
					MapTouchToGamepad(0, 0, false);
				}

				LOGD("Touch up: id=%d", pointerId);
				break;
			}
			case AMOTION_EVENT_ACTION_CANCEL:
			{
				s_touchPoints.clear();
				MapTouchToGamepad(0, 0, false);
				LOGD("Touch cancelled");
				break;
			}
			}

			return true;
		}

		bool HandleKeyEvent(AInputEvent* event)
		{
			int32_t keycode = AKeyEvent_getKeyCode(event);
			int32_t action = AKeyEvent_getAction(event);
			int32_t deviceId = AInputEvent_getDeviceId(event);
			int32_t source = AInputEvent_getSource(event);

			LOGD("Key event: device=%d, keycode=%d, action=%d, source=0x%x", deviceId, keycode, action, source);

			// Check if this is a gamepad key event
			if (source & (AINPUT_SOURCE_GAMEPAD | AINPUT_SOURCE_JOYSTICK))
			{
				return HandleGamepadKeyEvent(event);
			}

			// Handle regular keyboard events
			// Map Android keycode to Cemu keycode
			int32_t cemuKey;
			if (!MapAndroidKeycode(keycode, cemuKey))
			{
				return false; // Unknown key
			}

			// Handle key press/release
			bool pressed = (action == AKEY_EVENT_ACTION_DOWN);

			// Forward keyboard events to Cemu's keyboard controller
			// Cemu handles keyboard input through the KeyboardControllerProvider
			// We don't need to do anything special here as the keyboard provider
			// should handle Android keyboard events through the native input system

			return true;
		}

		bool HandleMotionEvent(AInputEvent* event)
		{
			int32_t source = AInputEvent_getSource(event);

			if (source & AINPUT_SOURCE_TOUCHSCREEN)
			{
				return HandleTouchEvent(event);
			}
			else if (source & (AINPUT_SOURCE_GAMEPAD | AINPUT_SOURCE_JOYSTICK))
			{
				return HandleGamepadEvent(event);
			}

			return false;
		}

		bool HandleGamepadEvent(AInputEvent* event)
		{
			int32_t deviceId = AInputEvent_getDeviceId(event);

			std::lock_guard<std::mutex> lock(s_inputMutex);

			GamepadState& gamepad = s_gamepads[deviceId];
			gamepad.deviceId = deviceId;
			gamepad.type = GetDeviceType(deviceId);
			gamepad.isQuestController = IsQuestController(deviceId);

			// Get analog stick values
			float leftStickX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);
			float leftStickY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);
			float rightStickX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Z, 0);
			float rightStickY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RZ, 0);

			// Apply deadzone
			auto applyDeadzone = [](float value) -> float {
				if (std::abs(value) < s_gamepadDeadzone) return 0.0f;
				return value;
			};

			gamepad.leftStickX = applyDeadzone(leftStickX);
			gamepad.leftStickY = applyDeadzone(leftStickY);
			gamepad.rightStickX = applyDeadzone(rightStickX);
			gamepad.rightStickY = applyDeadzone(rightStickY);

			// Get trigger values
			gamepad.leftTrigger = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_LTRIGGER, 0);
			gamepad.rightTrigger = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RTRIGGER, 0);

			// Handle Quest controller specific mapping
			if (gamepad.isQuestController)
			{
				HandleQuestControllerInput(deviceId, event);
			}

			LOGD("Gamepad motion: device=%d, LS=(%.2f,%.2f), RS=(%.2f,%.2f), LT=%.2f, RT=%.2f",
				deviceId, gamepad.leftStickX, gamepad.leftStickY,
				gamepad.rightStickX, gamepad.rightStickY,
				gamepad.leftTrigger, gamepad.rightTrigger);

			return true;
		}

		void HandleQuestControllerInput(int32_t deviceId, AInputEvent* event)
		{
			// Quest controllers have unique input handling
			// Left controller is typically index 0, right controller is index 1
			GamepadState& gamepad = s_gamepads[deviceId];

			// Determine if this is left or right controller based on device ID
			// This is a simplification - real implementation would need proper device detection
			gamepad.controllerIndex = (deviceId % 2);

			// Quest controllers use different axis mappings
			float thumbstickX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);
			float thumbstickY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);

			gamepad.leftStickX = thumbstickX;
			gamepad.leftStickY = thumbstickY;

			LOGD("Quest controller input: device=%d, index=%d, thumbstick=(%.2f,%.2f)",
				deviceId, gamepad.controllerIndex, thumbstickX, thumbstickY);
		}

		void OnDeviceAdded(int32_t deviceId)
		{
			LOGD("Input device added: %d", deviceId);
			DeviceType type = GetDeviceType(deviceId);

			std::lock_guard<std::mutex> lock(s_inputMutex);
			if (s_gamepads.find(deviceId) == s_gamepads.end())
			{
				GamepadState gamepad = {};
				gamepad.deviceId = deviceId;
				gamepad.type = type;
				gamepad.isQuestController = IsQuestController(deviceId);
				s_gamepads[deviceId] = gamepad;

				// Notify AndroidControllerProvider
				auto& inputManager = InputManager::instance();
				auto provider = inputManager.get_api_provider(InputAPI::AndroidController);
				auto androidProvider = std::dynamic_pointer_cast<AndroidControllerProvider>(provider);
				if (androidProvider)
				{
					androidProvider->on_device_added(deviceId);
				}
			}
		}

		void OnDeviceRemoved(int32_t deviceId)
		{
			LOGD("Input device removed: %d", deviceId);

			std::lock_guard<std::mutex> lock(s_inputMutex);
			s_gamepads.erase(deviceId);

			// Notify AndroidControllerProvider
			auto& inputManager = InputManager::instance();
			auto provider = inputManager.get_api_provider(InputAPI::AndroidController);
			auto androidProvider = std::dynamic_pointer_cast<AndroidControllerProvider>(provider);
			if (androidProvider)
			{
				androidProvider->on_device_removed(deviceId);
			}
		}

		DeviceType GetDeviceType(int32_t deviceId)
		{
			// This would typically query the Android input system for device capabilities
			// For now, make some assumptions based on common patterns
			if (IsQuestController(deviceId))
			{
				return DeviceType::QuestController;
			}

			// Default to generic gamepad
			return DeviceType::Gamepad;
		}

		bool IsQuestController(int32_t deviceId)
		{
			// Quest controllers typically have specific vendor/product IDs
			// This would need proper device enumeration to detect accurately
			// Check if we're running in Quest environment by detecting VR-specific inputs

			// For now, use a simple heuristic:
			// - Quest controllers usually have device IDs in a specific range
			// - Check if the device supports Quest-specific input axes/buttons
			// TODO: Implement proper VR controller detection based on Android VR API

			// Simplified detection - assume Quest if device ID suggests it
			// Real implementation would query device capabilities
			return (deviceId >= 100 && deviceId < 200); // Common Quest controller ID range
		}

		void UpdateTouchState(const TouchPoint* points, int32_t pointCount)
		{
			std::lock_guard<std::mutex> lock(s_inputMutex);

			s_touchPoints.clear();
			for (int32_t i = 0; i < pointCount; i++)
			{
				s_touchPoints.push_back(points[i]);
			}
		}

		void MapTouchToGamepad(float x, float y, bool pressed)
		{
			// Map touch input to virtual gamepad for basic navigation
			// This is a simple implementation - could be made more sophisticated

			// Create a virtual gamepad for touch input
			constexpr int32_t TOUCH_DEVICE_ID = -1;

			std::lock_guard<std::mutex> lock(s_inputMutex);
			GamepadState& touchGamepad = s_gamepads[TOUCH_DEVICE_ID];
			touchGamepad.deviceId = TOUCH_DEVICE_ID;
			touchGamepad.type = DeviceType::Touchscreen;

			if (pressed)
			{
				// Map touch position to analog stick for basic movement
				// Center screen is neutral, edges are max deflection
				touchGamepad.leftStickX = (x - 0.5f) * 2.0f * s_touchSensitivity;
				touchGamepad.leftStickY = (y - 0.5f) * 2.0f * s_touchSensitivity;

				// Clamp to valid range
				touchGamepad.leftStickX = std::max(-1.0f, std::min(1.0f, touchGamepad.leftStickX));
				touchGamepad.leftStickY = std::max(-1.0f, std::min(1.0f, touchGamepad.leftStickY));

				// Set a button press for touch
				touchGamepad.buttons |= GamepadButtons::A;
			}
			else
			{
				// Release all touch input
				touchGamepad.leftStickX = 0.0f;
				touchGamepad.leftStickY = 0.0f;
				touchGamepad.buttons = 0;
			}
		}

		void UpdateGamepadState(int32_t deviceId, const GamepadState& state)
		{
			std::lock_guard<std::mutex> lock(s_inputMutex);
			s_gamepads[deviceId] = state;
		}

		bool MapAndroidKeycode(int32_t keycode, int32_t& cemuKey)
		{
			auto it = s_keycodeMap.find(keycode);
			if (it != s_keycodeMap.end())
			{
				cemuKey = it->second;
				return true;
			}

			bool HandleGamepadKeyEvent(AInputEvent* event)
			{
				int32_t deviceId = AInputEvent_getDeviceId(event);
				int32_t keycode = AKeyEvent_getKeyCode(event);
				int32_t action = AKeyEvent_getAction(event);

				std::lock_guard<std::mutex> lock(s_inputMutex);

				// Ensure gamepad state exists
				GamepadState& gamepad = s_gamepads[deviceId];
				gamepad.deviceId = deviceId;
				gamepad.type = GetDeviceType(deviceId);
				gamepad.isQuestController = IsQuestController(deviceId);

				bool pressed = (action == AKEY_EVENT_ACTION_DOWN);

				// Map Android gamepad keycodes to our button flags
				uint32_t buttonFlag = 0;
				switch (keycode)
				{
				case AKEYCODE_BUTTON_A:
					buttonFlag = GamepadButtons::A;
					break;
				case AKEYCODE_BUTTON_B:
					buttonFlag = GamepadButtons::B;
					break;
				case AKEYCODE_BUTTON_X:
					buttonFlag = GamepadButtons::X;
					break;
				case AKEYCODE_BUTTON_Y:
					buttonFlag = GamepadButtons::Y;
					break;
				case AKEYCODE_BUTTON_L1:
					buttonFlag = GamepadButtons::LeftShoulder;
					break;
				case AKEYCODE_BUTTON_R1:
					buttonFlag = GamepadButtons::RightShoulder;
					break;
				case AKEYCODE_BUTTON_L2:
					buttonFlag = 1 << 6; // ZL
					break;
				case AKEYCODE_BUTTON_R2:
					buttonFlag = 1 << 7; // ZR
					break;
				case AKEYCODE_BUTTON_START:
					buttonFlag = GamepadButtons::Start;
					break;
				case AKEYCODE_BUTTON_SELECT:
					buttonFlag = GamepadButtons::Back;
					break;
				case AKEYCODE_DPAD_UP:
					buttonFlag = GamepadButtons::DpadUp;
					break;
				case AKEYCODE_DPAD_DOWN:
					buttonFlag = GamepadButtons::DpadDown;
					break;
				case AKEYCODE_DPAD_LEFT:
					buttonFlag = GamepadButtons::DpadLeft;
					break;
				case AKEYCODE_DPAD_RIGHT:
					buttonFlag = GamepadButtons::DpadRight;
					break;
				case AKEYCODE_BUTTON_THUMBL:
					buttonFlag = GamepadButtons::LeftThumb;
					break;
				case AKEYCODE_BUTTON_THUMBR:
					buttonFlag = GamepadButtons::RightThumb;
					break;
				}

				// Update button state
				if (buttonFlag != 0)
				{
					if (pressed)
					{
						gamepad.buttons |= buttonFlag;
					}
					else
					{
						gamepad.buttons &= ~buttonFlag;
					}

					LOGD("Gamepad button: device=%d, keycode=%d, pressed=%d, buttons=0x%x",
						deviceId, keycode, pressed, gamepad.buttons);
				}

				return buttonFlag != 0; // Return true if we handled the key
			}
			return false;
		}

		void MapQuestButtonsToCemu(const GamepadState& state)
		{
			// Map Quest controller buttons to Pro Controller format for Cemu
			// Quest controllers have unique button layouts that need special handling

			if (!state.isQuestController)
				return;

			// Create a modified gamepad state with Quest-specific mappings
			GamepadState mappedState = state;

			// Quest left controller (index 0) mapping
			if (state.controllerIndex == 0)
			{
				// Quest left controller has trigger, grip, thumbstick, X, Y, menu buttons
				// Map to Pro Controller equivalents:
				// Quest trigger -> ZL
				// Quest grip -> L
				// Quest X -> X
				// Quest Y -> Y
				// Quest menu -> Minus

				if (state.buttons & GamepadButtons::QuestTrigger)
				{
					mappedState.buttons |= GamepadButtons::LeftShoulder; // Map to L
				}
				if (state.buttons & GamepadButtons::QuestGrip)
				{
					mappedState.buttons |= (1 << 6); // Map to ZL trigger button
				}
				if (state.buttons & GamepadButtons::QuestButtonOne)
				{
					mappedState.buttons |= GamepadButtons::X;
				}
				if (state.buttons & GamepadButtons::QuestButtonTwo)
				{
					mappedState.buttons |= GamepadButtons::Y;
				}
			}
			// Quest right controller (index 1) mapping
			else if (state.controllerIndex == 1)
			{
				// Quest right controller has trigger, grip, thumbstick, A, B, Oculus button
				// Map to Pro Controller equivalents:
				// Quest trigger -> ZR
				// Quest grip -> R
				// Quest A -> A
				// Quest B -> B
				// Quest Oculus -> Plus

				if (state.buttons & GamepadButtons::QuestTrigger)
				{
					mappedState.buttons |= GamepadButtons::RightShoulder; // Map to R
				}
				if (state.buttons & GamepadButtons::QuestGrip)
				{
					mappedState.buttons |= (1 << 7); // Map to ZR trigger button
				}
				if (state.buttons & GamepadButtons::QuestButtonOne)
				{
					mappedState.buttons |= GamepadButtons::A;
				}
				if (state.buttons & GamepadButtons::QuestButtonTwo)
				{
					mappedState.buttons |= GamepadButtons::B;
				}
			}

			// Update the gamepad state with the mapped buttons
			std::lock_guard<std::mutex> lock(s_inputMutex);
			s_gamepads[state.deviceId] = mappedState;
		}

		void Initialize()
		{
			LOGD("Initializing Android input system");

			std::lock_guard<std::mutex> lock(s_inputMutex);
			s_touchPoints.clear();
			s_gamepads.clear();

			LOGD("Android input system initialized");
		}

		void Shutdown()
		{
			LOGD("Shutting down Android input system");

			std::lock_guard<std::mutex> lock(s_inputMutex);
			s_touchPoints.clear();
			s_gamepads.clear();
		}

		void Update()
		{
			// Process any pending input events
			// Forward input state to Cemu's input system
			ForwardToCemuInput();
		}

		void SetTouchscreenEnabled(bool enabled)
		{
			s_touchscreenEnabled = enabled;
			LOGD("Touchscreen input %s", enabled ? "enabled" : "disabled");
		}

		void SetGamepadDeadzone(float deadzone)
		{
			s_gamepadDeadzone = deadzone;
			LOGD("Gamepad deadzone set to %.2f", deadzone);
		}

		void SetTouchSensitivity(float sensitivity)
		{
			s_touchSensitivity = sensitivity;
			LOGD("Touch sensitivity set to %.2f", sensitivity);
		}

		void ForwardToCemuInput()
		{
			// Forward Android input to Cemu's InputManager through AndroidControllerProvider
			std::lock_guard<std::mutex> lock(s_inputMutex);

			// Get the AndroidControllerProvider from InputManager
			auto& inputManager = InputManager::instance();
			auto provider = inputManager.get_api_provider(InputAPI::AndroidController);
			auto androidProvider = std::dynamic_pointer_cast<AndroidControllerProvider>(provider);

			if (!androidProvider)
			{
				LOGE("AndroidControllerProvider not available in InputManager");
				return;
			}

			// Process each gamepad and forward its state
			for (const auto& [deviceId, gamepad] : s_gamepads)
			{
				// Apply Quest controller button mapping if needed
				GamepadState processedState = gamepad;
				if (gamepad.isQuestController)
				{
					MapQuestButtonsToCemu(processedState);
				}

				// Ensure the controller exists in the provider
				// This handles device addition automatically
				androidProvider->on_device_added(deviceId);

				// Update the controller state
				androidProvider->update_device_state(deviceId, processedState);

				LOGD("Forwarded input for device %d: LS=(%.2f,%.2f) RS=(%.2f,%.2f) buttons=0x%x",
					deviceId, processedState.leftStickX, processedState.leftStickY,
					processedState.rightStickX, processedState.rightStickY, processedState.buttons);
			}
		}
	}
}