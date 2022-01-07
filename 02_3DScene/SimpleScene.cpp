#include "SimpleScene.h"
#include <glm/gtx/transform.hpp>

// For ImGui
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

void SimpleScene::OnInit()
{
    // シーンに配置するジオメトリを準備します.
    CreateSceneGeometries();

    // ジオメトリのBLASを準備します.
    CreateSceneBLAS();

    // レイトレーシングするためTLASを準備します.
    CreateSceneTLAS();

    // レイトレーシング用の結果バッファを準備する.
    CreateRaytracedBuffer();

    // これから必要になる各種レイアウトの準備.
    CreateLayouts();

    // シーンパラメータ用UniformBufferを生成.
    m_sceneUBO.Initialize(m_device, sizeof(SceneParam),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    // レイトレーシングパイプラインを構築する.
    CreateRaytracePipeline();

    // シェーダーバインディングテーブルを構築する.
    CreateShaderBindingTable();

    // ディスクリプタの準備・書き込み.
    CreateDescriptorSets();

    // ImGui の初期化.
    InitializeImGui();

    // 初期パラメータ設定.
    auto eye = glm::vec3(0.0f, 4.0f, 15.0f);
    auto target = glm::vec3(0.0f, 0.0f, 0.0f);
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

void SimpleScene::OnDestroy()
{
    m_sceneUBO.Destroy(m_device);
    m_device->DestroyBuffer(m_instancesBuffer);
    m_topLevelAS.Destroy(m_device);

    m_device->DestroyBuffer(m_meshCube.vertexBuffer);
    m_device->DestroyBuffer(m_meshCube.indexBuffer);
    m_meshCube.blas.Destroy(m_device);

    m_device->DestroyBuffer(m_meshPlane.vertexBuffer);
    m_device->DestroyBuffer(m_meshPlane.indexBuffer);
    m_meshPlane.blas.Destroy(m_device);

    m_device->DestroyImage(m_raytracedImage);
    m_device->DestroyBuffer(m_shaderBindingTable);

    DestroyImGui();

    auto device = m_device->GetDevice();
    vkDestroyPipeline(device, m_raytracePipeline, nullptr);
    vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, m_dsLayout, nullptr);
    vkFreeDescriptorSets(device, m_device->GetDescriptorPool(), 1, &m_descriptorSet);
}

void SimpleScene::OnUpdate()
{
    UpdateHUD();
    m_sceneParam.mtxView = m_camera.GetViewMatrix();
    m_sceneParam.mtxProj = m_camera.GetProjectionMatrix();
    m_sceneParam.mtxViewInv = glm::inverse(m_sceneParam.mtxView);
    m_sceneParam.mtxProjInv = glm::inverse(m_sceneParam.mtxProj);
}

void SimpleScene::OnRender()
{
    m_device->WaitAvailableFrame();
    auto frameIndex = m_device->GetCurrentFrameIndex();
    auto command = m_device->GetCurrentFrameCommandBuffer();

    void* p = m_sceneUBO.Map(frameIndex);
    if (p) {
        memcpy(p, &m_sceneParam, sizeof(m_sceneParam));
    }

    VkCommandBufferBeginInfo commandBI{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr, 0, nullptr
    };
    vkBeginCommandBuffer(command, &commandBI);

    // レイトレーシングを行う.
    uint32_t offsets[] = {
        uint32_t(m_sceneUBO.GetBlockSize() * frameIndex)
    };

    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_raytracePipeline);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, m_pipelineLayout, 0, 
        1, &m_descriptorSet, 
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

    // レイトレーシング結果画像をバックバッファへコピー.
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

    // 次回の書き込みに備えて状態遷移.
    m_raytracedImage.BarrierToGeneral(command);  

    // ImGui によるラスタライズ描画パスを実行.
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
    
    // ImGui の描画はここで行う.
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);

    // レンダーパスが終了するとバックバッファは
    // TRANSFER_DST_OPTIMAL->PRESENT_SRC_KHR へレイアウト変更が適用される.
    vkCmdEndRenderPass(command);

    vkEndCommandBuffer(command);

    // コマンドを実行して画面表示.
    m_device->SubmitCurrentFrameCommandBuffer();
    m_device->Present();
}

void SimpleScene::OnMouseDown(MouseButton button)
{
    if (ImGui::GetIO().WantCaptureMouse) {
        return;
    }
    m_camera.OnMouseButtonDown(int(button));
}

void SimpleScene::OnMouseUp(MouseButton button)
{
    if (ImGui::GetIO().WantCaptureMouse) {
        return;
    }
    m_camera.OnMouseButtonUp();
}

void SimpleScene::OnMouseMove()
{
    if (ImGui::GetIO().WantCaptureMouse) {
        return;
    }
    auto mouseDelta = ImGui::GetIO().MouseDelta;
    float dx = float(mouseDelta.x) / GetWidth();
    float dy = float(mouseDelta.y) / GetHeight();
    m_camera.OnMouseMove(-dx, dy);
}

void SimpleScene::CreateSceneGeometries()
{
    auto hostMemProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    auto usageForRT= \
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | \
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
    std::vector<util::primitive::VertexPNC> vertices;
    std::vector<uint32_t> indices;
    const auto vstride = uint32_t(sizeof(util::primitive::VertexPNC));
    const auto istride = uint32_t(sizeof(uint32_t));

    // 床平面の準備.
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
    // Cubeの準備.
    {
        util::primitive::GetColoredCube(vertices, indices);
        auto vbCubeSize = vstride * vertices.size();
        auto ibCubeSize = istride * indices.size();

        VkBufferUsageFlags usage;
        usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        usage |= usageForRT;
        m_meshCube.vertexBuffer = m_device->CreateBuffer(vbCubeSize, usage, hostMemProps);
        m_device->WriteToBuffer(
            m_meshCube.vertexBuffer, vertices.data(), vbCubeSize);
        m_meshCube.vertexCount = uint32_t(vertices.size());
        m_meshCube.vertexStride = vstride;

        usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        usage |= usageForRT;
        m_meshCube.indexBuffer = m_device->CreateBuffer(ibCubeSize, usage, hostMemProps);
        m_device->WriteToBuffer(
            m_meshCube.indexBuffer, indices.data(), ibCubeSize);
        m_meshCube.indexCount = uint32_t(indices.size());
    }
}

void SimpleScene::CreateSceneBLAS()
{
    // Plane BLAS の生成.
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

        // 使用するヒットシェーダー(のインデックス)を設定しておく.
        m_meshPlane.hitShaderIndex = AppHitShaderGroups::PlaneHitShader;
    }
    // Cube BLAS の生成.
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
        triangles.vertexData.deviceAddress = m_meshCube.vertexBuffer.GetDeviceAddress();
        triangles.maxVertex = m_meshCube.vertexCount;
        triangles.vertexStride = m_meshCube.vertexStride;
        triangles.indexType = VK_INDEX_TYPE_UINT32;
        triangles.indexData.deviceAddress = m_meshCube.indexBuffer.GetDeviceAddress();

        VkAccelerationStructureBuildRangeInfoKHR asBuildRangeInfo{};
        asBuildRangeInfo.primitiveCount = m_meshCube.indexCount / 3;
        asBuildRangeInfo.primitiveOffset = 0;
        asBuildRangeInfo.firstVertex = 0;
        asBuildRangeInfo.transformOffset = 0;

        AccelerationStructure::Input blasInput;
        blasInput.asGeometry = { asGeometry };
        blasInput.asBuildRangeInfo = { asBuildRangeInfo };

        m_meshCube.blas.BuildAS(m_device,
            VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, blasInput, 0);
        m_meshCube.blas.DestroyScratchBuffer(m_device);

        // 使用するヒットシェーダー(のインデックス)を設定しておく.
        m_meshCube.hitShaderIndex = AppHitShaderGroups::CubeHitShader;
    }
}

void SimpleScene::CreateSceneTLAS()
{
    std::vector<VkAccelerationStructureInstanceKHR> asInstances;
    DeployObjects(asInstances);

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

void SimpleScene::CreateRaytracedBuffer()
{
    // バックバッファと同じフォーマットで作成する.
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

    // バッファの状態を変更しておく.
    auto command = m_device->CreateCommandBuffer();
    m_raytracedImage.BarrierToGeneral(command);
    vkEndCommandBuffer(command);

    m_device->SubmitAndWait(command);
    m_device->DestroyCommandBuffer(command);
}

void SimpleScene::CreateRaytracePipeline()
{
    // レイトレーシングのシェーダーを読み込む.
    auto rgsStage = util::LoadShader(m_device, L"shaders/raygen.rgen.spv", VK_SHADER_STAGE_RAYGEN_BIT_KHR);
    auto missStage = util::LoadShader(m_device, L"shaders/miss.rmiss.spv", VK_SHADER_STAGE_MISS_BIT_KHR);
    auto chitStage = util::LoadShader(m_device, L"shaders/closesthit.rchit.spv", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);

    std::vector<VkPipelineShaderStageCreateInfo> stages = {
        rgsStage, missStage, chitStage
    };

    // stages 配列内での各シェーダーのインデックス.
    const int indexRaygen = 0;
    const int indexMiss = 1;
    const int indexClosestHit = 2;

    // シェーダーグループの生成.
    m_shaderGroups.resize(MaxShaderGroup);
    m_shaderGroups[GroupRayGenShader] = util::CreateShaderGroupRayGeneration(indexRaygen);
    m_shaderGroups[GroupMissShader] = util::CreateShaderGroupMiss(indexMiss);
    m_shaderGroups[GroupHitShader] = util::CreateShaderGroupHit(indexClosestHit);

    // レイトレーシングパイプラインの生成.
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

    // 作り終えたのでシェーダーモジュールは解放してしまう.
    for (auto& v : stages) {
        vkDestroyShaderModule(
            m_device->GetDevice(), v.module, nullptr);
    }
}

void SimpleScene::CreateShaderBindingTable()
{
    auto memProps = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    auto usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    const auto rtPipelineProps = m_device->GetRayTracingPipelineProperties();

    // 各エントリのサイズを求める.
    //  各エントリサイズ shaderGroupHandleAlignment に切り上げておく.
    const auto handleSize = rtPipelineProps.shaderGroupHandleSize;
    const auto handleAlignment = rtPipelineProps.shaderGroupHandleAlignment;
    auto raygenShaderEntrySize = Align(handleSize, handleAlignment);
    auto missShaderEntrySize = Align(handleSize, handleAlignment);

    // HitShader にはジオメトリ用のバッファを設定するので計算.
    uint32_t hitShaderEntrySize = handleSize;
    hitShaderEntrySize += sizeof(uint64_t); // IndexBuffer アドレス.
    hitShaderEntrySize += sizeof(uint64_t); // VertexBuffer アドレス.
    hitShaderEntrySize = Align(hitShaderEntrySize, handleAlignment);

    // 各シェーダーの個数.
    const auto raygenShaderCount = 1;
    const auto missShaderCount = 1;
    const auto hitShaderCount = 2;  // 床 / Cube の計2つ.

    // 各グループで必要なサイズを求める.
    const auto baseAlign = rtPipelineProps.shaderGroupBaseAlignment;
    auto regionRaygen = Align(raygenShaderEntrySize * raygenShaderCount, baseAlign);
    auto regionMiss = Align(missShaderEntrySize * missShaderCount, baseAlign);
    auto regionHit = Align(hitShaderEntrySize * hitShaderCount, baseAlign);

    m_shaderBindingTable = m_device->CreateBuffer(
        regionRaygen + regionMiss + regionHit, usage, memProps);

    // パイプラインのShaderGroupハンドルを取得.
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

    // RayGenerationシェーダーのエントリを書き込む.
    auto raygen = shaderHandleStorage.data() + handleSizeAligned * GroupRayGenShader;
    memcpy(dst, raygen, handleSize);
    dst += regionRaygen;
    m_sbtInfo.rgen.deviceAddress = deviceAddress;
    // Raygen は size=strideが必要.
    m_sbtInfo.rgen.stride = raygenShaderEntrySize;
    m_sbtInfo.rgen.size = m_sbtInfo.rgen.stride;

    // Missシェーダーのエントリを書き込む.
    auto miss = shaderHandleStorage.data() + handleSizeAligned * GroupMissShader;
    memcpy(dst, miss, handleSize);
    dst += regionMiss;
    m_sbtInfo.miss.deviceAddress = deviceAddress + regionRaygen;
    m_sbtInfo.miss.size = regionMiss;
    m_sbtInfo.miss.stride = missShaderEntrySize;

    // ヒットシェーダーのエントリを書き込む.
    auto hit = shaderHandleStorage.data() + handleSizeAligned * GroupHitShader;
    auto entryStart = dst;
    {
        // Plane 
        memcpy(entryStart, hit, handleSize);
        WriteSBTDataForHitShader(entryStart + handleSize, m_meshPlane);
    }
    entryStart = entryStart + hitShaderEntrySize;
    {
        // Cube
        memcpy(entryStart, hit, handleSize);
        WriteSBTDataForHitShader(entryStart + handleSize, m_meshCube);
    }

    m_sbtInfo.hit.deviceAddress = deviceAddress + regionRaygen + regionMiss;
    m_sbtInfo.hit.size = regionHit;
    m_sbtInfo.hit.stride = hitShaderEntrySize;

    m_device->Unmap(m_shaderBindingTable);

}

void SimpleScene::CreateLayouts()
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

    VkDescriptorSetLayoutBinding layoutSceneUBO{};
    layoutSceneUBO.binding = 2;
    layoutSceneUBO.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    layoutSceneUBO.descriptorCount = 1;
    layoutSceneUBO.stageFlags = VK_SHADER_STAGE_ALL;

    std::vector<VkDescriptorSetLayoutBinding> bindings({
        layoutAS, layoutRtImage, layoutSceneUBO });

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
    pipelineLayoutCI.setLayoutCount = 1;
    pipelineLayoutCI.pSetLayouts = &m_dsLayout;
    vkCreatePipelineLayout(m_device->GetDevice(),
        &pipelineLayoutCI, nullptr, &m_pipelineLayout);
}

void SimpleScene::CreateDescriptorSets()
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

    std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
        asWrite, imageWrite, sceneUboWrite
    };
    vkUpdateDescriptorSets(
        m_device->GetDevice(),
        uint32_t(writeDescriptorSets.size()),
        writeDescriptorSets.data(),
        0,
        nullptr);
}

void SimpleScene::InitializeImGui()
{
    // 先にレンダーパスを準備する.
    {
        // レイトレの描画結果の上に重ねて書くことに注意.
        //  開始時にクリアしないこと.
        //  初期状態: 転送先, 最終状態: PresentSrc.
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
            1,& subpassDesc,
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

    // ImGui のコンテキストを生成.
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    // ImGui が要求するパラメータをセットして初期化.
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

    // フォントテクスチャの準備.
    auto command = m_device->CreateCommandBuffer();
    ImGui_ImplVulkan_CreateFontsTexture(command);
    vkEndCommandBuffer(command);
    m_device->SubmitAndWait(command);
    m_device->DestroyCommandBuffer(command);
}

void SimpleScene::DestroyImGui()
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

void SimpleScene::UpdateHUD()
{
    // ImGui
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Information");
    auto framerate = ImGui::GetIO().Framerate;
    ImGui::Text("%s", m_device->GetDeviceName());
    ImGui::Text("frametime %.3f ms", 1000.0f / framerate);
    ImGui::End();
}

void SimpleScene::DeployObjects(std::vector<VkAccelerationStructureInstanceKHR>& instances)
{
    VkAccelerationStructureInstanceKHR templateDesc{};
    templateDesc.instanceCustomIndex = 0;
    templateDesc.mask = 0xFF;
    templateDesc.flags = 0;

    // 床を配置.
    {
        VkTransformMatrixKHR mtxTransform = util::ConvertTransform(glm::mat3x4(1.0f));
        VkAccelerationStructureInstanceKHR asInstance = templateDesc;
        asInstance.transform = mtxTransform;
        asInstance.accelerationStructureReference = m_meshPlane.blas.GetDeviceAddress();
        asInstance.instanceShaderBindingTableRecordOffset = m_meshPlane.hitShaderIndex;
        instances.push_back(asInstance);
    }
    // Cubeを配置(1).
    {
        auto m = glm::translate(glm::vec3(-2.0f, 1.0f, 0.0f));
        VkTransformMatrixKHR mtxTransform = util::ConvertTransform(m);
        VkAccelerationStructureInstanceKHR asInstance = templateDesc;
        asInstance.transform = mtxTransform;
        asInstance.accelerationStructureReference = m_meshCube.blas.GetDeviceAddress();
        asInstance.instanceShaderBindingTableRecordOffset = m_meshCube.hitShaderIndex;
        instances.push_back(asInstance);
    }
    // Cubeを配置(2).
    {
        glm::mat4 m = glm::translate(glm::vec3(+2.0f, 1.0f, 0.0f));
        m = glm::rotate(m, glm::radians(45.f), glm::vec3(0, 1, 0));
        VkTransformMatrixKHR mtxTransform = util::ConvertTransform(m);
        VkAccelerationStructureInstanceKHR asInstance = templateDesc;
        asInstance.transform = mtxTransform;
        asInstance.accelerationStructureReference = m_meshCube.blas.GetDeviceAddress();
        asInstance.instanceShaderBindingTableRecordOffset = m_meshCube.hitShaderIndex;
        instances.push_back(asInstance);
    }

}

void SimpleScene::WriteSBTDataForHitShader(void* dst, const PolygonMesh& mesh)
{
    uint64_t deviceAddr = 0;
    auto p = static_cast<uint8_t*>(dst);
    // IndexBufferのデバイスアドレスを書き込む.
    deviceAddr = mesh.indexBuffer.GetDeviceAddress();
    memcpy(p, &deviceAddr, sizeof(deviceAddr));
    p += sizeof(deviceAddr);

    // VertexBufferのデバイスアドレスを書き込む.
    deviceAddr = mesh.vertexBuffer.GetDeviceAddress();
    memcpy(p, &deviceAddr, sizeof(deviceAddr));
    p += sizeof(deviceAddr);
}

