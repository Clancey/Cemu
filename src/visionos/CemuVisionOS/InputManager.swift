import GameController
import Observation
import os

/// Manages game controller discovery and forwards input to Cemu's keyboard
/// input system via virtual key codes.
///
/// On visionOS, physical input comes from GCController (Bluetooth gamepads).
/// Each button press is mapped to a virtual key code that Cemu's keyboard
/// controller provider reads via WindowSystem::IsKeyDown().
@Observable
@MainActor
final class InputManager {

    // MARK: - Published State

    /// The currently connected and active game controller, if any.
    private(set) var connectedController: GCController?

    /// Human-readable name of the connected controller.
    var controllerName: String? {
        connectedController?.vendorName
    }

    /// True while wireless discovery is running.
    private(set) var isDiscovering = false

    // MARK: - Private

    private let logger = Logger(subsystem: "org.cemu.CemuVision", category: "InputManager")
    private let bridge = CemuBridge.shared()

    private var connectObserver: NSObjectProtocol?
    private var disconnectObserver: NSObjectProtocol?

    // Virtual key codes for GCController → Cemu keyboard mapping.
    // These are arbitrary unique codes; Cemu's keyboard controller maps them
    // to Wii U buttons via its config. We write a default config on first run.
    // Must match VisionOSControllerProvider.h button constants (hex values)
    enum VKey: UInt32 {
        case a         = 0x1000
        case b         = 0x1001
        case x         = 0x1002
        case y         = 0x1003
        case l         = 0x1004
        case r         = 0x1005
        case zl        = 0x1006
        case zr        = 0x1007
        case dpadUp    = 0x1008
        case dpadDown  = 0x1009
        case dpadLeft  = 0x1010
        case dpadRight = 0x1011
        case plus      = 0x1012
        case minus     = 0x1013
        case home      = 0x1014
        case lStick    = 0x1015
        case rStick    = 0x1016
    }

    // MARK: - Lifecycle

    init() {
        startObservingControllers()
    }

    deinit {
        GCController.stopWirelessControllerDiscovery()
    }

    /// Begin Bluetooth controller discovery.
    func startDiscovery() {
        guard !isDiscovering else { return }
        isDiscovering = true
        GCController.startWirelessControllerDiscovery { [weak self] in
            Task { @MainActor in
                self?.isDiscovering = false
            }
        }
        logger.info("Wireless controller discovery started")
    }

    /// Stop Bluetooth controller discovery.
    func stopDiscovery() {
        guard isDiscovering else { return }
        GCController.stopWirelessControllerDiscovery()
        isDiscovering = false
    }

    // MARK: - Controller Observation

    private func startObservingControllers() {
        let center = NotificationCenter.default

        connectObserver = center.addObserver(
            forName: .GCControllerDidConnect,
            object: nil,
            queue: .main
        ) { [weak self] notification in
            guard let controller = notification.object as? GCController else { return }
            Task { @MainActor in
                self?.controllerDidConnect(controller)
            }
        }

        disconnectObserver = center.addObserver(
            forName: .GCControllerDidDisconnect,
            object: nil,
            queue: .main
        ) { [weak self] notification in
            guard let controller = notification.object as? GCController else { return }
            Task { @MainActor in
                self?.controllerDidDisconnect(controller)
            }
        }

        // Check for already-connected controllers.
        if let existing = GCController.controllers().first {
            controllerDidConnect(existing)
        }

        // Also check for keyboard (simulator)
        if let keyboard = GCKeyboard.coalesced {
            setupKeyboardMapping(keyboard)
        }

        startDiscovery()
    }

    private func controllerDidConnect(_ controller: GCController) {
        logger.info("Controller connected: \(controller.vendorName ?? "Unknown")")
        connectedController = controller
        controller.playerIndex = .index1
        configureMapping(for: controller)
    }

    private func controllerDidDisconnect(_ controller: GCController) {
        logger.info("Controller disconnected: \(controller.vendorName ?? "Unknown")")
        if connectedController === controller {
            connectedController = nil
            bridge.releaseAllKeys()
        }
    }

    // MARK: - Keyboard Mapping (Simulator)

    private func setupKeyboardMapping(_ keyboard: GCKeyboard) {
        guard let input = keyboard.keyboardInput else { return }
        logger.info("Keyboard detected — mapping keys for simulator")

        // Map keyboard keys using button handlers on specific keys
        let keyMap: [(GCKeyCode, VKey)] = [
            (.keyJ, .a),           // J = A (confirm)
            (.keyK, .b),           // K = B (back)
            (.keyI, .x),           // I = X
            (.keyU, .y),           // U = Y
            (.keyW, .dpadUp),
            (.keyS, .dpadDown),
            (.keyA, .dpadLeft),
            (.keyD, .dpadRight),
            (.upArrow, .dpadUp),
            (.downArrow, .dpadDown),
            (.leftArrow, .dpadLeft),
            (.rightArrow, .dpadRight),
            (.keyQ, .l),
            (.keyE, .r),
            (.keyZ, .zl),
            (.keyC, .zr),
            (.returnOrEnter, .plus),
            (.deleteOrBackspace, .minus),
            (.escape, .home),
        ]
        for (keyCode, vkey) in keyMap {
            if let button = input.button(forKeyCode: keyCode) {
                let capturedVKey = vkey
                button.pressedChangedHandler = { [weak self] _, _, pressed in
                    self?.bridge.onControllerButtonEvent(capturedVKey.rawValue, pressed: pressed)
                }
            }
        }
    }

    // MARK: - Gamepad Mapping

    private func configureMapping(for controller: GCController) {
        guard let gamepad = controller.extendedGamepad else {
            logger.warning("Controller does not support extended gamepad profile")
            return
        }

        // Poll-based: set up a value changed handler that updates all keys
        gamepad.valueChangedHandler = { [weak self] pad, _ in
            self?.syncGamepadState(pad)
        }
    }

    private nonisolated func syncGamepadState(_ pad: GCExtendedGamepad) {
        let bridge = CemuBridge.shared()

        // Face buttons
        bridge.onControllerButtonEvent(VKey.a.rawValue, pressed: pad.buttonA.isPressed)
        bridge.onControllerButtonEvent(VKey.b.rawValue, pressed: pad.buttonB.isPressed)
        bridge.onControllerButtonEvent(VKey.x.rawValue, pressed: pad.buttonX.isPressed)
        bridge.onControllerButtonEvent(VKey.y.rawValue, pressed: pad.buttonY.isPressed)

        // Shoulders/triggers
        bridge.onControllerButtonEvent(VKey.l.rawValue, pressed: pad.leftShoulder.isPressed)
        bridge.onControllerButtonEvent(VKey.r.rawValue, pressed: pad.rightShoulder.isPressed)
        bridge.onControllerButtonEvent(VKey.zl.rawValue, pressed: pad.leftTrigger.isPressed)
        bridge.onControllerButtonEvent(VKey.zr.rawValue, pressed: pad.rightTrigger.isPressed)

        // D-pad
        bridge.onControllerButtonEvent(VKey.dpadUp.rawValue, pressed: pad.dpad.up.isPressed)
        bridge.onControllerButtonEvent(VKey.dpadDown.rawValue, pressed: pad.dpad.down.isPressed)
        bridge.onControllerButtonEvent(VKey.dpadLeft.rawValue, pressed: pad.dpad.left.isPressed)
        bridge.onControllerButtonEvent(VKey.dpadRight.rawValue, pressed: pad.dpad.right.isPressed)

        // Menu
        bridge.onControllerButtonEvent(VKey.plus.rawValue, pressed: pad.buttonMenu.isPressed)
        bridge.onControllerButtonEvent(VKey.minus.rawValue, pressed: pad.buttonOptions?.isPressed ?? false)

        // Stick buttons
        bridge.onControllerButtonEvent(VKey.lStick.rawValue, pressed: pad.leftThumbstickButton?.isPressed ?? false)
        bridge.onControllerButtonEvent(VKey.rStick.rawValue, pressed: pad.rightThumbstickButton?.isPressed ?? false)

        // Analog sticks — send as proper axes
        bridge.onControllerAxisEvent(0, value: pad.leftThumbstick.xAxis.value)   // kAxisLStickX
        bridge.onControllerAxisEvent(1, value: pad.leftThumbstick.yAxis.value)   // kAxisLStickY
        bridge.onControllerAxisEvent(2, value: pad.rightThumbstick.xAxis.value)  // kAxisRStickX
        bridge.onControllerAxisEvent(3, value: pad.rightThumbstick.yAxis.value)  // kAxisRStickY
        bridge.onControllerAxisEvent(4, value: pad.leftTrigger.value)            // kAxisLTrigger
        bridge.onControllerAxisEvent(5, value: pad.rightTrigger.value)           // kAxisRTrigger
    }
}
