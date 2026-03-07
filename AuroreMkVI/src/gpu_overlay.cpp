// Verified headers: [vulkan/vulkan.h, gbm.h, xf86drm.h, cstring, fcntl.h, unistd.h, stdexcept, iostream, vector, set, algorithm]
// Verification timestamp: 2026-02-07 15:00:00 (Updated for Vulkan conversion)
#include "gpu_overlay.h"
#include "util_logging.h" // For APP_LOG_ERROR, etc.

#include <stdexcept>
#include <iostream>
#include <set>
#include <algorithm>
#include <fstream>
#include <array>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// These will be defined by the generated SPIR-V headers.
#include "simple_vert.spv.h"
#include "simple_frag.spv.h"


// PFNs for Debug Messenger
PFN_vkCreateDebugUtilsMessengerEXT GpuOverlay::pfnVkCreateDebugUtilsMessengerEXT = nullptr;
PFN_vkDestroyDebugUtilsMessengerEXT GpuOverlay::pfnVkDestroyDebugUtilsMessengerEXT = nullptr;

// Vertex definition for a simple quad
struct Vertex {
    glm::vec2 pos;
    glm::vec2 texCoord;

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 2> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 2> attributeDescriptions{};
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, texCoord);
        return attributeDescriptions;
    }
};

const std::vector<Vertex> quadVertices = {
    {{-1.0f, -1.0f}, {0.0f, 1.0f}}, // Bottom Left
    {{ 1.0f, -1.0f}, {1.0f, 1.0f}}, // Bottom Right
    {{-1.0f,  1.0f}, {0.0f, 0.0f}}, // Top Left
    {{ 1.0f,  1.0f}, {1.0f, 0.0f}}  // Top Right
};

const std::vector<uint32_t> quadIndices = {
    0, 1, 2, 2, 1, 3 // Two triangles to form a quad
};


// Debug callback implementation
VKAPI_ATTR VkBool32 VKAPI_CALL GpuOverlay::debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {

    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        APP_LOG_WARNING(std::string("Validation Layer: ") + pCallbackData->pMessage);
    } else {
        APP_LOG_INFO(std::string("Validation Layer: ") + pCallbackData->pMessage);
    }
    (void)messageType; // Suppress unused parameter warning
    (void)pUserData;   // Suppress unused parameter warning
    return VK_FALSE;
}

GpuOverlay::GpuOverlay(int width, int height)
    : width_(width),
      height_(height),
      tpu_temperature_(0.0f),
      graphicsFamilyIndex_((uint32_t)-1),
      drm_fd_(-1),
      gbm_dev_(nullptr),
      gbm_surface_(nullptr),
      instance_(VK_NULL_HANDLE),
      debugMessenger_(VK_NULL_HANDLE),
      physicalDevice_(VK_NULL_HANDLE),
      device_(VK_NULL_HANDLE),
      surface_(VK_NULL_HANDLE),
      dmaBufImage_(VK_NULL_HANDLE),
      dmaBufImageMemory_(VK_NULL_HANDLE),
      dmaBufImageView_(VK_NULL_HANDLE),
      importedCameraImage_(VK_NULL_HANDLE),
      importedCameraImageMemory_(VK_NULL_HANDLE),
      importedCameraImageView_(VK_NULL_HANDLE),
      importedCameraSampler_(VK_NULL_HANDLE),
      renderPass_(VK_NULL_HANDLE),
      pipelineLayout_(VK_NULL_HANDLE),
      graphicsPipeline_(VK_NULL_HANDLE),
      commandPool_(VK_NULL_HANDLE),
      vertexBuffer_(VK_NULL_HANDLE),
      vertexBufferMemory_(VK_NULL_HANDLE),
      indexBuffer_(VK_NULL_HANDLE),
      indexBufferMemory_(VK_NULL_HANDLE),
      descriptorSetLayout_(VK_NULL_HANDLE),
      descriptorPool_(VK_NULL_HANDLE),
      descriptorSets_(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE),
      swapChainImageFormat_(VK_FORMAT_UNDEFINED),
      swapChainExtent_({static_cast<uint32_t>(width), static_cast<uint32_t>(height)}),
      swapChainFramebuffers_(1, VK_NULL_HANDLE),
      // Initialize PFNs to nullptr
      vkGetMemoryFdKHR_(nullptr),
      vkGetMemoryFdPropertiesKHR_(nullptr),
      vkGetPhysicalDeviceImageFormatProperties2_(nullptr),
      vkGetPhysicalDeviceExternalBufferProperties_(nullptr) {
    APP_LOG_INFO("GpuOverlay: Constructor entered.");
}

GpuOverlay::~GpuOverlay() {
    // 0. Ensure device is idle before destroying any device-level objects
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }

    // 1. Clean up render target specific resources (framebuffers, pipeline, pipeline layout, render pass)
    cleanupRenderTargets(); 

    // 2. Destroy synchronization objects
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (renderFinishedSemaphores_[i] != VK_NULL_HANDLE) vkDestroySemaphore(device_, renderFinishedSemaphores_[i], nullptr);
        if (imageAvailableSemaphores_[i] != VK_NULL_HANDLE) vkDestroySemaphore(device_, imageAvailableSemaphores_[i], nullptr);
        if (inFlightFences_[i] != VK_NULL_HANDLE) vkDestroyFence(device_, inFlightFences_[i], nullptr);
    }

    // 3. Destroy command pool
    if (commandPool_ != VK_NULL_HANDLE) vkDestroyCommandPool(device_, commandPool_, nullptr);

    // 4. Destroy descriptor sets, layout, and pool
    if (descriptorSetLayout_ != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
    if (descriptorPool_ != VK_NULL_HANDLE) vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);

    // 5. Destroy vertex and index buffers and their memories
    if (indexBuffer_ != VK_NULL_HANDLE) vkDestroyBuffer(device_, indexBuffer_, nullptr);
    if (indexBufferMemory_ != VK_NULL_HANDLE) vkFreeMemory(device_, indexBufferMemory_, nullptr);
    if (vertexBuffer_ != VK_NULL_HANDLE) vkDestroyBuffer(device_, vertexBuffer_, nullptr);
    if (vertexBufferMemory_ != VK_NULL_HANDLE) vkFreeMemory(device_, vertexBufferMemory_, nullptr);

    // 6. Destroy imported camera image resources
    if (importedCameraSampler_ != VK_NULL_HANDLE) vkDestroySampler(device_, importedCameraSampler_, nullptr);
    if (importedCameraImageView_ != VK_NULL_HANDLE) vkDestroyImageView(device_, importedCameraImageView_, nullptr);
    if (importedCameraImage_ != VK_NULL_HANDLE) vkDestroyImage(device_, importedCameraImage_, nullptr);
    if (importedCameraImageMemory_ != VK_NULL_HANDLE) vkFreeMemory(device_, importedCameraImageMemory_, nullptr);

    // 7. Destroy output DMA-BUF resources
    if (dmaBufImageView_ != VK_NULL_HANDLE) vkDestroyImageView(device_, dmaBufImageView_, nullptr);
    if (dmaBufImage_ != VK_NULL_HANDLE) vkDestroyImage(device_, dmaBufImage_, nullptr);
    if (dmaBufImageMemory_ != VK_NULL_HANDLE) vkFreeMemory(device_, dmaBufImageMemory_, nullptr);
    
    // 8. Destroy logical device (this MUST be done AFTER all device-level objects are destroyed)
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
    }

    // 9. Destroy debug messenger (if enabled)
    if (enableValidationLayers && debugMessenger_ != VK_NULL_HANDLE) {
        pfnVkDestroyDebugUtilsMessengerEXT(instance_, debugMessenger_, nullptr);
    }

    // 10. Destroy surface (if not VK_NULL_HANDLE)
    // This is VK_NULL_HANDLE for our headless setup, but included for completeness.
    if (surface_ != VK_NULL_HANDLE) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    
    // 11. Destroy Vulkan instance (this MUST be done AFTER all other Vulkan objects)
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
    }

    // 12. Destroy GBM surface, GBM device, and close DRM FD
    if (gbm_surface_) gbm_surface_destroy(gbm_surface_);
    if (gbm_dev_) gbm_device_destroy(gbm_dev_);
    if (drm_fd_ != -1) close(drm_fd_);
}

bool GpuOverlay::initialize(int drm_fd) {
    APP_LOG_INFO("GpuOverlay: Initializing Vulkan...");
    drm_fd_ = drm_fd;

    // Initialize GBM
    gbm_dev_ = gbm_create_device(drm_fd_);
    if (!gbm_dev_) {
        APP_LOG_ERROR("GpuOverlay: Failed to create GBM device.");
        return false;
    }
    APP_LOG_INFO("GpuOverlay: GBM device created.");

    APP_LOG_INFO("GpuOverlay: Creating instance...");
    createInstance();
    APP_LOG_INFO("GpuOverlay: Instance created.");

    APP_LOG_INFO("GpuOverlay: Setting up debug messenger...");
    setupDebugMessenger();
    APP_LOG_INFO("GpuOverlay: Debug messenger set up.");

    APP_LOG_INFO("GpuOverlay: Creating surface...");
    createSurface();
    APP_LOG_INFO("GpuOverlay: Surface created.");

    APP_LOG_INFO("GpuOverlay: Picking physical device...");
    pickPhysicalDevice();
    APP_LOG_INFO("GpuOverlay: Physical device picked.");

    APP_LOG_INFO("GpuOverlay: Creating logical device...");
    createLogicalDevice();
    APP_LOG_INFO("GpuOverlay: Logical device created.");

    APP_LOG_INFO("GpuOverlay: Creating render targets...");
    createRenderTargets(); // This creates our DMA-BUF exportable image
    APP_LOG_INFO("GpuOverlay: Render targets created.");

    APP_LOG_INFO("GpuOverlay: Creating image views...");
    createImageViews(); // Creates image view for dmaBufImage_
    APP_LOG_INFO("GpuOverlay: Image views created.");

    APP_LOG_INFO("GpuOverlay: Creating render pass...");
    createRenderPass();
    APP_LOG_INFO("GpuOverlay: Render pass created.");

    APP_LOG_INFO("GpuOverlay: Creating graphics pipeline...");
    createGraphicsPipeline();
    APP_LOG_INFO("GpuOverlay: Graphics pipeline created.");

    APP_LOG_INFO("GpuOverlay: Creating framebuffers...");
    createFramebuffers();
    APP_LOG_INFO("GpuOverlay: Framebuffers created.");

    APP_LOG_INFO("GpuOverlay: Creating command pool...");
    createCommandPool();
    APP_LOG_INFO("GpuOverlay: Command pool created.");

    APP_LOG_INFO("GpuOverlay: Creating texture sampler...");
    createTextureSampler(); // Creates sampler for importedCameraImage_
    APP_LOG_INFO("GpuOverlay: Texture sampler created.");

    APP_LOG_INFO("GpuOverlay: Creating vertex buffer...");
    createVertexBuffer();
    APP_LOG_INFO("GpuOverlay: Vertex buffer created.");

    APP_LOG_INFO("GpuOverlay: Creating index buffer...");
    createIndexBuffer();
    APP_LOG_INFO("GpuOverlay: Index buffer created.");

    APP_LOG_INFO("GpuOverlay: Creating descriptor set layout...");
    createDescriptorSetLayout();
    APP_LOG_INFO("GpuOverlay: Descriptor set layout created.");

    APP_LOG_INFO("GpuOverlay: Creating descriptor pool...");
    createDescriptorPool();
    APP_LOG_INFO("GpuOverlay: Descriptor pool created.");

    APP_LOG_INFO("GpuOverlay: Creating descriptor sets...");
    createDescriptorSets();
    APP_LOG_INFO("GpuOverlay: Descriptor sets created.");

    APP_LOG_INFO("GpuOverlay: Creating command buffers...");
    createCommandBuffers();
    APP_LOG_INFO("GpuOverlay: Command buffers created.");

    APP_LOG_INFO("GpuOverlay: Creating sync objects...");
    createSyncObjects();
    APP_LOG_INFO("GpuOverlay: Sync objects created.");

    return true;
}

void GpuOverlay::render(int dma_buf_fd, size_t dma_buf_size, uint64_t frame_counter) {
    (void)frame_counter; // Suppress unused parameter warning

    if (dma_buf_fd != -1) {
        // Assume camera format is YUV420 as per camera_capture.cpp
        // For simplicity in Phase 1, we import as VK_FORMAT_R8G8B8A8_UNORM
        // A real YUV format would require VK_KHR_sampler_ycbcr_conversion
        importDmaBufImage(dma_buf_fd, dma_buf_size, width_, height_, VK_FORMAT_R8G8B8A8_UNORM, graphicsFamilyIndex_);
    }

    vkWaitForFences(device_, 1, &inFlightFences_[currentFrame_], VK_TRUE, UINT64_MAX);
    
    // We only have one target image for offscreen rendering.
    // We must wait for the output image (dmaBufImage_) to be available.
    // imageIndex is always 0 since we have only one output image.
    uint32_t imageIndex = 0; 

    // No vkAcquireNextImageKHR or presentation for offscreen rendering.
    // Instead, we just reset the fence for the current frame.
    if (imagesInFlight_[imageIndex] != VK_NULL_HANDLE) { // Ensure no previous work is using it
        vkWaitForFences(device_, 1, &imagesInFlight_[imageIndex], VK_TRUE, UINT64_MAX);
    }
    // Mark this image as being in use by this frame
    imagesInFlight_[imageIndex] = inFlightFences_[currentFrame_];

    vkResetFences(device_, 1, &inFlightFences_[currentFrame_]);

    vkResetCommandBuffer(commandBuffers_[currentFrame_], /*VkCommandBufferResetFlagBits*/ 0);
    
    // Begin recording
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0; // Optional
    beginInfo.pInheritanceInfo = nullptr; // Optional

    if (vkBeginCommandBuffer(commandBuffers_[currentFrame_], &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    // Begin render pass
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass_;
    renderPassInfo.framebuffer = swapChainFramebuffers_[0]; // Our single framebuffer
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapChainExtent_;

    std::array<VkClearValue, 1> clearColors{};
    clearColors[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}}; // Black background
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearColors.size());
    renderPassInfo.pClearValues = clearColors.data();

    vkCmdBeginRenderPass(commandBuffers_[currentFrame_], &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(commandBuffers_[currentFrame_], VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_);

    VkBuffer vertexBuffers[] = {vertexBuffer_};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffers_[currentFrame_], 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffers_[currentFrame_], indexBuffer_, 0, VK_INDEX_TYPE_UINT32);

    // Bind descriptor sets (for texture). This will now sample the *imported* camera image.
    vkCmdBindDescriptorSets(commandBuffers_[currentFrame_], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descriptorSets_[currentFrame_], 0, nullptr);

    vkCmdDrawIndexed(commandBuffers_[currentFrame_], static_cast<uint32_t>(quadIndices.size()), 1, 0, 0, 0);

    vkCmdEndRenderPass(commandBuffers_[currentFrame_]);

    if (vkEndCommandBuffer(commandBuffers_[currentFrame_]) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }

    // Submit command buffer
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    // No wait semaphores from image acquisition for offscreen render
    submitInfo.waitSemaphoreCount = 0;
    submitInfo.pWaitSemaphores = nullptr;
    submitInfo.pWaitDstStageMask = nullptr;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers_[currentFrame_];

    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores_[currentFrame_]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(graphicsQueue_, 1, &submitInfo, inFlightFences_[currentFrame_]) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit draw command buffer!");
    }
    
    // We are rendering to dmaBufImage_ (our output image).
    // The DRMDisplay module will now take the FD from GpuOverlay::get_rendered_dma_buf_fd()
    // and queue it for display. No vkQueuePresentKHR here.

    currentFrame_ = (currentFrame_ + 1) % MAX_FRAMES_IN_FLIGHT;
}

int GpuOverlay::get_rendered_dma_buf_fd() const {
    // We assume the rendered image is in dmaBufImage_ and its memory is dmaBufImageMemory_.
    // We need to export a FD for this memory.
    VkMemoryGetFdInfoKHR getFdInfo{};
    getFdInfo.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
    getFdInfo.memory = dmaBufImageMemory_;
    getFdInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    int fd = -1;
    if (vkGetMemoryFdKHR_(device_, &getFdInfo, &fd) != VK_SUCCESS) {
        APP_LOG_ERROR("Failed to export DMA-BUF FD for rendered image!");
        return -1;
    }
    return fd;
}

void GpuOverlay::createInstance() {
    if (enableValidationLayers && !checkValidationLayerSupport()) {
        throw std::runtime_error("validation layers requested, but not available!");
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Aurore Mk VI GPU Overlay";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    auto extensions = getRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();

        debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debugCreateInfo.pfnUserCallback = debugCallback;
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*) &debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }

    if (vkCreateInstance(&createInfo, nullptr, &instance_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create instance!");
    }

    // Load PFNs for debug messenger
    pfnVkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT");
    pfnVkDestroyDebugUtilsMessengerEXT = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT");
    
    // Load instance-level PFNs for external memory queries
    vkGetPhysicalDeviceImageFormatProperties2_ = (PFN_vkGetPhysicalDeviceImageFormatProperties2)vkGetInstanceProcAddr(instance_, "vkGetPhysicalDeviceImageFormatProperties2");
    vkGetPhysicalDeviceExternalBufferProperties_ = (PFN_vkGetPhysicalDeviceExternalBufferProperties)vkGetInstanceProcAddr(instance_, "vkGetPhysicalDeviceExternalBufferProperties");
    
    if (!vkGetPhysicalDeviceImageFormatProperties2_ || !vkGetPhysicalDeviceExternalBufferProperties_) {
        APP_LOG_ERROR("GpuOverlay: Failed to get all external memory instance function pointers.");
        throw std::runtime_error("Failed to get external memory instance function pointers!");
    }
}

void GpuOverlay::setupDebugMessenger() {
    if (!enableValidationLayers) return;

    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugCallback;

    if (pfnVkCreateDebugUtilsMessengerEXT(instance_, &createInfo, nullptr, &debugMessenger_) != VK_SUCCESS) {
        throw std::runtime_error("failed to set up debug messenger!");
    }
}

void GpuOverlay::createSurface() {
    // For headless DRM/KMS rendering, we don't need a traditional VkSurfaceKHR.
    // We render to a DMA-BUF exportable VkImage and let drm_display handle presentation.
    surface_ = VK_NULL_HANDLE; 
}

void GpuOverlay::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);

    if (deviceCount == 0) {
        throw std::runtime_error("failed to find GPUs with Vulkan support!");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());

    for (const auto& device : devices) {
        if (isDeviceSuitable(device)) {
            physicalDevice_ = device;
            break;
        }
    }

    if (physicalDevice_ == VK_NULL_HANDLE) {
        throw std::runtime_error("failed to find a suitable GPU!");
    }
}

bool GpuOverlay::isDeviceSuitable(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);

    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);

    bool extensionsSupported = checkDeviceExtensionSupport(device);

    QueueFamilyIndices indices = findQueueFamilies(device);
    graphicsFamilyIndex_ = indices.graphicsFamily; // Store graphics family index

    // Check for external memory support (DMA-BUF) for an output image
    VkPhysicalDeviceExternalBufferInfo externalBufferInfo{};
    externalBufferInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO;
    externalBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT; // Example usage
    externalBufferInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkExternalBufferProperties externalBufferProperties{};
    externalBufferProperties.sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES;
    // PFN for this is loaded at instance creation
    vkGetPhysicalDeviceExternalBufferProperties_(device, &externalBufferInfo, &externalBufferProperties);
    bool dmaBufExportSupported = (externalBufferProperties.externalMemoryProperties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT_KHR);

    return deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU && // RPi5 has integrated GPU
           deviceFeatures.samplerAnisotropy && // Example feature, check other needed
           indices.isComplete() &&
           extensionsSupported &&
           dmaBufExportSupported;
}

bool GpuOverlay::checkDeviceExtensionSupport(VkPhysicalDevice device) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);

    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions(deviceExtensions.begin(), deviceExtensions.end());

    for (const auto& extension : availableExtensions) {
        requiredExtensions.erase(extension.extensionName);
    }

    return requiredExtensions.empty();
}

QueueFamilyIndices GpuOverlay::findQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices;

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());

    int i = 0;
    for (const auto& queueFamily : queueFamilies) {
        if (queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }

        indices.presentFamily = i; // For offscreen, present family can be same as graphics

        if (indices.isComplete()) {
            break;
        }

        i++;
    }

    return indices;
}

void GpuOverlay::createLogicalDevice() {
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice_);

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily, indices.presentFamily}; // Present family now points to graphics family

    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = queueFamily;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfos.push_back(queueCreateInfo);
    }

    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE; // For texture sampling

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
    createInfo.pQueueCreateInfos = queueCreateInfos.data();
    createInfo.pEnabledFeatures = &deviceFeatures;

    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }

    if (vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create logical device!");
    }

    vkGetDeviceQueue(device_, indices.graphicsFamily, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, indices.presentFamily, 0, &presentQueue_); // Will be the same queue for offscreen rendering

    // Load device-level PFNs for external memory extensions
    vkGetMemoryFdKHR_ = (PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(device_, "vkGetMemoryFdKHR");
    vkGetMemoryFdPropertiesKHR_ = (PFN_vkGetMemoryFdPropertiesKHR)vkGetDeviceProcAddr(device_, "vkGetMemoryFdPropertiesKHR");
    
    if (!vkGetMemoryFdKHR_ || !vkGetMemoryFdPropertiesKHR_) {
        APP_LOG_ERROR("GpuOverlay: Failed to get all external memory extension device function pointers.");
        throw std::runtime_error("Failed to get external memory extension device function pointers!");
    }
}

void GpuOverlay::createRenderTargets() { // Renamed from createSwapChain
    APP_LOG_INFO("GpuOverlay: Creating offscreen DMA-BUF exportable image as render target.");

    VkFormat imageFormat = VK_FORMAT_B8G8R8A8_UNORM; // Common format for display/compositors
    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT; // Render to it, transfer src for export, sampled for debug/feedback

    // Check for external memory image format properties
    VkPhysicalDeviceExternalImageFormatInfo externalImageFormatInfo{};
    externalImageFormatInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
    externalImageFormatInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkPhysicalDeviceImageFormatInfo2 imageFormatInfo2{};
    imageFormatInfo2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
    imageFormatInfo2.format = imageFormat;
    imageFormatInfo2.type = VK_IMAGE_TYPE_2D;
    imageFormatInfo2.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageFormatInfo2.usage = usage;
    imageFormatInfo2.flags = 0; // No sparse binding, etc.
    imageFormatInfo2.pNext = &externalImageFormatInfo;

    VkImageFormatProperties2 imageFormatProperties2{};
    imageFormatProperties2.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
    // Chain VkExternalImageFormatProperties to output structure
    VkExternalImageFormatProperties externalImageFormatProperties{};
    externalImageFormatProperties.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;
    imageFormatProperties2.pNext = &externalImageFormatProperties;
    
    // PFN for this is loaded at instance creation
    VkResult result = vkGetPhysicalDeviceImageFormatProperties2_(physicalDevice_, &imageFormatInfo2, &imageFormatProperties2);
    if (result != VK_SUCCESS) {
        APP_LOG_ERROR("Failed to get physical device image format properties for external memory: " + std::to_string(result));
        throw std::runtime_error("Failed to get physical device image format properties for external memory!");
    }


    // Check externalMemoryFeatures from the chained struct
    if (!(externalImageFormatProperties.externalMemoryProperties.externalMemoryFeatures & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT_KHR)) {
         APP_LOG_ERROR("External image memory does not support exportable bit.");
         throw std::runtime_error("External image memory does not support exportable bit.");
    }

    // Create the VkImage
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width_;
    imageInfo.extent.height = height_;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = imageFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // Single queue ownership
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.flags = 0;

    VkExternalMemoryImageCreateInfo externalMemoryImageInfo_alloc{}; // Renamed to avoid conflict
    externalMemoryImageInfo_alloc.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    externalMemoryImageInfo_alloc.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    imageInfo.pNext = &externalMemoryImageInfo_alloc;

    if (vkCreateImage(device_, &imageInfo, nullptr, &dmaBufImage_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create DMA-BUF exportable image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device_, dmaBufImage_, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; // Corrected typo
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkExportMemoryAllocateInfo exportAllocInfo{};
    exportAllocInfo.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO; // Corrected typo
    exportAllocInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    allocInfo.pNext = &exportAllocInfo;

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &dmaBufImageMemory_) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate DMA-BUF exportable image memory!");
    }

    vkBindImageMemory(device_, dmaBufImage_, dmaBufImageMemory_, 0);

    // Create image view for our single render target image
    dmaBufImageView_ = createImageView(dmaBufImage_, imageFormat, VK_IMAGE_ASPECT_COLOR_BIT);
}

void GpuOverlay::createImageViews() {
    // For offscreen DMA-BUF rendering, we have only one image view (dmaBufImageView_)
    // that was created in createRenderTargets().
    // This function is largely a no-op now.
}

void GpuOverlay::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapChainImageFormat_;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; // Ready for export/transfer

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{}; // Declared here
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependency.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass; // Used here
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    if (vkCreateRenderPass(device_, &renderPassInfo, nullptr, &renderPass_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create render pass!");
    }
}

void GpuOverlay::createGraphicsPipeline() {
    // Use glslangValidator -x generated headers
    // vertShaderCode and fragShaderCode are now const unsigned int arrays
    VkShaderModule vertShaderModule = createShaderModule(vertShaderCode, sizeof(vertShaderCode)); // Corrected size access
    VkShaderModule fragShaderModule = createShaderModule(fragShaderCode, sizeof(fragShaderCode)); // Corrected size access

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float)swapChainExtent_.width;
    viewport.height = (float)swapChainExtent_.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = swapChainExtent_;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout_;
    pipelineLayoutInfo.pushConstantRangeCount = 0;

    if (vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline layout!");
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create graphics pipeline!");
    }

    vkDestroyShaderModule(device_, fragShaderModule, nullptr);
    vkDestroyShaderModule(device_, vertShaderModule, nullptr);
}

void GpuOverlay::createFramebuffers() {
    // For offscreen DMA-BUF rendering, we have only one framebuffer
    // that renders to our single dmaBufImageView_.
    swapChainFramebuffers_.resize(1); // Only one for our offscreen image

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass_;
    framebufferInfo.attachmentCount = 1;
    framebufferInfo.pAttachments = &dmaBufImageView_; // Our single output image view
    framebufferInfo.width = swapChainExtent_.width;
    framebufferInfo.height = swapChainExtent_.height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(device_, &framebufferInfo, nullptr, &swapChainFramebuffers_[0]) != VK_SUCCESS) {
        throw std::runtime_error("failed to create framebuffer!");
    }
}

void GpuOverlay::createCommandPool() {
    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice_);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // Allows command buffers to be reset individually

    if (vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create command pool!");
    }
}

void GpuOverlay::createTextureSampler() {
    // Create a sampler for our imported DMA-BUF image (importedCameraSampler_)
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE; // No anisotropy for simple textures
    samplerInfo.maxAnisotropy = 1;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(device_, &samplerInfo, nullptr, &importedCameraSampler_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create imported camera texture sampler!");
    }
}

void GpuOverlay::createVertexBuffer() {
    VkDeviceSize bufferSize = sizeof(quadVertices[0]) * quadVertices.size();

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device_, &bufferInfo, nullptr, &vertexBuffer_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create vertex buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device_, vertexBuffer_, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; // Corrected typo
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &vertexBufferMemory_) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate vertex buffer memory!");
    }

    vkBindBufferMemory(device_, vertexBuffer_, vertexBufferMemory_, 0);

    void* data;
    vkMapMemory(device_, vertexBufferMemory_, 0, bufferSize, 0, &data);
        memcpy(data, quadVertices.data(), (size_t) bufferSize);
    vkUnmapMemory(device_, vertexBufferMemory_);
}

void GpuOverlay::createIndexBuffer() {
    VkDeviceSize bufferSize = sizeof(quadIndices[0]) * quadIndices.size();

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device_, &bufferInfo, nullptr, &indexBuffer_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create index buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device_, indexBuffer_, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; // Corrected typo
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &indexBufferMemory_) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate index buffer memory!");
    }

    vkBindBufferMemory(device_, indexBuffer_, indexBufferMemory_, 0);

    void* data;
    vkMapMemory(device_, indexBufferMemory_, 0, bufferSize, 0, &data);
        memcpy(data, quadIndices.data(), (size_t) bufferSize);
    vkUnmapMemory(device_, indexBufferMemory_);
}

void GpuOverlay::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};
    samplerLayoutBinding.binding = 0;
    samplerLayoutBinding.descriptorCount = 1;
    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerLayoutBinding.pImmutableSamplers = nullptr;
    samplerLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &samplerLayoutBinding;

    if (vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout!");
    }
}

void GpuOverlay::createDescriptorPool() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);

    if (vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool!");
    }
}

void GpuOverlay::createDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout_);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets_.resize(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(device_, &allocInfo, descriptorSets_.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor sets!");
    }

    // Initial binding for descriptor sets - will be updated in render()
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorImageInfo imageDescriptorInfo{};
        imageDescriptorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageDescriptorInfo.imageView = importedCameraImageView_; // Use imported camera image
        imageDescriptorInfo.sampler = importedCameraSampler_;

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSets_[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageDescriptorInfo;

        vkUpdateDescriptorSets(device_, 1, &descriptorWrite, 0, nullptr);
    }
}

void GpuOverlay::createCommandBuffers() {
    commandBuffers_.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t) commandBuffers_.size();

    if (vkAllocateCommandBuffers(device_, &allocInfo, commandBuffers_.data()) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate command buffers!");
    }
}

void GpuOverlay::createSyncObjects() {
    imageAvailableSemaphores_.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores_.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences_.resize(MAX_FRAMES_IN_FLIGHT);
    // imagesInFlight_ will track status of our single output image (dmaBufImage_)
    imagesInFlight_.resize(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE); // Corrected size

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &imageAvailableSemaphores_[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &renderFinishedSemaphores_[i]) != VK_SUCCESS ||
            vkCreateFence(device_, &fenceInfo, nullptr, &inFlightFences_[i]) != VK_SUCCESS) {
            throw std::runtime_error("failed to create synchronization objects for a frame!");
        }
    }
}

void GpuOverlay::cleanupRenderTargets() { // Renamed from cleanupSwapChain
    // Destroy framebuffers only
    for (auto framebuffer : swapChainFramebuffers_) {
        if (framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device_, framebuffer, nullptr);
        }
    }
    if (graphicsPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, graphicsPipeline_, nullptr);
    }
    if (pipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    }
    if (renderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, renderPass_, nullptr);
    }
    
    // Core images/views/memory for dmaBufImage_ are destroyed in destructor.
}


uint32_t GpuOverlay::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("failed to find suitable memory type!");
}

VkCommandBuffer GpuOverlay::beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool_;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device_, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    return commandBuffer;
}

void GpuOverlay::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue_);

    vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
}

void GpuOverlay::transitionImageLayout(VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout) {
    (void)format; // Suppress unused parameter warning
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else {
        throw std::invalid_argument("unsupported layout transition!");
    }

    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    endSingleTimeCommands(commandBuffer);
}


VkShaderModule GpuOverlay::createShaderModule(const unsigned int* code, size_t codeSize) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = codeSize;
    createInfo.pCode = code; // Already unsigned int*

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device_, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("failed to create shader module!");
    }

    return shaderModule;
}

std::vector<const char*> GpuOverlay::getRequiredExtensions() {
    // We are not using GLFW, but direct DRM/KMS. So, list required Vulkan extensions.
    std::vector<const char*> extensions;

    // For Vulkan debugging
    if (enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    
    // For direct DRM/KMS integration, may need specific extensions or direct rendering approach
    // VK_KHR_EXTERNAL_MEMORY_CAPABILITIES and VK_KHR_EXTERNAL_MEMORY are needed for DMA-BUF import/export.
    extensions.push_back(VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME);
    extensions.push_back(VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME);
    extensions.push_back(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
    extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME);
    extensions.push_back(VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME);
    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME); // Required by external memory
    
    // Potentially needed for RPi GBM integration if direct surface creation via GBM
    // As we are not using a traditional swapchain, VK_KHR_SURFACE is not strictly for presentation.
    // However, some internal Vulkan operations might implicitly use it.
    // But for direct DRM/KMS output using exported DMA-BUF, it's not a display surface.
    // Removed VK_KHR_SURFACE_EXTENSION_NAME; this is causing issues with getRequiredExtensions.
    // extensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    // extensions.push_back(VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);
    // On RPi5 with Mesa, VK_KHR_DISPLAY or VK_KHR_WAYLAND_SURFACE / VK_KHR_XCB_SURFACE might not be relevant.
    // Instead, VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME will be used for DMA-BUF.

    return extensions;
}

bool GpuOverlay::checkValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    for (const char* layerName : validationLayers) {
        bool layerFound = false;
        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }

        if (!layerFound) {
            return false;
        }
    }

    return true;
}

void GpuOverlay::importDmaBufImage(int dma_buf_fd, size_t dma_buf_size, int image_width, int image_height, VkFormat format, uint32_t queueFamilyIndex) {
    (void)dma_buf_size; // Suppress unused parameter warning

    // If imported image exists, destroy it before importing a new one
    if (importedCameraImage_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, importedCameraSampler_, nullptr);
        vkDestroyImageView(device_, importedCameraImageView_, nullptr);
        vkDestroyImage(device_, importedCameraImage_, nullptr);
        vkFreeMemory(device_, importedCameraImageMemory_, nullptr);
        importedCameraImage_ = VK_NULL_HANDLE;
        importedCameraImageMemory_ = VK_NULL_HANDLE;
        importedCameraImageView_ = VK_NULL_HANDLE;
        importedCameraSampler_ = VK_NULL_HANDLE;
    }

    VkExternalMemoryImageCreateInfo externalMemoryImageInfo{};
    externalMemoryImageInfo.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    externalMemoryImageInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.pNext = &externalMemoryImageInfo;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent.width = static_cast<uint32_t>(image_width);
    imageInfo.extent.height = static_cast<uint32_t>(image_height);
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL; // Assuming optimal tiling from camera
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT; // Sampled for rendering, transfer_dst for layout transition
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.queueFamilyIndexCount = 1;
    imageInfo.pQueueFamilyIndices = &queueFamilyIndex;


    if (vkCreateImage(device_, &imageInfo, nullptr, &importedCameraImage_) != VK_SUCCESS) {
        APP_LOG_ERROR("failed to create DMA-BUF image for import!");
        throw std::runtime_error("failed to create DMA-BUF image for import!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device_, importedCameraImage_, &memRequirements);

    VkImportMemoryFdInfoKHR importFdInfo{};
    importFdInfo.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
    importFdInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    importFdInfo.fd = dma_buf_fd; // The actual DMA-BUF FD. Pass ownership.

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; // Corrected typo
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    allocInfo.pNext = &importFdInfo;

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &importedCameraImageMemory_) != VK_SUCCESS) {
        APP_LOG_ERROR("failed to allocate external memory for DMA-BUF import!");
        throw std::runtime_error("failed to allocate external memory for DMA-BUF import!");
    }

    vkBindImageMemory(device_, importedCameraImage_, importedCameraImageMemory_, 0);

    // Transition image to SHADER_READ_ONLY_OPTIMAL for use in fragment shader
    transitionImageLayout(importedCameraImage_, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Create ImageView and Sampler for the imported image
    importedCameraImageView_ = createImageView(importedCameraImage_, format, VK_IMAGE_ASPECT_COLOR_BIT);
    // Create a new sampler for the imported image
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(device_, &samplerInfo, nullptr, &importedCameraSampler_) != VK_SUCCESS) {
        APP_LOG_ERROR("failed to create imported camera image sampler!");
        throw std::runtime_error("failed to create imported camera image sampler!");
    }

    // Update descriptor set to point to the newly imported image
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkDescriptorImageInfo imageDescriptorInfo{};
        imageDescriptorInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageDescriptorInfo.imageView = importedCameraImageView_;
        imageDescriptorInfo.sampler = importedCameraSampler_;

        VkWriteDescriptorSet descriptorWrite{};
        descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrite.dstSet = descriptorSets_[i];
        descriptorWrite.dstBinding = 0;
        descriptorWrite.dstArrayElement = 0;
        descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrite.descriptorCount = 1;
        descriptorWrite.pImageInfo = &imageDescriptorInfo;

        vkUpdateDescriptorSets(device_, 1, &descriptorWrite, 0, nullptr);
    }
}

void GpuOverlay::createImage(uint32_t width, uint32_t height, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, VkImage& image, VkDeviceMemory& imageMemory) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = tiling;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device_, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device_, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; // Corrected typo
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate image memory!");
    }

    vkBindImageMemory(device_, image, imageMemory, 0);
}

VkImageView GpuOverlay::createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = aspectFlags;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (vkCreateImageView(device_, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image view!");
    }

    return imageView;
}