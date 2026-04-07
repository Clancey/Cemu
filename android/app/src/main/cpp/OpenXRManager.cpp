#include "OpenXRManager.h"
#include "JNIUtils.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#include <android/log.h>
#include <array>
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

bool OpenXRManager::Initialize(VkInstance vkInstance, VkPhysicalDevice vkPhysicalDevice,
                              VkDevice vkDevice, uint32_t queueFamilyIndex,
                              ANativeActivity* activity)
{
    LogInfo("Initializing OpenXR with dynamic loading");

    // Store Vulkan objects
    m_vkInstance = vkInstance;
    m_vkPhysicalDevice = vkPhysicalDevice;
    m_vkDevice = vkDevice;
    m_queueFamilyIndex = queueFamilyIndex;
    m_activity = activity;

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
        // Get application context
        JNIUtils::ScopedJNIENV scopedEnv;
        JNIEnv* env = *scopedEnv;
        if (env) {
            jclass activityThreadClass = env->FindClass("android/app/ActivityThread");
            if (activityThreadClass) {
                jmethodID currentAppMethod = env->GetStaticMethodID(activityThreadClass, "currentApplication", "()Landroid/app/Application;");
                if (currentAppMethod) {
                    jobject appContext = env->CallStaticObjectMethod(activityThreadClass, currentAppMethod);
                    loaderInitInfo.applicationContext = appContext;
                }
                env->DeleteLocalRef(activityThreadClass);
            }
        }
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
    // Get Java VM from JNIUtils (set during JNI_OnLoad)
    androidCreateInfo.applicationVM = JNIUtils::g_jvm;
    // Get the application context via JNI - must be called on a thread with JNI env
    JNIUtils::ScopedJNIENV scopedEnv;
    JNIEnv* env = *scopedEnv;
    if (env) {
        // Get the current application context via ActivityThread
        jclass activityThreadClass = env->FindClass("android/app/ActivityThread");
        if (activityThreadClass) {
            jmethodID currentAppMethod = env->GetStaticMethodID(activityThreadClass, "currentApplication", "()Landroid/app/Application;");
            if (currentAppMethod) {
                jobject appContext = env->CallStaticObjectMethod(activityThreadClass, currentAppMethod);
                if (appContext) {
                    androidCreateInfo.applicationActivity = env->NewGlobalRef(appContext);
                    LogInfo("Got application context for OpenXR");
                } else {
                    LogError("currentApplication() returned null");
                }
                env->DeleteLocalRef(appContext);
            }
            env->DeleteLocalRef(activityThreadClass);
        } else {
            LogError("Could not find ActivityThread class");
        }
    } else {
        LogError("No JNI environment available");
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
    return CheckXrResult(result, "xrGetInstanceProcAddr(xrGetVulkanGraphicsRequirements2KHR)");
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
    __android_log_print(ANDROID_LOG_DEBUG, "OpenXRManager", "xrCreateSession result: %d", result);
    if (XR_FAILED(result)) {
        __android_log_print(ANDROID_LOG_DEBUG, "OpenXRManager", "First attempt failed, retrying with fresh Vulkan...");
        LogInfo("Retrying with fresh Vulkan objects...");

        // Create a minimal Vulkan instance
        VkApplicationInfo appInfo = {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Cemu";
        appInfo.apiVersion = VK_API_VERSION_1_1;

        std::vector<const char*> instExts = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_ANDROID_SURFACE_EXTENSION_NAME,
            VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
            VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME,
            VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
            VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
        };

        VkInstanceCreateInfo instCI = {};
        instCI.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instCI.pApplicationInfo = &appInfo;
        instCI.enabledExtensionCount = instExts.size();
        instCI.ppEnabledExtensionNames = instExts.data();

        VkInstance freshInstance;
        if (vkCreateInstance(&instCI, nullptr, &freshInstance) != VK_SUCCESS) {
            LogError("Failed to create fresh Vulkan instance");
            return false;
        }

        uint32_t devCount = 0;
        vkEnumeratePhysicalDevices(freshInstance, &devCount, nullptr);
        std::vector<VkPhysicalDevice> devs(devCount);
        vkEnumeratePhysicalDevices(freshInstance, &devCount, devs.data());

        float qp = 1.0f;
        VkDeviceQueueCreateInfo qCI = {};
        qCI.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qCI.queueFamilyIndex = 0;
        qCI.queueCount = 1;
        qCI.pQueuePriorities = &qp;

        std::vector<const char*> devExts = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
            VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
            VK_KHR_EXTERNAL_FENCE_EXTENSION_NAME,
            VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
            VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
        };

        VkDeviceCreateInfo devCI = {};
        devCI.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        devCI.queueCreateInfoCount = 1;
        devCI.pQueueCreateInfos = &qCI;
        devCI.enabledExtensionCount = devExts.size();
        devCI.ppEnabledExtensionNames = devExts.data();

        VkDevice freshDevice;
        if (vkCreateDevice(devs[0], &devCI, nullptr, &freshDevice) != VK_SUCCESS) {
            LogError("Failed to create fresh Vulkan device");
            vkDestroyInstance(freshInstance, nullptr);
            return false;
        }

        LogInfo("Created fresh Vulkan objects with OpenXR-required extensions");

        vulkanBinding.instance = freshInstance;
        vulkanBinding.physicalDevice = devs[0];
        vulkanBinding.device = freshDevice;

        result = p_xrCreateSession(m_instance, &sessionCreateInfo, &m_session);
        if (!CheckXrResult(result, "p_xrCreateSession (retry)")) {
            vkDestroyDevice(freshDevice, nullptr);
            vkDestroyInstance(freshInstance, nullptr);
            return false;
        }
        // Store fresh objects (they need to stay alive)
        m_vkInstance = freshInstance;
        m_vkPhysicalDevice = devs[0];
        m_vkDevice = freshDevice;
    }
    return true;
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
    XrEventDataBuffer eventBuffer = {XR_TYPE_EVENT_DATA_BUFFER};

    while (XR_SUCCEEDED(p_xrPollEvent(m_instance, &eventBuffer))) {
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
    return m_frameState.shouldRender;
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

    XrSwapchainImageReleaseInfo releaseInfo = {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    XrResult result = p_xrReleaseSwapchainImage(m_swapchain, &releaseInfo);
    CheckXrResult(result, "p_xrReleaseSwapchainImage");

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

    XrResult result = p_xrEndFrame(m_session, &frameEndInfo);
    m_frameActive = false;

    return CheckXrResult(result, "p_xrEndFrame");
}

void OpenXRManager::Shutdown()
{
    LogInfo("Shutting down OpenXR");

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

    if (m_instance != XR_NULL_HANDLE) {
        p_xrDestroyInstance(m_instance);
        m_instance = XR_NULL_HANDLE;
    }

    // Unload the OpenXR library
    if (m_openxrLibrary) {
        dlclose(m_openxrLibrary);
        m_openxrLibrary = nullptr;
    }

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

    // Clear state
    m_openxrLoaded = false;
    m_sessionState = XR_SESSION_STATE_UNKNOWN;
    m_frameActive = false;
    m_swapchainImages.clear();
    m_swapchainWidth = m_swapchainHeight = 0;
    m_swapchainFormat = VK_FORMAT_UNDEFINED;
    m_acquiredImageIndex = UINT32_MAX;

    LogInfo("OpenXR shutdown complete");
}