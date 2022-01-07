#include "AccelerationStructure.h"
#include "VkrayBookUtility.h"
#include "GraphicsDevice.h"

void AccelerationStructure::Destroy(VkGraphicsDevice& device)
{
    DestroyScratchBuffer(device);
    if (m_updateBuffer.GetBuffer()) {
        device->DestroyBuffer(m_updateBuffer);
    }
    vkDestroyAccelerationStructureKHR(device->GetDevice(), m_accelerationStructure.handle, nullptr);
    device->DestroyBuffer(m_accelerationStructure.bufferResource);
}

void AccelerationStructure::BuildAS(
    VkGraphicsDevice& device,
    VkAccelerationStructureTypeKHR type,
    const Input& input,
    VkBuildAccelerationStructureFlagsKHR buildFlags)
{
    auto deviceVk = device->GetDevice();

    // サイズを求める.
    VkAccelerationStructureBuildGeometryInfoKHR asBuildGeometryInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR
    };
    asBuildGeometryInfo.type = type;
    asBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    asBuildGeometryInfo.flags = buildFlags;

    asBuildGeometryInfo.geometryCount = uint32_t(input.asGeometry.size());
    asBuildGeometryInfo.pGeometries = input.asGeometry.data();

    VkAccelerationStructureBuildSizesInfoKHR asBuildSizesInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
    };
    std::vector<uint32_t> numPrimitives;
    numPrimitives.reserve(input.asBuildRangeInfo.size());
    for (int i = 0; i < input.asBuildRangeInfo.size(); ++i) {
        numPrimitives.push_back(input.asBuildRangeInfo[i].primitiveCount);
    }

    vkGetAccelerationStructureBuildSizesKHR(
        deviceVk,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &asBuildGeometryInfo,
        numPrimitives.data(),
        &asBuildSizesInfo
    );

    // Accleration Structure を確保する.
    VkBufferUsageFlags asUsage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    m_accelerationStructure.bufferResource = device->CreateBuffer(
        asBuildSizesInfo.accelerationStructureSize, asUsage, memProps);
    m_accelerationStructure.size = asBuildSizesInfo.accelerationStructureSize;

    VkAccelerationStructureCreateInfoKHR asCreateInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR
    };
    asCreateInfo.buffer = m_accelerationStructure.bufferResource.GetBuffer();
    asCreateInfo.size = m_accelerationStructure.size;
    asCreateInfo.type = asBuildGeometryInfo.type;
    vkCreateAccelerationStructureKHR(
        deviceVk, &asCreateInfo, nullptr, &m_accelerationStructure.handle);

    // AccelerationStructureのデバイスアドレスを取得.
    VkAccelerationStructureDeviceAddressInfoKHR asDeviceAddressInfo{};
    asDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    asDeviceAddressInfo.accelerationStructure = m_accelerationStructure.handle;
    m_accelerationStructure.deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(
        deviceVk, &asDeviceAddressInfo);

    // スクラッチバッファを準備する.
    if (asBuildSizesInfo.buildScratchSize > 0) {
        m_scratchBuffer = device->CreateBuffer(
            asBuildSizesInfo.buildScratchSize, asUsage, memProps);
    }

    // アップデートバッファを準備する.
    if (asBuildSizesInfo.updateScratchSize > 0) {
        m_updateBuffer = device->CreateBuffer(
            asBuildSizesInfo.updateScratchSize, asUsage, memProps);
    }

    // Acceleration Structure を構築する.
    asBuildGeometryInfo.dstAccelerationStructure = m_accelerationStructure.handle;
    asBuildGeometryInfo.scratchData.deviceAddress = m_scratchBuffer.GetDeviceAddress();
    Build(device, asBuildGeometryInfo, input.asBuildRangeInfo);
}

void AccelerationStructure::Update(VkCommandBuffer command, 
    VkAccelerationStructureTypeKHR type, 
    const Input& input, 
    VkBuildAccelerationStructureFlagsKHR buildFlags)
{
    VkAccelerationStructureBuildGeometryInfoKHR accelerationStructureBuildGeometryInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR
    };
    accelerationStructureBuildGeometryInfo.type = type;
    accelerationStructureBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
    accelerationStructureBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR | buildFlags;

    accelerationStructureBuildGeometryInfo.geometryCount = uint32_t(input.asGeometry.size());
    accelerationStructureBuildGeometryInfo.pGeometries = input.asGeometry.data();

    accelerationStructureBuildGeometryInfo.dstAccelerationStructure = m_accelerationStructure.handle;
    accelerationStructureBuildGeometryInfo.srcAccelerationStructure = m_accelerationStructure.handle;
    accelerationStructureBuildGeometryInfo.scratchData.deviceAddress = m_updateBuffer.GetDeviceAddress();
    
    // 更新処理.
    std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> asBuildRangeInfoPtrs;
    for (auto& v : input.asBuildRangeInfo) {
        asBuildRangeInfoPtrs.push_back(&v);
    }
    vkCmdBuildAccelerationStructuresKHR(
        command, 1, &accelerationStructureBuildGeometryInfo, asBuildRangeInfoPtrs.data()
    );

    // メモリバリアが必要.
    VkMemoryBarrier barrier{
        VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    };
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(
        command,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        0, 1, &barrier,
        0, nullptr,
        0, nullptr
    );
}


void AccelerationStructure::DestroyScratchBuffer(VkGraphicsDevice& device)
{
    if (m_scratchBuffer.GetBuffer()) {
        device->DestroyBuffer(m_scratchBuffer);
    }
}

void AccelerationStructure::Build(
    VkGraphicsDevice& device,
    const VkAccelerationStructureBuildGeometryInfoKHR& asBuildGeometryInfo,
    const std::vector<VkAccelerationStructureBuildRangeInfoKHR>& asBuildRangeInfo)
{
    std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> asBuildRangeInfoPtrs;
    for (auto& v : asBuildRangeInfo) {
        asBuildRangeInfoPtrs.push_back(&v);
    }
    auto command = device->CreateCommandBuffer();
    vkCmdBuildAccelerationStructuresKHR(
        command, 1, &asBuildGeometryInfo, asBuildRangeInfoPtrs.data()
    );
    // メモリバリアが必要.
    VkMemoryBarrier barrier{
        VK_STRUCTURE_TYPE_MEMORY_BARRIER,
    };
    barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(
        command,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        0, 1, &barrier,
        0, nullptr,
        0, nullptr
    );
    vkEndCommandBuffer(command);

    // AccelerationStructure ビルド＆完了するまでを待機.
    device->SubmitAndWait(command);
    device->DestroyCommandBuffer(command);
}

