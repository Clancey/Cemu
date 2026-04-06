#if __ANDROID__

#include "AndroidCanvas.h"
#include "AndroidWindowSystem.h"
#include "gui/interface/WindowSystem.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include "Cafe/HW/Latte/Renderer/Renderer.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"

#include <android/native_window.h>
#include <android/log.h>

#define LOG_TAG "Cemu"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace AndroidWindowSystem
{
	AndroidCanvas::AndroidCanvas(bool is_main_window)
		: m_is_main_window(is_main_window)
	{
		LOGD("AndroidCanvas constructor: is_main_window=%s", is_main_window ? "true" : "false");
	}

	AndroidCanvas::~AndroidCanvas()
	{
		LOGD("AndroidCanvas destructor");
		Shutdown();
	}

	bool AndroidCanvas::Initialize(ANativeWindow* window)
	{
		if (m_initialized)
		{
			LOGD("AndroidCanvas already initialized");
			return true;
		}

		if (!window)
		{
			LOGE("AndroidCanvas::Initialize: null window provided");
			return false;
		}

		m_nativeWindow = window;

		// Get window dimensions
		m_size.x = ANativeWindow_getWidth(m_nativeWindow);
		m_size.y = ANativeWindow_getHeight(m_nativeWindow);
		LOGD("AndroidCanvas size: %dx%d", m_size.x, m_size.y);

		UpdateWindowHandleInfo();

		// Ensure Vulkan is available
		if (!g_vulkan_available)
		{
			LOGE("AndroidCanvas::Initialize: Vulkan not available");
			return false;
		}

		try
		{
			InitializeRenderer();
			m_initialized = true;
			LOGD("AndroidCanvas initialized successfully");
			return true;
		}
		catch (const std::exception& ex)
		{
			LOGE("AndroidCanvas::Initialize: Exception during renderer creation: %s", ex.what());
			return false;
		}
	}

	void AndroidCanvas::Shutdown()
	{
		if (m_initialized)
		{
			LOGD("AndroidCanvas::Shutdown");

			if (!m_is_main_window && m_rendererCreated)
			{
				// Stop pad rendering if this is a pad window
				VulkanRenderer* vkr = (VulkanRenderer*)g_renderer.get();
				if (vkr)
					vkr->StopUsingPadAndWait();
			}

			m_initialized = false;
			m_rendererCreated = false;
		}

		m_nativeWindow = nullptr;
	}

	void AndroidCanvas::OnWindowChanged(ANativeWindow* window)
	{
		LOGD("AndroidCanvas::OnWindowChanged");

		if (m_nativeWindow != window)
		{
			bool wasInitialized = m_initialized;

			// Shutdown current state
			if (m_initialized)
			{
				Shutdown();
			}

			// Initialize with new window
			if (window && wasInitialized)
			{
				Initialize(window);
			}
			else
			{
				m_nativeWindow = window;
				if (window)
				{
					m_size.x = ANativeWindow_getWidth(window);
					m_size.y = ANativeWindow_getHeight(window);
					UpdateWindowHandleInfo();
				}
			}
		}
	}

	void AndroidCanvas::OnResize(int32_t width, int32_t height)
	{
		LOGD("AndroidCanvas::OnResize: %dx%d", width, height);

		if (m_size.x != width || m_size.y != height)
		{
			m_size.x = width;
			m_size.y = height;
			UpdateWindowHandleInfo();

			// If renderer is initialized, it may need to handle the resize
			if (m_initialized && g_renderer)
			{
				// VulkanRenderer should automatically handle surface recreation
				// when the swapchain becomes invalid
			}
		}
	}

	void AndroidCanvas::InitializeRenderer()
	{
		LOGD("AndroidCanvas::InitializeRenderer");

		if (m_is_main_window && !g_renderer)
		{
			// Create the main Vulkan renderer
			g_renderer = std::make_unique<VulkanRenderer>();
			m_rendererCreated = true;
			LOGD("Created VulkanRenderer for main window");
		}

		// Initialize surface for this canvas
		auto vulkan_renderer = VulkanRenderer::GetInstance();
		if (vulkan_renderer)
		{
			vulkan_renderer->InitializeSurface({m_size.x, m_size.y}, m_is_main_window);
			LOGD("Initialized Vulkan surface: %dx%d, main_window=%s",
				  m_size.x, m_size.y, m_is_main_window ? "true" : "false");
		}
		else
		{
			throw std::runtime_error("Failed to get VulkanRenderer instance");
		}
	}

	void AndroidCanvas::UpdateWindowHandleInfo()
	{
		if (!m_nativeWindow)
			return;

		auto& windowInfo = WindowSystem::GetWindowInfo();
		auto& canvas = m_is_main_window ? windowInfo.canvas_main : windowInfo.canvas_pad;

		canvas.backend = WindowSystem::WindowHandleInfo::Backend::Android;
		canvas.surface = m_nativeWindow;
		canvas.display = nullptr; // Not used on Android
		canvas.nativeWindow = m_nativeWindow;

		if (m_is_main_window)
		{
			windowInfo.width = m_size.x;
			windowInfo.height = m_size.y;
			windowInfo.phys_width = m_size.x;
			windowInfo.phys_height = m_size.y;
		}
		else
		{
			windowInfo.pad_width = m_size.x;
			windowInfo.pad_height = m_size.y;
			windowInfo.phys_pad_width = m_size.x;
			windowInfo.phys_pad_height = m_size.y;
			windowInfo.pad_open = true;
		}

		LOGD("Updated window handle info: size=%dx%d, main_window=%s",
			  m_size.x, m_size.y, m_is_main_window ? "true" : "false");
	}

} // namespace AndroidWindowSystem

#endif // __ANDROID__