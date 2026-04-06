#include "SwapchainInfoXR.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include <android/log.h>

#define LOG_TAG "SwapchainInfoXR"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

SwapchainInfoXR::SwapchainInfoXR(OpenXRManager* openxrManager, bool mainWindow, Vector2i size)
    : m_openxrManager(openxrManager)
    , m_mainWindow(mainWindow)
    , m_desiredExtent(size)
{
    LOGI("Creating SwapchainInfoXR %dx%d", size.x, size.y);
}

SwapchainInfoXR::~SwapchainInfoXR()
{
    Cleanup();
}

void SwapchainInfoXR::Create()
{
    if (!m_openxrManager) {
        LOGE("OpenXR manager is null");
        return;
    }

    // Get Vulkan device from renderer
    auto* renderer = VulkanRenderer::GetInstance();
    m_logicalDevice = renderer->GetLogicalDevice();

    // Get swapchain images from OpenXR
    m_swapchainImages = m_openxrManager->GetSwapchainImages();
    if (m_swapchainImages.empty()) {
        LOGE("No OpenXR swapchain images available");
        return;
    }

    // Set up swapchain properties
    uint32_t width, height;
    m_openxrManager->GetSwapchainSize(width, height);
    m_actualExtent = {width, height};

    // Use SRGB format for proper color space
    m_surfaceFormat.format = VK_FORMAT_R8G8B8A8_SRGB;
    m_surfaceFormat.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

    LOGI("OpenXR swapchain: %dx%d, %zu images", width, height, m_swapchainImages.size());

    // Create Vulkan resources
    CreateRenderPass();
    CreateImageViews();
    CreateFramebuffers();

    m_isValid = true;
    LOGI("SwapchainInfoXR created successfully");
}

void SwapchainInfoXR::Cleanup()
{
    if (!m_logicalDevice) return;

    LOGI("Cleaning up SwapchainInfoXR");

    DestroyVulkanObjects();

    m_swapchainImages.clear();
    m_isValid = false;
    m_currentImageIndex = UINT32_MAX;
    m_frameActive = false;
}

bool SwapchainInfoXR::IsValid() const
{
    return m_isValid && m_openxrManager && m_openxrManager->IsSessionRunning();
}

bool SwapchainInfoXR::AcquireImage()
{
    if (!IsValid()) {
        return false;
    }

    // Poll OpenXR events
    m_openxrManager->PollEvents();

    // Begin OpenXR frame
    if (!m_openxrManager->BeginFrame()) {
        return false;
    }

    // Acquire swapchain image
    m_currentImageIndex = m_openxrManager->AcquireSwapchainImage();
    if (m_currentImageIndex == UINT32_MAX) {
        return false;
    }

    m_frameActive = true;
    return true;
}

VkImage SwapchainInfoXR::GetCurrentImage() const
{
    if (m_currentImageIndex >= m_swapchainImages.size()) {
        return VK_NULL_HANDLE;
    }
    return m_swapchainImages[m_currentImageIndex];
}

VkImageView SwapchainInfoXR::GetCurrentImageView() const
{
    if (m_currentImageIndex >= m_swapchainImageViews.size()) {
        return VK_NULL_HANDLE;
    }
    return m_swapchainImageViews[m_currentImageIndex];
}

VkFramebuffer SwapchainInfoXR::GetCurrentFramebuffer() const
{
    if (m_currentImageIndex >= m_swapchainFramebuffers.size()) {
        return VK_NULL_HANDLE;
    }
    return m_swapchainFramebuffers[m_currentImageIndex];
}

void SwapchainInfoXR::PresentImage()
{
    if (!m_frameActive || !m_openxrManager) {
        return;
    }

    // Release the swapchain image
    m_openxrManager->ReleaseSwapchainImage();

    // End the OpenXR frame with default quad settings
    m_openxrManager->EndFrame(DefaultQuadPose, DefaultQuadSize);

    m_frameActive = false;
    m_currentImageIndex = UINT32_MAX;
}

void SwapchainInfoXR::CreateRenderPass()
{
    // Create a simple color attachment render pass
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = m_surfaceFormat.format;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    VkResult result = vkCreateRenderPass(m_logicalDevice, &renderPassInfo, nullptr, &m_swapchainRenderPass);
    if (result != VK_SUCCESS) {
        LOGE("Failed to create render pass: %d", result);
    }
}

void SwapchainInfoXR::CreateImageViews()
{
    m_swapchainImageViews.resize(m_swapchainImages.size());

    for (size_t i = 0; i < m_swapchainImages.size(); i++) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_surfaceFormat.format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        VkResult result = vkCreateImageView(m_logicalDevice, &viewInfo, nullptr, &m_swapchainImageViews[i]);
        if (result != VK_SUCCESS) {
            LOGE("Failed to create image view %zu: %d", i, result);
        }
    }
}

void SwapchainInfoXR::CreateFramebuffers()
{
    m_swapchainFramebuffers.resize(m_swapchainImageViews.size());

    for (size_t i = 0; i < m_swapchainImageViews.size(); i++) {
        VkImageView attachments[] = { m_swapchainImageViews[i] };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = m_swapchainRenderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = m_actualExtent.width;
        framebufferInfo.height = m_actualExtent.height;
        framebufferInfo.layers = 1;

        VkResult result = vkCreateFramebuffer(m_logicalDevice, &framebufferInfo, nullptr, &m_swapchainFramebuffers[i]);
        if (result != VK_SUCCESS) {
            LOGE("Failed to create framebuffer %zu: %d", i, result);
        }
    }
}

void SwapchainInfoXR::DestroyVulkanObjects()
{
    // Destroy framebuffers
    for (auto framebuffer : m_swapchainFramebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(m_logicalDevice, framebuffer, nullptr);
        }
    }
    m_swapchainFramebuffers.clear();

    // Destroy image views
    for (auto imageView : m_swapchainImageViews) {
        if (imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(m_logicalDevice, imageView, nullptr);
        }
    }
    m_swapchainImageViews.clear();

    // Destroy render pass
    if (m_swapchainRenderPass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(m_logicalDevice, m_swapchainRenderPass, nullptr);
        m_swapchainRenderPass = VK_NULL_HANDLE;
    }
}