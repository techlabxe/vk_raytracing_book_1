#include "HelloTriangle.h"

void HelloTriangle::OnInit()
{
    // 3�p�`�W�I���g���p�ӂ���BLAS���������܂�.
    CreateTriangleBLAS();

    // ���C�g���[�V���O���邽��TLAS���������܂�.
    CreateSceneTLAS();

    // ���C�g���[�V���O�p�̌��ʃo�b�t�@����������.
    CreateRaytracedBuffer();

    // ���ꂩ��K�v�ɂȂ�e�탌�C�A�E�g�̏���.
    CreateLayouts();

    // ���C�g���[�V���O�p�C�v���C�����\�z����.
    CreateRaytracePipeline();

    // �V�F�[�_�[�o�C���f�B���O�e�[�u�����\�z����.
    CreateShaderBindingTable();

    // �f�B�X�N���v�^�̏����E��������.
    CreateDescriptorSets();
}

void HelloTriangle::OnDestroy()
{
    // �g�p�����e���\�[�X��j��.
    m_device->DestroyImage(m_raytracedImage);
    m_device->DestroyBuffer(m_vertexBuffer);
    m_device->DestroyBuffer(m_instancesBuffer);

    DestroyAcceleratioStructureBuffer(m_topLevelAS);
    DestroyAcceleratioStructureBuffer(m_bottomLevelAS);
    m_device->DestroyBuffer(m_shaderBindingTable);

    auto device = m_device->GetDevice();
    vkFreeDescriptorSets(device, m_device->GetDescriptorPool(), 1, &m_descriptorSet);
    vkDestroyPipeline(device, m_raytracePipeline, nullptr);
    vkDestroyDescriptorSetLayout(device, m_dsLayout, nullptr);
    vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
}

void HelloTriangle::OnUpdate()
{
}

void HelloTriangle::OnRender()
{
    m_device->WaitAvailableFrame();
    auto frameIndex = m_device->GetCurrentFrameIndex();
    auto command = m_device->GetCurrentFrameCommandBuffer();
    VkCommandBufferBeginInfo commandBI{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr, 0, nullptr
    };

    vkBeginCommandBuffer(command, &commandBI);

    auto area = m_device->GetRenderArea().extent;

    // ���C�g���[�V���O���s��.
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_raytracePipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

    VkStridedDeviceAddressRegionKHR callableSbtEntry{};
    vkCmdTraceRaysKHR(
        command,
        &m_regionRaygen,
        &m_regionMiss,
        &m_regionHit,
        &callableSbtEntry,
        area.width, area.height, 1
    );

    // ���C�g���[�V���O���ʉ摜���o�b�N�o�b�t�@�փR�s�[.
    auto backbuffer = m_device->GetRenderTarget(frameIndex);
    VkImageCopy region{};
    region.extent = { area.width, area.height, 1 };
    region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    
    m_raytracedImage.BarrierToSrc(command);
    backbuffer.BarrierToDst(command);

    vkCmdCopyImage(command,
        m_raytracedImage.GetImage(), m_raytracedImage.GetImageLayout(),
        backbuffer.GetImage(), backbuffer.GetImageLayout(),
        1, &region);

    m_raytracedImage.BarrierToGeneral(command);
    backbuffer.BarrierToPresentSrc(command);

    vkEndCommandBuffer(command);
    m_device->SubmitCurrentFrameCommandBuffer();
    m_device->Present();
}

void HelloTriangle::CreateTriangleBLAS()
{
    Vertex tri[] = {
        glm::vec3{-0.5f, -0.5f, 0.0f},
        glm::vec3{+0.5f, -0.5f, 0.0f},
        glm::vec3{ 0.0f, 0.75f, 0.0f},
    };
    auto vbSize = sizeof(tri);
    // 3�p�`���i�[�������_�o�b�t�@������.
    VkBufferUsageFlags usageVB = \
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | \
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | \
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |\
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    VkMemoryPropertyFlags hostMemProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    m_vertexBuffer = m_device->CreateBuffer( vbSize, usageVB, hostMemProps );
    m_device->WriteToBuffer(m_vertexBuffer, &tri[0], vbSize);

    // BLAS ���쐬.
    VkDeviceOrHostAddressConstKHR vbDeviceAddress{};
    vbDeviceAddress.deviceAddress = m_device->GetDeviceAddress(m_vertexBuffer.GetBuffer());

    VkAccelerationStructureGeometryKHR asGeometry{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR
    };
    asGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    asGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    asGeometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    asGeometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    asGeometry.geometry.triangles.vertexData = vbDeviceAddress;
    asGeometry.geometry.triangles.maxVertex = _countof(tri);
    asGeometry.geometry.triangles.vertexStride = sizeof(Vertex);
    asGeometry.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;

    // �T�C�Y�����擾����.
    VkAccelerationStructureBuildGeometryInfoKHR asBuildGeometryInfo{};
    asBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    asBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    asBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    asBuildGeometryInfo.geometryCount = 1;
    asBuildGeometryInfo.pGeometries = &asGeometry;

    uint32_t numTriangles = 1;
    VkAccelerationStructureBuildSizesInfoKHR asBuildSizesInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
    };

    auto device = m_device->GetDevice();
    vkGetAccelerationStructureBuildSizesKHR(
        device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &asBuildGeometryInfo,
        &numTriangles,
        &asBuildSizesInfo);

    // AccelerationStructure�o�b�t�@.
    //  �܂��̓o�b�t�@�p�̃��������m�ۂ���.
    m_bottomLevelAS = CreateAccelerationStructureBuffer(asBuildSizesInfo);
    //  AccelerationStructure�o�b�t�@�𐶐�����.
    VkAccelerationStructureCreateInfoKHR asCI{};
    asCI.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    asCI.buffer = m_bottomLevelAS.buffer;
    asCI.size = asBuildSizesInfo.accelerationStructureSize;
    asCI.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    vkCreateAccelerationStructureKHR(device, &asCI, nullptr, &m_bottomLevelAS.handle);

    // AccelerationStructure�̃f�o�C�X�A�h���X���擾.
    VkAccelerationStructureDeviceAddressInfoKHR asDeviceAddressInfo{};
    asDeviceAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    asDeviceAddressInfo.accelerationStructure = m_bottomLevelAS.handle;
    m_bottomLevelAS.deviceAddress = vkGetAccelerationStructureDeviceAddressKHR(device, &asDeviceAddressInfo);

    // BLAS���g���\�z���邽�߂̃X�N���b�`�o�b�t�@����������.
    RayTracingScratchBuffer scratchBuffer = CreateScratchBuffer(asBuildSizesInfo.buildScratchSize);

    // VkAccelerationStructureBuildGeometryInfoKHR �̑��̃p�����[�^�ɔ��f������.
    asBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    asBuildGeometryInfo.dstAccelerationStructure = m_bottomLevelAS.handle;
    asBuildGeometryInfo.scratchData.deviceAddress = scratchBuffer.deviceAddress;

    // AccelerationStructure(BLAS)�̍\�z�R�}���h�����s����.
    VkAccelerationStructureBuildRangeInfoKHR asBuildRangeInfo{};
    asBuildRangeInfo.primitiveCount = numTriangles;
    asBuildRangeInfo.primitiveOffset = 0;
    asBuildRangeInfo.firstVertex = 0;
    asBuildRangeInfo.transformOffset = 0;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR*> asBuildRangeInfos = { 
        &asBuildRangeInfo };

    auto command = m_device->CreateCommandBuffer();
    vkCmdBuildAccelerationStructuresKHR(
        command, 1,
        &asBuildGeometryInfo,
        asBuildRangeInfos.data());

    // �\�z������̃������o���A��ݒ肷��.
    VkBufferMemoryBarrier bmb{
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER
    };
    bmb.buffer = m_bottomLevelAS.buffer;
    bmb.size = VK_WHOLE_SIZE;
    bmb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bmb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bmb.srcAccessMask = \
        VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    bmb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(command,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        0, 0, nullptr, 1, &bmb, 0, nullptr);
    vkEndCommandBuffer(command);

    // �\�z�R�}���h�𔭍s���āA�����܂őҋ@����.
    m_device->SubmitAndWait(command);
    m_device->DestroyCommandBuffer(command);

    // �X�N���b�`�o�b�t�@�͂���ȏ�g��Ȃ��̂Ŕj��.
    vkDestroyBuffer(device, scratchBuffer.handle, nullptr);
    vkFreeMemory(device, scratchBuffer.memory, nullptr);
}

void HelloTriangle::CreateSceneTLAS()
{
    VkTransformMatrixKHR transformMatrix = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f };

    VkAccelerationStructureInstanceKHR asInstance{};
    asInstance.transform = transformMatrix;
    asInstance.instanceCustomIndex = 0;
    asInstance.mask = 0xFF;
    asInstance.instanceShaderBindingTableRecordOffset = 0;
    asInstance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    asInstance.accelerationStructureReference = m_bottomLevelAS.deviceAddress;

    auto device = m_device->GetDevice();
    VkBufferUsageFlags usage = \
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | \
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    VkMemoryPropertyFlags hostMemProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    // VkAccelerationStructureInstanceKHR�̃f�[�^���i�[����o�b�t�@������.
    VkBufferCreateInfo instanceBufferCI{
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, nullptr,
    };
    auto bufferSize = sizeof(VkAccelerationStructureInstanceKHR);
    m_instancesBuffer = m_device->CreateBuffer(
        bufferSize, usage, hostMemProps);
    m_device->WriteToBuffer(
        m_instancesBuffer, &asInstance, sizeof(asInstance));

    VkDeviceOrHostAddressConstKHR instanceDataDeviceAddress{};
    instanceDataDeviceAddress.deviceAddress = m_device->GetDeviceAddress(m_instancesBuffer.GetBuffer());

    VkAccelerationStructureGeometryKHR asGeometry{};
    asGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    asGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    asGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    asGeometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    asGeometry.geometry.instances.arrayOfPointers = VK_FALSE;
    asGeometry.geometry.instances.data = instanceDataDeviceAddress;

    // �T�C�Y�����擾����.
    VkAccelerationStructureBuildGeometryInfoKHR asBuildGeometryInfo{};
    asBuildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    asBuildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    asBuildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    asBuildGeometryInfo.geometryCount = 1;
    asBuildGeometryInfo.pGeometries = &asGeometry;

    uint32_t primitiveCount = 1;
    VkAccelerationStructureBuildSizesInfoKHR asBuildSizesInfo{
        VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR
    };
    vkGetAccelerationStructureBuildSizesKHR(
        device,
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &asBuildGeometryInfo,
        &primitiveCount,
        &asBuildSizesInfo);

    // AccelerationStructure�o�b�t�@.
    //  �܂��̓o�b�t�@�p�̃��������m�ۂ���.
    m_topLevelAS = CreateAccelerationStructureBuffer(asBuildSizesInfo);

    // AccelerationStructure�o�b�t�@�𐶐�����.
    VkAccelerationStructureCreateInfoKHR asCI{};
    asCI.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    asCI.buffer = m_topLevelAS.buffer;
    asCI.size = asBuildSizesInfo.accelerationStructureSize;
    asCI.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    vkCreateAccelerationStructureKHR(device, &asCI, nullptr, &m_topLevelAS.handle);

    // TLAS���g���\�z���邽�߂̃X�N���b�`�o�b�t�@����������.
    RayTracingScratchBuffer scratchBuffer = CreateScratchBuffer(asBuildSizesInfo.buildScratchSize);

    // VkAccelerationStructureBuildGeometryInfoKHR �̑��̃p�����[�^�ɔ��f������.
    asBuildGeometryInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    asBuildGeometryInfo.dstAccelerationStructure = m_topLevelAS.handle;
    asBuildGeometryInfo.scratchData.deviceAddress = scratchBuffer.deviceAddress;

    // AccelerationStructure(TLAS)�̍\�z�R�}���h�����s����.
    VkAccelerationStructureBuildRangeInfoKHR asBuildRangeInfo{};
    asBuildRangeInfo.primitiveCount = primitiveCount;
    asBuildRangeInfo.primitiveOffset = 0;
    asBuildRangeInfo.firstVertex = 0;
    asBuildRangeInfo.transformOffset = 0;
    std::vector<VkAccelerationStructureBuildRangeInfoKHR*> asBuildRangeInfos = { 
        &asBuildRangeInfo };

    auto command = m_device->CreateCommandBuffer();
    vkCmdBuildAccelerationStructuresKHR(
        command, 1,
        &asBuildGeometryInfo,
        asBuildRangeInfos.data());

    // �\�z������̃������o���A��ݒ肷��.
    VkBufferMemoryBarrier bmb{
        VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER
    };
    bmb.buffer = m_topLevelAS.buffer;
    bmb.size = VK_WHOLE_SIZE;
    bmb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bmb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bmb.srcAccessMask = \
        VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    bmb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
    vkCmdPipelineBarrier(command,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        0, 0, nullptr, 1, &bmb, 0, nullptr);
    vkEndCommandBuffer(command);

    // �\�z�R�}���h�𔭍s���āA�����܂őҋ@����.
    m_device->SubmitAndWait(command);
    m_device->DestroyCommandBuffer(command);

    // �X�N���b�`�o�b�t�@�͂���ȏ�g��Ȃ��̂Ŕj��.
    vkDestroyBuffer(device, scratchBuffer.handle, nullptr);
    vkFreeMemory(device, scratchBuffer.memory, nullptr);
}

void HelloTriangle::CreateRaytracedBuffer()
{
    // �o�b�N�o�b�t�@�Ɠ����t�H�[�}�b�g�ō쐬����.
    auto format = m_device->GetBackBufferFormat().format;
    auto device = m_device->GetDevice();
    auto rectSize = m_device->GetRenderArea().extent;

    VkImageUsageFlags usage = \
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT | \
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | \
        VK_IMAGE_USAGE_STORAGE_BIT;
    VkMemoryPropertyFlags devMemProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    m_raytracedImage = m_device->CreateTexture2D(
        rectSize.width, rectSize.height, format, usage, devMemProps);

    // �o�b�t�@�̏�Ԃ�ύX���Ă���.
    auto command = m_device->CreateCommandBuffer();
    m_raytracedImage.BarrierToGeneral(command);
    vkEndCommandBuffer(command);

    m_device->SubmitAndWait(command);   
    m_device->DestroyCommandBuffer(command);
}

void HelloTriangle::CreateRaytracePipeline()
{
    // ���C�g���[�V���O�̃V�F�[�_�[��ǂݍ���.
    auto rgsStage = util::LoadShader(m_device, L"shaders/raygen.rgen.spv", VK_SHADER_STAGE_RAYGEN_BIT_KHR);
    auto missStage = util::LoadShader(m_device, L"shaders/miss.rmiss.spv", VK_SHADER_STAGE_MISS_BIT_KHR);
    auto chitStage = util::LoadShader(m_device, L"shaders/closesthit.rchit.spv", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);

    std::vector<VkPipelineShaderStageCreateInfo> stages = {
        rgsStage, missStage, chitStage
    };

    // stages �z����ł̊e�V�F�[�_�[�̃C���f�b�N�X.
    const int indexRaygen = 0;
    const int indexMiss = 1;
    const int indexClosestHit = 2;

    // �V�F�[�_�[�O���[�v�̐���.

    auto rgsGroup = VkRayTracingShaderGroupCreateInfoKHR{
        VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR
    };
    rgsGroup.generalShader = indexRaygen;
    rgsGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    rgsGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
    rgsGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
    rgsGroup.intersectionShader = VK_SHADER_UNUSED_KHR;

    auto missGroup = VkRayTracingShaderGroupCreateInfoKHR{
        VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR
    };
    missGroup.generalShader = indexMiss;
    missGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    missGroup.closestHitShader = VK_SHADER_UNUSED_KHR;
    missGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
    missGroup.intersectionShader = VK_SHADER_UNUSED_KHR;

    auto rchitGroup = VkRayTracingShaderGroupCreateInfoKHR{
        VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR
    };
    rchitGroup.generalShader = VK_SHADER_UNUSED_KHR;
    rchitGroup.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;
    rchitGroup.closestHitShader = indexClosestHit;
    rchitGroup.anyHitShader = VK_SHADER_UNUSED_KHR;
    rchitGroup.intersectionShader = VK_SHADER_UNUSED_KHR;

    m_shaderGroups.resize(MaxShaderGroup);
    m_shaderGroups[GroupRayGenShader] = rgsGroup;
    m_shaderGroups[GroupMissShader] = missGroup;
    m_shaderGroups[GroupHitShader] = rchitGroup;

    // ���C�g���[�V���O�p�C�v���C���̐���.

    VkRayTracingPipelineCreateInfoKHR rtPipelineCI{};

    rtPipelineCI.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    rtPipelineCI.stageCount = uint32_t(stages.size());
    rtPipelineCI.pStages = stages.data();
    rtPipelineCI.groupCount = uint32_t(m_shaderGroups.size());
    rtPipelineCI.pGroups = m_shaderGroups.data();
    rtPipelineCI.maxPipelineRayRecursionDepth = 1;
    rtPipelineCI.layout = m_pipelineLayout;
    vkCreateRayTracingPipelinesKHR(
        m_device->GetDevice(), VK_NULL_HANDLE, VK_NULL_HANDLE, 
        1, &rtPipelineCI, nullptr, &m_raytracePipeline);
    
    // ���I�����̂ŃV�F�[�_�[���W���[���͉�����Ă��܂�.
    for (auto& v : stages) {
        vkDestroyShaderModule(
            m_device->GetDevice(), v.module, nullptr);
    }
}

void HelloTriangle::CreateShaderBindingTable()
{
    auto memProps = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    auto usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    const auto rtPipelineProps = m_device->GetRayTracingPipelineProperties();

    // �e�G���g���̃T�C�Y�����߂�.
    //  �e�G���g���T�C�Y shaderGroupHandleAlignment �ɐ؂�グ�Ă���.
    //  �{�T���v���ł͂��ꂼ��n���h���̂ݕێ�����.
    const auto handleSize = rtPipelineProps.shaderGroupHandleSize;
    const auto handleAlignment = rtPipelineProps.shaderGroupHandleAlignment;
    auto raygenShaderEntrySize = Align(handleSize, handleAlignment);
    auto missShaderEntrySize = Align(handleSize, handleAlignment);
    auto hitShaderEntrySize = Align(handleSize, handleAlignment);

    // �e�V�F�[�_�[�̌�.
    const auto raygenShaderCount = 1;
    const auto missShaderCount = 1;
    const auto hitShaderCount = 1;

    // �e�O���[�v�ŕK�v�ȃT�C�Y�����߂�.
    const auto baseAlign = rtPipelineProps.shaderGroupBaseAlignment;
    auto regionRaygen = Align(raygenShaderEntrySize * raygenShaderCount, baseAlign);
    auto regionMiss = Align(missShaderEntrySize * missShaderCount, baseAlign);
    auto regionHit = Align(hitShaderEntrySize * hitShaderCount, baseAlign);

    m_shaderBindingTable = m_device->CreateBuffer(
        regionRaygen + regionMiss + regionHit, usage, memProps);

    // �p�C�v���C����ShaderGroup�n���h�����擾.
    auto handleSizeAligned = Align(handleSize, handleAlignment);
    auto handleStorageSize = m_shaderGroups.size() * handleSizeAligned;
    std::vector<uint8_t> shaderHandleStorage(handleStorageSize);
    vkGetRayTracingShaderGroupHandlesKHR(m_device->GetDevice(),
        m_raytracePipeline,
        0, uint32_t(m_shaderGroups.size()),
        shaderHandleStorage.size(), shaderHandleStorage.data());

    auto device = m_device->GetDevice();
    auto deviceAddress = m_device->GetDeviceAddress(m_shaderBindingTable.GetBuffer());

    void* p = m_device->Map(m_shaderBindingTable);
    auto dst = static_cast<uint8_t*>(p);

    // RayGeneration�V�F�[�_�[�̃G���g������������.
    auto raygen = shaderHandleStorage.data() + handleSizeAligned * GroupRayGenShader;
    memcpy(dst, raygen, handleSize);
    dst += regionRaygen;
    m_regionRaygen.deviceAddress = deviceAddress;
    // Raygen �� size=stride���K�v.
    m_regionRaygen.stride = raygenShaderEntrySize;
    m_regionRaygen.size = m_regionRaygen.stride;

    // Miss�V�F�[�_�[�̃G���g������������.
    auto miss = shaderHandleStorage.data() + handleSizeAligned * GroupMissShader;
    memcpy(dst, miss, handleSize);
    dst += regionMiss;
    m_regionMiss.deviceAddress = deviceAddress + regionRaygen;
    m_regionMiss.size = regionMiss;
    m_regionMiss.stride = missShaderEntrySize;

    // �q�b�g�V�F�[�_�[�̃G���g������������.
    auto hit = shaderHandleStorage.data() + handleSizeAligned * GroupHitShader;
    memcpy(dst, hit, handleSize);
    dst += regionHit;
    m_regionHit.deviceAddress = deviceAddress + regionRaygen + regionMiss;
    m_regionHit.size = regionHit;
    m_regionHit.stride = hitShaderEntrySize;

    m_device->Unmap(m_shaderBindingTable);
}

void HelloTriangle::CreateLayouts()
{
    VkDescriptorSetLayoutBinding layoutAS{};
    layoutAS.binding = 0;
    layoutAS.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    layoutAS.descriptorCount = 1;
    layoutAS.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    VkDescriptorSetLayoutBinding layoutRtImage{};
    layoutRtImage.binding = 1;
    layoutRtImage.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    layoutRtImage.descriptorCount = 1;
    layoutRtImage.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    std::vector<VkDescriptorSetLayoutBinding> bindings({
        layoutAS, layoutRtImage });

    VkDescriptorSetLayoutCreateInfo dsLayoutCI{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
    };
    dsLayoutCI.bindingCount = static_cast<uint32_t>(bindings.size());
    dsLayoutCI.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(
        m_device->GetDevice(), &dsLayoutCI, nullptr, &m_dsLayout);

    VkPipelineLayoutCreateInfo pipelineLayoutCI{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO 
    };
    pipelineLayoutCI.setLayoutCount = 1;
    pipelineLayoutCI.pSetLayouts = &m_dsLayout;
    vkCreatePipelineLayout( m_device->GetDevice(), 
        &pipelineLayoutCI, nullptr, &m_pipelineLayout);
}

void HelloTriangle::CreateDescriptorSets()
{
    m_descriptorSet = m_device->AllocateDescriptorSet(m_dsLayout);

    VkWriteDescriptorSetAccelerationStructureKHR asDescriptor{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR
    };
    asDescriptor.accelerationStructureCount = 1;
    asDescriptor.pAccelerationStructures = &m_topLevelAS.handle;

    VkWriteDescriptorSet asWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    asWrite.pNext = &asDescriptor;
    asWrite.dstSet = m_descriptorSet;
    asWrite.dstBinding = 0;
    asWrite.descriptorCount = 1;
    asWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

    VkDescriptorImageInfo imageDescriptor{};
    imageDescriptor.imageView = m_raytracedImage.GetImageView();
    imageDescriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet imageWrite{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
    };
    imageWrite.dstSet = m_descriptorSet;
    imageWrite.dstBinding = 1;
    imageWrite.descriptorCount = 1;
    imageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    imageWrite.pImageInfo = &imageDescriptor;

    std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
        asWrite, imageWrite
    };
    vkUpdateDescriptorSets(
        m_device->GetDevice(),
        uint32_t(writeDescriptorSets.size()),
        writeDescriptorSets.data(),
        0,
        nullptr);
}

HelloTriangle::AccelerationStructure HelloTriangle::CreateAccelerationStructureBuffer(VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo)
{
    AccelerationStructure accelerationStructure;
    auto device = m_device->GetDevice();
    VkBufferCreateInfo bufferCreateInfo{};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size = buildSizeInfo.accelerationStructureSize;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    vkCreateBuffer(device, &bufferCreateInfo, nullptr, &accelerationStructure.buffer);

    VkMemoryRequirements memoryRequirements{};
    vkGetBufferMemoryRequirements(device, accelerationStructure.buffer, &memoryRequirements);
    VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo{};
    memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;
    VkMemoryAllocateInfo memoryAllocateInfo{};
    memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
    memoryAllocateInfo.allocationSize = memoryRequirements.size;
    memoryAllocateInfo.memoryTypeIndex = m_device->GetMemoryTypeIndex(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(device, &memoryAllocateInfo, nullptr, &accelerationStructure.memory);
    vkBindBufferMemory(device, accelerationStructure.buffer, accelerationStructure.memory, 0);

    return accelerationStructure;
}

void HelloTriangle::DestroyAcceleratioStructureBuffer(AccelerationStructure& as)
{
    auto device = m_device->GetDevice();
    vkDestroyAccelerationStructureKHR(device, as.handle, nullptr);
    vkDestroyBuffer(device, as.buffer, nullptr);
    vkFreeMemory(device, as.memory, nullptr);
}

HelloTriangle::RayTracingScratchBuffer HelloTriangle::CreateScratchBuffer(VkDeviceSize size)
{
    RayTracingScratchBuffer scratchBuffer{};
    auto device = m_device->GetDevice();

    VkBufferCreateInfo bufferCreateInfo{};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.size = size;
    bufferCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    vkCreateBuffer(device, &bufferCreateInfo, nullptr, &scratchBuffer.handle);

    VkMemoryRequirements memoryRequirements{};
    vkGetBufferMemoryRequirements(device, scratchBuffer.handle, &memoryRequirements);

    VkMemoryAllocateFlagsInfo memoryAllocateFlagsInfo{};
    memoryAllocateFlagsInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
    memoryAllocateFlagsInfo.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR;

    VkMemoryAllocateInfo memoryAllocateInfo = {};
    memoryAllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memoryAllocateInfo.pNext = &memoryAllocateFlagsInfo;
    memoryAllocateInfo.allocationSize = memoryRequirements.size;
    memoryAllocateInfo.memoryTypeIndex = m_device->GetMemoryTypeIndex(memoryRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkAllocateMemory(device, &memoryAllocateInfo, nullptr, &scratchBuffer.memory);
    vkBindBufferMemory(device, scratchBuffer.handle, scratchBuffer.memory, 0);

    scratchBuffer.deviceAddress = m_device->GetDeviceAddress(scratchBuffer.handle);
    return scratchBuffer;
}
