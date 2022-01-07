#include "GraphicsDevice.h"
#include "AccelerationStructure.h"
#include "scene/SimplePolygonMesh.h"
#include "VkrayBookUtility.h"

void SimplePolygonMesh::Create(VkGraphicsDevice& device, const CreateInfo& createInfo, MaterialManager& materialManager)
{
    auto hostMemProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    auto usageForRT = \
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | \
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    VkBufferUsageFlags usageVB = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    usageVB |= usageForRT;

    auto vbSize = createInfo.vbSize;
    vertexBuffer = device->CreateBuffer(vbSize, usageVB, hostMemProps);
    device->WriteToBuffer(vertexBuffer, createInfo.srcVertices, vbSize);

    VkBufferUsageFlags usageIB = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    usageIB |= usageForRT;
    auto ibSize = createInfo.ibSize;
    indexBuffer = device->CreateBuffer(ibSize, usageIB, hostMemProps);
    device->WriteToBuffer(indexBuffer, createInfo.srcIndices, ibSize);

    indexCount = createInfo.indexCount;
    vertexCount = createInfo.vertexCount;
    vertexStride = createInfo.stride;
    m_material = createInfo.material;
    m_materialIndex = materialManager.AddMaterial(m_material);
}

void SimplePolygonMesh::BuildAS(VkGraphicsDevice& device, VkBuildAccelerationStructureFlagsKHR buildFlags)
{
    AccelerationStructure::Input blasInput;
    blasInput.asGeometry = GetAccelerationStructureGeometry();
    blasInput.asBuildRangeInfo = GetAccelerationStructureBuildRangeInfo();

    m_blas.BuildAS(device,
        VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        blasInput,
        buildFlags);
    m_blas.DestroyScratchBuffer(device);

    m_asInstance.accelerationStructureReference = m_blas.GetDeviceAddress();
}

std::vector<VkAccelerationStructureGeometryKHR> SimplePolygonMesh::GetAccelerationStructureGeometry(int frameIndex)
{
    VkAccelerationStructureGeometryKHR asGeometry{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR
    };
    asGeometry.flags = m_geometryFlags;
    asGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    // VkAccelerationStructureGeometryTrianglesDataKHR
    auto& triangles = asGeometry.geometry.triangles;
    triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    triangles.vertexData.deviceAddress = vertexBuffer.GetDeviceAddress();
    triangles.maxVertex = vertexCount;
    triangles.vertexStride = vertexStride;
    triangles.indexType = VK_INDEX_TYPE_UINT32;
    triangles.indexData.deviceAddress = indexBuffer.GetDeviceAddress();
    return { asGeometry };
}

std::vector<VkAccelerationStructureBuildRangeInfoKHR> SimplePolygonMesh::GetAccelerationStructureBuildRangeInfo()
{
    VkAccelerationStructureBuildRangeInfoKHR asBuildRangeInfo{};
    asBuildRangeInfo.primitiveCount = indexCount / 3;
    asBuildRangeInfo.primitiveOffset = 0;
    asBuildRangeInfo.firstVertex = 0;
    asBuildRangeInfo.transformOffset = 0;
    return { asBuildRangeInfo };
}

std::vector<SceneObject::SceneObjectParameter> SimplePolygonMesh::GetSceneObjectParameters()
{
    SceneObjectParameter objParam{};

    objParam.blasMatrixIndex = 0;
    objParam.materialIndex = m_materialIndex;
    objParam.vertexPosition = GetDeviceAddressVB();
    objParam.vertexNormal = 0;
    objParam.vertexTexcoord = 0;
    objParam.indexBuffer = GetDeviceAddressIB();

    return { objParam };
}
