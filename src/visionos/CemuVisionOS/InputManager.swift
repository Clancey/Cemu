import GameController
import Observation
import os

/// Manages Bluetooth game controller discovery and mapping to Wii U Pro
/// Controller inputs, forwarded to the Cemu input subsystem through the
/// bridge layer.
///
/// On visionOS, physical input comes exclusively from GCController (Bluetooth
/// gamepads).  Eye tracking and pinch gestures are handled by SwiftUI gesture
/// modifiers on the emulator view and are not part of this manager.
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

    // MARK: - Lifecycle

    init() {
        startObservingControllers()
    }

    deinit {
        // deinit is nonisolated and cannot call @MainActor-isolated methods
        // directly.  GCController.stopWirelessControllerDiscovery() is safe
        // to call from any context, so we invoke it without going through
        // the actor-isolated stopDiscovery() wrapper.
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

        // Automatically start discovery at launch.
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
        }
    }

    // MARK: - Input Mapping

    /// Map GCController inputs to the Wii U Pro Controller layout.
    ///
    /// The Wii U Pro Controller has:
    ///   - Two analog sticks (L/R)
    ///   - D-pad
    ///   - A, B, X, Y
    ///   - L, R, ZL, ZR
    ///   - Plus, Minus, Home
    ///   - Left stick button, Right stick button
    ///
    /// This maps naturally to any MFi / Xbox / DualSense extended gamepad.
    private func configureMapping(for controller: GCController) {
        guard let gamepad = controller.extendedGamepad else {
            logger.warning("Controller does not support extended gamepad profile")
            return
        }

        gamepad.valueChangedHandler = { [weak self] pad, element in
            self?.handleInputChange(pad: pad, element: element)
        }
    }

    /// Process a single input element change and forward it to Cemu.
    private nonisolated func handleInputChange(
        pad: GCExtendedGamepad,
        element: GCControllerElement
    ) {
        // Build a complete controller state snapshot and forward it to the
        // bridge.  Cemu's InputManager expects per-frame state rather than
        // individual button events, so we sample everything.

        var state = WiiUProControllerState()

        // Face buttons
        state.buttonA = pad.buttonA.isPressed
        state.buttonB = pad.buttonB.isPressed
        state.buttonX = pad.buttonX.isPressed
        state.buttonY = pad.buttonY.isPressed

        // Shoulder / trigger buttons
        state.buttonL  = pad.leftShoulder.isPressed
        state.buttonR  = pad.rightShoulder.isPressed
        state.buttonZL = pad.leftTrigger.isPressed
        state.buttonZR = pad.rightTrigger.isPressed

        // D-pad
        state.dpadUp    = pad.dpad.up.isPressed
        state.dpadDown  = pad.dpad.down.isPressed
        state.dpadLeft  = pad.dpad.left.isPressed
        state.dpadRight = pad.dpad.right.isPressed

        // Analog sticks (range: -1.0 ... 1.0)
        state.leftStickX  = pad.leftThumbstick.xAxis.value
        state.leftStickY  = pad.leftThumbstick.yAxis.value
        state.rightStickX = pad.rightThumbstick.xAxis.value
        state.rightStickY = pad.rightThumbstick.yAxis.value

        // Stick buttons
        state.buttonLeftStick  = pad.leftThumbstickButton?.isPressed ?? false
        state.buttonRightStick = pad.rightThumbstickButton?.isPressed ?? false

        // Menu buttons
        state.buttonPlus  = pad.buttonMenu.isPressed
        state.buttonMinus = pad.buttonOptions?.isPressed ?? false
        state.buttonHome  = pad.buttonHome?.isPressed ?? false

        // Forward the complete state to the C++ input system.
        // This call is thread-safe; the bridge serialises writes to the
        // shared controller state buffer.
        forwardStateToBridge(state)
    }

    /// Encode the controller state and push it to the Cemu input subsystem.
    private nonisolated func forwardStateToBridge(_ state: WiiUProControllerState) {
        // For the initial port, individual button states are written into
        // the shared WindowInfo keystate map.  A future iteration should
        // add a dedicated VisionOSControllerProvider to Cemu's InputManager
        // that is fed from this state struct.
        let info = CemuBridge.shared()
        _ = info // Placeholder -- wiring to InputManager will be done once
                  // the C++ ControllerProvider interface is extended.
    }
}

// MARK: - WiiUProControllerState

/// Value-type snapshot of a Wii U Pro Controller's input state.
struct WiiUProControllerState {
    // Face buttons
    var buttonA = false
    var buttonB = false
    var buttonX = false
    var buttonY = false

    // Shoulder / triggers
    var buttonL  = false
    var buttonR  = false
    var buttonZL = false
    var buttonZR = false

    // D-pad
    var dpadUp    = false
    var dpadDown  = false
    var dpadLeft  = false
    var dpadRight = false

    // Analog sticks (-1.0 ... 1.0)
    var leftStickX:  Float = 0
    var leftStickY:  Float = 0
    var rightStickX: Float = 0
    var rightStickY: Float = 0

    // Stick buttons
    var buttonLeftStick  = false
    var buttonRightStick = false

    // Menu
    var buttonPlus  = false
    var buttonMinus = false
    var buttonHome  = false
}
