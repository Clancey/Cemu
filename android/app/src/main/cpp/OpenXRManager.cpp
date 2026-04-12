#include "OpenXRManager.h"
#include "JNIUtils.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#include <android/log.h>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdarg>
#include <algorithm>
#include <dlfcn.h>

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

namespace
{
constexpr float kGravityMetersPerSecondSquared = 9.80665f;
constexpr float kPointerPartialMargin = 0.2f;
constexpr float kTwoPi = 6.28318530718f;

XrVector3f Add(const XrVector3f& lhs, const XrVector3f& rhs)
{
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z};
}

XrVector3f Subtract(const XrVector3f& lhs, const XrVector3f& rhs)
{
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

XrVector3f Scale(const XrVector3f& value, float scalar)
{
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

float Dot(const XrVector3f& lhs, const XrVector3f& rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

XrVector3f Cross(const XrVector3f& lhs, const XrVector3f& rhs)
{
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

XrQuaternionf Conjugate(const XrQuaternionf& quaternion)
{
    return {-quaternion.x, -quaternion.y, -quaternion.z, quaternion.w};
}

XrVector3f RotateVector(const XrQuaternionf& quaternion, const XrVector3f& vector)
{
    const XrVector3f u{quaternion.x, quaternion.y, quaternion.z};
    const float s = quaternion.w;

    return Add(
        Add(
            Scale(u, 2.0f * Dot(u, vector)),
            Scale(vector, (s * s) - Dot(u, u))
        ),
        Scale(Cross(u, vector), 2.0f * s)
    );
}

bool HasValidPose(const XrSpaceLocation& location)
{
    return (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0 &&
           (location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
}

void QuaternionToYawPitchRoll(const XrQuaternionf& quaternion, float& yaw, float& pitch, float& roll)
{
    const float sinr_cosp = 2.0f * (quaternion.w * quaternion.x + quaternion.y * quaternion.z);
    const float cosr_cosp = 1.0f - 2.0f * (quaternion.x * quaternion.x + quaternion.y * quaternion.y);
    roll = std::atan2(sinr_cosp, cosr_cosp);

    const float sinp = 2.0f * (quaternion.w * quaternion.y - quaternion.z * quaternion.x);
    if (std::abs(sinp) >= 1.0f) {
        pitch = std::copysign(3.14159265359f / 2.0f, sinp);
    } else {
        pitch = std::asin(sinp);
    }

    const float siny_cosp = 2.0f * (quaternion.w * quaternion.z + quaternion.x * quaternion.y);
    const float cosy_cosp = 1.0f - 2.0f * (quaternion.y * quaternion.y + quaternion.z * quaternion.z);
    yaw = std::atan2(siny_cosp, cosy_cosp);
}
}

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
    if (m_instance != XR_NULL_HANDLE && p_xrResultToString) {
        p_xrResultToString(m_instance, result, errorBuffer);
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

bool OpenXRManager::Initialize(jobject activityObject)
{
    LogInfo("Initializing OpenXR with dynamic loading");

    m_activityObject = activityObject;

    if (!LoadOpenXRLibrary()) {
        LogError("Failed to load OpenXR library");
        return false;
    }

    if (!CreateInstance()) {
        LogError("Failed to create OpenXR instance");
        return false;
    }

    if (!LoadInstanceFunctions()) {
        LogError("Failed to load OpenXR instance functions");
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

    if (!CreateVulkanObjects()) {
        LogError("Failed to create Vulkan objects through OpenXR");
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

    if (!InitializeInputActions()) {
        LogError("Failed to initialize OpenXR input actions");
        return false;
    }

    LogInfo("OpenXR initialized successfully");
    return true;
}

bool OpenXRManager::InitializeVulkanOnly(jobject activityObject)
{
    LogInfo("Initializing OpenXR (Vulkan objects only, no session)");
    m_activityObject = activityObject;

    if (!LoadOpenXRLibrary()) { LogError("Failed to load OpenXR library"); return false; }
    if (!CreateInstance()) { LogError("Failed to create OpenXR instance"); return false; }
    if (!LoadInstanceFunctions()) { LogError("Failed to load OpenXR instance functions"); return false; }
    if (!LoadExtensions()) { LogError("Failed to load required OpenXR extensions"); return false; }
    if (!GetSystem()) { LogError("Failed to get OpenXR system"); return false; }
    if (!CreateVulkanObjects()) { LogError("Failed to create Vulkan objects through OpenXR"); return false; }

    LogInfo("OpenXR Vulkan objects created (session NOT started)");
    return true;
}

bool OpenXRManager::StartSession()
{
    LogInfo("Starting OpenXR session (phase 2)");

    if (!CreateSession()) { LogError("Failed to create OpenXR session"); return false; }
    if (!CreateReferenceSpace()) { LogError("Failed to create reference space"); return false; }
    if (!InitializeInputActions()) { LogError("Failed to initialize input actions"); return false; }

    // Create swapchain for frame submission
    if (!CreateSwapchain(1920, 1080, VK_FORMAT_R8G8B8A8_SRGB)) {
        LogError("Warning: swapchain creation failed");
    }

    // Don't poll events yet — let caller decide when to begin session
    // (PollEvents triggers session READY → begin, which starts the frame deadline)
    LogInfo("OpenXR session created (not yet begun — call PollEvents when ready)");
    return true;
}

bool OpenXRManager::LoadOpenXRLibrary()
{
    LogInfo("Loading OpenXR library dynamically");

    m_openxrLibrary = dlopen("libopenxr_loader.so", RTLD_NOW);
    if (!m_openxrLibrary) {
        LogError("Failed to load libopenxr_loader.so: %s", dlerror());
        return false;
    }

    // Load the core function that loads all other functions
    p_xrGetInstanceProcAddr = reinterpret_cast<PFN_xrGetInstanceProcAddr>(
        dlsym(m_openxrLibrary, "xrGetInstanceProcAddr"));
    if (!p_xrGetInstanceProcAddr) {
        LogError("Failed to load xrGetInstanceProcAddr: %s", dlerror());
        dlclose(m_openxrLibrary);
        m_openxrLibrary = nullptr;
        return false;
    }

    // Load pre-instance functions directly via xrGetInstanceProcAddr with XR_NULL_HANDLE
    XrResult result = p_xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrCreateInstance",
                                            reinterpret_cast<PFN_xrVoidFunction*>(&p_xrCreateInstance));
    if (XR_FAILED(result) || !p_xrCreateInstance) {
        LogError("Failed to load xrCreateInstance");
        dlclose(m_openxrLibrary);
        m_openxrLibrary = nullptr;
        return false;
    }

    result = p_xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrEnumerateInstanceExtensionProperties",
                                   reinterpret_cast<PFN_xrVoidFunction*>(&p_xrEnumerateInstanceExtensionProperties));
    if (XR_FAILED(result) || !p_xrEnumerateInstanceExtensionProperties) {
        LogError("Failed to load xrEnumerateInstanceExtensionProperties");
        dlclose(m_openxrLibrary);
        m_openxrLibrary = nullptr;
        return false;
    }

    // Initialize the loader with Android context BEFORE any other XR call
    // This is required for the Khronos loader to discover the Meta runtime
    PFN_xrInitializeLoaderKHR xrInitializeLoaderKHR = nullptr;
    p_xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                           reinterpret_cast<PFN_xrVoidFunction*>(&xrInitializeLoaderKHR));
    if (xrInitializeLoaderKHR) {
        XrLoaderInitInfoAndroidKHR loaderInitInfo = {XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
        loaderInitInfo.applicationVM = JNIUtils::g_jvm;
        // Use the Activity reference directly — required for Quest VR session
        loaderInitInfo.applicationContext = m_activityObject;
        XrResult initResult = xrInitializeLoaderKHR(reinterpret_cast<XrLoaderInitInfoBaseHeaderKHR*>(&loaderInitInfo));
        if (XR_FAILED(initResult)) {
            LogError("xrInitializeLoaderKHR failed: %d", initResult);
        } else {
            LogInfo("OpenXR loader initialized with Android context");
        }
    } else {
        LogInfo("xrInitializeLoaderKHR not available (may not be needed)");
    }

    m_openxrLoaded = true;
    LogInfo("OpenXR library loaded successfully");
    return true;
}

bool OpenXRManager::LoadInstanceFunctions()
{
    if (!m_instance || !p_xrGetInstanceProcAddr) {
        LogError("Cannot load instance functions - no instance or proc addr");
        return false;
    }

    LogInfo("Loading OpenXR instance functions");

#define LOAD_XR_FUNCTION(name) \
    do { \
        XrResult result = p_xrGetInstanceProcAddr(m_instance, #name, \
                                                reinterpret_cast<PFN_xrVoidFunction*>(&p_##name)); \
        if (XR_FAILED(result) || !p_##name) { \
            LogError("Failed to load " #name); \
            return false; \
        } \
    } while(0)

    LOAD_XR_FUNCTION(xrDestroyInstance);
    LOAD_XR_FUNCTION(xrGetSystem);
    LOAD_XR_FUNCTION(xrCreateSession);
    LOAD_XR_FUNCTION(xrDestroySession);
    LOAD_XR_FUNCTION(xrBeginSession);
    LOAD_XR_FUNCTION(xrEndSession);
    LOAD_XR_FUNCTION(xrCreateReferenceSpace);
    LOAD_XR_FUNCTION(xrDestroySpace);
    LOAD_XR_FUNCTION(xrCreateSwapchain);
    LOAD_XR_FUNCTION(xrDestroySwapchain);
    LOAD_XR_FUNCTION(xrEnumerateSwapchainImages);
    LOAD_XR_FUNCTION(xrAcquireSwapchainImage);
    LOAD_XR_FUNCTION(xrWaitSwapchainImage);
    LOAD_XR_FUNCTION(xrReleaseSwapchainImage);
    LOAD_XR_FUNCTION(xrWaitFrame);
    LOAD_XR_FUNCTION(xrBeginFrame);
    LOAD_XR_FUNCTION(xrEndFrame);
    LOAD_XR_FUNCTION(xrPollEvent);
    LOAD_XR_FUNCTION(xrResultToString);
    LOAD_XR_FUNCTION(xrCreateActionSet);
    LOAD_XR_FUNCTION(xrCreateAction);
    LOAD_XR_FUNCTION(xrSuggestInteractionProfileBindings);
    LOAD_XR_FUNCTION(xrAttachSessionActionSets);
    LOAD_XR_FUNCTION(xrSyncActions);
    LOAD_XR_FUNCTION(xrGetActionStateBoolean);
    LOAD_XR_FUNCTION(xrGetActionStateFloat);
    LOAD_XR_FUNCTION(xrGetActionStateVector2f);
    LOAD_XR_FUNCTION(xrGetCurrentInteractionProfile);
    LOAD_XR_FUNCTION(xrPathToString);
    LOAD_XR_FUNCTION(xrStringToPath);
    LOAD_XR_FUNCTION(xrCreateActionSpace);
    LOAD_XR_FUNCTION(xrLocateSpace);

#undef LOAD_XR_FUNCTION

    LogInfo("OpenXR instance functions loaded successfully");
    return true;
}

bool OpenXRManager::CreateInstance()
{
    // Enumerate available extensions
    uint32_t extensionCount = 0;
    p_xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionCount, nullptr);
    std::vector<XrExtensionProperties> availableExtensions(extensionCount, {XR_TYPE_EXTENSION_PROPERTIES});
    p_xrEnumerateInstanceExtensionProperties(nullptr, extensionCount, &extensionCount, availableExtensions.data());

    LogInfo("Available OpenXR extensions (%d):", extensionCount);
    bool hasVulkanEnable = false, hasVulkanEnable2 = false, hasAndroidCreate = false;
    for (const auto& ext : availableExtensions) {
        LogInfo("  %s v%d", ext.extensionName, ext.extensionVersion);
        if (strcmp(ext.extensionName, XR_KHR_VULKAN_ENABLE_EXTENSION_NAME) == 0) hasVulkanEnable = true;
        if (strcmp(ext.extensionName, XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME) == 0) hasVulkanEnable2 = true;
        if (strcmp(ext.extensionName, XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME) == 0) hasAndroidCreate = true;
    }

    // Use vulkan_enable if vulkan_enable2 not available
    std::vector<const char*> extensions = {
        XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
    };
    if (hasVulkanEnable2)
        extensions.push_back(XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME);
    else if (hasVulkanEnable)
        extensions.push_back(XR_KHR_VULKAN_ENABLE_EXTENSION_NAME);
    else {
        LogError("No Vulkan OpenXR extension available!");
        return false;
    }

    XrInstanceCreateInfoAndroidKHR androidCreateInfo = {XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    androidCreateInfo.applicationVM = JNIUtils::g_jvm;
    // Use the Activity reference — Quest needs this for VR session focus
    androidCreateInfo.applicationActivity = m_activityObject;
    if (m_activityObject) {
        LogInfo("Using Activity reference for OpenXR instance");
    } else {
        LogError("No Activity reference available for OpenXR!");
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

    XrResult result = p_xrCreateInstance(&instanceCreateInfo, &m_instance);
    return CheckXrResult(result, "xrCreateInstance");
}

bool OpenXRManager::LoadExtensions()
{
    XrResult result = p_xrGetInstanceProcAddr(m_instance, "xrGetVulkanGraphicsRequirements2KHR",
                                            reinterpret_cast<PFN_xrVoidFunction*>(&m_xrGetVulkanGraphicsRequirements2KHR));
    if (!CheckXrResult(result, "xrGetInstanceProcAddr(xrGetVulkanGraphicsRequirements2KHR)")) {
        return false;
    }

    // Load XR_KHR_vulkan_enable2 functions
    result = p_xrGetInstanceProcAddr(m_instance, "xrCreateVulkanInstanceKHR",
                                   reinterpret_cast<PFN_xrVoidFunction*>(&m_xrCreateVulkanInstanceKHR));
    if (!CheckXrResult(result, "xrGetInstanceProcAddr(xrCreateVulkanInstanceKHR)")) {
        return false;
    }

    result = p_xrGetInstanceProcAddr(m_instance, "xrCreateVulkanDeviceKHR",
                                   reinterpret_cast<PFN_xrVoidFunction*>(&m_xrCreateVulkanDeviceKHR));
    if (!CheckXrResult(result, "xrGetInstanceProcAddr(xrCreateVulkanDeviceKHR)")) {
        return false;
    }

    result = p_xrGetInstanceProcAddr(m_instance, "xrGetVulkanGraphicsDevice2KHR",
                                   reinterpret_cast<PFN_xrVoidFunction*>(&m_xrGetVulkanGraphicsDevice2KHR));
    if (!CheckXrResult(result, "xrGetInstanceProcAddr(xrGetVulkanGraphicsDevice2KHR)")) {
        return false;
    }

    LogInfo("OpenXR Vulkan enable2 functions loaded successfully");
    return true;
}

bool OpenXRManager::GetSystem()
{
    XrSystemGetInfo systemGetInfo = {XR_TYPE_SYSTEM_GET_INFO};
    systemGetInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    XrResult result = p_xrGetSystem(m_instance, &systemGetInfo, &m_systemId);
    if (!CheckXrResult(result, "p_xrGetSystem")) {
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

bool OpenXRManager::CreateVulkanObjects()
{
    LogInfo("Creating Vulkan objects through OpenXR");

    // Create Vulkan instance through OpenXR
    VkApplicationInfo appInfo = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = "Cemu";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Cemu Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    std::vector<const char*> instExts = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
        VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
    };

    VkInstanceCreateInfo vkInstanceCI = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    vkInstanceCI.pApplicationInfo = &appInfo;
    vkInstanceCI.enabledExtensionCount = static_cast<uint32_t>(instExts.size());
    vkInstanceCI.ppEnabledExtensionNames = instExts.data();

    XrVulkanInstanceCreateInfoKHR xrVulkanInstanceCI = {XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR};
    xrVulkanInstanceCI.systemId = m_systemId;
    xrVulkanInstanceCI.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xrVulkanInstanceCI.vulkanCreateInfo = &vkInstanceCI;

    VkResult vkResult;
    XrResult xrResult = m_xrCreateVulkanInstanceKHR(m_instance, &xrVulkanInstanceCI, &m_vkInstance, &vkResult);

    if (!CheckXrResult(xrResult, "xrCreateVulkanInstanceKHR")) {
        return false;
    }
    if (vkResult != VK_SUCCESS) {
        LogError("xrCreateVulkanInstanceKHR returned VkResult %d", vkResult);
        return false;
    }

    LogInfo("OpenXR created Vulkan instance successfully");

    // Initialize Vulkan instance functions for the OpenXR-created instance
    if (!InitializeInstanceVulkan(m_vkInstance)) {
        LogError("Failed to initialize Vulkan instance functions");
        return false;
    }

    // Get physical device from OpenXR
    XrVulkanGraphicsDeviceGetInfoKHR deviceGetInfo = {XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR};
    deviceGetInfo.systemId = m_systemId;
    deviceGetInfo.vulkanInstance = m_vkInstance;

    xrResult = m_xrGetVulkanGraphicsDevice2KHR(m_instance, &deviceGetInfo, &m_vkPhysicalDevice);
    if (!CheckXrResult(xrResult, "xrGetVulkanGraphicsDevice2KHR")) {
        return false;
    }

    LogInfo("OpenXR selected Vulkan physical device successfully");

    // Find graphics queue family
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_vkPhysicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_vkPhysicalDevice, &queueFamilyCount, queueFamilies.data());

    m_queueFamilyIndex = UINT32_MAX;
    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            m_queueFamilyIndex = i;
            break;
        }
    }

    if (m_queueFamilyIndex == UINT32_MAX) {
        LogError("No graphics queue family found");
        return false;
    }

    // Create logical device through OpenXR
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCI = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueCI.queueFamilyIndex = m_queueFamilyIndex;
    queueCI.queueCount = 1;
    queueCI.pQueuePriorities = &queuePriority;

    // Enumerate available device extensions and enable all that VulkanRenderer might need
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(m_vkPhysicalDevice, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> availExts(extCount);
    vkEnumerateDeviceExtensionProperties(m_vkPhysicalDevice, nullptr, &extCount, availExts.data());

    auto hasExt = [&](const char* name) {
        for (auto& e : availExts)
            if (strcmp(e.extensionName, name) == 0) return true;
        return false;
    };

    // Start with required extensions for OpenXR + VulkanRenderer
    std::vector<const char*> devExts = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
        VK_KHR_EXTERNAL_FENCE_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
    };

    // Add all optional extensions that VulkanRenderer may use (if available)
    const char* optionalExts[] = {
        VK_EXT_TRANSFORM_FEEDBACK_EXTENSION_NAME,
        VK_EXT_DEPTH_RANGE_UNRESTRICTED_EXTENSION_NAME,
        VK_EXT_DEPTH_CLIP_ENABLE_EXTENSION_NAME,
        VK_EXT_CUSTOM_BORDER_COLOR_EXTENSION_NAME,
        VK_EXT_PIPELINE_CREATION_FEEDBACK_EXTENSION_NAME,
        VK_EXT_PIPELINE_CREATION_CACHE_CONTROL_EXTENSION_NAME,
        VK_EXT_PIPELINE_ROBUSTNESS_EXTENSION_NAME,
        VK_EXT_FILTER_CUBIC_EXTENSION_NAME,
        VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME,
        VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME,
        VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME,
        VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME,
        VK_EXT_TOOLING_INFO_EXTENSION_NAME,
        VK_KHR_MAINTENANCE1_EXTENSION_NAME,
        VK_KHR_MAINTENANCE2_EXTENSION_NAME,
        VK_KHR_MAINTENANCE3_EXTENSION_NAME,
    };
    for (auto ext : optionalExts) {
        if (hasExt(ext))
            devExts.push_back(ext);
    }

    LogInfo("OpenXR: Enabling %d device extensions", (int)devExts.size());

    // Enable device features that VulkanRenderer requires
    VkPhysicalDeviceFeatures2 supportedFeatures2 = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    vkGetPhysicalDeviceFeatures2(m_vkPhysicalDevice, &supportedFeatures2);
    VkPhysicalDeviceFeatures& supportedFeatures = supportedFeatures2.features;

    VkPhysicalDeviceFeatures enabledFeatures = {};
    enabledFeatures.independentBlend = VK_TRUE;
    enabledFeatures.samplerAnisotropy = supportedFeatures.samplerAnisotropy;
    enabledFeatures.imageCubeArray = VK_TRUE;
    enabledFeatures.geometryShader = supportedFeatures.geometryShader;
    enabledFeatures.logicOp = supportedFeatures.logicOp;
    enabledFeatures.occlusionQueryPrecise = supportedFeatures.occlusionQueryPrecise;
    enabledFeatures.depthClamp = supportedFeatures.depthClamp;
    enabledFeatures.depthBiasClamp = VK_TRUE;
    enabledFeatures.robustBufferAccess = VK_TRUE;
    enabledFeatures.vertexPipelineStoresAndAtomics = supportedFeatures.vertexPipelineStoresAndAtomics;

    VkDeviceCreateInfo vkDeviceCI = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    vkDeviceCI.queueCreateInfoCount = 1;
    vkDeviceCI.pQueueCreateInfos = &queueCI;
    vkDeviceCI.enabledExtensionCount = static_cast<uint32_t>(devExts.size());
    vkDeviceCI.ppEnabledExtensionNames = devExts.data();
    vkDeviceCI.pEnabledFeatures = &enabledFeatures;

    XrVulkanDeviceCreateInfoKHR xrVulkanDeviceCI = {XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR};
    xrVulkanDeviceCI.systemId = m_systemId;
    xrVulkanDeviceCI.pfnGetInstanceProcAddr = vkGetInstanceProcAddr;
    xrVulkanDeviceCI.vulkanPhysicalDevice = m_vkPhysicalDevice;
    xrVulkanDeviceCI.vulkanCreateInfo = &vkDeviceCI;

    xrResult = m_xrCreateVulkanDeviceKHR(m_instance, &xrVulkanDeviceCI, &m_vkDevice, &vkResult);

    if (!CheckXrResult(xrResult, "xrCreateVulkanDeviceKHR")) {
        return false;
    }
    if (vkResult != VK_SUCCESS) {
        LogError("xrCreateVulkanDeviceKHR returned VkResult %d", vkResult);
        return false;
    }

    // Initialize Vulkan device functions for the OpenXR-created device
    if (!InitializeDeviceVulkan(m_vkDevice)) {
        LogError("Failed to initialize Vulkan device functions");
        return false;
    }

    m_ownVulkanObjects = true;
    LogInfo("OpenXR created Vulkan device successfully - Instance=%p PhysDevice=%p Device=%p QueueFamily=%d",
        (void*)m_vkInstance, (void*)m_vkPhysicalDevice, (void*)m_vkDevice, m_queueFamilyIndex);
    return true;
}

bool OpenXRManager::CreateSession()
{
    LogInfo("Creating OpenXR session with Vulkan binding");
    LogInfo("  VkInstance=%p PhysDevice=%p Device=%p QueueFamily=%d",
        (void*)m_vkInstance, (void*)m_vkPhysicalDevice, (void*)m_vkDevice, m_queueFamilyIndex);

    XrGraphicsBindingVulkanKHR vulkanBinding = {XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR};
    vulkanBinding.instance = m_vkInstance;
    vulkanBinding.physicalDevice = m_vkPhysicalDevice;
    vulkanBinding.device = m_vkDevice;
    vulkanBinding.queueFamilyIndex = m_queueFamilyIndex;
    vulkanBinding.queueIndex = 0;

    XrSessionCreateInfo sessionCreateInfo = {XR_TYPE_SESSION_CREATE_INFO};
    sessionCreateInfo.next = &vulkanBinding;
    sessionCreateInfo.systemId = m_systemId;

    XrResult result = p_xrCreateSession(m_instance, &sessionCreateInfo, &m_session);
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

    XrResult result = p_xrCreateReferenceSpace(m_session, &referenceSpaceCreateInfo, &m_localSpace);
    return CheckXrResult(result, "p_xrCreateReferenceSpace");
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

    XrResult result = p_xrCreateSwapchain(m_session, &swapchainCreateInfo, &m_swapchain);
    if (!CheckXrResult(result, "p_xrCreateSwapchain")) {
        return false;
    }

    // Get swapchain images
    uint32_t imageCount = 0;
    result = p_xrEnumerateSwapchainImages(m_swapchain, 0, &imageCount, nullptr);
    if (!CheckXrResult(result, "p_xrEnumerateSwapchainImages (count)")) {
        return false;
    }

    std::vector<XrSwapchainImageVulkanKHR> swapchainImageStructs(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR});
    result = p_xrEnumerateSwapchainImages(m_swapchain, imageCount, &imageCount,
                                       reinterpret_cast<XrSwapchainImageBaseHeader*>(swapchainImageStructs.data()));
    if (!CheckXrResult(result, "p_xrEnumerateSwapchainImages")) {
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
    LOGI("PollEvents enter: xrPollEvent=%p instance=%llu", (void*)p_xrPollEvent, (unsigned long long)m_instance);
    if (!p_xrPollEvent || m_instance == XR_NULL_HANDLE) {
        LOGE("PollEvents: INVALID xrPollEvent=%p instance=%llu", (void*)p_xrPollEvent, (unsigned long long)m_instance);
        return;
    }
    XrEventDataBuffer eventBuffer = {XR_TYPE_EVENT_DATA_BUFFER};

    int maxEvents = 20; // Prevent infinite loop from event flooding
    while (maxEvents-- > 0) {
        XrResult pollResult = p_xrPollEvent(m_instance, &eventBuffer);
        if (XR_FAILED(pollResult)) break;
        if (eventBuffer.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            ProcessEvent(eventBuffer);
        }
        eventBuffer = {XR_TYPE_EVENT_DATA_BUFFER};
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
                    XrResult result = p_xrBeginSession(m_session, &sessionBeginInfo);
                    if (CheckXrResult(result, "p_xrBeginSession")) {
                        m_sessionRunning = true;
                        LogInfo("OpenXR session started");
                    }
                    break;
                }
                case XR_SESSION_STATE_STOPPING: {
                    m_sessionRunning = false;
                    XrResult result = p_xrEndSession(m_session);
                    CheckXrResult(result, "p_xrEndSession");
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
    XrResult result = p_xrWaitFrame(m_session, &frameWaitInfo, &m_frameState);
    if (!CheckXrResult(result, "p_xrWaitFrame")) {
        return false;
    }

    // Begin the frame
    XrFrameBeginInfo frameBeginInfo = {XR_TYPE_FRAME_BEGIN_INFO};
    result = p_xrBeginFrame(m_session, &frameBeginInfo);
    if (!CheckXrResult(result, "p_xrBeginFrame")) {
        return false;
    }

    m_frameActive = true;

    if (!SyncInputActions()) {
        LogError("Failed to sync OpenXR input actions during BeginFrame");
    }

    if (!m_frameState.shouldRender) {
        EndFrameEmpty();
        return false;
    }

    return true;
}

bool OpenXRManager::SubmitEmptyFrame()
{
    if (!BeginFrame()) return false;

    return EndFrameEmpty();
}

uint32_t OpenXRManager::AcquireSwapchainImage()
{
    if (!m_swapchain || !m_frameActive) {
        return UINT32_MAX;
    }

    XrSwapchainImageAcquireInfo acquireInfo = {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    XrResult result = p_xrAcquireSwapchainImage(m_swapchain, &acquireInfo, &m_acquiredImageIndex);
    if (!CheckXrResult(result, "p_xrAcquireSwapchainImage")) {
        return UINT32_MAX;
    }

    XrSwapchainImageWaitInfo waitInfo = {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    result = p_xrWaitSwapchainImage(m_swapchain, &waitInfo);
    if (!CheckXrResult(result, "p_xrWaitSwapchainImage")) {
        return UINT32_MAX;
    }

    return m_acquiredImageIndex;
}

void OpenXRManager::ReleaseSwapchainImage()
{
    if (!m_swapchain || m_acquiredImageIndex == UINT32_MAX) {
        return;
    }

#if BOOST_PLAT_ANDROID
    __android_log_print(ANDROID_LOG_DEBUG, "Cemu", "OpenXR ReleaseSwapchainImage: begin index=%u", m_acquiredImageIndex);
#endif
    XrSwapchainImageReleaseInfo releaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    XrResult result = p_xrReleaseSwapchainImage(m_swapchain, &releaseInfo);
    CheckXrResult(result, "p_xrReleaseSwapchainImage");
#if BOOST_PLAT_ANDROID
    __android_log_print(ANDROID_LOG_DEBUG, "Cemu", "OpenXR ReleaseSwapchainImage: end result=%d", (int)result);
#endif

    m_acquiredImageIndex = UINT32_MAX;
}

bool OpenXRManager::CancelFrame()
{
    if (!m_session || !m_frameActive) {
        return false;
    }

    if (m_acquiredImageIndex != UINT32_MAX) {
        ReleaseSwapchainImage();
    }

    return EndFrameEmpty();
}

bool OpenXRManager::EndFrame(XrPosef quadPose, XrExtent2Df quadSize)
{
    if (!m_session || !m_frameActive) {
        return false;
    }

#if BOOST_PLAT_ANDROID
    __android_log_print(ANDROID_LOG_DEBUG, "Cemu", "OpenXR EndFrame: begin shouldRender=%d running=%d swapchain=%p size=%ux%u", m_frameState.shouldRender ? 1 : 0, IsSessionRunning() ? 1 : 0, (void*)m_swapchain, m_swapchainWidth, m_swapchainHeight);
#endif
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

#if BOOST_PLAT_ANDROID
    __android_log_print(ANDROID_LOG_DEBUG, "Cemu", "OpenXR EndFrame: xrEndFrame begin layerCount=%u", frameEndInfo.layerCount);
#endif
    XrResult result = p_xrEndFrame(m_session, &frameEndInfo);
    m_frameActive = false;
#if BOOST_PLAT_ANDROID
    __android_log_print(ANDROID_LOG_DEBUG, "Cemu", "OpenXR EndFrame: xrEndFrame end result=%d", (int)result);
#endif

    return CheckXrResult(result, "p_xrEndFrame");
}

bool OpenXRManager::EndFrameEmpty()
{
    if (!m_session || !m_frameActive) {
        return false;
    }

    XrFrameEndInfo endInfo = {XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = m_frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = 0;
    endInfo.layers = nullptr;

    XrResult result = p_xrEndFrame(m_session, &endInfo);
    m_frameActive = false;
    return CheckXrResult(result, "xrEndFrame(empty)");
}

void OpenXRManager::Shutdown()
{
    LogInfo("Shutting down OpenXR");

    if (m_aimSpaceR != XR_NULL_HANDLE) {
        p_xrDestroySpace(m_aimSpaceR);
        m_aimSpaceR = XR_NULL_HANDLE;
    }
    if (m_gripSpaceL != XR_NULL_HANDLE) {
        p_xrDestroySpace(m_gripSpaceL);
        m_gripSpaceL = XR_NULL_HANDLE;
    }
    if (m_gripSpaceR != XR_NULL_HANDLE) {
        p_xrDestroySpace(m_gripSpaceR);
        m_gripSpaceR = XR_NULL_HANDLE;
    }

    if (m_swapchain != XR_NULL_HANDLE) {
        p_xrDestroySwapchain(m_swapchain);
        m_swapchain = XR_NULL_HANDLE;
    }

    if (m_localSpace != XR_NULL_HANDLE) {
        p_xrDestroySpace(m_localSpace);
        m_localSpace = XR_NULL_HANDLE;
    }

    if (m_session != XR_NULL_HANDLE) {
        if (m_sessionRunning) {
            p_xrEndSession(m_session);
            m_sessionRunning = false;
        }
        p_xrDestroySession(m_session);
        m_session = XR_NULL_HANDLE;
    }

    // Clean up Vulkan objects if we created them
    if (m_ownVulkanObjects) {
        if (m_vkDevice != VK_NULL_HANDLE) {
            vkDestroyDevice(m_vkDevice, nullptr);
            m_vkDevice = VK_NULL_HANDLE;
        }
        if (m_vkInstance != VK_NULL_HANDLE) {
            vkDestroyInstance(m_vkInstance, nullptr);
            m_vkInstance = VK_NULL_HANDLE;
        }
        m_ownVulkanObjects = false;
    }

    if (m_instance != XR_NULL_HANDLE) {
        p_xrDestroyInstance(m_instance);
        m_instance = XR_NULL_HANDLE;
    }

    // Unload the OpenXR library
    if (m_openxrLibrary) {
        dlclose(m_openxrLibrary);
        m_openxrLibrary = nullptr;
    }

    // Reset input action handles (no explicit destroy needed for actions, they're owned by action set)
    m_actionButtonA = XR_NULL_HANDLE;
    m_actionButtonB = XR_NULL_HANDLE;
    m_actionButtonX = XR_NULL_HANDLE;
    m_actionButtonY = XR_NULL_HANDLE;
    m_actionButtonMenu = XR_NULL_HANDLE;
    m_actionThumbstickClickL = XR_NULL_HANDLE;
    m_actionThumbstickClickR = XR_NULL_HANDLE;
    m_actionTriggerL = XR_NULL_HANDLE;
    m_actionTriggerR = XR_NULL_HANDLE;
    m_actionGripL = XR_NULL_HANDLE;
    m_actionGripR = XR_NULL_HANDLE;
    m_actionThumbstickL = XR_NULL_HANDLE;
    m_actionThumbstickR = XR_NULL_HANDLE;
    m_actionGripPoseL = XR_NULL_HANDLE;
    m_actionGripPoseR = XR_NULL_HANDLE;
    m_actionAimPoseR = XR_NULL_HANDLE;
    m_actionSet = XR_NULL_HANDLE;

    // Clear function pointers
    p_xrGetInstanceProcAddr = nullptr;
    p_xrCreateInstance = nullptr;
    p_xrDestroyInstance = nullptr;
    p_xrGetSystem = nullptr;
    p_xrEnumerateInstanceExtensionProperties = nullptr;
    p_xrCreateSession = nullptr;
    p_xrDestroySession = nullptr;
    p_xrBeginSession = nullptr;
    p_xrEndSession = nullptr;
    p_xrCreateReferenceSpace = nullptr;
    p_xrDestroySpace = nullptr;
    p_xrCreateSwapchain = nullptr;
    p_xrDestroySwapchain = nullptr;
    p_xrEnumerateSwapchainImages = nullptr;
    p_xrAcquireSwapchainImage = nullptr;
    p_xrWaitSwapchainImage = nullptr;
    p_xrReleaseSwapchainImage = nullptr;
    p_xrWaitFrame = nullptr;
    p_xrBeginFrame = nullptr;
    p_xrEndFrame = nullptr;
    p_xrPollEvent = nullptr;
    p_xrResultToString = nullptr;
    m_xrGetVulkanGraphicsRequirements2KHR = nullptr;
    m_xrCreateVulkanInstanceKHR = nullptr;
    m_xrCreateVulkanDeviceKHR = nullptr;
    m_xrGetVulkanGraphicsDevice2KHR = nullptr;
    p_xrCreateActionSet = nullptr;
    p_xrCreateAction = nullptr;
    p_xrSuggestInteractionProfileBindings = nullptr;
    p_xrAttachSessionActionSets = nullptr;
    p_xrSyncActions = nullptr;
    p_xrGetActionStateBoolean = nullptr;
    p_xrGetActionStateFloat = nullptr;
    p_xrGetActionStateVector2f = nullptr;
    p_xrGetCurrentInteractionProfile = nullptr;
    p_xrPathToString = nullptr;
    p_xrStringToPath = nullptr;
    p_xrCreateActionSpace = nullptr;
    p_xrLocateSpace = nullptr;

    // Clear state
    m_openxrLoaded = false;
    m_sessionState = XR_SESSION_STATE_UNKNOWN;
    m_frameActive = false;
    m_swapchainImages.clear();
    m_swapchainWidth = m_swapchainHeight = 0;
    m_swapchainFormat = VK_FORMAT_UNDEFINED;
    m_acquiredImageIndex = UINT32_MAX;
    m_leftHandPath = XR_NULL_PATH;
    m_rightHandPath = XR_NULL_PATH;
    m_motionCacheL = {};
    m_motionCacheR = {};

    LogInfo("OpenXR shutdown complete");
}

bool OpenXRManager::InitializeInputActions()
{
    LogInfo("Initializing OpenXR input actions");

    if (!CheckXrResult(p_xrStringToPath(m_instance, "/user/hand/left", &m_leftHandPath), "xrStringToPath /user/hand/left")) {
        return false;
    }
    if (!CheckXrResult(p_xrStringToPath(m_instance, "/user/hand/right", &m_rightHandPath), "xrStringToPath /user/hand/right")) {
        return false;
    }

    // Create action set
    XrActionSetCreateInfo actionSetCreateInfo = {XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy(actionSetCreateInfo.actionSetName, "gameplay");
    strcpy(actionSetCreateInfo.localizedActionSetName, "Gameplay");
    actionSetCreateInfo.priority = 0;

    XrResult result = p_xrCreateActionSet(m_instance, &actionSetCreateInfo, &m_actionSet);
    if (!CheckXrResult(result, "xrCreateActionSet")) {
        return false;
    }

    auto createAction = [&](XrActionType actionType, const char* actionName, const char* localizedActionName,
                            XrPath subactionPath, XrAction& action) -> bool
    {
        XrActionCreateInfo actionCreateInfo = {XR_TYPE_ACTION_CREATE_INFO};
        actionCreateInfo.actionType = actionType;
        std::strncpy(actionCreateInfo.actionName, actionName, XR_MAX_ACTION_NAME_SIZE - 1);
        std::strncpy(actionCreateInfo.localizedActionName, localizedActionName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
        if (subactionPath != XR_NULL_PATH) {
            actionCreateInfo.countSubactionPaths = 1;
            actionCreateInfo.subactionPaths = &subactionPath;
        }

        XrResult createResult = p_xrCreateAction(m_actionSet, &actionCreateInfo, &action);
        char operation[128];
        snprintf(operation, sizeof(operation), "xrCreateAction %s", actionName);
        return CheckXrResult(createResult, operation);
    };

    if (!createAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "button_a", "Button A", m_rightHandPath, m_actionButtonA)) return false;
    if (!createAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "button_b", "Button B", m_rightHandPath, m_actionButtonB)) return false;
    if (!createAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "button_x", "Button X", m_leftHandPath, m_actionButtonX)) return false;
    if (!createAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "button_y", "Button Y", m_leftHandPath, m_actionButtonY)) return false;
    if (!createAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "button_menu", "Menu Button", m_leftHandPath, m_actionButtonMenu)) return false;
    if (!createAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "thumbstick_click_left", "Left Thumbstick Click", m_leftHandPath, m_actionThumbstickClickL)) return false;
    if (!createAction(XR_ACTION_TYPE_BOOLEAN_INPUT, "thumbstick_click_right", "Right Thumbstick Click", m_rightHandPath, m_actionThumbstickClickR)) return false;
    if (!createAction(XR_ACTION_TYPE_FLOAT_INPUT, "trigger_left", "Left Trigger", m_leftHandPath, m_actionTriggerL)) return false;
    if (!createAction(XR_ACTION_TYPE_FLOAT_INPUT, "trigger_right", "Right Trigger", m_rightHandPath, m_actionTriggerR)) return false;
    if (!createAction(XR_ACTION_TYPE_FLOAT_INPUT, "grip_left", "Left Grip", m_leftHandPath, m_actionGripL)) return false;
    if (!createAction(XR_ACTION_TYPE_FLOAT_INPUT, "grip_right", "Right Grip", m_rightHandPath, m_actionGripR)) return false;
    if (!createAction(XR_ACTION_TYPE_VECTOR2F_INPUT, "thumbstick_left", "Left Thumbstick", m_leftHandPath, m_actionThumbstickL)) return false;
    if (!createAction(XR_ACTION_TYPE_VECTOR2F_INPUT, "thumbstick_right", "Right Thumbstick", m_rightHandPath, m_actionThumbstickR)) return false;
    if (!createAction(XR_ACTION_TYPE_POSE_INPUT, "grip_pose_left", "Left Grip Pose", m_leftHandPath, m_actionGripPoseL)) return false;
    if (!createAction(XR_ACTION_TYPE_POSE_INPUT, "grip_pose_right", "Right Grip Pose", m_rightHandPath, m_actionGripPoseR)) return false;
    if (!createAction(XR_ACTION_TYPE_POSE_INPUT, "aim_pose_right", "Right Aim Pose", m_rightHandPath, m_actionAimPoseR)) return false;

    std::vector<XrActionSuggestedBinding> bindings;

    // Helper lambda to add bindings
    auto addBinding = [&](XrAction action, const char* bindingPath) {
        XrPath path;
        XrResult res = p_xrStringToPath(m_instance, bindingPath, &path);
        if (XR_SUCCEEDED(res)) {
            bindings.push_back({action, path});
        }
        return XR_SUCCEEDED(res);
    };

    // Add all bindings
    addBinding(m_actionButtonA, "/user/hand/right/input/a/click");
    addBinding(m_actionButtonB, "/user/hand/right/input/b/click");
    addBinding(m_actionButtonX, "/user/hand/left/input/x/click");
    addBinding(m_actionButtonY, "/user/hand/left/input/y/click");
    addBinding(m_actionButtonMenu, "/user/hand/left/input/menu/click");
    addBinding(m_actionThumbstickClickL, "/user/hand/left/input/thumbstick/click");
    addBinding(m_actionThumbstickClickR, "/user/hand/right/input/thumbstick/click");
    addBinding(m_actionTriggerL, "/user/hand/left/input/trigger/value");
    addBinding(m_actionTriggerR, "/user/hand/right/input/trigger/value");
    addBinding(m_actionGripL, "/user/hand/left/input/squeeze/value");
    addBinding(m_actionGripR, "/user/hand/right/input/squeeze/value");
    addBinding(m_actionThumbstickL, "/user/hand/left/input/thumbstick");
    addBinding(m_actionThumbstickR, "/user/hand/right/input/thumbstick");
    addBinding(m_actionGripPoseL, "/user/hand/left/input/grip/pose");
    addBinding(m_actionGripPoseR, "/user/hand/right/input/grip/pose");
    addBinding(m_actionAimPoseR, "/user/hand/right/input/aim/pose");

    auto suggestBindingsForProfile = [&](const char* interactionProfilePath) -> bool
    {
        XrPath profilePath = XR_NULL_PATH;
        char pathOperation[128];
        snprintf(pathOperation, sizeof(pathOperation), "xrStringToPath %s", interactionProfilePath);
        XrResult pathResult = p_xrStringToPath(m_instance, interactionProfilePath, &profilePath);
        if (!CheckXrResult(pathResult, pathOperation)) {
            return false;
        }

        XrInteractionProfileSuggestedBinding suggestedBindings = {XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
        suggestedBindings.interactionProfile = profilePath;
        suggestedBindings.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
        suggestedBindings.suggestedBindings = bindings.data();

        XrResult bindingResult = p_xrSuggestInteractionProfileBindings(m_instance, &suggestedBindings);
        if (bindingResult == XR_ERROR_PATH_UNSUPPORTED || bindingResult == XR_ERROR_PATH_INVALID) {
            LogInfo("Interaction profile not supported by runtime: %s", interactionProfilePath);
            return true;
        }

        char bindOperation[160];
        snprintf(bindOperation, sizeof(bindOperation), "xrSuggestInteractionProfileBindings %s", interactionProfilePath);
        if (!CheckXrResult(bindingResult, bindOperation)) {
            return false;
        }

        LogInfo("Suggested bindings for interaction profile %s", interactionProfilePath);
        return true;
    };

    if (!suggestBindingsForProfile("/interaction_profiles/oculus/touch_controller")) return false;
    if (!suggestBindingsForProfile("/interaction_profiles/meta/touch_plus_controller")) return false;

    // Attach action set to session
    XrSessionActionSetsAttachInfo attachInfo = {XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attachInfo.countActionSets = 1;
    attachInfo.actionSets = &m_actionSet;

    result = p_xrAttachSessionActionSets(m_session, &attachInfo);
    if (!CheckXrResult(result, "xrAttachSessionActionSets")) return false;

    // Create action spaces for tracked poses
    XrActionSpaceCreateInfo actionSpaceCreateInfo = {XR_TYPE_ACTION_SPACE_CREATE_INFO};
    actionSpaceCreateInfo.poseInActionSpace = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};

    actionSpaceCreateInfo.action = m_actionGripPoseL;
    actionSpaceCreateInfo.subactionPath = m_leftHandPath;
    result = p_xrCreateActionSpace(m_session, &actionSpaceCreateInfo, &m_gripSpaceL);
    if (!CheckXrResult(result, "xrCreateActionSpace grip_left")) return false;

    actionSpaceCreateInfo.action = m_actionGripPoseR;
    actionSpaceCreateInfo.subactionPath = m_rightHandPath;
    result = p_xrCreateActionSpace(m_session, &actionSpaceCreateInfo, &m_gripSpaceR);
    if (!CheckXrResult(result, "xrCreateActionSpace grip_right")) return false;

    actionSpaceCreateInfo.action = m_actionAimPoseR;
    actionSpaceCreateInfo.subactionPath = m_rightHandPath;
    result = p_xrCreateActionSpace(m_session, &actionSpaceCreateInfo, &m_aimSpaceR);
    if (!CheckXrResult(result, "xrCreateActionSpace aim_right")) return false;

    LogInfo("OpenXR input actions initialized successfully");
    return true;
}

bool OpenXRManager::PollInput()
{
    if (m_actionSet == XR_NULL_HANDLE || !m_sessionRunning) {
        return false;
    }

    // OpenXR requires an active frame to sync actions.
    // Submit empty frames (no layers) to keep input working.
    static int frameLogCount = 0;
    if (frameLogCount++ < 3) {
        __android_log_print(ANDROID_LOG_DEBUG, "Cemu", "PollInput: about to xrWaitFrame...");
    }
    XrFrameWaitInfo waitInfo = {XR_TYPE_FRAME_WAIT_INFO};
    if (!CheckXrResult(p_xrWaitFrame(m_session, &waitInfo, &m_frameState), "xrWaitFrame(input)")) {
        return false;
    }

    XrFrameBeginInfo beginInfo = {XR_TYPE_FRAME_BEGIN_INFO};
    if (!CheckXrResult(p_xrBeginFrame(m_session, &beginInfo), "xrBeginFrame(input)")) {
        return false;
    }

    m_frameActive = true;
    const bool synced = SyncInputActions();
    const bool ended = EndFrameEmpty();
    return synced && ended;
}

bool OpenXRManager::SyncInputActions()
{
    if (m_actionSet == XR_NULL_HANDLE || !m_sessionRunning) {
        return false;
    }

    // Sync actions
    XrActiveActionSet activeActionSet = {};
    activeActionSet.actionSet = m_actionSet;
    activeActionSet.subactionPath = XR_NULL_PATH;

    XrActionsSyncInfo syncInfo = {XR_TYPE_ACTIONS_SYNC_INFO};
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeActionSet;

    XrResult result = p_xrSyncActions(m_session, &syncInfo);
    static int syncLogCount = 0;
    if (syncLogCount++ < 5) {
        __android_log_print(ANDROID_LOG_DEBUG, "Cemu", "xrSyncActions result: %d actionSet=%p", (int)result, (void*)m_actionSet);
    }
    if (!CheckXrResult(result, "xrSyncActions")) {
        return false;
    }

    static int interactionProfileLogCount = 0;
    if (interactionProfileLogCount < 10 || (interactionProfileLogCount % 300) == 0) {
        auto logInteractionProfile = [&](const char* handLabel, XrPath handPath) {
            if (p_xrGetCurrentInteractionProfile == nullptr || p_xrPathToString == nullptr || handPath == XR_NULL_PATH) {
                return;
            }

            XrInteractionProfileState profileState = {XR_TYPE_INTERACTION_PROFILE_STATE};
            XrResult profileResult = p_xrGetCurrentInteractionProfile(m_session, handPath, &profileState);
            if (XR_FAILED(profileResult)) {
                if (interactionProfileLogCount < 10) {
                    __android_log_print(ANDROID_LOG_DEBUG, "Cemu", "Current interaction profile lookup failed for %s: %d",
                        handLabel, (int)profileResult);
                }
                return;
            }

            char profileBuffer[256] = {};
            uint32_t profileLength = 0;
            XrResult pathResult = p_xrPathToString(m_instance, profileState.interactionProfile,
                sizeof(profileBuffer), &profileLength, profileBuffer);
            if (XR_SUCCEEDED(pathResult)) {
                __android_log_print(ANDROID_LOG_DEBUG, "Cemu", "Current interaction profile %s: %s", handLabel, profileBuffer);
            }
        };

        logInteractionProfile("left", m_leftHandPath);
        logInteractionProfile("right", m_rightHandPath);
    }
    ++interactionProfileLogCount;

    // Get button states
    XrActionStateGetInfo getInfo = {XR_TYPE_ACTION_STATE_GET_INFO};
    XrActionStateBoolean boolState = {XR_TYPE_ACTION_STATE_BOOLEAN};
    XrActionStateFloat floatState = {XR_TYPE_ACTION_STATE_FLOAT};
    XrActionStateVector2f vector2State = {XR_TYPE_ACTION_STATE_VECTOR2F};

    // Clear previous state
    m_inputState = {};

    // Button A (index 0)
    getInfo.action = m_actionButtonA;
    XrResult aResult = p_xrGetActionStateBoolean(m_session, &getInfo, &boolState);
    if (XR_SUCCEEDED(aResult)) {
        m_inputState.buttons[0] = boolState.currentState && boolState.isActive;
        if (syncLogCount <= 5 || boolState.currentState) {
            __android_log_print(ANDROID_LOG_DEBUG, "Cemu", "Button A: active=%d current=%d changed=%d", boolState.isActive, boolState.currentState, boolState.changedSinceLastSync);
        }
    } else {
        if (syncLogCount <= 5) {
            __android_log_print(ANDROID_LOG_DEBUG, "Cemu", "Button A getState failed: %d action=%p", (int)aResult, (void*)m_actionButtonA);
        }
    }

    // Button B (index 1)
    getInfo.action = m_actionButtonB;
    if (XR_SUCCEEDED(p_xrGetActionStateBoolean(m_session, &getInfo, &boolState))) {
        m_inputState.buttons[1] = boolState.currentState && boolState.isActive;
    }

    // Button X (index 2)
    getInfo.action = m_actionButtonX;
    if (XR_SUCCEEDED(p_xrGetActionStateBoolean(m_session, &getInfo, &boolState))) {
        m_inputState.buttons[2] = boolState.currentState && boolState.isActive;
    }

    // Button Y (index 3)
    getInfo.action = m_actionButtonY;
    if (XR_SUCCEEDED(p_xrGetActionStateBoolean(m_session, &getInfo, &boolState))) {
        m_inputState.buttons[3] = boolState.currentState && boolState.isActive;
    }

    // Menu button (index 4)
    getInfo.action = m_actionButtonMenu;
    if (XR_SUCCEEDED(p_xrGetActionStateBoolean(m_session, &getInfo, &boolState))) {
        m_inputState.buttons[4] = boolState.currentState && boolState.isActive;
    }

    // Left thumbstick click (index 5)
    getInfo.action = m_actionThumbstickClickL;
    if (XR_SUCCEEDED(p_xrGetActionStateBoolean(m_session, &getInfo, &boolState))) {
        m_inputState.buttons[5] = boolState.currentState && boolState.isActive;
    }

    // Right thumbstick click (index 6)
    getInfo.action = m_actionThumbstickClickR;
    if (XR_SUCCEEDED(p_xrGetActionStateBoolean(m_session, &getInfo, &boolState))) {
        m_inputState.buttons[6] = boolState.currentState && boolState.isActive;
    }

    // Left trigger
    getInfo.action = m_actionTriggerL;
    if (XR_SUCCEEDED(p_xrGetActionStateFloat(m_session, &getInfo, &floatState))) {
        m_inputState.triggerL = floatState.isActive ? floatState.currentState : 0.0f;
    }

    // Right trigger
    getInfo.action = m_actionTriggerR;
    if (XR_SUCCEEDED(p_xrGetActionStateFloat(m_session, &getInfo, &floatState))) {
        m_inputState.triggerR = floatState.isActive ? floatState.currentState : 0.0f;
    }

    // Left grip
    getInfo.action = m_actionGripL;
    if (XR_SUCCEEDED(p_xrGetActionStateFloat(m_session, &getInfo, &floatState))) {
        m_inputState.gripL = floatState.isActive ? floatState.currentState : 0.0f;
    }

    // Right grip
    getInfo.action = m_actionGripR;
    if (XR_SUCCEEDED(p_xrGetActionStateFloat(m_session, &getInfo, &floatState))) {
        m_inputState.gripR = floatState.isActive ? floatState.currentState : 0.0f;
    }

    // Left thumbstick
    getInfo.action = m_actionThumbstickL;
    if (XR_SUCCEEDED(p_xrGetActionStateVector2f(m_session, &getInfo, &vector2State))) {
        if (vector2State.isActive) {
            m_inputState.thumbstickLX = vector2State.currentState.x;
            m_inputState.thumbstickLY = vector2State.currentState.y;
        }
    }

    // Right thumbstick
    getInfo.action = m_actionThumbstickR;
    if (XR_SUCCEEDED(p_xrGetActionStateVector2f(m_session, &getInfo, &vector2State))) {
        if (vector2State.isActive) {
            m_inputState.thumbstickRX = vector2State.currentState.x;
            m_inputState.thumbstickRY = vector2State.currentState.y;
        }
    }

    UpdatePointerState();
    UpdateMotionState(m_gripSpaceL, m_motionCacheL, m_inputState.motionL);
    UpdateMotionState(m_gripSpaceR, m_motionCacheR, m_inputState.motionR);

    if (syncLogCount <= 5 || (syncLogCount % 120) == 0 ||
        m_inputState.buttons[0] || m_inputState.buttons[1] || m_inputState.buttons[2] || m_inputState.buttons[3] ||
        m_inputState.gripL > 0.1f || m_inputState.gripR > 0.1f ||
        m_inputState.triggerL > 0.1f || m_inputState.triggerR > 0.1f ||
        m_inputState.pointerVisibility != 0) {
        __android_log_print(ANDROID_LOG_DEBUG, "Cemu",
            "OpenXR state A=%d B=%d X=%d Y=%d trigL=%.2f trigR=%.2f gripL=%.2f gripR=%.2f ptr=%d x=%.2f y=%.2f",
            m_inputState.buttons[0] ? 1 : 0,
            m_inputState.buttons[1] ? 1 : 0,
            m_inputState.buttons[2] ? 1 : 0,
            m_inputState.buttons[3] ? 1 : 0,
            m_inputState.triggerL,
            m_inputState.triggerR,
            m_inputState.gripL,
            m_inputState.gripR,
            m_inputState.pointerVisibility,
            m_inputState.pointerX,
            m_inputState.pointerY);
    }

    return true;
}

void OpenXRManager::UpdatePointerState()
{
    if (p_xrLocateSpace == nullptr || m_aimSpaceR == XR_NULL_HANDLE || m_localSpace == XR_NULL_HANDLE) {
        return;
    }

    XrSpaceLocation location = {XR_TYPE_SPACE_LOCATION};
    if (!CheckXrResult(p_xrLocateSpace(m_aimSpaceR, m_localSpace, m_frameState.predictedDisplayTime, &location), "xrLocateSpace aim_pose_right")) {
        return;
    }

    if (!HasValidPose(location)) {
        return;
    }

    const XrVector3f rayOrigin = location.pose.position;
    const XrVector3f rayDirection = RotateVector(location.pose.orientation, {0.0f, 0.0f, -1.0f});
    const XrVector3f planeNormal = RotateVector(DefaultQuadPose.orientation, {0.0f, 0.0f, 1.0f});
    const float denominator = Dot(rayDirection, planeNormal);
    if (std::abs(denominator) < 0.0001f) {
        return;
    }

    const float distance = Dot(Subtract(DefaultQuadPose.position, rayOrigin), planeNormal) / denominator;
    if (distance <= 0.0f) {
        return;
    }

    const XrVector3f hitPoint = Add(rayOrigin, Scale(rayDirection, distance));
    const XrVector3f localOffset = Subtract(hitPoint, DefaultQuadPose.position);
    const XrVector3f planeX = RotateVector(DefaultQuadPose.orientation, {1.0f, 0.0f, 0.0f});
    const XrVector3f planeY = RotateVector(DefaultQuadPose.orientation, {0.0f, 1.0f, 0.0f});

    const float pointerX = (Dot(localOffset, planeX) / DefaultQuadSize.width) + 0.5f;
    const float pointerY = 0.5f - (Dot(localOffset, planeY) / DefaultQuadSize.height);
    const bool insideFullBounds = pointerX >= 0.0f && pointerX <= 1.0f && pointerY >= 0.0f && pointerY <= 1.0f;
    const bool insidePartialBounds =
        pointerX >= -kPointerPartialMargin && pointerX <= 1.0f + kPointerPartialMargin &&
        pointerY >= -kPointerPartialMargin && pointerY <= 1.0f + kPointerPartialMargin;

    if (!insidePartialBounds) {
        return;
    }

    m_inputState.pointerX = std::clamp(pointerX, 0.0f, 1.0f);
    m_inputState.pointerY = std::clamp(pointerY, 0.0f, 1.0f);
    m_inputState.pointerVisibility = insideFullBounds ? 1 : 2;
}

void OpenXRManager::UpdateMotionState(XrSpace space, MotionTrackingCache& cache, ControllerInputState::MotionState& motionState)
{
    if (p_xrLocateSpace == nullptr || space == XR_NULL_HANDLE || m_localSpace == XR_NULL_HANDLE) {
        return;
    }

    XrSpaceVelocity velocity = {XR_TYPE_SPACE_VELOCITY};
    XrSpaceLocation location = {XR_TYPE_SPACE_LOCATION};
    location.next = &velocity;

    if (!CheckXrResult(p_xrLocateSpace(space, m_localSpace, m_frameState.predictedDisplayTime, &location), "xrLocateSpace grip_pose")) {
        cache = {};
        return;
    }

    if (!HasValidPose(location)) {
        cache = {};
        return;
    }

    const XrQuaternionf inverseOrientation = Conjugate(location.pose.orientation);
    XrVector3f localAcceleration = RotateVector(inverseOrientation, {0.0f, -1.0f, 0.0f});
    XrVector3f localGyro{};

    if ((velocity.velocityFlags & XR_SPACE_VELOCITY_ANGULAR_VALID_BIT) != 0) {
        localGyro = RotateVector(inverseOrientation, velocity.angularVelocity);
    }

    if ((velocity.velocityFlags & XR_SPACE_VELOCITY_LINEAR_VALID_BIT) != 0) {
        if (cache.hasPreviousSample && m_frameState.predictedDisplayTime > cache.previousSampleTime) {
            const float deltaTime = static_cast<float>(m_frameState.predictedDisplayTime - cache.previousSampleTime) / 1000000000.0f;
            if (deltaTime > 0.0001f && deltaTime < 0.5f) {
                const XrVector3f worldAcceleration = Scale(
                    Subtract(velocity.linearVelocity, cache.previousLinearVelocity),
                    1.0f / (deltaTime * kGravityMetersPerSecondSquared));
                localAcceleration = Add(localAcceleration, RotateVector(inverseOrientation, worldAcceleration));
            }
        }

        cache.previousLinearVelocity = velocity.linearVelocity;
        cache.previousSampleTime = m_frameState.predictedDisplayTime;
        cache.hasPreviousSample = true;
    } else {
        cache = {};
    }

    float yaw, pitch, roll;
    QuaternionToYawPitchRoll(location.pose.orientation, yaw, pitch, roll);

    motionState.valid = true;
    motionState.acceleration[0] = localAcceleration.x;
    motionState.acceleration[1] = localAcceleration.y;
    motionState.acceleration[2] = localAcceleration.z;
    motionState.gyro[0] = localGyro.x;
    motionState.gyro[1] = localGyro.y;
    motionState.gyro[2] = localGyro.z;
    motionState.orientation[0] = (-yaw / kTwoPi) - 0.5f;
    motionState.orientation[1] = (-pitch / kTwoPi) - 0.5f;
    motionState.orientation[2] = roll / kTwoPi;
    motionState.quaternion[0] = location.pose.orientation.x;
    motionState.quaternion[1] = location.pose.orientation.y;
    motionState.quaternion[2] = location.pose.orientation.z;
    motionState.quaternion[3] = location.pose.orientation.w;
}

OpenXRManager::ControllerInputState OpenXRManager::GetInputState() const
{
    return m_inputState;
}
