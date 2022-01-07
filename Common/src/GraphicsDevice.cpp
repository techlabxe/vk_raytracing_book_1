#ifndef NOMINMAX
# define NOMINMAX
#endif

#ifndef WIN32_LEAN_AND_MEAN
# include <windows.h>
#endif

#include "GraphicsDevice.h"
#include <vulkan/vulkan_win32.h>

#include <vector>
#include <sstream>
#include <fstream>
#include <algorithm>

#include <GLFW/glfw3.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#pragma comment(lib, "vulkan-1.lib")

#define CHECK_RESULT(x) { VkResult res = (x); if(res != VK_SUCCESS) { return false;} }
#define GetInstanceProcAddr(instance, FuncName) \
  g_pfn##FuncName = reinterpret_cast<PFN_##FuncName>(vkGetInstanceProcAddr(instance, #FuncName))

namespace {
    PFN_vkCreateDebugReportCallbackEXT	g_pfnvkCreateDebugReportCallbackEXT;
    PFN_vkDebugReportMessageEXT	        g_pfnvkDebugReportMessageEXT;
    PFN_vkDestroyDebugReportCallbackEXT g_pfnvkDestroyDebugReportCallbackEXT;

    static VkBool32 VKAPI_CALL DebugReportFunction(
        VkDebugReportFlagsEXT flags,
        VkDebugReportObjectTypeEXT objactTypes,
        uint64_t object,
        size_t	location,
        int32_t messageCode,
        const char* pLayerPrefix,
        const char* pMessage,
        void* pUserData)
    {
        VkBool32 ret = VK_FALSE;
        if (flags & VK_DEBUG_REPORT_INFORMATION_BIT_EXT ||
            flags & VK_DEBUG_REPORT_DEBUG_BIT_EXT) {
            ret = VK_TRUE;
        }
        std::stringstream ss;
        if (pLayerPrefix) {
            ss << "[" << pLayerPrefix << "] ";
        }
        ss << pMessage << std::endl;

        OutputDebugStringA(ss.str().c_str());

        return ret;
    }

    inline VkComponentMapping DefaultComponentMapping()
    {
        return VkComponentMapping{
          VK_COMPONENT_SWIZZLE_R,VK_COMPONENT_SWIZZLE_G,VK_COMPONENT_SWIZZLE_B,VK_COMPONENT_SWIZZLE_A
        };
    }
}

VkDebugReportCallbackEXT EnableDebugReport(VkInstance instance)
{
    VkDebugReportCallbackEXT debugReportCallback = nullptr;

    // Enable Debug Report
    GetInstanceProcAddr(instance, vkCreateDebugReportCallbackEXT);
    GetInstanceProcAddr(instance, vkDebugReportMessageEXT);
    GetInstanceProcAddr(instance, vkDestroyDebugReportCallbackEXT);

    VkDebugReportFlagsEXT flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
    VkDebugReportCallbackCreateInfoEXT drcCI{};
    drcCI.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
    drcCI.flags = flags;
    drcCI.pfnCallback = &DebugReportFunction;

    if (g_pfnvkCreateDebugReportCallbackEXT == nullptr ||
        g_pfnvkDestroyDebugReportCallbackEXT == nullptr ||
        g_pfnvkDebugReportMessageEXT == nullptr) {
        return nullptr;
    }
    g_pfnvkCreateDebugReportCallbackEXT(instance, &drcCI, nullptr, &debugReportCallback);
    return debugReportCallback;
}

void DisableDebugReport(VkInstance instance, VkDebugReportCallbackEXT callback)
{
    if (g_pfnvkCreateDebugReportCallbackEXT == nullptr ||
        g_pfnvkDestroyDebugReportCallbackEXT == nullptr ||
        g_pfnvkDebugReportMessageEXT == nullptr) {
        return;
    }
    g_pfnvkDestroyDebugReportCallbackEXT(instance, callback, nullptr);
}


vk::GraphicsDevice::GraphicsDevice()
{
}

vk::GraphicsDevice::~GraphicsDevice()
{
}

bool vk::GraphicsDevice::OnInit(const std::vector<const char*>& requiredExtensions, bool enableValidationLayer)
{
    VkApplicationInfo appinfo{};
    appinfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appinfo.pApplicationName = "VkrayBook1";
    appinfo.pEngineName = "VkrayBook1";
    appinfo.apiVersion = VK_API_VERSION_1_2;
    appinfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);


    uint32_t count = 0;
    auto surfaceExtensions = glfwGetRequiredInstanceExtensions(&count);
    if (!surfaceExtensions) {
    }
    std::vector<const char*> extensions(surfaceExtensions, surfaceExtensions + count);

    VkInstanceCreateInfo instanceCI{};
    instanceCI.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceCI.pApplicationInfo = &appinfo;

    extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

    const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
    if (enableValidationLayer) {
        // 検証レイヤーを有効化
        extensions.push_back(VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        instanceCI.enabledLayerCount = 1;
        instanceCI.ppEnabledLayerNames = layers;
    }

    instanceCI.enabledExtensionCount = uint32_t(extensions.size());
    instanceCI.ppEnabledExtensionNames = extensions.data();
    auto res = vkCreateInstance(&instanceCI, nullptr, &m_instance);
    if (res != VK_SUCCESS) { return false; }

    // 物理デバイスの列挙・選択.
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    m_physicalDevices.resize(count);
    vkEnumeratePhysicalDevices(m_instance, &count, m_physicalDevices.data());

    m_physicalDevice = m_physicalDevices[0];
    vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_memProps);

    uint32_t queuePropCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queuePropCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilyProps(queuePropCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &queuePropCount, queueFamilyProps.data());
    uint32_t graphicsQueue = 0;
    for (const auto& v : queueFamilyProps) {
        if (v.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            break;
        }
        graphicsQueue++;
    }
    if (graphicsQueue >= queuePropCount) {
        return false;
    }
    m_gfxQueueIndex = graphicsQueue;


    if (enableValidationLayer) {
        m_debugReport = EnableDebugReport(m_instance);
    }

    const float defaultQueuePriority(1.0f);
    VkDeviceQueueCreateInfo devQueueCI{
      VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
      nullptr, 0,
      m_gfxQueueIndex,
      1, &defaultQueuePriority
    };

    extensions.clear();
    for (auto& e : requiredExtensions) {
        extensions.push_back(e);
    }

    VkDeviceCreateInfo deviceCI{
      VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
      nullptr, 0,
      1, &devQueueCI,
      0, nullptr,
      uint32_t(extensions.size()), extensions.data(),
    };

    // PhysicalDeviceが備える各種機能を使うための準備.
    //  この情報がvkCreateDevice時に必要となる.
    VkPhysicalDeviceBufferDeviceAddressFeaturesKHR enabledBufferDeviceAddressFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES, nullptr,
    };
    enabledBufferDeviceAddressFeatures.bufferDeviceAddress = VK_TRUE;

    VkPhysicalDeviceRayTracingPipelineFeaturesKHR enabledRayTracingPipelineFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR, nullptr,
    };
    enabledRayTracingPipelineFeatures.rayTracingPipeline = VK_TRUE;
    enabledRayTracingPipelineFeatures.pNext = &enabledBufferDeviceAddressFeatures;

    VkPhysicalDeviceAccelerationStructureFeaturesKHR enabledAccelerataionStuctureFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR, nullptr,
    };
    enabledAccelerataionStuctureFeatures.accelerationStructure = VK_TRUE;
    enabledAccelerataionStuctureFeatures.pNext = &enabledRayTracingPipelineFeatures;

    VkPhysicalDeviceDescriptorIndexingFeatures enabledDescriptorIndexingFeatures{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES
    };
    enabledDescriptorIndexingFeatures.pNext = &enabledAccelerataionStuctureFeatures;
    enabledDescriptorIndexingFeatures.shaderUniformBufferArrayNonUniformIndexing = VK_TRUE;
    enabledDescriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    enabledDescriptorIndexingFeatures.runtimeDescriptorArray = VK_TRUE;
    enabledDescriptorIndexingFeatures.descriptorBindingVariableDescriptorCount = VK_TRUE;
    enabledDescriptorIndexingFeatures.descriptorBindingPartiallyBound = VK_TRUE;// 部分的なバインディング.

    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(m_physicalDevice, &features);
    VkPhysicalDeviceFeatures2 physicalDeviceFeatures2{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, nullptr,
    };
    physicalDeviceFeatures2.pNext = &enabledDescriptorIndexingFeatures;
    physicalDeviceFeatures2.features = features;

    // VkPhysicalDeviceFeatures2 を pNextで指定しているため,
    // pEnabledFeatures = nullptr であることが必要.
    deviceCI.pNext = &physicalDeviceFeatures2;
    deviceCI.pEnabledFeatures = nullptr;


    auto result = vkCreateDevice(m_physicalDevice, &deviceCI, nullptr, &m_device);
    if (result != VK_SUCCESS) {
        return false;
    }

    // コマンドプールの作成.
    VkCommandPoolCreateInfo cmdPoolCI{
      VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      nullptr,
      VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
      m_gfxQueueIndex
    };
    result = vkCreateCommandPool(m_device, &cmdPoolCI, nullptr, &m_commandPool);

    vkGetDeviceQueue(m_device, m_gfxQueueIndex, 0, &m_deviceQueue);

    // ディスクリプタプールの準備.
    CreateDescriptorPool();

    // 情報を取得しておく.
    vkGetPhysicalDeviceProperties(m_physicalDevice, &m_physicalDeviceProperties);

    // Vulkan Raytracing 用の様々な拡張関数を使えるようにセットアップ.
    load_VK_EXTENSIONS(
        m_instance,
        vkGetInstanceProcAddr,
        m_device,
        vkGetDeviceProcAddr
    );
    return true;
}

void vk::GraphicsDevice::OnDestroy()
{
    if (m_device) {
        vkDeviceWaitIdle(m_device);
    }
    if (m_swapchain) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
    }
    for (auto& rt : m_renderTargets) {
        vkDestroyImageView(m_device, rt.GetImageView(), nullptr);
    }
    m_renderTargets.clear();

    for (auto& cb : m_commandBuffers) {
        vkDestroyFence(m_device, cb.fence, nullptr);
        vkFreeCommandBuffers(m_device, m_commandPool, 1, &cb.commandBuffer);
    }
    m_commandBuffers.clear();

    if (m_renderCompleted) {
        vkDestroySemaphore(m_device, m_renderCompleted, nullptr);
    }
    if (m_presentCompleted) {
        vkDestroySemaphore(m_device, m_presentCompleted, nullptr);
    }
    if (m_surface) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    }
    if (m_commandPool) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    }
    if (m_descriptorPool) {
        vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    }
    if (m_device) {
        vkDestroyDevice(m_device, nullptr);
    }

    if (m_debugReport) {
        DisableDebugReport(m_instance, m_debugReport);
    }

    if (m_instance) {
        vkDestroyInstance(m_instance, nullptr);
    }
}

bool vk::GraphicsDevice::CreateSwapchain(uint32_t width, uint32_t height, GLFWwindow* window)
{
    m_width = width;
    m_height = height;

    VkResult result;
    if (!m_surface) {
        glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface);
        VkSurfaceCapabilitiesKHR surfaceCaps{};
        result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &surfaceCaps);
        if (result != VK_SUCCESS) {
            return false;
        }

        uint32_t count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &count, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &count, formats.data());
        auto selectFormat = VkSurfaceFormatKHR{ VK_FORMAT_UNDEFINED };

        auto compFormat = [=](auto f) {
            return f.format == BackBufferFormat.format && f.colorSpace == BackBufferFormat.colorSpace;
        };

        if (auto it = std::find_if(formats.begin(), formats.end(), compFormat); it != formats.end()) {
            selectFormat = *it;
        } else {
            it = std::find_if(formats.begin(), formats.end(), [=](auto f) { return f.colorSpace == BackBufferFormat.colorSpace; });
            if (it != formats.end()) {
                selectFormat = *it;
            }
        }
        if (selectFormat.format == VK_FORMAT_UNDEFINED) {
            return false;
        }
        // このフォーマットを使用する.
        BackBufferFormat = selectFormat;

        VkBool32 isSupport;
        result = vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, m_gfxQueueIndex, m_surface, &isSupport);
        if (isSupport == VK_FALSE) {
            return false;
        }
        auto backbufferCount = std::max(DesiredBackBufferCount, surfaceCaps.minImageCount);
        auto extent = surfaceCaps.currentExtent;
        if (extent.width == ~0u) {
            // 値が無効のためウィンドウサイズを使用する.
            extent.width = m_width;
            extent.height = m_height;
        }
        m_surfaceExtent = extent;

        uint32_t queueFamilyIndices[] = { m_gfxQueueIndex };
        auto presentMode = VK_PRESENT_MODE_FIFO_KHR;
        VkSwapchainCreateInfoKHR swapchainCI{
          VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
          nullptr, 0,
          m_surface,
          backbufferCount,
          BackBufferFormat.format, BackBufferFormat.colorSpace,
          m_surfaceExtent,
          1,
          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
          VK_SHARING_MODE_EXCLUSIVE,
          0, nullptr,//_countof(queueFamilyIndices), queueFamilyIndices , VK_SHARING_MODE_CONCURRENTのときに設定が必要. 
          surfaceCaps.currentTransform,
          VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
          presentMode,
          VK_TRUE,
          VK_NULL_HANDLE
        };
        result = vkCreateSwapchainKHR(m_device, &swapchainCI, nullptr, &m_swapchain);
        if (result != VK_SUCCESS) {
            return false;
        }
    } else {
        // TODO: スワップチェイン作り直し処理用.
        // 可変ウィンドウサイズを実現するときにはここを実装しましょう.
    }

    // スワップチェインイメージ取得.
    uint32_t imageCount = 0;
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, nullptr);

    std::vector<VkImage> images(imageCount);
    vkGetSwapchainImagesKHR(m_device, m_swapchain, &imageCount, images.data());

    m_renderTargets.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i) {
        m_renderTargets[i].m_image = images[i];

        VkImageViewCreateInfo viewCI{
          VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
          nullptr, 0,
          images[i],
          VK_IMAGE_VIEW_TYPE_2D,
          BackBufferFormat.format,
          DefaultComponentMapping(),
          { // VkImageSubresourceRange
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1
          }
        };
        result = vkCreateImageView(m_device, &viewCI, nullptr, &m_renderTargets[i].m_view);
        if (result != VK_SUCCESS) {
            return false;
        }
    }

    {   // スワップチェインのイメージ状態を UNDEFINED -> PRESENT_SRC にする.
        auto command = CreateCommandBuffer();
        for (uint32_t i = 0; i < imageCount; ++i) {
            m_renderTargets[i].BarrierToPresentSrc(command);
        }
        vkEndCommandBuffer(command);
        SubmitAndWait(command);
        DestroyCommandBuffer(command);
    }

    if (m_commandBuffers.empty()) {
        // 初回作成.
        VkSemaphoreCreateInfo semCI{
          VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
          nullptr, 0,
        };
        vkCreateSemaphore(m_device, &semCI, nullptr, &m_renderCompleted);
        vkCreateSemaphore(m_device, &semCI, nullptr, &m_presentCompleted);

        m_commandBuffers.resize(imageCount);
        for (auto& cb : m_commandBuffers) {
            cb.fence = CreateFence();
            cb.commandBuffer = CreateCommandBuffer(false);
        }
    }

    return true;
}

VkCommandBuffer vk::GraphicsDevice::CreateCommandBuffer(bool isBegin)
{
    VkCommandBufferAllocateInfo commandAI{
       VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      nullptr, m_commandPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      1
    };
    VkCommandBufferBeginInfo beginInfo{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
    };

    VkCommandBuffer command;
    vkAllocateCommandBuffers(m_device, &commandAI, &command);
    if (isBegin) {
        vkBeginCommandBuffer(command, &beginInfo);
    }
    return command;
}

void vk::GraphicsDevice::DestroyCommandBuffer(VkCommandBuffer command)
{
    vkFreeCommandBuffers(m_device, m_commandPool, 1, &command);
}

VkFence vk::GraphicsDevice::CreateFence()
{
    VkFenceCreateInfo fenceCI{
      VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
      nullptr,
      VK_FENCE_CREATE_SIGNALED_BIT
    };
    VkFence fence;
    vkCreateFence(m_device, &fenceCI, nullptr, &fence);
    return fence;
}

void vk::GraphicsDevice::DestroyFence(VkFence fence)
{
    vkDestroyFence(m_device, fence, nullptr);
}

void vk::GraphicsDevice::ResetFence(VkFence fence)
{
    vkResetFences(m_device, 1, &fence);
}

void vk::GraphicsDevice::WaitFence(VkFence fence)
{
    vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX);
}

// コマンドバッファを送信して実行.
//  フレームと関連付かないコマンドバッファを実行用.
void vk::GraphicsDevice::SubmitAndWait(VkCommandBuffer command)
{
    VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{
      VK_STRUCTURE_TYPE_SUBMIT_INFO,
      nullptr,
      0, nullptr, // WaitSemaphore
      &waitStageMask, // DstStageMask
      1, &command, // CommandBuffer
      0, nullptr, // SignalSemaphore
    };

    auto fence = CreateFence();
    ResetFence(fence);
    vkQueueSubmit(m_deviceQueue, 1, &submitInfo, fence);
    WaitFence(fence);
    DestroyFence(fence);
    vkQueueWaitIdle(m_deviceQueue);
}

void vk::GraphicsDevice::Present()
{
    VkPresentInfoKHR presentInfo{
        VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        nullptr,
        1,&m_renderCompleted,
        1,&m_swapchain,
        &m_frameIndex
    };
    vkQueuePresentKHR(m_deviceQueue, &presentInfo);
}

VkCommandBuffer vk::GraphicsDevice::GetCurrentFrameCommandBuffer()
{
    return m_commandBuffers[m_frameIndex].commandBuffer;
}

// コマンドバッファを送信して実行.
void vk::GraphicsDevice::SubmitCurrentFrameCommandBuffer()
{
    auto command = GetCurrentFrameCommandBuffer();
    VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{
      VK_STRUCTURE_TYPE_SUBMIT_INFO,
      nullptr,
      1, &m_presentCompleted, // WaitSemaphore
      &waitStageMask, // DstStageMask
      1, &command, // CommandBuffer
      1, &m_renderCompleted, // SignalSemaphore
    };
    auto fence = m_commandBuffers[m_frameIndex].fence;
    vkResetFences(m_device, 1, &fence);
    vkQueueSubmit(m_deviceQueue, 1, &submitInfo, fence);
}


void vk::GraphicsDevice::WaitForIdleGpu()
{
    if (m_device) {
        vkDeviceWaitIdle(m_device);
    }
}

void vk::GraphicsDevice::WaitAvailableFrame()
{
    auto timeout = UINT64_MAX;
    auto result = vkAcquireNextImageKHR(m_device, m_swapchain, timeout, m_presentCompleted, VK_NULL_HANDLE, &m_frameIndex);
    auto fence = m_commandBuffers[m_frameIndex].fence;
    vkWaitForFences(m_device, 1, &fence, VK_TRUE, timeout);
}

vk::BufferResource vk::GraphicsDevice::CreateBuffer(size_t requestSize, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps)
{
    BufferResource ret;

    VkBufferCreateInfo bufferCI{
      VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
      nullptr
    };
    bufferCI.size = requestSize;
    bufferCI.usage = usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer buffer;
    vkCreateBuffer(m_device, &bufferCI, nullptr, &buffer);

    // メモリの確保.
    auto memory = AllocateMemory(buffer, bufferCI.usage, memProps);
    vkBindBufferMemory(m_device, buffer, memory, 0);

    ret.m_buffer = buffer;
    ret.m_memory = memory;
    ret.m_memProps = memProps;
    ret.m_usage = usage;

    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        ret.m_deviceAddress = GetDeviceAddress(ret.m_buffer);
    }

    return ret;
}

void vk::GraphicsDevice::DestroyBuffer(BufferResource& objBuffer)
{
    vkDestroyBuffer(m_device, objBuffer.GetBuffer(), nullptr);
    vkFreeMemory(m_device, objBuffer.GetMemory(), nullptr);
    objBuffer.m_buffer = VK_NULL_HANDLE;
    objBuffer.m_memory = VK_NULL_HANDLE;
}

vk::ImageResource vk::GraphicsDevice::CreateTexture2D(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkMemoryPropertyFlags memProps)
{
    ImageResource ret{};
    VkImageCreateInfo imageCI{
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, nullptr
    };
    imageCI.imageType = VK_IMAGE_TYPE_2D;
    imageCI.format = format;
    imageCI.extent = { width, height, 1 };
    imageCI.mipLevels = 1;
    imageCI.arrayLayers = 1;
    imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCI.usage = usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCI.queueFamilyIndexCount;
    imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image;
    vkCreateImage(m_device, &imageCI, nullptr, &image);

    // メモリの確保.
    auto memory = AllocateMemory(image, memProps);
    vkBindImageMemory(m_device, image, memory, 0);

    // ビューの生成.
    VkImageViewCreateInfo viewCI{
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr
    };
    viewCI.image = image;
    viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewCI.format = format;
    viewCI.components = DefaultComponentMapping();
    viewCI.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    VkImageView view;
    vkCreateImageView(m_device, &viewCI, nullptr, &view);

    ret.m_image = image;
    ret.m_view = view;
    ret.m_memory = memory;
    return ret;
}

vk::ImageResource vk::GraphicsDevice::CreateTexture2DFromFile(const wchar_t* fileName, VkImageUsageFlags usage, VkMemoryPropertyFlags memProps)
{
    stbi_uc* image = nullptr;
    std::vector<char> binImage;
    std::ifstream infile(fileName, std::ios::binary);
    if (!infile) {
        return vk::ImageResource();
    }

    binImage.resize(infile.seekg(0, std::ifstream::end).tellg());
    infile.seekg(0, std::ifstream::beg).read(binImage.data(), binImage.size());

    return CreateTexture2DFromMemory(binImage.data(), binImage.size(), usage, memProps);
}

vk::ImageResource vk::GraphicsDevice::CreateTexture2DFromMemory(const void* imageData, size_t size, VkImageUsageFlags usage, VkMemoryPropertyFlags memProps)
{
    int width, height;
    auto image = stbi_load_from_memory(static_cast<const stbi_uc*>(imageData), int(size), &width, &height, nullptr, 4);
    usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    // 書き込み先のテクスチャを生成する.
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    vk::ImageResource tex = CreateTexture2D(width, height, format, usage, memProps);

    // ステージング用準備.
    auto imageSize = width * height * sizeof(uint32_t);
    auto buffersSrc = CreateBuffer(
        imageSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    WriteToBuffer(buffersSrc, image, imageSize);

    auto command = CreateCommandBuffer();
    VkImageSubresourceRange subresource{};
    subresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresource.baseMipLevel = 0;
    subresource.levelCount = 1;
    subresource.baseArrayLayer = 0;
    subresource.layerCount = 1;

    // 転送先に設定する.
    tex.BarrierToDst(command);

    // 転送処理.
    VkBufferImageCopy region{};
    region.imageExtent = { uint32_t(width), uint32_t(height), 1 };
    region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };

    vkCmdCopyBufferToImage(
        command,
        buffersSrc.GetBuffer(),
        tex.m_image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &region
    );
    // テクスチャとして読み取り可能状態へ設定.
    tex.BarrierToShaderReadOnly(command);

    vkEndCommandBuffer(command);
    SubmitAndWait(command);
    DestroyCommandBuffer(command);
    
    DestroyBuffer(buffersSrc);
    return tex;
}

vk::ImageResource vk::GraphicsDevice::CreateTextureCube(const wchar_t* faceFiles[6], VkImageUsageFlags usage, VkMemoryPropertyFlags memProps)
{
    int width, height;
    stbi_uc* faceImages[6] = { 0 };
    std::vector<char> binImages[6];

    for (int i = 0; i < 6; ++i) {
        std::ifstream infile(faceFiles[i], std::ios::binary);
        if (!infile) {
            return vk::ImageResource();
        }
        binImages[i].resize(infile.seekg(0, std::ifstream::end).tellg());
        infile.seekg(0, std::ifstream::beg).read(binImages[i].data(), binImages[i].size());

        faceImages[i] = stbi_load_from_memory(
            reinterpret_cast<uint8_t*>(binImages[i].data()),
            int(binImages[i].size()),
            &width,
            &height,
            nullptr,
            4);
    }
    usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    VkImageCreateInfo imageCI{
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    };
    imageCI.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    imageCI.imageType = VK_IMAGE_TYPE_2D;
    imageCI.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageCI.extent = { uint32_t(width), uint32_t(height), 1 };
    imageCI.mipLevels = 1;
    imageCI.arrayLayers = 6;
    imageCI.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCI.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCI.usage = usage;
    imageCI.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCI.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    vk::ImageResource cubemap;
    vkCreateImage(m_device, &imageCI, nullptr, &cubemap.m_image);
    cubemap.m_memory = AllocateMemory(cubemap.m_image, memProps);
    vkBindImageMemory(m_device, cubemap.m_image, cubemap.m_memory, 0);

    // ビューの生成.
    VkImageViewCreateInfo viewCI{
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, nullptr
    };
    viewCI.image = cubemap.m_image;
    viewCI.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewCI.format = imageCI.format;
    viewCI.components = DefaultComponentMapping();
    viewCI.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 6 };
    vkCreateImageView(m_device, &viewCI, nullptr, &cubemap.m_view);
    cubemap.m_subresourceRange = viewCI.subresourceRange;

    // ステージング用準備.
    BufferResource buffersSrc[6];
    auto faceBytes = width * height * sizeof(uint32_t);
    for (int i = 0; i < 6; ++i) {
        buffersSrc[i] = CreateBuffer(
            faceBytes,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        WriteToBuffer(buffersSrc[i], faceImages[i], faceBytes);
    }

    cubemap.m_subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT , 0, 1, 0, 6 };

    auto command = CreateCommandBuffer();
    cubemap.BarrierToDst(command);

    // 転送処理.
    for (int i = 0; i < 6; ++i) {
        VkBufferImageCopy region{};
        region.imageExtent = { uint32_t(width), uint32_t(height), 1 };
        region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.imageSubresource.baseArrayLayer = i;

        vkCmdCopyBufferToImage(
            command,
            buffersSrc[i].GetBuffer(),
            cubemap.m_image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &region
        );
    }
    cubemap.BarrierToShaderReadOnly(command);
    vkEndCommandBuffer(command);
    SubmitAndWait(command);
    DestroyCommandBuffer(command);

    for (auto& v : buffersSrc) {
        DestroyBuffer(v);
    }
    return cubemap;
}

void vk::GraphicsDevice::DestroyImage(ImageResource& objImage)
{
    vkDestroyImage(m_device, objImage.GetImage(), nullptr);
    vkDestroyImageView(m_device, objImage.GetImageView(), nullptr);
    vkFreeMemory(m_device, objImage.GetMemory(), nullptr);
    objImage.m_image = VK_NULL_HANDLE;
    objImage.m_memory = VK_NULL_HANDLE;
    objImage.m_view = VK_NULL_HANDLE;
}

VkDeviceMemory vk::GraphicsDevice::AllocateMemory(VkBuffer buffer, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps)
{
    VkDeviceMemory memory;
    VkMemoryRequirements reqs;
    vkGetBufferMemoryRequirements(m_device, buffer, &reqs);
    VkMemoryAllocateInfo info{
      VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      nullptr,
      reqs.size,
      GetMemoryTypeIndex(reqs.memoryTypeBits, memProps)
    };

    VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo{
    VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO, nullptr,
    };
    memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) {
        info.pNext = &memoryAllocateFlagsInfo;
    }

    vkAllocateMemory(m_device, &info, nullptr, &memory);
    return memory;
}

VkDeviceMemory vk::GraphicsDevice::AllocateMemory(VkImage image, VkMemoryPropertyFlags memProps)
{
    VkDeviceMemory memory;
    VkMemoryRequirements reqs;
    vkGetImageMemoryRequirements(m_device, image, &reqs);
    VkMemoryAllocateInfo info{
      VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      nullptr,
      reqs.size,
      GetMemoryTypeIndex(reqs.memoryTypeBits, memProps)
    };

    vkAllocateMemory(m_device, &info, nullptr, &memory);
    return memory;
}


void* vk::GraphicsDevice::Map(const BufferResource& bufferRes)
{
    void* p = nullptr;
    if (bufferRes.m_memProps & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        vkMapMemory(m_device, bufferRes.GetMemory(), 0, VK_WHOLE_SIZE, 0, &p);
    }
    return p;
}

void vk::GraphicsDevice::Unmap(const BufferResource& bufferRes)
{
    if (bufferRes.m_memProps & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        vkUnmapMemory(m_device, bufferRes.GetMemory());
    }
}


void vk::GraphicsDevice::WriteToBuffer(BufferResource& bufferRes, const void* data, size_t size)
{
    auto memProps = bufferRes.m_memProps;
    if (memProps & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        void* p;
        vkMapMemory(m_device, bufferRes.GetMemory(), 0, VK_WHOLE_SIZE, 0, &p);
        memcpy(p, data, size);
        vkUnmapMemory(m_device, bufferRes.GetMemory());
        if ((memProps & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) == 0) {
            VkMappedMemoryRange range{
                VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE
            };
            range.memory = bufferRes.GetMemory();
            range.offset = 0;
            range.size = VK_WHOLE_SIZE;
            vkFlushMappedMemoryRanges(m_device, 1, &range);
        }
        return;
    }
    if (memProps & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
        // ステージングバッファを用意する.
        VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        auto stagingBuffer = CreateBuffer(size, usage, memProps);
        WriteToBuffer(stagingBuffer, data, size);

        // 転送.
        auto command = CreateCommandBuffer();
        VkBufferCopy region{};
        region.size = size;
        vkCmdCopyBuffer(command, stagingBuffer.GetBuffer(), bufferRes.GetBuffer(), 1, &region);
        vkEndCommandBuffer(command);
        
        // 転送の完了待って抜ける.
        SubmitAndWait(command);
        DestroyCommandBuffer(command);

        DestroyBuffer(stagingBuffer);
        return;
    }
}

uint64_t vk::GraphicsDevice::GetDeviceAddress(VkBuffer buffer)
{
    // Vulkan1.2を使わない場合には、末尾に KHR 付きのものが使える.
    VkBufferDeviceAddressInfo bufferDeviceInfo{
        VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, nullptr
    };
    bufferDeviceInfo.buffer = buffer;
    return vkGetBufferDeviceAddress(m_device, &bufferDeviceInfo);
}

bool vk::GraphicsDevice::CreateDescriptorPool()
{
    VkResult result;
    VkDescriptorPoolSize poolSize[] = {
      { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
      { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
      { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
      { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 100 },
      { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
      { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
    };
    VkDescriptorPoolCreateInfo descPoolCI{
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
      nullptr,  VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
      100, // maxSets
      _countof(poolSize), poolSize,
    };
    result = vkCreateDescriptorPool(m_device, &descPoolCI, nullptr, &m_descriptorPool);
    return result == VK_SUCCESS;
}

uint32_t vk::GraphicsDevice::GetMemoryTypeIndex(uint32_t requestBits, VkMemoryPropertyFlags requestProps) const
{
    uint32_t result = ~0u;
    for (uint32_t i = 0; i < m_memProps.memoryTypeCount; ++i) {
        if (requestBits & 1) {
            const auto& types = m_memProps.memoryTypes[i];
            if ((types.propertyFlags & requestProps) == requestProps) {
                result = i;
                break;
            }
        }
        requestBits >>= 1;
    }
    if (result == ~0u) {
        OutputDebugStringA("No matching memory type.\n");
    }
    return result;
}

VkDescriptorSet vk::GraphicsDevice::AllocateDescriptorSet(VkDescriptorSetLayout dsLayout, const void* pNext)
{
    VkDescriptorSetAllocateInfo dsAI{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO, nullptr
    };
    dsAI.descriptorPool = m_descriptorPool;
    dsAI.pSetLayouts = &dsLayout;
    dsAI.descriptorSetCount = 1;
    dsAI.pNext = pNext;
    VkDescriptorSet ds{};
    auto r = vkAllocateDescriptorSets(m_device, &dsAI, &ds);
    assert(r == VK_SUCCESS);
    return ds;
}

void vk::GraphicsDevice::DeallocateDescriptorSet(VkDescriptorSet ds)
{
    vkFreeDescriptorSets(m_device, m_descriptorPool, 1, &ds);
}

VkSampler vk::GraphicsDevice::CreateSampler(VkFilter minFilter, VkFilter magFilter, VkSamplerMipmapMode mipmapMode, VkSamplerAddressMode addressU, VkSamplerAddressMode addressV)
{
    VkSamplerCreateInfo samplerCI{
      VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, nullptr,
      0,
      magFilter,
      minFilter,
      mipmapMode,
      addressU,
      addressV,
      VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
      0.0f,
      VK_FALSE,
      1.0f,
      VK_FALSE,
      VK_COMPARE_OP_NEVER,
      0.0f,
      1.0f,
      VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
      VK_FALSE
    };
    VkSampler sampler{};
    vkCreateSampler(m_device, &samplerCI, nullptr, &sampler);
    return sampler;
}

void vk::GraphicsDevice::DestroySampler(VkSampler sampler)
{
    vkDestroySampler(m_device, sampler, nullptr);
}

VkPhysicalDeviceRayTracingPipelinePropertiesKHR vk::GraphicsDevice::GetRayTracingPipelineProperties()
{
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR physDevRtPipelineProps{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR
    };
    VkPhysicalDeviceProperties2 physDevProps2{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2
    };
    physDevProps2.pNext = &physDevRtPipelineProps;
    vkGetPhysicalDeviceProperties2(m_physicalDevice, &physDevProps2);
    return physDevRtPipelineProps;
}



void vk::ImageResource::BarrierToGeneral(VkCommandBuffer command)
{
    SetImageLayoutBarrier(command, m_image, m_layout, VK_IMAGE_LAYOUT_GENERAL);
}

void vk::ImageResource::BarrierToSrc(VkCommandBuffer command)
{
    SetImageLayoutBarrier(command, m_image, m_layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
}

void vk::ImageResource::BarrierToDst(VkCommandBuffer command)
{
    SetImageLayoutBarrier(command, m_image, m_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
}

void vk::ImageResource::BarrierToShaderReadOnly(VkCommandBuffer command)
{
    SetImageLayoutBarrier(command, m_image, m_layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void vk::ImageResource::BarrierToPresentSrc(VkCommandBuffer command)
{
    SetImageLayoutBarrier(command, m_image, m_layout, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
}

const VkDescriptorImageInfo* vk::ImageResource::GetDescriptor(VkSampler sampler)
{
    m_descriptor.imageView = m_view;
    m_descriptor.imageLayout = m_layout;
    m_descriptor.sampler = sampler;
    return &m_descriptor;
}

void vk::ImageResource::SetImageLayoutBarrier(VkCommandBuffer command, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout)
{
    VkImageMemoryBarrier imb{};
    imb.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imb.oldLayout = oldLayout;
    imb.newLayout = newLayout;
    imb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imb.subresourceRange = m_subresourceRange;
    imb.image = image;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    switch (oldLayout) {
    case VK_IMAGE_LAYOUT_UNDEFINED:
        imb.srcAccessMask = 0;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        imb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        break;
    }

    switch (newLayout) {
    case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
        imb.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        break;
    case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
        imb.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        break;
    case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
        imb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dstStage = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
        break;
    }

    //srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;  // パイプライン中でリソースへの書込み最終のステージ.
    //dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;  // パイプライン中で次にリソースに書き込むステージ.

    vkCmdPipelineBarrier(
        command,
        srcStage,
        dstStage,
        0,
        0,  // memoryBarrierCount
        nullptr,
        0,  // bufferMemoryBarrierCount
        nullptr,
        1,  // imageMemoryBarrierCount
        &imb);

    m_layout = newLayout;
}


