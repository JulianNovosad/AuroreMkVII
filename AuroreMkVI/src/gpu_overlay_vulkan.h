// Verified headers: [vulkan/vulkan.h, gbm.h, xf86drm.h, vector, string, pipeline_structs.h]
// Verification timestamp: 2026-02-07 15:00:00 (Updated for Vulkan conversion)
#pragma once

#include <vulkan/vulkan.h>
// #include <vulkan/vulkan_wayland.h> // Not directly needed for headless DRM/KMS
// #include <vulkan/vulkan_gbm.h>    // Removed due to "No such file or directory"
#include <gbm.h>
#include <xf86drm.h>
#include <vector>
#include <string>
#include <stdexcept>
#include <iostream>
#include <set>
#include <algorithm>

#include "pipeline_structs.h"
#include "util_logging.h" // For APP_LOG_ERROR, etc.

const int MAX_FRAMES_IN_FLIGHT = 2;

struct QueueFamilyIndices {
    uint32_t graphicsFamily = (uint32_t)-1;
    uint32_t presentFamily = (uint32_t)-1; // For offscreen, can be same as graphics

    bool isComplete() {
        return graphicsFamily != (uint32_t)-1; // Only graphics is strictly needed for offscreen
    }
};

// Removed SwapChainSupportDetails as we are not using a traditional swapchain.

class GpuOverlay {
public:
    GpuOverlay(int width, int height);
    ~GpuOverlay();

    bool initialize(int drm_fd); // Pass DRM FD for GBM integration
    void render(int dma_buf_fd, size_t dma_buf_size, uint64_t frame_counter); // Takes DMA-BUF FD directly
    void set_tpu_temperature(float temp) { tpu_temperature_ = temp; } // Kept for consistency, though TPU is disabled

    // Accessor for the rendered DMA-BUF (output image)
    int get_rendered_dma_buf_fd() const;

private:
    int width_;
    int height_;
    float tpu_temperature_ = 0.0f; // Kept for consistency
    uint32_t graphicsFamilyIndex_ = (uint32_t)-1; // Store graphics family index

    // DRM/GBM context
    int drm_fd_ = -1;
    gbm_device* gbm_dev_ = nullptr;
    gbm_surface* gbm_surface_ = nullptr; // For output to DRM, not for Vulkan swapchain

    // Vulkan objects
    VkInstance instance_;
    VkDebugUtilsMessengerEXT debugMessenger_;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_;
    VkQueue graphicsQueue_;
    VkQueue presentQueue_; // Will be the same as graphicsQueue for offscreen rendering
    VkSurfaceKHR surface_ = VK_NULL_HANDLE; // Not used for direct presentation, but some extensions might expect it.

    // Output render target (DMA-BUF exportable)
    VkImage dmaBufImage_ = VK_NULL_HANDLE;
    VkDeviceMemory dmaBufImageMemory_ = VK_NULL_HANDLE;
    VkImageView dmaBufImageView_ = VK_NULL_HANDLE;
    
    // Imported Camera Image (DMA-BUF from camera)
    VkImage importedCameraImage_ = VK_NULL_HANDLE;
    VkDeviceMemory importedCameraImageMemory_ = VK_NULL_HANDLE;
    VkImageView importedCameraImageView_ = VK_NULL_HANDLE;
    VkSampler importedCameraSampler_ = VK_NULL_HANDLE; // Sampler for the imported camera image

    VkRenderPass renderPass_;
    VkPipelineLayout pipelineLayout_;
    VkPipeline graphicsPipeline_;

    VkCommandPool commandPool_;
    std::vector<VkCommandBuffer> commandBuffers_;

    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    std::vector<VkFence> inFlightFences_;
    std::vector<VkFence> imagesInFlight_; // To track fences for images in flight
    size_t currentFrame_ = 0;

    // Vertex data for a simple textured quad
    VkBuffer vertexBuffer_;
    VkDeviceMemory vertexBufferMemory_;
    VkBuffer indexBuffer_; // For indexed drawing
    VkDeviceMemory indexBufferMemory_;
    
    // Descriptor Set for texture
    VkDescriptorSetLayout descriptorSetLayout_;
    VkDescriptorPool descriptorPool_;
    std::vector<VkDescriptorSet> descriptorSets_;

    // Added missing members for rendering infrastructure
    VkFormat swapChainImageFormat_; // For our single output image format
    VkExtent2D swapChainExtent_;    // For our single output image extent
    std::vector<VkFramebuffer> swapChainFramebuffers_; // For our single output image framebuffer

    // Helper functions
    void createInstance();
    void setupDebugMessenger();
    void createSurface(); // Will be a no-op or abstract
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createRenderTargets(); // Creates our single output DMA-BUF exportable image
    void createImageViews(); // Creates image view for dmaBufImage_
    void createRenderPass();
    void createGraphicsPipeline();
    void createFramebuffers();
    void createCommandPool();
    void createTextureSampler(); // Creates sampler for importedCameraImage_
    void createVertexBuffer();
    void createIndexBuffer();
    void createDescriptorSetLayout();
    void createDescriptorPool();
    void createDescriptorSets();
    void createCommandBuffers();
    void createSyncObjects();
    void cleanupRenderTargets(); // Cleans up output render target resources

    // Vulkan utility functions
    QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
    bool isDeviceSuitable(VkPhysicalDevice device);
    bool checkDeviceExtensionSupport(VkPhysicalDevice device);
    VkShaderModule createShaderModule(const unsigned int* code, size_t codeSize); // Updated signature
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    // DMA-BUF import/export functions
    void importDmaBufImage(int dma_buf_fd, size_t dma_buf_size, int width, int height, VkFormat format, uint32_t queueFamilyIndex);
    int exportDmaBufImage(); // Function to export the rendered dmaBufImage_
    void transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout);
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);
    void createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory);
    VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags);
    
    // Function pointers for Vulkan extensions
    PFN_vkGetMemoryFdKHR vkGetMemoryFdKHR_ = nullptr;
    PFN_vkGetMemoryFdPropertiesKHR vkGetMemoryFdPropertiesKHR_ = nullptr;
    PFN_vkGetPhysicalDeviceImageFormatProperties2 vkGetPhysicalDeviceImageFormatProperties2_ = nullptr;
    PFN_vkGetPhysicalDeviceExternalBufferProperties vkGetPhysicalDeviceExternalBufferProperties_ = nullptr;

    // For Debugging
    bool checkValidationLayerSupport();
    std::vector<const char*> getRequiredExtensions();
    static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData);
    static PFN_vkCreateDebugUtilsMessengerEXT pfnVkCreateDebugUtilsMessengerEXT;
    static PFN_vkDestroyDebugUtilsMessengerEXT pfnVkDestroyDebugUtilsMessengerEXT;

    const std::vector<const char*> validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    const std::vector<const char*> deviceExtensions = {
        VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
        VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME, // For YUV formats, needed later
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME // For querying image format properties
    };

#ifdef NDEBUG
    const bool enableValidationLayers = false;
#else
    const bool enableValidationLayers = true;
#endif
};