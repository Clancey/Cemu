import SwiftUI
import UIKit
import Metal
import QuartzCore

/// UIViewRepresentable wrapper that hosts the Metal rendering surface.
///
/// The view creates a ``MetalHostView`` (UIView subclass backed by
/// CAMetalLayer), passes the layer to the C++ bridge via ``EmulatorCore``,
/// and forwards resize events.
struct MetalEmulatorView: UIViewRepresentable {

    /// The shared emulator state manager.
    let core: EmulatorCore

    // MARK: - UIViewRepresentable

    func makeUIView(context: Context) -> MetalHostView {
        let view = MetalHostView(frame: .zero)

        // Obtain the system default Metal device and configure the layer.
        if let device = MTLCreateSystemDefaultDevice() {
            view.configure(with: device)
        }

        return view
    }

    func updateUIView(_ uiView: MetalHostView, context: Context) {
        // updateUIView is called whenever SwiftUI state changes.
        // We use it to ensure the bridge has the current layer and size.
    }

    func makeCoordinator() -> Coordinator {
        Coordinator(core: core)
    }

    func sizeThatFits(
        _ proposal: ProposedViewSize,
        uiView: MetalHostView,
        context: Context
    ) -> CGSize? {
        // Prefer the proposed size (the parent constrains us with aspectRatio).
        return nil
    }

    // MARK: - Coordinator

    /// Coordinator that observes layout changes and forwards the CAMetalLayer
    /// to the emulator bridge once the view has a non-zero size.
    final class Coordinator: NSObject {
        let core: EmulatorCore
        private var layerProvided = false

        init(core: EmulatorCore) {
            self.core = core
        }
    }

    // MARK: - Layout Notification

    /// Called by SwiftUI after layout.  We hook into the UIView lifecycle
    /// instead via MetalHostView.layoutSubviews to forward size changes.
    /// The initial layer handoff happens in makeUIView.
    static func dismantleUIView(_ uiView: MetalHostView, coordinator: Coordinator) {
        // Clean-up when the view is removed.
        coordinator.core.stop()
    }
}

// MARK: - MetalHostView Swift Extension

extension MetalHostView {

    /// Stored property keys for the associated EmulatorCore reference.
    private static var coreKey: UInt8 = 0
    private static var layerProvidedKey: UInt8 = 0

    /// The emulator core associated with this view (set externally).
    var emulatorCore: EmulatorCore? {
        get { objc_getAssociatedObject(self, &Self.coreKey) as? EmulatorCore }
        set { objc_setAssociatedObject(self, &Self.coreKey, newValue, .OBJC_ASSOCIATION_RETAIN_NONATOMIC) }
    }

    private var layerProvided: Bool {
        get { (objc_getAssociatedObject(self, &Self.layerProvidedKey) as? Bool) ?? false }
        set { objc_setAssociatedObject(self, &Self.layerProvidedKey, newValue, .OBJC_ASSOCIATION_RETAIN_NONATOMIC) }
    }

    /// Provide the layer to the bridge when layout produces a valid size.
    func provideLayerToBridgeIfNeeded() {
        guard !layerProvided else { return }
        guard let core = emulatorCore else { return }

        let size = bounds.size
        guard size.width > 0, size.height > 0 else { return }

        let scale = traitCollection.displayScale > 0 ? traitCollection.displayScale : 1.0
        let pixelWidth  = Int(size.width  * scale)
        let pixelHeight = Int(size.height * scale)

        core.setMainDisplayLayer(metalLayer, width: pixelWidth, height: pixelHeight)
        layerProvided = true
    }

    /// Notify the bridge of a resize.
    func notifyBridgeOfResize() {
        guard layerProvided, let core = emulatorCore else { return }
        let size = bounds.size
        let scale = traitCollection.displayScale > 0 ? traitCollection.displayScale : 1.0
        let pixelWidth  = Int(size.width  * scale)
        let pixelHeight = Int(size.height * scale)
        core.resizeMainDisplay(width: pixelWidth, height: pixelHeight)
    }
}
