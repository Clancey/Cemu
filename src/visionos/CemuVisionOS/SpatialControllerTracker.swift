import Foundation
import GameController
import RealityKit
import simd
import os

/// Tracks PSVR2 spatial controllers via ARKit and raycasts against the
/// emulator screen to produce Wiimote-style pointing coordinates.
@available(visionOS 26.0, *)
@MainActor
final class SpatialControllerTracker {

    private let logger = Logger(subsystem: "org.cemu.CemuVision", category: "SpatialTracker")
    private let bridge = CemuBridge.shared()

    private var trackingSession: SpatialTrackingSession?
    private var rightAnchor: AnchorEntity?
    private var leftAnchor: AnchorEntity?
    private var rootEntity: Entity?
    private var displayLink: CADisplayLink?

    // Screen plane definition (set from the MetalEmulatorView's world position)
    // Default: screen 1.5m in front, centered, 854x480 aspect
    private var screenCenter = simd_float3(0, 1.2, -1.5)
    private var screenNormal = simd_float3(0, 0, 1) // facing towards user
    private var screenRight = simd_float3(1, 0, 0)
    private var screenUp = simd_float3(0, 1, 0)
    private var screenWidthMeters: Float = 1.0
    private var screenHeightMeters: Float = 1.0 / (854.0 / 480.0)

    // Last computed pointing position (0-1024, 0-768 for Wiimote DPD)
    private(set) var pointingX: Float = 512.0
    private(set) var pointingY: Float = 384.0
    private(set) var isPointing: Bool = false

    func start() async {
        let session = SpatialTrackingSession()
        let config = SpatialTrackingSession.Configuration(tracking: [.accessory])
        let unavailable = await session.run(config)
        if let unavailable, unavailable.anchor.contains(.accessory) {
            logger.warning("Accessory tracking not available")
            return
        }
        trackingSession = session
        logger.info("Spatial tracking session started")

        // Start polling controller transforms
        startDisplayLink()
    }

    func stop() {
        displayLink?.invalidate()
        displayLink = nil
        trackingSession = nil
        rightAnchor = nil
        leftAnchor = nil
    }

    /// Set the screen plane from the emulator view's world-space bounds.
    func setScreenPlane(center: simd_float3, normal: simd_float3, right: simd_float3, up: simd_float3, width: Float, height: Float) {
        screenCenter = center
        screenNormal = normal
        screenRight = right
        screenUp = up
        screenWidthMeters = width
        screenHeightMeters = height
    }

    /// Attach to a spatial controller for tracking.
    func trackController(_ controller: GCController) async {
        do {
            let source = try await AnchoringComponent.AccessoryAnchoringSource(device: controller)

            // Track the "aim" point (where the controller points)
            if let aimLocation = source.locationName(named: "aim") {
                let anchor = AnchorEntity(
                    .accessory(from: source, location: aimLocation),
                    trackingMode: .predicted
                )
                rightAnchor = anchor
                logger.info("Tracking spatial controller aim point")
            } else if let firstLocation = source.accessoryLocations.first {
                // Fallback to first available location
                let anchor = AnchorEntity(
                    .accessory(from: source, location: firstLocation),
                    trackingMode: .predicted
                )
                rightAnchor = anchor
                logger.info("Tracking spatial controller (fallback location)")
            }
        } catch {
            logger.error("Failed to create accessory anchor: \(error)")
        }
    }

    // MARK: - Polling

    private func startDisplayLink() {
        displayLink = CADisplayLink(target: self, selector: #selector(pollTransforms))
        displayLink?.preferredFrameRateRange = .init(minimum: 30, maximum: 90, preferred: 60)
        displayLink?.add(to: .main, forMode: .common)
    }

    @objc private func pollTransforms() {
        guard let anchor = rightAnchor else { return }

        // Get the anchor's world transform
        let transform = anchor.transformMatrix(relativeTo: nil)
        let position = simd_float3(transform.columns.3.x, transform.columns.3.y, transform.columns.3.z)

        // Forward direction from transform (negative Z in Metal/RealityKit convention)
        let forward = -simd_float3(transform.columns.2.x, transform.columns.2.y, transform.columns.2.z)

        // Raycast against the screen plane
        if let hitPoint = rayPlaneIntersection(
            rayOrigin: position,
            rayDirection: simd_normalize(forward),
            planePoint: screenCenter,
            planeNormal: screenNormal
        ) {
            // Convert world hit point to screen-local coordinates
            let localOffset = hitPoint - screenCenter
            let u = simd_dot(localOffset, screenRight) / screenWidthMeters  // -0.5 to 0.5
            let v = simd_dot(localOffset, screenUp) / screenHeightMeters    // -0.5 to 0.5

            // Convert to Wiimote DPD coordinates (0-1024, 0-768)
            pointingX = (u + 0.5) * 1024.0
            pointingY = (0.5 - v) * 768.0 // flip Y
            pointingX = min(max(pointingX, 0), 1024)
            pointingY = min(max(pointingY, 0), 768)
            isPointing = true

            // Send to C++ side
            bridge.onControllerAxisEvent(10, value: pointingX)  // axis 10 = pointing X
            bridge.onControllerAxisEvent(11, value: pointingY)  // axis 11 = pointing Y
        } else {
            isPointing = false
        }
    }

    // MARK: - Math

    private func rayPlaneIntersection(
        rayOrigin: simd_float3,
        rayDirection: simd_float3,
        planePoint: simd_float3,
        planeNormal: simd_float3
    ) -> simd_float3? {
        let denom = simd_dot(rayDirection, planeNormal)
        guard abs(denom) > 1e-6 else { return nil }

        let t = simd_dot(planePoint - rayOrigin, planeNormal) / denom
        guard t > 0 else { return nil }

        return rayOrigin + t * rayDirection
    }
}

/// Fallback for pre-visionOS 26 — no spatial tracking
@MainActor
final class SpatialControllerTrackerFallback {
    func start() async {}
    func stop() {}
    func trackController(_ controller: GCController) async {}
}
