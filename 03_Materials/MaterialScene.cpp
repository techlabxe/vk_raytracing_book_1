#include "MaterialScene.h"
#include <glm/gtx/transform.hpp>
#include <random>

// For ImGui
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

void MaterialScene::OnInit()
{
    // �V�[���ɔz�u����W�I���g�����������܂�.
    CreateSceneGeometries();

    // �W�I���g����BLAS���������܂�.
    CreateSceneBLAS();

    // �V�[���ɃI�u�W�F�N�g��z�u.
    DeployObjects();
    CreateSceneList();
    CreateSceneBuffers();

    // ���C�g���[�V���O���邽��TLAS���������܂�.
    CreateSceneTLAS();

    // ���C�g���[�V���O�p�̌��ʃo�b�t�@����������.
    CreateRaytracedBuffer();

    // ���ꂩ��K�v�ɂȂ�e�탌�C�A�E�g�̏���.
    CreateLayouts();

    // �V�[���p�����[�^�pUniformBuffer�𐶐�.
    m_sceneUBO.Initialize(m_device, sizeof(SceneParam),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    // ���C�g���[�V���O�p�C�v���C�����\�z����.
    CreateRaytracePipeline();

    // �V�F�[�_�[�o�C���f�B���O�e�[�u�����\�z����.
    CreateShaderBindingTable();

    // �f�B�X�N���v�^�̏����E��������.
    CreateDescriptorSets();

    // ImGui �̏�����.
    InitializeImGui();

    // �����p�����[�^�ݒ�.
    auto eye = glm::vec3(0.0f, 4.0f, 15.0f);
    auto target = glm::vec3(0.0f, 0.0f, 0.0f);

    eye = glm::vec3(5.5f, 1.25f, 1.75f);

    m_camera.SetLookAt(eye, target);

    m_camera.SetPerspective(
        glm::radians(45.0f),
        GetAspect(),
        0.1f,
        100.f
    );

    m_sceneParam.lightColor = glm::vec4(1.0f);
    m_sceneParam.lightDirection = glm::vec4(-0.2f, -1.0f, -1.0f, 0.0f);
    m_sceneParam.ambientColor = glm::vec4(0.25f);
}

void MaterialScene::OnDestroy()
{
    m_sceneUBO.Destroy(m_device);
    m_device->DestroyBuffer(m_objectsSBO);
    m_device->DestroyBuffer(m_materialsSBO);
    m_device->DestroyBuffer(m_instancesBuffer);
    m_topLevelAS.Destroy(m_device);

    m_device->DestroyBuffer(m_meshSphere.vertexBuffer);
    m_device->DestroyBuffer(m_meshSphere.indexBuffer);
    m_meshSphere.blas.Destroy(m_device);

    m_device->DestroyBuffer(m_meshPlane.vertexBuffer);
    m_device->DestroyBuffer(m_meshPlane.indexBuffer);
    m_meshPlane.blas.Destroy(m_device);

    m_device->DestroyImage(m_raytracedImage);
    m_device->DestroyBuffer(m_shaderBindingTable);

    m_device->DestroyImage(m_cubemap);
    for (auto& t : m_textures) {
        m_device->DestroyImage(t);
    }
    m_textures.clear();
    m_device->DeallocateDescriptorSet(m_descriptorSet);

    DestroyImGui();

    auto device = m_device->GetDevice();
    vkDestroySampler(device, m_cubemapSampler, nullptr);
    vkDestroySampler(device, m_defaultSampler, nullptr);
    vkDestroyPipeline(device, m_raytracePipeline, nullptr);
    vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, m_dsLayout, nullptr);
}

void MaterialScene::OnUpdate()
{
    UpdateHUD();
    m_sceneParam.mtxView = m_camera.GetViewMatrix();
    m_sceneParam.mtxProj = m_camera.GetProjectionMatrix();
    m_sceneParam.mtxViewInv = glm::inverse(m_sceneParam.mtxView);
    m_sceneParam.mtxProjInv = glm::inverse(m_sceneParam.mtxProj);
    m_sceneParam.cameraPosition = m_camera.GetPosition();
}

void MaterialScene::OnRender()
{
    m_device->WaitAvailableFrame();
    auto frameIndex = m_device->GetCurrentFrameIndex();
    auto command = m_device->GetCurrentFrameCommandBuffer();

    m_sceneParam.frameIndex = frameIndex;
    void* p = m_sceneUBO.Map(frameIndex);
    if (p) {
        memcpy(p, &m_sceneParam, sizeof(m_sceneParam));
    }

    VkCommandBufferBeginInfo commandBI{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr, 0, nullptr
    };
    vkBeginCommandBuffer(command, &commandBI);

    // ���C�g���[�V���O���s��.
    uint32_t offsets[] = {
        uint32_t(m_sceneUBO.GetBlockSize() * frameIndex)
    };
    std::vector<VkDescriptorSet> descriptorSets = {
        m_descriptorSet
    };

    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_raytracePipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipelineLayout, 0,
        uint32_t(descriptorSets.size()), descriptorSets.data(),
        _countof(offsets), offsets);

    auto area = m_device->GetRenderArea().extent;
    VkStridedDeviceAddressRegionKHR callable_shader_sbt_entry{};
    vkCmdTraceRaysKHR(
        command,
        &m_sbtInfo.rgen,
        &m_sbtInfo.miss,
        &m_sbtInfo.hit,
        &callable_shader_sbt_entry,
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

    // ����̏������݂ɔ����ď�ԑJ��.
    m_raytracedImage.BarrierToGeneral(command);

    // ImGui �ɂ�郉�X�^���C�Y�`��p�X�����s.
    VkClearValue clearValue = {
        { 0.85f, 0.5f, 0.5f, 0.0f}, // for Color
    };
    VkRenderPassBeginInfo rpBI{
      VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
      nullptr,
      m_renderPass,
      m_framebuffers[frameIndex],
      m_device->GetRenderArea(),
      1, &clearValue
    };
    vkCmdBeginRenderPass(command, &rpBI, VK_SUBPASS_CONTENTS_INLINE);

    // ImGui �̕`��͂����ōs��.
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);

    // �����_�[�p�X���I������ƃo�b�N�o�b�t�@��
    // TRANSFER_DST_OPTIMAL->PRESENT_SRC_KHR �փ��C�A�E�g�ύX���K�p�����.
    vkCmdEndRenderPass(command);

    vkEndCommandBuffer(command);

    // �R�}���h�����s���ĉ�ʕ\��.
    m_device->SubmitCurrentFrameCommandBuffer();
    m_device->Present();
}

void MaterialScene::OnMouseDown(MouseButton button)
{
    if (ImGui::GetIO().WantCaptureMouse) {
        return;
    }
    m_camera.OnMouseButtonDown(int(button));
}

void MaterialScene::OnMouseUp(MouseButton button)
{
    if (ImGui::GetIO().WantCaptureMouse) {
        return;
    }
    m_camera.OnMouseButtonUp();
}

void MaterialScene::OnMouseMove()
{
    if (ImGui::GetIO().WantCaptureMouse) {
        return;
    }
    auto mouseDelta = ImGui::GetIO().MouseDelta;
    float dx = float(mouseDelta.x) / GetWidth();
    float dy = float(mouseDelta.y) / GetHeight();
    m_camera.OnMouseMove(-dx, dy);
}

void MaterialScene::CreateSceneGeometries()
{
    auto hostMemProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    auto usageForRT = \
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | \
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    std::vector<util::primitive::VertexPNT> vertices;
    std::vector<uint32_t> indices;
    const auto vstride = uint32_t(sizeof(util::primitive::VertexPNT));
    const auto istride = uint32_t(sizeof(uint32_t));

    // �����ʂ̏���.
    {
        util::primitive::GetPlane(vertices, indices);

        auto vbPlaneSize = vstride * vertices.size();
        auto ibPlaneSize = istride * indices.size();

        VkBufferUsageFlags usage;
        usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        usage |= usageForRT;
        m_meshPlane.vertexBuffer = m_device->CreateBuffer(vbPlaneSize, usage, hostMemProps);
        m_device->WriteToBuffer(
            m_meshPlane.vertexBuffer, vertices.data(), vbPlaneSize);
        m_meshPlane.vertexCount = uint32_t(vertices.size());
        m_meshPlane.vertexStride = vstride;

        usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        usage |= usageForRT;
        m_meshPlane.indexBuffer = m_device->CreateBuffer(ibPlaneSize, usage, hostMemProps);
        m_device->WriteToBuffer(
            m_meshPlane.indexBuffer, indices.data(), ibPlaneSize);
        m_meshPlane.indexCount = uint32_t(indices.size());
    }
    // Sphere�̏���.
    {
        util::primitive::GetSphere(vertices, indices, 0.5f, 32, 32);
        auto vbSphereSize = vstride * vertices.size();
        auto ibSphereSize = istride * indices.size();

        VkBufferUsageFlags usage;
        usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        usage |= usageForRT;
        m_meshSphere.vertexBuffer = m_device->CreateBuffer(vbSphereSize, usage, hostMemProps);
        m_device->WriteToBuffer(
            m_meshSphere.vertexBuffer, vertices.data(), vbSphereSize);
        m_meshSphere.vertexCount = uint32_t(vertices.size());
        m_meshSphere.vertexStride = vstride;

        usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        usage |= usageForRT;
        m_meshSphere.indexBuffer = m_device->CreateBuffer(ibSphereSize, usage, hostMemProps);
        m_device->WriteToBuffer(
            m_meshSphere.indexBuffer, indices.data(), ibSphereSize);
        m_meshSphere.indexCount = uint32_t(indices.size());
    }

    // �w�i�Ŏg�p����e�N�X�`��(�L���[�u�}�b�v)������.
    {
        const wchar_t* faceFiles[6] = {
            L"textures/posx.jpg",L"textures/negx.jpg",
            L"textures/posy.jpg",L"textures/negy.jpg",
            L"textures/posz.jpg",L"textures/negz.jpg",
        };
        m_cubemap = m_device->CreateTextureCube(
            faceFiles,
            VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );
        m_cubemapSampler = m_device->CreateSampler();
    }

    // ���Ŏg�p����e�N�X�`���E���Ŏg�p����e�N�X�`��.
    for (const auto* textureFile : { L"textures/trianglify-lowres.png", L"textures/land_ocean_ice_cloud.jpg" })
    {
        auto usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        auto devMemProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        auto texture = m_device->CreateTexture2DFromFile(textureFile, usage, devMemProps);

        assert(texture.GetImage() != VK_NULL_HANDLE);
        m_textures.push_back(texture);
    }
    m_defaultSampler = m_device->CreateSampler(
        VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_REPEAT);
}

void MaterialScene::CreateSceneBLAS()
{
    // Plane BLAS �̐���.
    {
        VkAccelerationStructureGeometryKHR asGeometry{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR
        };
        asGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        asGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        // VkAccelerationStructureGeometryTrianglesDataKHR
        auto& triangles = asGeometry.geometry.triangles;
        triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        triangles.vertexData.deviceAddress = m_meshPlane.vertexBuffer.GetDeviceAddress();
        triangles.maxVertex = m_meshPlane.vertexCount;
        triangles.vertexStride = m_meshPlane.vertexStride;
        triangles.indexType = VK_INDEX_TYPE_UINT32;
        triangles.indexData.deviceAddress = m_meshPlane.indexBuffer.GetDeviceAddress();

        VkAccelerationStructureBuildRangeInfoKHR asBuildRangeInfo{};
        asBuildRangeInfo.primitiveCount = m_meshPlane.indexCount / 3;
        asBuildRangeInfo.primitiveOffset = 0;
        asBuildRangeInfo.firstVertex = 0;
        asBuildRangeInfo.transformOffset = 0;

        AccelerationStructure::Input blasInput;
        blasInput.asGeometry = { asGeometry };
        blasInput.asBuildRangeInfo = { asBuildRangeInfo };

        m_meshPlane.blas.BuildAS(m_device,
            VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, blasInput, 0);
        m_meshPlane.blas.DestroyScratchBuffer(m_device);
    }
    // Sphere BLAS �̐���.
    {
        VkAccelerationStructureGeometryKHR asGeometry{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR
        };
        asGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
        asGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        // VkAccelerationStructureGeometryTrianglesDataKHR
        auto& triangles = asGeometry.geometry.triangles;
        triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        triangles.vertexData.deviceAddress = m_meshSphere.vertexBuffer.GetDeviceAddress();
        triangles.maxVertex = m_meshSphere.vertexCount;
        triangles.vertexStride = m_meshSphere.vertexStride;
        triangles.indexType = VK_INDEX_TYPE_UINT32;
        triangles.indexData.deviceAddress = m_meshSphere.indexBuffer.GetDeviceAddress();

        VkAccelerationStructureBuildRangeInfoKHR asBuildRangeInfo{};
        asBuildRangeInfo.primitiveCount = m_meshSphere.indexCount / 3;
        asBuildRangeInfo.primitiveOffset = 0;
        asBuildRangeInfo.firstVertex = 0;
        asBuildRangeInfo.transformOffset = 0;

        AccelerationStructure::Input blasInput;
        blasInput.asGeometry = { asGeometry };
        blasInput.asBuildRangeInfo = { asBuildRangeInfo };

        m_meshSphere.blas.BuildAS(m_device,
            VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, blasInput, 0);
        m_meshSphere.blas.DestroyScratchBuffer(m_device);
    }
}

void MaterialScene::CreateSceneTLAS()
{
    auto asInstances = CreateAccelerationStructureIncenceFromSceneObjects();

    auto instancesBufferSize = sizeof(VkAccelerationStructureInstanceKHR) * asInstances.size();
    auto usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    auto hostMemProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    m_instancesBuffer = m_device->CreateBuffer(
        instancesBufferSize, usage, hostMemProps);
    m_device->WriteToBuffer(m_instancesBuffer, asInstances.data(), instancesBufferSize);

    VkDeviceOrHostAddressConstKHR instanceDataDeviceAddress{};
    instanceDataDeviceAddress.deviceAddress = m_instancesBuffer.GetDeviceAddress();

    VkAccelerationStructureGeometryKHR asGeometry{};
    asGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    asGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    asGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    asGeometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    asGeometry.geometry.instances.arrayOfPointers = VK_FALSE;
    asGeometry.geometry.instances.data = instanceDataDeviceAddress;

    VkAccelerationStructureBuildRangeInfoKHR asBuildRangeInfo{};
    asBuildRangeInfo.primitiveCount = uint32_t(asInstances.size());
    asBuildRangeInfo.primitiveOffset = 0;
    asBuildRangeInfo.firstVertex = 0;
    asBuildRangeInfo.transformOffset = 0;

    AccelerationStructure::Input tlasInput{};
    tlasInput.asGeometry = { asGeometry };
    tlasInput.asBuildRangeInfo = { asBuildRangeInfo };
    m_topLevelAS.BuildAS(m_device, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, tlasInput, 0);

    m_topLevelAS.DestroyScratchBuffer(m_device);
}

void MaterialScene::CreateRaytracedBuffer()
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

void MaterialScene::CreateRaytracePipeline()
{
    // ���C�g���[�V���O�̃V�F�[�_�[��ǂݍ���.
    std::wstring shaderFiles[] = {
        L"shaders/raygen.rgen.spv",
        L"shaders/miss.rmiss.spv",
        L"shaders/chitPlane.rchit.spv",
        L"shaders/chitSphere.rchit.spv"
    };

    bool useNoRecursiveRaytrace = false;
    const std::string deviceName = m_device->GetDeviceName();
    useNoRecursiveRaytrace = deviceName.find("Radeon") != deviceName.npos;
    
    if (useNoRecursiveRaytrace) {
        // �ċA������p���Ȃ��o�[�W����.
        shaderFiles[0] = L"shaders/no-recursion_raygen.rgen.spv";
        shaderFiles[1] = L"shaders/no-recursion_miss.rmiss.spv";
        shaderFiles[2] = L"shaders/no-recursion_chitPlane.rchit.spv";
        shaderFiles[3] = L"shaders/no-recursion_chitSphere.rchit.spv";
    }

    std::vector<VkPipelineShaderStageCreateInfo> stages = {
        util::LoadShader(m_device, shaderFiles[0], VK_SHADER_STAGE_RAYGEN_BIT_KHR),
        util::LoadShader(m_device, shaderFiles[1], VK_SHADER_STAGE_MISS_BIT_KHR),
        util::LoadShader(m_device, shaderFiles[2], VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR),
        util::LoadShader(m_device, shaderFiles[3], VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
    };

    // stages �z����ł̊e�V�F�[�_�[�̃C���f�b�N�X.
    const int indexRaygen = 0;
    const int indexMiss = 1;
    const int indexChitPlane = 2;
    const int indexChitSphere = 3;

    // �V�F�[�_�[�O���[�v�̐���.
    m_shaderGroups.resize(MaxShaderGroup);
    m_shaderGroups[GroupRayGenShader] = util::CreateShaderGroupRayGeneration(indexRaygen);
    m_shaderGroups[GroupMissShader] = util::CreateShaderGroupMiss(indexMiss);
    m_shaderGroups[GroupPlaneHit] = util::CreateShaderGroupHit(indexChitPlane);
    m_shaderGroups[GroupSphereHit] = util::CreateShaderGroupHit(indexChitSphere);

    // ���C�g���[�V���O�p�C�v���C���̐���.
    VkRayTracingPipelineCreateInfoKHR rtPipelineCI{};

    rtPipelineCI.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    rtPipelineCI.stageCount = uint32_t(stages.size());
    rtPipelineCI.pStages = stages.data();
    rtPipelineCI.groupCount = uint32_t(m_shaderGroups.size());
    rtPipelineCI.pGroups = m_shaderGroups.data();
    rtPipelineCI.maxPipelineRayRecursionDepth = useNoRecursiveRaytrace ? 1 : 5;
    rtPipelineCI.layout = m_pipelineLayout;

    vkCreateRayTracingPipelinesKHR(
        m_device->GetDevice(), VK_NULL_HANDLE, VK_NULL_HANDLE,
        1, &rtPipelineCI, nullptr, &m_raytracePipeline);

    // ���I�����̂ŃV�F�[�_�[���W���[���͉�����Ă��܂�.
    for (auto& v : stages) {
        vkDestroyShaderModule(
            m_device->GetDevice(), v.module, nullptr);
    }
    m_useNoRecursiveRT = useNoRecursiveRaytrace;
}

void MaterialScene::CreateShaderBindingTable()
{
    auto memProps = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    auto usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    const auto rtPipelineProps = m_device->GetRayTracingPipelineProperties();

    // �e�G���g���̃T�C�Y�����߂�.
    //  �e�G���g���T�C�Y shaderGroupHandleAlignment �ɐ؂�グ�Ă���.
    const auto handleSize = rtPipelineProps.shaderGroupHandleSize;
    const auto handleAlignment = rtPipelineProps.shaderGroupHandleAlignment;
    auto raygenShaderEntrySize = Align(handleSize, handleAlignment);
    auto missShaderEntrySize = Align(handleSize, handleAlignment);
    auto hitShaderEntrySize = Align(handleSize, handleAlignment);

    // �e�V�F�[�_�[�̌�.
    const auto raygenShaderCount = 1;
    const auto missShaderCount = 1;
    const auto hitShaderCount = 2;  // �� / Sphere �̌v2��.

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
    m_sbtInfo.rgen.deviceAddress = deviceAddress;
    // Raygen �� size=stride���K�v.
    m_sbtInfo.rgen.stride = raygenShaderEntrySize;
    m_sbtInfo.rgen.size = m_sbtInfo.rgen.stride;

    // Miss�V�F�[�_�[�̃G���g������������.
    auto miss = shaderHandleStorage.data() + handleSizeAligned * GroupMissShader;
    memcpy(dst, miss, handleSize);
    dst += regionMiss;
    m_sbtInfo.miss.deviceAddress = deviceAddress + regionRaygen;
    m_sbtInfo.miss.size = regionMiss;
    m_sbtInfo.miss.stride = missShaderEntrySize;

    // �q�b�g�V�F�[�_�[�̃G���g������������.
    auto hit = shaderHandleStorage.data() + handleSizeAligned * GroupPlaneHit;
    auto entryStart = dst;
    {
        // Plane 
        memcpy(entryStart, hit, handleSize);
    }
    hit = shaderHandleStorage.data() + handleSizeAligned * GroupSphereHit;
    entryStart = entryStart + hitShaderEntrySize;
    {
        // Sphere
        memcpy(entryStart, hit, handleSize);
    }

    m_sbtInfo.hit.deviceAddress = deviceAddress + regionRaygen + regionMiss;
    m_sbtInfo.hit.size = regionHit;
    m_sbtInfo.hit.stride = hitShaderEntrySize;

    m_device->Unmap(m_shaderBindingTable);

}

void MaterialScene::CreateLayouts()
{
    VkDescriptorSetLayoutBinding layoutAS{};
    layoutAS.binding = 0;
    layoutAS.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    layoutAS.descriptorCount = 1;
    layoutAS.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;

    VkDescriptorSetLayoutBinding layoutRtImage{};
    layoutRtImage.binding = 1;
    layoutRtImage.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    layoutRtImage.descriptorCount = 1;
    layoutRtImage.stageFlags = VK_SHADER_STAGE_RAYGEN_BIT_KHR;

    VkDescriptorSetLayoutBinding layoutSceneUBO{};
    layoutSceneUBO.binding = 2;
    layoutSceneUBO.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    layoutSceneUBO.descriptorCount = 1;
    layoutSceneUBO.stageFlags = VK_SHADER_STAGE_ALL;

    VkDescriptorSetLayoutBinding layoutBackgroundCube{};
    layoutBackgroundCube.binding = 3;
    layoutBackgroundCube.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutBackgroundCube.descriptorCount = 1;
    layoutBackgroundCube.stageFlags = VK_SHADER_STAGE_ALL;


    VkDescriptorSetLayoutBinding layoutObjectParamSBO{};
    layoutObjectParamSBO.binding = 4;
    layoutObjectParamSBO.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    layoutObjectParamSBO.descriptorCount = 1;
    layoutObjectParamSBO.stageFlags = VK_SHADER_STAGE_ALL;

    VkDescriptorSetLayoutBinding layoutMaterialSBO{};
    layoutMaterialSBO.binding = 5;
    layoutMaterialSBO.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    layoutMaterialSBO.descriptorCount = 1;
    layoutMaterialSBO.stageFlags = VK_SHADER_STAGE_ALL;

    VkDescriptorSetLayoutBinding layoutTextures{};
    layoutTextures.binding = 6;
    layoutTextures.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutTextures.descriptorCount = uint32_t(m_textures.size());
    layoutTextures.stageFlags = VK_SHADER_STAGE_ALL;

    std::vector<VkDescriptorSetLayoutBinding> bindings({
        layoutAS, layoutRtImage, layoutSceneUBO, layoutBackgroundCube,
        layoutObjectParamSBO, layoutMaterialSBO, layoutTextures
    });

    VkDescriptorSetLayoutCreateInfo dsLayoutCI{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
    };
    dsLayoutCI.bindingCount = static_cast<uint32_t>(bindings.size());
    dsLayoutCI.pBindings = bindings.data();
    vkCreateDescriptorSetLayout(
        m_device->GetDevice(), &dsLayoutCI, nullptr, &m_dsLayout);

    // Pipeline Layout

    VkPipelineLayoutCreateInfo pipelineLayoutCI{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO
    };
    std::vector<VkDescriptorSetLayout> layouts = {
        m_dsLayout
    };
    pipelineLayoutCI.setLayoutCount = uint32_t(layouts.size());
    pipelineLayoutCI.pSetLayouts = layouts.data();
    vkCreatePipelineLayout(m_device->GetDevice(),
        &pipelineLayoutCI, nullptr, &m_pipelineLayout);
}

void MaterialScene::CreateDescriptorSets()
{
    m_descriptorSet = m_device->AllocateDescriptorSet(m_dsLayout);

    std::vector<VkAccelerationStructureKHR> asHandles = {
        m_topLevelAS.GetHandle()
    };

    VkWriteDescriptorSetAccelerationStructureKHR asDescriptor{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR
    };
    asDescriptor.accelerationStructureCount = uint32_t(asHandles.size());
    asDescriptor.pAccelerationStructures = asHandles.data();

    VkWriteDescriptorSet asWrite{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
    };
    asWrite.pNext = &asDescriptor;
    asWrite.dstSet = m_descriptorSet;
    asWrite.dstBinding = 0;
    asWrite.descriptorCount = 1;
    asWrite.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

    VkWriteDescriptorSet imageWrite{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
    };
    imageWrite.dstSet = m_descriptorSet;
    imageWrite.dstBinding = 1;
    imageWrite.descriptorCount = 1;
    imageWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    imageWrite.pImageInfo = m_raytracedImage.GetDescriptor();

    VkDescriptorBufferInfo sceneUboDescriptor{};
    sceneUboDescriptor.buffer = m_sceneUBO.GetBuffer();
    sceneUboDescriptor.range = m_sceneUBO.GetBlockSize();

    VkWriteDescriptorSet sceneUboWrite{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
    };
    sceneUboWrite.dstSet = m_descriptorSet;
    sceneUboWrite.dstBinding = 2;
    sceneUboWrite.descriptorCount = 1;
    sceneUboWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    sceneUboWrite.pBufferInfo = &sceneUboDescriptor;

    VkWriteDescriptorSet bgCubeWrite{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
    };
    bgCubeWrite.dstSet = m_descriptorSet;
    bgCubeWrite.dstBinding = 3;
    bgCubeWrite.descriptorCount = 1;
    bgCubeWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bgCubeWrite.pImageInfo = m_cubemap.GetDescriptor(m_cubemapSampler);

    VkDescriptorBufferInfo objectInfoDescriptor{};
    objectInfoDescriptor.buffer = m_objectsSBO.GetBuffer();
    objectInfoDescriptor.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet objectInfoWrite{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
    };
    objectInfoWrite.dstSet = m_descriptorSet;
    objectInfoWrite.dstBinding = 4;
    objectInfoWrite.descriptorCount = 1;
    objectInfoWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    objectInfoWrite.pBufferInfo = &objectInfoDescriptor;

    VkDescriptorBufferInfo materialInfoDescriptor{};
    materialInfoDescriptor.buffer = m_materialsSBO.GetBuffer();
    materialInfoDescriptor.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet materialInfoWrite{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
    };
    materialInfoWrite.dstSet = m_descriptorSet;
    materialInfoWrite.dstBinding = 5;
    materialInfoWrite.descriptorCount = 1;
    materialInfoWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    materialInfoWrite.pBufferInfo = &materialInfoDescriptor;

    std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
        asWrite, imageWrite, sceneUboWrite, bgCubeWrite,
        objectInfoWrite, materialInfoWrite,
    };
    vkUpdateDescriptorSets(
        m_device->GetDevice(),
        uint32_t(writeDescriptorSets.size()),
        writeDescriptorSets.data(),
        0,
        nullptr);

    // �V�[���Ŏg�p���Ă���e�N�X�`�����f�B�X�N���v�^�ɏ�������.
    std::vector<VkDescriptorImageInfo> texturesDescriptors(m_textures.size());
    for (auto i = 0; i < m_textures.size(); ++i) {
        texturesDescriptors[i] = *(m_textures[i].GetDescriptor(m_defaultSampler));
    }
    VkWriteDescriptorSet texturesImageWrite{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
    };
    texturesImageWrite.dstSet = m_descriptorSet;
    texturesImageWrite.dstBinding = 6;
    texturesImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texturesImageWrite.descriptorCount = uint32_t(m_textures.size());
    texturesImageWrite.pImageInfo = texturesDescriptors.data();

    vkUpdateDescriptorSets(
        m_device->GetDevice(),
        1, &texturesImageWrite, 0, nullptr);
}

void MaterialScene::InitializeImGui()
{
    // ��Ƀ����_�[�p�X����������.
    {
        // �`�挋�ʂ̏�ɏd�˂ď������Ƃɒ���.
        //  �J�n���ɃN���A���Ȃ�����.
        //  �������: �]����, �ŏI���: PresentSrc.
        VkAttachmentDescription colorTarget{};
        colorTarget.format = m_device->GetBackBufferFormat().format;
        colorTarget.samples = VK_SAMPLE_COUNT_1_BIT;
        colorTarget.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorTarget.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorTarget.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorTarget.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorTarget.initialLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        colorTarget.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference colorRef{
          0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        };
        VkSubpassDescription subpassDesc{
          0, // Flags
          VK_PIPELINE_BIND_POINT_GRAPHICS,
          0, nullptr, // InputAttachments
          1, &colorRef, // ColorAttachments
          nullptr,    // ResolveAttachments
          nullptr,    // DepthStencilAttachments
          0, nullptr, // PreserveAttachments
        };
        std::vector<VkAttachmentDescription> attachments = { colorTarget };
        VkRenderPassCreateInfo rpCI{
            VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            nullptr, 0,
            uint32_t(attachments.size()), attachments.data(),
            1,&subpassDesc,
            0, nullptr, // Dependency
        };
        vkCreateRenderPass(m_device->GetDevice(), &rpCI, nullptr, &m_renderPass);
    }

    auto renderArea = m_device->GetRenderArea();
    for (uint32_t i = 0; i < m_device->GetBackBufferCount(); ++i) {
        auto rt = m_device->GetRenderTarget(i);
        VkImageView views[] = { rt.GetImageView() };
        VkFramebufferCreateInfo fbCI{
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            nullptr, 0,
            m_renderPass,
            _countof(views), views,
            renderArea.extent.width, renderArea.extent.height, 1,
        };
        VkFramebuffer fb;
        vkCreateFramebuffer(
            m_device->GetDevice(), &fbCI, nullptr, &fb);
        m_framebuffers.push_back(fb);
    }

    // ImGui �̃R���e�L�X�g�𐶐�.
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // ImGui ���v������p�����[�^���Z�b�g���ď�����.
    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = m_device->GetVulkanInstance();
    initInfo.PhysicalDevice = m_device->GetPhysicalDevice();
    initInfo.Device = m_device->GetDevice();
    initInfo.QueueFamily = m_device->GetGraphicsQueueFamily();
    initInfo.Queue = m_device->GetDefaultQueue();
    initInfo.DescriptorPool = m_device->GetDescriptorPool();
    initInfo.Subpass = 0;
    initInfo.MinImageCount = m_device->GetBackBufferCount();
    initInfo.ImageCount = m_device->GetBackBufferCount();
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&initInfo, m_renderPass);
    ImGui_ImplGlfw_InitForVulkan(m_window, true);

    // �t�H���g�e�N�X�`���̏���.
    auto command = m_device->CreateCommandBuffer();
    ImGui_ImplVulkan_CreateFontsTexture(command);
    vkEndCommandBuffer(command);
    m_device->SubmitAndWait(command);
    m_device->DestroyCommandBuffer(command);
}

void MaterialScene::DestroyImGui()
{
    ImGui_ImplGlfw_Shutdown();
    ImGui_ImplVulkan_Shutdown();
    ImGui::DestroyContext();

    auto device = m_device->GetDevice();
    vkDestroyRenderPass(device, m_renderPass, nullptr);
    for (auto fb : m_framebuffers) {
        vkDestroyFramebuffer(device, fb, nullptr);
    }
}

void MaterialScene::UpdateHUD()
{
    // ImGui
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Information");
    ImGui::Text("%s", m_device->GetDeviceName());
    auto framerate = ImGui::GetIO().Framerate;
    ImGui::Text("frametime %.3f ms", 1000.0f / framerate);
    std::string mode = "Mode: Recursive Raytrace";
    if (m_useNoRecursiveRT) {
        mode = "Mode: No Recursive Raytrace";
    }
    ImGui::Text(mode.c_str());
    auto camPos = m_camera.GetPosition();
    ImGui::Text("CameraPos: (%.2f,%.2f,%.2f)", camPos.x, camPos.y, camPos.z);
    ImGui::End();
}

void MaterialScene::DeployObjects()
{
    // ����z�u����.
    m_floor.transform = glm::mat4(1.0f);
    m_floor.meshRef = &m_meshPlane;
    m_floor.sbtOffset = AppHitShaderGroups::PlaneHitShader;
    m_floor.material.textureIndex = TexID_Floor; // �e�N�X�`�����g�p����.

    // Sphere��z�u.
    std::mt19937 mt;
    std::uniform_int_distribution rnd(-9, 9);

    std::vector<glm::vec3> spherePositionList;
    for (int i = 0; i < SphereCount; ++i) {
        glm::vec3 pos;
        pos.x = (i % 6) * 2.0f - 4.0f;
        pos.z = (i / 6) * 2.0f - 4.0f;
        pos.y = 0.5f;
        spherePositionList.push_back(pos);
    }
    std::vector<glm::vec4> colorTable = {
        glm::vec4(1.0f, 1.0f, 1.0f, 0.0f),
        glm::vec4(0.5f, 0.8f, 0.4f, 0.0f),
        glm::vec4(0.7f, 0.6f, 0.2f, 0.0f),
        glm::vec4(0.2f, 0.3f, 0.6f, 0.0f),
        glm::vec4(0.1f, 0.8f, 0.9f, 0.0f),
    };

    std::mt19937 mt2;
    std::uniform_int_distribution mt_rnd(0, int(MATERIAL_KIND_MAX-1));
    std::uniform_real_distribution tex_rnd(0.0f, 1.0f);

    int index = 0;
    const int tableCount = int(colorTable.size());
    for(auto pos : spherePositionList) {
        SceneObject objSphere;

        objSphere.transform = glm::translate(pos);
        objSphere.meshRef = &m_meshSphere;
        objSphere.sbtOffset = AppHitShaderGroups::SphereHitShader;

        objSphere.material.materialKind = mt_rnd(mt);
        objSphere.material.diffuse = colorTable[index % tableCount];

        // ���܂Ƀe�N�X�`���t���ɂ���.
        if (objSphere.material.materialKind == LAMBERT && tex_rnd(mt2) > 0.5f) {
            // �e�N�X�`�����Q��.
            objSphere.material.textureIndex = TexID_Sphere;
            objSphere.material.diffuse = glm::vec4(1.0f);
        }

        m_spheres.emplace_back(std::move(objSphere));
        index++;
    }
}

void MaterialScene::CreateSceneList()
{
    m_sceneObjects.clear();
    // ��.
    m_sceneObjects.push_back(m_floor);
    
    // ����.
    m_sceneObjects.insert(m_sceneObjects.end(), m_spheres.begin(), m_spheres.end());
}

void MaterialScene::CreateSceneBuffers()
{
    std::vector<ObjectParam> objParameters{};
    std::vector<Material> materialParams{};
    // �V�[���ŕ`�悷��I�u�W�F�N�g�Q����, �I�u�W�F�N�g�̏��ƃ}�e���A�����̏W���𐶐�.
    for (const auto& obj : m_sceneObjects) {
        ObjectParam objParam{};

        auto mesh = obj.meshRef;
        objParam.addressIndexBuffer = mesh->indexBuffer.GetDeviceAddress();
        objParam.addressVertexBuffer = mesh->vertexBuffer.GetDeviceAddress();
        objParam.materialIndex = uint32_t(materialParams.size());
        objParameters.push_back(objParam);
        materialParams.push_back(obj.material);
    }

    // �X�g���[�W�o�b�t�@�Ɏ��W���������z��̓��e����������.
    auto devMemProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    auto usage = \
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | \
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    auto materialBufSize = sizeof(Material) * materialParams.size();
    m_materialsSBO = m_device->CreateBuffer(
        materialBufSize, usage, devMemProps
    );
    m_device->WriteToBuffer(m_materialsSBO, materialParams.data(), materialBufSize);

    auto objectBufSize = sizeof(ObjectParam) * objParameters.size();
    m_objectsSBO = m_device->CreateBuffer(
        objectBufSize, usage, devMemProps);
    m_device->WriteToBuffer(m_objectsSBO, objParameters.data(), objectBufSize);

}

std::vector<VkAccelerationStructureInstanceKHR> MaterialScene::CreateAccelerationStructureIncenceFromSceneObjects()
{
    std::vector<VkAccelerationStructureInstanceKHR> asInstances;

    VkAccelerationStructureInstanceKHR templateDesc{};
    templateDesc.instanceCustomIndex = 0;
    templateDesc.mask = 0xFF;
    templateDesc.flags = 0;

    for (auto& obj : m_sceneObjects) {
        auto asInstance = templateDesc;
        asInstance.transform = util::ConvertTransform(obj.transform);
        asInstance.instanceCustomIndex = obj.customIndex;
        asInstance.instanceShaderBindingTableRecordOffset = obj.sbtOffset;
        asInstance.accelerationStructureReference = obj.meshRef->blas.GetDeviceAddress();
        asInstances.push_back(asInstance);
    }
    return asInstances;
}
