#pragma once

#pragma once
#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include <functional>

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include "GraphicsDevice.h"

#include "ShaderGroupHelper.h"

namespace util {
    // ------------------------------------------
    // Loading
    // ------------------------------------------
    bool LoadFile(std::vector<char>& out, const std::wstring& fileName);
    VkPipelineShaderStageCreateInfo LoadShader(std::unique_ptr<vk::GraphicsDevice>& device, const std::wstring& fileName, VkShaderStageFlagBits stage);
    
    // ------------------------------------------
    // Convert
    // ------------------------------------------
    VkTransformMatrixKHR ConvertTransform(const glm::mat4x3& m);
    std::wstring ConvertFromUTF8(const std::string& s);

    // ------------------------------------------
    // Helper Function
    // ------------------------------------------
    VkRayTracingShaderGroupCreateInfoKHR CreateShaderGroupRayGeneration(uint32_t shaderIndex);
    VkRayTracingShaderGroupCreateInfoKHR CreateShaderGroupMiss(uint32_t shaderIndex);
    VkRayTracingShaderGroupCreateInfoKHR CreateShaderGroupHit(
        uint32_t closestHitShaderIndex,
        uint32_t anyHitShaderIndex = VK_SHADER_UNUSED_KHR,
        uint32_t intersectionShaderIndex = VK_SHADER_UNUSED_KHR);

    inline VkComponentMapping GetComponentMapping()
    {
        return VkComponentMapping{
          VK_COMPONENT_SWIZZLE_R,VK_COMPONENT_SWIZZLE_G,VK_COMPONENT_SWIZZLE_B,VK_COMPONENT_SWIZZLE_A
        };
    }

    void SetImageLayoutBarrier(VkCommandBuffer command, VkImage image, VkImageLayout oldLayout, VkImageLayout newLayout);

    class DynamicBuffer {
    public:
        using Device = std::unique_ptr<vk::GraphicsDevice>;

        bool Initialize(Device& device, size_t requestSize, VkBufferUsageFlags usage);
        void Destroy(Device& device);

        VkBuffer GetBuffer() const { return m_buffer.GetBuffer(); }
        uint64_t GetBlockSize() const { return m_blockSize; }

        VkDeviceAddress GetDeviceAddress(uint32_t bufferIndex) const;
        void* Map(uint32_t bufferIndex);

        VkDescriptorBufferInfo GetDescriptor() const;
    private:
        uint32_t GetOffset(uint32_t index) const;
        vk::BufferResource m_buffer;

        // 要求サイズをアライメント制約まで切り上げたもの.
        // １回で使用するバッファはこれ以下の領域サイズとなる.
        uint64_t m_blockSize;

        void* m_mappedPtr;
        VkDeviceAddress m_deviceAddress = 0;
    };

    VkDescriptorBufferInfo CreateDescriptorBuffer(
        std::unique_ptr<vk::GraphicsDevice>& device,
        VkBuffer buffer,
        int start, int count, size_t stride);
}

namespace util {
    namespace primitive {
        using namespace glm;

        struct VertexPN {
            vec3 Position;
            vec3 Normal;
        };

        struct VertexPNC {
            vec3 Position;
            vec3 Normal;
            vec4 Color;
        };

        struct VertexPNT {
            vec3 Position;
            vec3 Normal;
            vec2 UV;
        };

        void GetPlane(std::vector<VertexPNC>& vertices, std::vector<uint32_t>& indices, float size = 10.0f);
        void GetPlane(std::vector<VertexPNT>& vertices, std::vector<uint32_t>& indices, float size = 10.0f);
        void GetPlane(std::vector<vec3>* positions, std::vector<vec3>* normals, std::vector<vec2>* texcoords, std::vector<uint32_t>& indices, float size = 10.0f);

        void GetColoredCube(std::vector<VertexPNC>& vertices, std::vector<uint32_t>& indices, float size = 1.0f);

        void GetSphere(std::vector<VertexPN>& vertices, std::vector<uint32_t>& indices, float radius = 1.0f, int slices = 16, int stacks = 24);
        void GetSphere(std::vector<VertexPNT>& vertices, std::vector<uint32_t>& indices, float radius = 1.0f, int slices = 16, int stacks = 24);

        void GetPlaneXY(std::vector<VertexPNT>& vertices, std::vector<uint32_t>& indices, float size = 1.f);
    }
}

