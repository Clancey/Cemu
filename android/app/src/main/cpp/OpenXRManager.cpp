#include "OpenXRManager.h"
#include <android/log.h>
#include <array>
#include <cstring>
#include <cstdarg>
#include <algorithm>

#define LOG_TAG "OpenXRManager"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

// Default quad pose - 2 meters in front of user, facing forward
static constexpr XrPosef DefaultQuadPose = {
    .orientation = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f},
    .position = {.x = 0.0f, .y = 0.0f, .z = -2.0f}
};

// Default quad size - 2m wide, 1.125m tall (16:9 aspect ratio)
static constexpr XrExtent2Df DefaultQuadSize = {.width = 2.0f, .height = 1.125f};

OpenXRManager::OpenXRManager() = default;

OpenXRManager::~OpenXRManager()
{
    Shutdown();
}

bool OpenXRManager::CheckXrResult(XrResult result, const char* operation) const
{
    if (XR_SUCCEEDED(result)) {
        return true;
    }

    char errorBuffer[XR_MAX_RESULT_STRING_SIZE];
    if (m_instance != XR_NULL_HANDLE) {
        xrResultToString(m_instance, result, errorBuffer);
    } else {
        snprintf(errorBuffer, sizeof(errorBuffer), "XrResult=%d", result);
    }

    LOGE("OpenXR operation failed: %s - %s", operation, errorBuffer);
    return false;
}

void OpenXRManager::LogError(const char* fmt, ...) const
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    LOGE("%s", buf);
}

void OpenXRManager::LogInfo(const char* fmt, ...) const
{
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    LOGI("%s", buf);
}

bool OpenXRManager::Initialize(VkInstance vkInstance, VkPhysicalDevice vkPhysicalDevice,
                              VkDevice vkDevice, uint32_t queueFamilyIndex,
                              ANativeActivity* activity)
{
    LogInfo("Initializing OpenXR");

    // Store Vulkan objects
    m_vkInstance = vkInstance;
    m_vkPhysicalDevice = vkPhysicalDevice;
    m_vkDevice = vkDevice;
    m_queueFamilyIndex = queueFamilyIndex;
    m_activity = activity;

    if (!CreateInstance()) {
        LogError("Failed to create OpenXR instance");
        return false;
    }

    if (!LoadExtensions()) {
        LogError("Failed to load required OpenXR extensions");
        return false;
    }

    if (!GetSystem()) {
        LogError("Failed to get OpenXR system");
        return false;
    }

    if (!CreateSession()) {
        LogError("Failed to create OpenXR session");
        return false;
    }

    if (!CreateReferenceSpace()) {
        LogError("Failed to create reference space");
        return false;
    }

    LogInfo("OpenXR initialized successfully");
    return true;
}

bool OpenXRManager::CreateInstance()
{
    // Required extensions for Android + Vulkan
    std::vector<const char*> extensions = {
        XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
        XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME
    };

    XrInstanceCreateInfoAndroidKHR androidCreateInfo = {XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    if (m_activity) {
        androidCreateInfo.applicationVM = m_activity->vm;
        androidCreateInfo.applicationActivity = m_activity->clazz;
    }

    XrApplicationInfo appInfo = {};
    strncpy(appInfo.applicationName, "Cemu", XR_MAX_APPLICATION_NAME_SIZE - 1);
    appInfo.applicationVersion = 1;
    strncpy(appInfo.engineName, "Cemu Engine", XR_MAX_ENGINE_NAME_SIZE - 1);
    appInfo.engineVersion = 1;
    appInfo.apiVersion = XR_CURRENT_API_VERSION;

    XrInstanceCreateInfo instanceCreateInfo = {XR_TYPE_INSTANCE_CREATE_INFO};
    instanceCreateInfo.next = &androidCreateInfo;
    instanceCreateInfo.applicationInfo = appInfo;
    instanceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    instanceCreateInfo.enabledExtensionNames = extensions.data();

    XrResult result = xrCreateInstance(&instanceCreateInfo, &m_instance);
    return CheckXrResult(result, "xrCreateInstance");
}

bool OpenXRManager::LoadExtensions()
{
    XrResult result = xrGetInstanceProcAddr(m_instance, "xrGetVulkanGraphicsRequirements2KHR",
                                          reinterpret_cast<PFN_xrVoidFunction*>(&m_xrGetVulkanGraphicsRequirements2KHR));
    return CheckXrResult(result, "xrGetInstanceProcAddr(xrGetVulkanGraphicsRequirements2KHR)");
}

bool OpenXRManager::GetSystem()
{
    XrSystemGetInfo systemGetInfo = {XR_TYPE_SYSTEM_GET_INFO};
    systemGetInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    XrResult result = xrGetSystem(m_instance, &systemGetInfo, &m_systemId);
    if (!CheckXrResult(result, "xrGetSystem")) {
        return false;
    }

    // Check Vulkan requirements
    XrGraphicsRequirementsVulkanKHR vulkanRequirements = {XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR};
    result = m_xrGetVulkanGraphicsRequirements2KHR(m_instance, m_systemId, &vulkanRequirements);
    if (!CheckXrResult(result, "xrGetVulkanGraphicsRequirements2KHR")) {
        return false;
    }

    LogInfo("OpenXR system acquired successfully");
    return true;
}

bool OpenXRManager::CreateSession()
{
    XrGraphicsBindingVulkanKHR vulkanBinding = {XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR};
    vulkanBinding.instance = m_vkInstance;
    vulkanBinding.physicalDevice = m_vkPhysicalDevice;
    vulkanBinding.device = m_vkDevice;
    vulkanBinding.queueFamilyIndex = m_queueFamilyIndex;
    vulkanBinding.queueIndex = 0; // Assume first queue in family

    XrSessionCreateInfo sessionCreateInfo = {XR_TYPE_SESSION_CREATE_INFO};
    sessionCreateInfo.next = &vulkanBinding;
    sessionCreateInfo.systemId = m_systemId;

    XrResult result = xrCreateSession(m_instance, &sessionCreateInfo, &m_session);
    return CheckXrResult(result, "xrCreateSession");
}

bool OpenXRManager::CreateReferenceSpace()
{
    XrReferenceSpaceCreateInfo referenceSpaceCreateInfo = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    referenceSpaceCreateInfo.poseInReferenceSpace = {
        .orientation = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f},
        .position = {.x = 0.0f, .y = 0.0f, .z = 0.0f}
    };

    XrResult result = xrCreateReferenceSpace(m_session, &referenceSpaceCreateInfo, &m_localSpace);
    return CheckXrResult(result, "xrCreateReferenceSpace");
}

bool OpenXRManager::CreateSwapchain(uint32_t width, uint32_t height, VkFormat format)
{
    LogInfo("Creating OpenXR swapchain %dx%d, format=%d", width, height, format);

    m_swapchainWidth = width;
    m_swapchainHeight = height;
    m_swapchainFormat = format;

    XrSwapchainCreateInfo swapchainCreateInfo = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    swapchainCreateInfo.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainCreateInfo.format = static_cast<int64_t>(format);
    swapchainCreateInfo.sampleCount = 1;
    swapchainCreateInfo.width = width;
    swapchainCreateInfo.height = height;
    swapchainCreateInfo.faceCount = 1;
    swapchainCreateInfo.arraySize = 1;
    swapchainCreateInfo.mipCount = 1;

    XrResult result = xrCreateSwapchain(m_session, &swapchainCreateInfo, &m_swapchain);
    if (!CheckXrResult(result, "xrCreateSwapchain")) {
        return false;
    }

    // Get swapchain images
    uint32_t imageCount = 0;
    result = xrEnumerateSwapchainImages(m_swapchain, 0, &imageCount, nullptr);
    if (!CheckXrResult(result, "xrEnumerateSwapchainImages (count)")) {
        return false;
    }

    std::vector<XrSwapchainImageVulkanKHR> swapchainImageStructs(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
    result = xrEnumerateSwapchainImages(m_swapchain, imageCount, &imageCount,
                                       reinterpret_cast<XrSwapchainImageBaseHeader*>(swapchainImageStructs.data()));
    if (!CheckXrResult(result, "xrEnumerateSwapchainImages")) {
        return false;
    }

    // Extract VkImage handles
    m_swapchainImages.clear();
    m_swapchainImages.reserve(imageCount);
    for (const auto& imageStruct : swapchainImageStructs) {
        m_swapchainImages.push_back(imageStruct.image);
    }

    LogInfo("OpenXR swapchain created successfully with %d images", imageCount);
    return true;
}

void OpenXRManager::PollEvents()
{
    XrEventDataBuffer eventBuffer = {XR_TYPE_EVENT_DATA_BUFFER};

    while (XR_SUCCEEDED(xrPollEvent(m_instance, &eventBuffer))) {
        ProcessEvent(eventBuffer);
        eventBuffer = {XR_TYPE_EVENT_DATA_BUFFER}; // Reset for next event
    }
}

void OpenXRManager::ProcessEvent(const XrEventDataBuffer& eventData)
{
    switch (eventData.type) {
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
            const auto& sessionStateEvent = reinterpret_cast<const XrEventDataSessionStateChanged&>(eventData);
            m_sessionState = sessionStateEvent.state;

            LogInfo("OpenXR session state changed to %d", m_sessionState);

            switch (m_sessionState) {
                case XR_SESSION_STATE_READY: {
                    XrSessionBeginInfo sessionBeginInfo = {XR_TYPE_SESSION_BEGIN_INFO};
                    sessionBeginInfo.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    XrResult result = xrBeginSession(m_session, &sessionBeginInfo);
                    if (CheckXrResult(result, "xrBeginSession")) {
                        m_sessionRunning = true;
                        LogInfo("OpenXR session started");
                    }
                    break;
                }
                case XR_SESSION_STATE_STOPPING: {
                    m_sessionRunning = false;
                    XrResult result = xrEndSession(m_session);
                    CheckXrResult(result, "xrEndSession");
                    LogInfo("OpenXR session stopped");
                    break;
                }
                case XR_SESSION_STATE_EXITING:
                case XR_SESSION_STATE_LOSS_PENDING: {
                    m_sessionRunning = false;
                    LogInfo("OpenXR session exiting");
                    break;
                }
                default:
                    break;
            }
            break;
        }
        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
            LogError("OpenXR instance loss pending");
            break;
        }
        default:
            break;
    }
}

bool OpenXRManager::BeginFrame()
{
    if (!m_session || !m_sessionRunning) {
        return false;
    }

    // Wait for the next frame
    XrFrameWaitInfo frameWaitInfo = {XR_TYPE_FRAME_WAIT_INFO};
    XrResult result = xrWaitFrame(m_session, &frameWaitInfo, &m_frameState);
    if (!CheckXrResult(result, "xrWaitFrame")) {
        return false;
    }

    // Begin the frame
    XrFrameBeginInfo frameBeginInfo = {XR_TYPE_FRAME_BEGIN_INFO};
    result = xrBeginFrame(m_session, &frameBeginInfo);
    if (!CheckXrResult(result, "xrBeginFrame")) {
        return false;
    }

    m_frameActive = true;
    return m_frameState.shouldRender;
}

uint32_t OpenXRManager::AcquireSwapchainImage()
{
    if (!m_swapchain || !m_frameActive) {
        return UINT32_MAX;
    }

    XrSwapchainImageAcquireInfo acquireInfo = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrResult result = xrAcquireSwapchainImage(m_swapchain, &acquireInfo, &m_acquiredImageIndex);
    if (!CheckXrResult(result, "xrAcquireSwapchainImage")) {
        return UINT32_MAX;
    }

    XrSwapchainImageWaitInfo waitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    result = xrWaitSwapchainImage(m_swapchain, &waitInfo);
    if (!CheckXrResult(result, "xrWaitSwapchainImage")) {
        return UINT32_MAX;
    }

    return m_acquiredImageIndex;
}

void OpenXRManager::ReleaseSwapchainImage()
{
    if (!m_swapchain || m_acquiredImageIndex == UINT32_MAX) {
        return;
    }

    XrSwapchainImageReleaseInfo releaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    XrResult result = xrReleaseSwapchainImage(m_swapchain, &releaseInfo);
    CheckXrResult(result, "xrReleaseSwapchainImage");

    m_acquiredImageIndex = UINT32_MAX;
}

bool OpenXRManager::EndFrame(XrPosef quadPose, XrExtent2Df quadSize)
{
    if (!m_session || !m_frameActive) {
        return false;
    }

    std::vector<XrCompositionLayerBaseHeader*> layers;

    // Create quad layer for the Cemu TV output
    XrCompositionLayerQuad quadLayer = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    if (m_swapchain && m_frameState.shouldRender && IsSessionRunning()) {
        quadLayer.layerFlags = 0;
        quadLayer.space = m_localSpace;
        quadLayer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
        quadLayer.subImage.swapchain = m_swapchain;
        quadLayer.subImage.imageRect.offset = {0, 0};
        quadLayer.subImage.imageRect.extent = {static_cast<int32_t>(m_swapchainWidth),
                                              static_cast<int32_t>(m_swapchainHeight)};
        quadLayer.pose = quadPose;
        quadLayer.size = quadSize;

        layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&quadLayer));
    }

    // End the frame
    XrFrameEndInfo frameEndInfo = {XR_TYPE_FRAME_END_INFO};
    frameEndInfo.displayTime = m_frameState.predictedDisplayTime;
    frameEndInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    frameEndInfo.layerCount = static_cast<uint32_t>(layers.size());
    frameEndInfo.layers = layers.data();

    XrResult result = xrEndFrame(m_session, &frameEndInfo);
    m_frameActive = false;

    return CheckXrResult(result, "xrEndFrame");
}

void OpenXRManager::Shutdown()
{
    LogInfo("Shutting down OpenXR");

    if (m_swapchain != XR_NULL_HANDLE) {
        xrDestroySwapchain(m_swapchain);
        m_swapchain = XR_NULL_HANDLE;
    }

    if (m_localSpace != XR_NULL_HANDLE) {
        xrDestroySpace(m_localSpace);
        m_localSpace = XR_NULL_HANDLE;
    }

    if (m_session != XR_NULL_HANDLE) {
        if (m_sessionRunning) {
            xrEndSession(m_session);
            m_sessionRunning = false;
        }
        xrDestroySession(m_session);
        m_session = XR_NULL_HANDLE;
    }

    if (m_instance != XR_NULL_HANDLE) {
        xrDestroyInstance(m_instance);
        m_instance = XR_NULL_HANDLE;
    }

    // Clear state
    m_sessionState = XR_SESSION_STATE_UNKNOWN;
    m_frameActive = false;
    m_swapchainImages.clear();
    m_swapchainWidth = m_swapchainHeight = 0;
    m_swapchainFormat = VK_FORMAT_UNDEFINED;
    m_acquiredImageIndex = UINT32_MAX;

    LogInfo("OpenXR shutdown complete");
}