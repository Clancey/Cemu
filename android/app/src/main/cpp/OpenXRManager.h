#pragma once

#include <jni.h>
#include <vulkan/vulkan.h>
#include <memory>
#include <vector>
#include <functional>
#include <atomic>
#include <dlfcn.h>

// Dynamic OpenXR loading - declare functions as pointers instead of extern
#define XR_NO_PROTOTYPES
#define XR_USE_PLATFORM_ANDROID 1
#define XR_USE_GRAPHICS_API_VULKAN 1
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <android/native_activity.h>

/**
 * OpenXRManager handles OpenXR session management and swapchain creation for
 * Cemu on Meta Quest. Creates a flat panel view in VR space to display the
 * Wii U TV output.
 *
 * This class manages:
 * - OpenXR instance, system, and session creation
 * - Vulkan integration via XR_KHR_vulkan_enable2
 * - Swapchain creation and image management
 * - Frame synchronization (xrWaitFrame/xrBeginFrame/xrEndFrame)
 * - Quad composition layer for flat panel rendering
 * - Session state tracking and lifecycle management
 */
class OpenXRManager
{
public:
    OpenXRManager();
    ~OpenXRManager();

    // Prevent copying
    OpenXRManager(const OpenXRManager&) = delete;
    OpenXRManager& operator=(const OpenXRManager&) = delete;

    /**
     * Initialize OpenXR and create Vulkan objects through OpenXR.
     * Must be called before any other operations.
     *
     * @param activity          Android native activity (for instance creation)
     * @return true on success, false on failure
     */
    bool Initialize(jobject activityObject = nullptr);

    /**
     * Create OpenXR swapchain for rendering.
     *
     * @param width    Swapchain width in pixels
     * @param height   Swapchain height in pixels
     * @param format   Vulkan format (typically VK_FORMAT_R8G8B8A8_SRGB)
     * @return true on success, false on failure
     */
    bool CreateSwapchain(uint32_t width, uint32_t height, VkFormat format);

    /**
     * Get the Vulkan images from the OpenXR swapchain.
     * Only valid after successful CreateSwapchain call.
     *
     * @return vector of VkImage handles from swapchain
     */
    std::vector<VkImage> GetSwapchainImages() const { return m_swapchainImages; }
    VkImage GetSwapchainImage(uint32_t index) const { return index < m_swapchainImages.size() ? m_swapchainImages[index] : VK_NULL_HANDLE; }

    /**
     * Get the current swapchain dimensions.
     */
    void GetSwapchainSize(uint32_t& width, uint32_t& height) const
    {
        width = m_swapchainWidth;
        height = m_swapchainHeight;
    }

    /**
     * Check if OpenXR session is in running state.
     * Only render when this returns true.
     */
    bool IsSessionRunning() const { return m_sessionRunning; }

    /**
     * Begin an OpenXR frame. Call before rendering.
     * This handles xrWaitFrame and xrBeginFrame.
     *
     * @return true if rendering should proceed, false if frame should be skipped
     */
    bool BeginFrame();

    /**
     * Acquire next swapchain image for rendering.
     * Must be called after BeginFrame and before rendering.
     *
     * @return swapchain image index, or UINT32_MAX on error
     */
    uint32_t AcquireSwapchainImage();

    /**
     * Release the acquired swapchain image after rendering.
     * Must be called after rendering is complete.
     */
    void ReleaseSwapchainImage();

    /**
     * End the OpenXR frame and submit for composition.
     * Call after rendering and releasing swapchain image.
     *
     * @param quadPose  Pose (position/orientation) of the quad in VR space
     * @param quadSize  Size of the quad in meters (width, height)
     * @return true on success
     */
    bool EndFrame(XrPosef quadPose, XrExtent2Df quadSize);

    /**
     * Shutdown OpenXR and cleanup resources.
     */
    void Shutdown();

    /**
     * Get OpenXR-created Vulkan objects.
     */
    VkInstance GetVkInstance() const { return m_vkInstance; }
    VkPhysicalDevice GetVkPhysicalDevice() const { return m_vkPhysicalDevice; }
    VkDevice GetVkDevice() const { return m_vkDevice; }
    uint32_t GetQueueFamilyIndex() const { return m_queueFamilyIndex; }

    /**
     * Get the OpenXR instance handle.
     */
    XrInstance GetInstance() const { return m_instance; }

    /**
     * Get the OpenXR session handle.
     */
    XrSession GetSession() const { return m_session; }

    /**
     * Poll OpenXR events and update session state.
     * Should be called regularly (e.g., once per frame).
     */
    void PollEvents();

private:
    // OpenXR state
    XrInstance m_instance = XR_NULL_HANDLE;
    XrSystemId m_systemId = XR_NULL_SYSTEM_ID;
    XrSession m_session = XR_NULL_HANDLE;
    XrSpace m_localSpace = XR_NULL_HANDLE;
    XrSwapchain m_swapchain = XR_NULL_HANDLE;
    XrSessionState m_sessionState = XR_SESSION_STATE_UNKNOWN;
    bool m_sessionRunning = false;

    // Frame synchronization
    XrFrameState m_frameState = {XR_TYPE_FRAME_STATE};
    bool m_frameActive = false;

    // Swapchain data
    std::vector<VkImage> m_swapchainImages;
    uint32_t m_swapchainWidth = 0;
    uint32_t m_swapchainHeight = 0;
    VkFormat m_swapchainFormat = VK_FORMAT_UNDEFINED;
    uint32_t m_acquiredImageIndex = UINT32_MAX;

    // Vulkan objects (owned by this class when created through OpenXR)
    VkInstance m_vkInstance = VK_NULL_HANDLE;
    VkPhysicalDevice m_vkPhysicalDevice = VK_NULL_HANDLE;
    VkDevice m_vkDevice = VK_NULL_HANDLE;
    uint32_t m_queueFamilyIndex = 0;
    bool m_ownVulkanObjects = false; // true if we created Vulkan objects, false if externally provided

    // Android
    jobject m_activityObject = nullptr;

    // Dynamic loading state
    void* m_openxrLibrary = nullptr;
    bool m_openxrLoaded = false;

    // Core OpenXR function pointers (loaded via dlsym)
    PFN_xrGetInstanceProcAddr p_xrGetInstanceProcAddr = nullptr;

    // Instance-level function pointers (loaded via xrGetInstanceProcAddr)
    PFN_xrCreateInstance p_xrCreateInstance = nullptr;
    PFN_xrDestroyInstance p_xrDestroyInstance = nullptr;
    PFN_xrGetSystem p_xrGetSystem = nullptr;
    PFN_xrEnumerateInstanceExtensionProperties p_xrEnumerateInstanceExtensionProperties = nullptr;
    PFN_xrCreateSession p_xrCreateSession = nullptr;
    PFN_xrDestroySession p_xrDestroySession = nullptr;
    PFN_xrBeginSession p_xrBeginSession = nullptr;
    PFN_xrEndSession p_xrEndSession = nullptr;
    PFN_xrCreateReferenceSpace p_xrCreateReferenceSpace = nullptr;
    PFN_xrDestroySpace p_xrDestroySpace = nullptr;
    PFN_xrCreateSwapchain p_xrCreateSwapchain = nullptr;
    PFN_xrDestroySwapchain p_xrDestroySwapchain = nullptr;
    PFN_xrEnumerateSwapchainImages p_xrEnumerateSwapchainImages = nullptr;
    PFN_xrAcquireSwapchainImage p_xrAcquireSwapchainImage = nullptr;
    PFN_xrWaitSwapchainImage p_xrWaitSwapchainImage = nullptr;
    PFN_xrReleaseSwapchainImage p_xrReleaseSwapchainImage = nullptr;
    PFN_xrWaitFrame p_xrWaitFrame = nullptr;
    PFN_xrBeginFrame p_xrBeginFrame = nullptr;
    PFN_xrEndFrame p_xrEndFrame = nullptr;
    PFN_xrPollEvent p_xrPollEvent = nullptr;
    PFN_xrResultToString p_xrResultToString = nullptr;

    // Extension function pointers
    PFN_xrGetVulkanGraphicsRequirements2KHR m_xrGetVulkanGraphicsRequirements2KHR = nullptr;
    PFN_xrCreateVulkanInstanceKHR m_xrCreateVulkanInstanceKHR = nullptr;
    PFN_xrCreateVulkanDeviceKHR m_xrCreateVulkanDeviceKHR = nullptr;
    PFN_xrGetVulkanGraphicsDevice2KHR m_xrGetVulkanGraphicsDevice2KHR = nullptr;

    // Helper methods
    bool LoadOpenXRLibrary();
    bool LoadInstanceFunctions();
    bool CreateInstance();
    bool GetSystem();
    bool CreateVulkanObjects();
    bool CreateSession();
    bool CreateReferenceSpace();
    bool LoadExtensions();
    void ProcessEvent(const XrEventDataBuffer& eventData);

    // Error checking
    bool CheckXrResult(XrResult result, const char* operation) const;
    void LogError(const char* fmt, ...) const;
    void LogInfo(const char* fmt, ...) const;
};