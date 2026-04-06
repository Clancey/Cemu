#pragma once

#include "OpenXRManager.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/SwapchainInfoVk.h"
#include "util/math/vector2.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <memory>

/**
 * SwapchainInfoXR implements a Vulkan swapchain interface that uses OpenXR
 * swapchain images instead of VkSwapchainKHR. This allows Cemu's VulkanRenderer
 * to render to OpenXR images for VR display.
 *
 * This class adapts the OpenXR swapchain to match the interface expected by
 * VulkanRenderer, providing seamless integration with the existing rendering
 * pipeline.
 */
class SwapchainInfoXR
{
public:
    SwapchainInfoXR(OpenXRManager* openxrManager, bool mainWindow, Vector2i size);
    ~SwapchainInfoXR();

    // Delete copy constructor and assignment operator
    SwapchainInfoXR(const SwapchainInfoXR&) = delete;
    SwapchainInfoXR& operator=(const SwapchainInfoXR&) = delete;

    /**
     * Create the OpenXR swapchain and set up Vulkan resources.
     * This replaces the traditional VkSwapchainKHR creation.
     */
    void Create();

    /**
     * Clean up all resources.
     */
    void Cleanup();

    /**
     * Check if the swapchain is valid and ready for rendering.
     */
    bool IsValid() const;

    /**
     * Begin OpenXR frame and acquire swapchain image.
     * This combines xrWaitFrame, xrBeginFrame, and xrAcquireSwapchainImage.
     * @return true if rendering should proceed, false if frame should be skipped
     */
    bool AcquireImage();

    /**
     * Get the current swapchain image index.
     */
    uint32_t GetCurrentImageIndex() const { return m_currentImageIndex; }

    /**
     * Get the Vulkan image for the current swapchain index.
     */
    VkImage GetCurrentImage() const;

    /**
     * Get the image view for the current swapchain index.
     */
    VkImageView GetCurrentImageView() const;

    /**
     * Get the framebuffer for the current swapchain index.
     */
    VkFramebuffer GetCurrentFramebuffer() const;

    /**
     * Get all swapchain images.
     */
    const std::vector<VkImage>& GetImages() const { return m_swapchainImages; }

    /**
     * Get all image views.
     */
    const std::vector<VkImageView>& GetImageViews() const { return m_swapchainImageViews; }

    /**
     * Get all framebuffers.
     */
    const std::vector<VkFramebuffer>& GetFramebuffers() const { return m_swapchainFramebuffers; }

    /**
     * Get swapchain extent.
     */
    VkExtent2D GetExtent() const { return m_actualExtent; }

    /**
     * Get swapchain format.
     */
    VkFormat GetFormat() const { return m_surfaceFormat.format; }

    /**
     * Get the render pass used for this swapchain.
     */
    VkRenderPass GetRenderPass() const { return m_swapchainRenderPass; }

    /**
     * End the current frame and submit for OpenXR composition.
     * This handles xrReleaseSwapchainImage and xrEndFrame.
     */
    void PresentImage();

    /**
     * Simulate present semaphore for compatibility with VulkanRenderer.
     * OpenXR handles synchronization internally.
     */
    VkSemaphore ConsumePresentSemaphore() { return VK_NULL_HANDLE; }

private:
    OpenXRManager* m_openxrManager = nullptr;
    bool m_mainWindow = false;
    Vector2i m_desiredExtent;
    VkExtent2D m_actualExtent{};
    VkSurfaceFormatKHR m_surfaceFormat{};

    // OpenXR and Vulkan state
    bool m_isValid = false;
    uint32_t m_currentImageIndex = UINT32_MAX;
    bool m_frameActive = false;

    // Swapchain images and views (from OpenXR)
    std::vector<VkImage> m_swapchainImages;
    std::vector<VkImageView> m_swapchainImageViews;
    std::vector<VkFramebuffer> m_swapchainFramebuffers;

    // Vulkan objects (we create these)
    VkDevice m_logicalDevice = VK_NULL_HANDLE;
    VkRenderPass m_swapchainRenderPass = VK_NULL_HANDLE;

    // Helper methods
    void CreateImageViews();
    void CreateFramebuffers();
    void CreateRenderPass();
    void DestroyVulkanObjects();

    // Default quad pose and size for OpenXR composition
    static constexpr XrPosef DefaultQuadPose = {
        .orientation = {.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f},
        .position = {.x = 0.0f, .y = 0.0f, .z = -2.0f}
    };
    static constexpr XrExtent2Df DefaultQuadSize = {.width = 2.0f, .height = 1.125f};
};