#pragma once

#if __ANDROID__

#include "util/math/vector2.h"
#include <memory>

struct ANativeWindow;

namespace AndroidWindowSystem
{
	/**
	 * Android implementation of render canvas
	 * Manages Vulkan rendering surface for Android native windows
	 * Note: Does NOT inherit from IRenderCanvas to avoid wxWidgets dependency
	 */
	class AndroidCanvas
	{
	public:
		explicit AndroidCanvas(bool is_main_window);
		virtual ~AndroidCanvas();

		// Android-specific lifecycle methods
		void OnWindowChanged(ANativeWindow* window);
		void OnResize(int32_t width, int32_t height);

		// Initialization
		bool Initialize(ANativeWindow* window);
		void Shutdown();

		// Window management
		ANativeWindow* GetNativeWindow() const { return m_nativeWindow; }
		Vector2i GetSize() const { return m_size; }

	protected:
		bool m_is_main_window;

	private:
		void InitializeRenderer();
		void UpdateWindowHandleInfo();

		ANativeWindow* m_nativeWindow = nullptr;
		Vector2i m_size{0, 0};
		bool m_initialized = false;
		bool m_rendererCreated = false;
	};

} // namespace AndroidWindowSystem

#endif // __ANDROID__
