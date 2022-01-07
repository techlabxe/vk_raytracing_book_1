#pragma once

#include <vulkan/vulkan.h>
#include <memory>
#include <vector>

#include "GraphicsDevice.h"

class AccelerationStructure {
public:
    using VkGraphicsDevice = std::unique_ptr<vk::GraphicsDevice>;

    void Destroy(VkGraphicsDevice& device);

    struct Input {
        std::vector<VkAccelerationStructureGeometryKHR> asGeometry;
        std::vector<VkAccelerationStructureBuildRangeInfoKHR> asBuildRangeInfo;
    };

    // AccelerationStructureを構築
    void BuildAS(
        VkGraphicsDevice& device,
        VkAccelerationStructureTypeKHR type,
        const Input& input,
        VkBuildAccelerationStructureFlagsKHR buildFlags);

    // AccelerationStructureを更新
    void Update(
        VkCommandBuffer command, 
        VkAccelerationStructureTypeKHR type, 
        const Input& input,
        VkBuildAccelerationStructureFlagsKHR buildFlags = 0);


    void DestroyScratchBuffer(VkGraphicsDevice& device);

    VkAccelerationStructureKHR GetHandle() const { return m_accelerationStructure.handle; }
    VkDeviceAddress GetDeviceAddress() const { return m_accelerationStructure.deviceAddress; }
private:
    
    void Build(VkGraphicsDevice& device,
        const VkAccelerationStructureBuildGeometryInfoKHR& asBuildGeometryInfo,
        const std::vector<VkAccelerationStructureBuildRangeInfoKHR>& asBuildRangeInfo);

    // AccelerationStructure本体データ.
    struct {
        VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
        VkDeviceAddress deviceAddress = 0;
        VkDeviceSize size = 0;
        vk::BufferResource bufferResource;
    } m_accelerationStructure;

    // AccelerationStructure構築/更新のための作業バッファ.
    vk::BufferResource m_scratchBuffer;
    vk::BufferResource m_updateBuffer;
};