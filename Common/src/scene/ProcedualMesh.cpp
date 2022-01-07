#include "GraphicsDevice.h"
#include "AccelerationStructure.h"
#include "scene/ProcedualMesh.h"

#include "VkrayBookUtility.h"

void ProcedualMesh::Create(VkGraphicsDevice& device, const CreateInfo& createInfo, MaterialManager& materialManager)
{
    auto hostMemProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    auto usageForRT = \
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | \
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    usage |= usageForRT;

    aabbBuffer = device->CreateBuffer(createInfo.size, usage, hostMemProps);
    device->WriteToBuffer(aabbBuffer, createInfo.data, createInfo.size);

    dataStride = createInfo.stride;
    m_material = createInfo.material;
    m_materialIndex = materialManager.AddMaterial(m_material);
}

void ProcedualMesh::BuildAS(VkGraphicsDevice& device, VkBuildAccelerationStructureFlagsKHR buildFlags)
{
    auto asGeometry = GetAccelerationStructureGeometry();
    auto asBuildRangeInfo = GetAccelerationStructureBuildRangeInfo();

    AccelerationStructure::Input blasInput;
    blasInput.asGeometry = { asGeometry };
    blasInput.asBuildRangeInfo = { asBuildRangeInfo };

    m_blas.BuildAS(device,
        VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        blasInput,
        buildFlags);
    m_blas.DestroyScratchBuffer(device);

    m_asInstance.accelerationStructureReference = m_blas.GetDeviceAddress();
}

std::vector<VkAccelerationStructureGeometryKHR> ProcedualMesh::GetAccelerationStructureGeometry(int frameIndex)
{
    VkAccelerationStructureGeometryKHR asGeometry{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR
    };
    asGeometry.flags = m_geometryFlags;
    asGeometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
    // VkAccelerationStructureGeometryAabbsDataKHR
    auto& aabbs = asGeometry.geometry.aabbs;
    aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
    aabbs.stride = dataStride;
    aabbs.data.deviceAddress = aabbBuffer.GetDeviceAddress();

    return { asGeometry };
}

std::vector<VkAccelerationStructureBuildRangeInfoKHR> ProcedualMesh::GetAccelerationStructureBuildRangeInfo()
{
    VkAccelerationStructureBuildRangeInfoKHR asBuildRangeInfo{};
    asBuildRangeInfo.primitiveCount = 1;
    asBuildRangeInfo.primitiveOffset = 0;
    asBuildRangeInfo.firstVertex = 0;
    asBuildRangeInfo.transformOffset = 0;
    return { asBuildRangeInfo };
}

std::vector<SceneObject::SceneObjectParameter> ProcedualMesh::GetSceneObjectParameters()
{
    SceneObjectParameter objParam{};

    objParam.blasMatrixIndex = 0;
    objParam.materialIndex = m_materialIndex;
    objParam.vertexPosition = aabbBuffer.GetDeviceAddress();
    objParam.vertexNormal = 0;
    objParam.vertexTexcoord = 0;
    objParam.indexBuffer = 0;

    return { objParam };
}
