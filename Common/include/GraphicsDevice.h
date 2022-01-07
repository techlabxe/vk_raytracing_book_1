#pragma once

#pragma once
#include <cstdint>
#include <vulkan/vulkan.h>
#include <vector>

#include "extensions_vk.hpp"

// forward declaration.
struct GLFWwindow;

namespace vk {
    class GraphicsDevice;

    class BufferResource {
    public:
        VkBuffer GetBuffer() const { return m_buffer; }
        VkDeviceMemory GetMemory()const { return m_memory; }
        VkDeviceAddress GetDeviceAddress()const { return m_deviceAddress; }

        VkDescriptorBufferInfo GetDescriptor() const { return VkDescriptorBufferInfo{ m_buffer, 0, VK_WHOLE_SIZE }; }
    private:
        VkBuffer m_buffer = VK_NULL_HANDLE;
        VkDeviceMemory m_memory = VK_NULL_HANDLE;
        VkBufferUsageFlags  m_usage = 0;
        VkMemoryPropertyFlags m_memProps = 0;
        VkDeviceAddress m_deviceAddress = 0;

        friend class GraphicsDevice;
    };

    class ImageResource {
    public:
        // リソース状態の遷移関数(コマンドに積む)
        
        void BarrierToGeneral(VkCommandBuffer command);
        void BarrierToSrc(VkCommandBuffer command);
        void BarrierToDst(VkCommandBuffer command);
        void BarrierToShaderReadOnly(VkCommandBuffer command);
        void BarrierToPresentSrc(VkCommandBuffer command);

        VkImage GetImage() const { return m_image; }
        VkImageView GetImageView()const { return m_view; }
        VkDeviceMemory GetMemory()const { return m_memory; }

        VkImageLayout GetImageLayout() const { return m_layout; }

        const VkDescriptorImageInfo* GetDescriptor(VkSampler sampler = VK_NULL_HANDLE);
    private:
        void SetImageLayoutBarrier(VkCommandBuffer command, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);

        VkImage m_image = VK_NULL_HANDLE;
        VkImageView m_view = VK_NULL_HANDLE;
        VkDeviceMemory m_memory = VK_NULL_HANDLE;

        VkImageLayout m_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageSubresourceRange m_subresourceRange = { 
            VK_IMAGE_ASPECT_COLOR_BIT, 
            0, /*baseMipLevel*/
            1, /*levelCount*/
            0, /*baseArrayLayer*/
            1  /*layerCount*/ };
        VkDescriptorImageInfo m_descriptor={};
        friend class GraphicsDevice;
    };

    class GraphicsDevice {
    public:
        GraphicsDevice();
        GraphicsDevice(const GraphicsDevice&) = delete;
        GraphicsDevice& operator=(const GraphicsDevice&) = delete;

        ~GraphicsDevice();

        bool OnInit(const std::vector<const char*>& requiredExtensions, bool enableValidationLayer);
        void OnDestroy();

        bool CreateSwapchain(uint32_t width, uint32_t height, GLFWwindow* window);

        uint32_t GetCurrentFrameIndex() const { return m_frameIndex; }

        VkCommandBuffer CreateCommandBuffer(bool isBegin = true);
        void DestroyCommandBuffer(VkCommandBuffer command);

        VkFence CreateFence();
        void DestroyFence(VkFence fence);
        void ResetFence(VkFence fence);
        void WaitFence(VkFence fence);

        vk::ImageResource GetRenderTarget(uint32_t idx) const { return m_renderTargets[idx]; }
        VkRect2D GetRenderArea() const
        {
            return VkRect2D{ VkOffset2D{0,0}, m_surfaceExtent };
        }

        VkCommandBuffer GetCurrentFrameCommandBuffer();

        // コマンドバッファを送信して実行.
        void SubmitCurrentFrameCommandBuffer();

        // コマンドバッファを送信して実行.
        //  フレームと関連付かないコマンドバッファを実行用.
        void SubmitAndWait(VkCommandBuffer command);

        void Present();
        void WaitForIdleGpu();
        void WaitAvailableFrame();

        BufferResource CreateBuffer(size_t requestSize, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps);
        void DestroyBuffer(BufferResource& objBuffer);

        ImageResource  CreateTexture2D(uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkMemoryPropertyFlags memProps);
        ImageResource  CreateTexture2DFromFile(const wchar_t* fileName, VkImageUsageFlags usage, VkMemoryPropertyFlags memProps);
        ImageResource  CreateTexture2DFromMemory(const void* imageData, size_t size, VkImageUsageFlags usage, VkMemoryPropertyFlags memProps);

        ImageResource  CreateTextureCube(const wchar_t* faceFiles[6], VkImageUsageFlags usage, VkMemoryPropertyFlags memProps);
        void DestroyImage(ImageResource& objImage);

        VkDeviceMemory AllocateMemory(VkBuffer buffer, VkBufferUsageFlags usage, VkMemoryPropertyFlags memProps);
        VkDeviceMemory AllocateMemory(VkImage image, VkMemoryPropertyFlags memProps);

        // CPU可視の状態で使用可能なメモリマップ関数.
        void* Map(const BufferResource&);
        void  Unmap(const BufferResource&);

        // バッファへの書き込み関数.
        //  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT への書き込みはコマンド発行、完了待ちになるので注意.
        void WriteToBuffer(BufferResource&, const void* data, size_t size);

        VkDevice GetDevice() const { return m_device; }
        VkPhysicalDevice GetPhysicalDevice() const { return m_physicalDevice; }
        VkInstance GetVulkanInstance() const { return m_instance; }
        VkQueue GetDefaultQueue() const { return m_deviceQueue; }
        VkDescriptorPool GetDescriptorPool() const { return m_descriptorPool; }
        uint32_t GetGraphicsQueueFamily() const { return m_gfxQueueIndex; }

        VkSurfaceFormatKHR GetBackBufferFormat() const { return BackBufferFormat; }
        uint32_t GetBackBufferCount() const { return uint32_t(m_renderTargets.size()); }

        const char* GetDeviceName() const { return m_physicalDeviceProperties.deviceName; }
        uint32_t GetMemoryTypeIndex(uint32_t requestBits, VkMemoryPropertyFlags requestProps) const;

        VkDescriptorSet AllocateDescriptorSet(VkDescriptorSetLayout dsLayout, const void* pNext = nullptr);
        void DeallocateDescriptorSet(VkDescriptorSet ds);


        VkSampler CreateSampler(
            VkFilter minFilter = VK_FILTER_LINEAR,
            VkFilter magFilter = VK_FILTER_LINEAR,
            VkSamplerMipmapMode mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            VkSamplerAddressMode addressU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VkSamplerAddressMode addressV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
        void DestroySampler(VkSampler sampler);

        // デバイスアドレスの取得.
        uint64_t GetDeviceAddress(VkBuffer buffer);
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR GetRayTracingPipelineProperties();
        VkDeviceSize GetUniformBufferAlignment() const { return m_physicalDeviceProperties.limits.minUniformBufferOffsetAlignment; }
        VkDeviceSize GetStorageBufferAlignment() const { return m_physicalDeviceProperties.limits.minStorageBufferOffsetAlignment; }

    private:
        bool CreateDescriptorPool();

        VkInstance m_instance = VK_NULL_HANDLE;
        VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
        VkDevice   m_device = VK_NULL_HANDLE;
        VkQueue    m_deviceQueue = VK_NULL_HANDLE;
        uint32_t   m_gfxQueueIndex = 0;

        VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
        VkCommandPool m_commandPool = VK_NULL_HANDLE;
        std::vector<VkPhysicalDevice> m_physicalDevices;
        VkPhysicalDeviceMemoryProperties m_memProps;
        VkPhysicalDeviceProperties  m_physicalDeviceProperties;

        uint32_t m_width = 0;
        uint32_t m_height = 0;
        VkSurfaceKHR    m_surface = VK_NULL_HANDLE;
        VkExtent2D      m_surfaceExtent;
        VkSwapchainKHR  m_swapchain = VK_NULL_HANDLE;

        VkSemaphore m_renderCompleted;
        VkSemaphore m_presentCompleted;

        std::vector<vk::ImageResource> m_renderTargets;

        uint32_t m_frameIndex = 0;
        struct FrameCommandBuffer {
            VkCommandBuffer commandBuffer;
            VkFence fence;
        };
        std::vector<FrameCommandBuffer> m_commandBuffers;

        VkDebugReportCallbackEXT  m_debugReport = VK_NULL_HANDLE;


        // バックバッファのフォーマット指定.
        VkSurfaceFormatKHR BackBufferFormat = {
            VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
        };
        const uint32_t DesiredBackBufferCount = 3;
    };
}