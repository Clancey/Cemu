import UIKit
import Metal
import QuartzCore

/// A thin Swift wrapper around ``MetalHostView`` (the Objective-C++ UIView
/// subclass) that handles the initial Metal device setup and forwards layout
/// changes to the emulator bridge.
///
/// In the typical flow the ``MetalEmulatorView`` (UIViewRepresentable) creates
/// this view, and the coordinator sets the ``emulatorCore`` property so that
/// the layer is provided to C++ once layout completes.
final class CemuMetalView: MetalHostView {

    /// The emulator core that owns this view's layer.
    weak var core: EmulatorCore?

    /// Whether the CAMetalLayer has been handed off to the bridge.
    private var hasProvidedLayer = false

    // MARK: - Initialisation

    /// Create a new Metal-backed view, optionally pre-configured with a device.
    init(device: MTLDevice? = nil) {
        super.init(frame: .zero)
        if let device {
            configure(with: device)
        }
        backgroundColor = .black
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) {
        fatalError("CemuMetalView does not support Interface Builder.")
    }

    // MARK: - Layout

    override func layoutSubviews() {
        super.layoutSubviews()

        guard let core else { return }

        if !hasProvidedLayer {
            provideLayerIfReady(core: core)
        } else {
            notifyResize(core: core)
        }
    }

    // MARK: - Private

    private func provideLayerIfReady(core: EmulatorCore) {
        let size = bounds.size
        guard size.width > 0, size.height > 0 else { return }

        let scale = effectiveScale
        let pw = Int(size.width  * scale)
        let ph = Int(size.height * scale)

        core.setMainDisplayLayer(metalLayer, width: pw, height: ph)
        hasProvidedLayer = true
    }

    private func notifyResize(core: EmulatorCore) {
        let size = bounds.size
        let scale = effectiveScale
        let pw = Int(size.width  * scale)
        let ph = Int(size.height * scale)
        core.resizeMainDisplay(width: pw, height: ph)
    }

    private var effectiveScale: CGFloat {
        let s = traitCollection.displayScale
        return s > 0 ? s : 1.0
    }
}
