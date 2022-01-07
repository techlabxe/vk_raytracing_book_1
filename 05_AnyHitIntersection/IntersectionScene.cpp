#include "IntersectionScene.h"
#include <glm/gtx/transform.hpp>
#include <random>

// For ImGui
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#define MODE_RADEON_RAYTRACE
#ifndef MODE_RADEON_RAYTRACE
#define APP_RAYTRACE_RECURSIVE (2)
#else
#define APP_RAYTRACE_RECURSIVE (1)
#endif

void IntersectionScene::OnInit()
{
    m_materialManager.Create(m_device);

    // シーンに配置するジオメトリを準備します.
    CreateSceneGeometries();

    // ジオメトリのBLASを準備します.
    CreateSceneBLAS();

    // オブジェクトの配置・マテリアル設定など.
    DeployObjects();

    // シーンを構成する.
    CreateSceneList();

    // レイトレーシングするためTLASを準備します.
    CreateSceneTLAS();

    // レイトレーシング用の結果バッファを準備する.
    CreateRaytracedBuffer();

    // これから必要になる各種レイアウトの準備.
    CreateLayouts();

    // シーンで使用するオブジェクト・マテリアルのためのバッファを準備.
    CreateSceneBuffers();

    // 各UniformBufferを生成.
    m_sceneUBO.Initialize(m_device, sizeof(SceneParam),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    m_analyticUBO.Initialize(m_device, sizeof(AnalyticGeometryParam), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    m_sdfUBO.Initialize(m_device, sizeof(SDFGeometryParam), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

    // レイトレーシングパイプラインを構築する.
    CreateRaytracePipeline();

    // シェーダーバインディングテーブルを構築する.
    CreateShaderBindingTable();

    // ディスクリプタの準備・書き込み.
    CreateDescriptorSets();

    // ImGui の初期化.
    InitializeImGui();

    // 初期パラメータ設定.
    auto eye = glm::vec3(-3.3f, 1.8f, -0.4f);
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

void IntersectionScene::OnDestroy()
{
    m_sceneUBO.Destroy(m_device);
    m_analyticUBO.Destroy(m_device);
    m_sdfUBO.Destroy(m_device);
    m_device->DestroyBuffer(m_objectsSBO);
    m_device->DestroyBuffer(m_materialsSBO);
    m_instancesBuffer.Destroy(m_device);
    m_topLevelAS.Destroy(m_device);

    m_meshFence->Destroy(m_device);
    m_meshPlane->Destroy(m_device);
    m_meshAABB->Destroy(m_device);
    m_meshAABBSDF->Destroy(m_device);
    m_shaderGroupHelper.Destroy(m_device);

    m_device->DestroyImage(m_raytracedImage);
    m_device->DestroyBuffer(m_shaderBindingTable);

    m_device->DestroyImage(m_cubemap);
    m_device->DeallocateDescriptorSet(m_descriptorSet);

    m_materialManager.Destroy(m_device);
    DestroyImGui();

    auto device = m_device->GetDevice();
    vkDestroySampler(device, m_cubemapSampler, nullptr);
    vkDestroySampler(device, m_defaultSampler, nullptr);
    vkDestroyPipeline(device, m_raytracePipeline, nullptr);
    vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, m_dsLayout, nullptr);
}

void IntersectionScene::OnUpdate()
{
    UpdateHUD();
    m_sceneParam.mtxView = m_camera.GetViewMatrix();
    m_sceneParam.mtxProj = m_camera.GetProjectionMatrix();
    m_sceneParam.mtxViewInv = glm::inverse(m_sceneParam.mtxView);
    m_sceneParam.mtxProjInv = glm::inverse(m_sceneParam.mtxProj);
    m_analyticGeomParam.objectType = static_cast<AABBTypes>(m_guiParams.aabbObjectType);
    m_sdfGeomParam.objectType = static_cast<SDFTypes>(m_guiParams.sdfObjectType);
    m_sdfGeomParam.extent = m_guiParams.sdfBoxExtent;
    m_sdfGeomParam.radius = m_guiParams.sdfRadius;

    // 配置情報更新.
    DeployObjects();
}

void IntersectionScene::OnRender()
{
    m_device->WaitAvailableFrame();
    auto frameIndex = m_device->GetCurrentFrameIndex();
    auto command = m_device->GetCurrentFrameCommandBuffer();

    void* p = m_sceneUBO.Map(frameIndex);
    if (p) {
        memcpy(p, &m_sceneParam, sizeof(m_sceneParam));
    }
    p = m_analyticUBO.Map(frameIndex);
    if (p) {
        memcpy(p, &m_analyticGeomParam, sizeof(m_analyticGeomParam));
    }
    p = m_sdfUBO.Map(frameIndex);
    if (p) {
        memcpy(p, &m_sdfGeomParam, sizeof(m_sdfGeomParam));
    }

    VkCommandBufferBeginInfo commandBI{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        nullptr, 0, nullptr
    };
    vkBeginCommandBuffer(command, &commandBI);

    // TLAS を更新する.
    UpdateSceneTLAS();

    // レイトレーシングを行う.
    uint32_t offsets[] = {
        uint32_t(m_sceneUBO.GetBlockSize() * frameIndex),
        uint32_t(m_analyticUBO.GetBlockSize() * frameIndex),
        uint32_t(m_sdfUBO.GetBlockSize() * frameIndex),
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
        m_sbtHelper.GetRaygenRegion(),
        m_sbtHelper.GetMissRegion(),
        m_sbtHelper.GetHitRegion(),
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

void IntersectionScene::OnMouseDown(MouseButton button)
{
    if (ImGui::GetIO().WantCaptureMouse) {
        return;
    }
    m_camera.OnMouseButtonDown(int(button));
}

void IntersectionScene::OnMouseUp(MouseButton button)
{
    if (ImGui::GetIO().WantCaptureMouse) {
        return;
    }
    m_camera.OnMouseButtonUp();
}

void IntersectionScene::OnMouseMove()
{
    if (ImGui::GetIO().WantCaptureMouse) {
        return;
    }
    auto mouseDelta = ImGui::GetIO().MouseDelta;
    float dx = float(mouseDelta.x) / GetWidth();
    float dy = float(mouseDelta.y) / GetHeight();
    m_camera.OnMouseMove(-dx, dy);
}

void IntersectionScene::CreateSceneGeometries()
{
    std::vector<util::primitive::VertexPNT> vertices;
    std::vector<uint32_t> indices;
    const auto vstride = uint32_t(sizeof(util::primitive::VertexPNT));
    const auto istride = uint32_t(sizeof(uint32_t));

    // 先にマテリアル用のテクスチャを読み込んでおく.
    const auto floorTexFile = L"textures/trianglify-lowres.png";
    const auto fenceTexFile = L"textures/fence.png";
    for (const auto* textureFile : { floorTexFile, fenceTexFile }) {
        auto usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        auto devMemProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        auto texture = m_device->CreateTexture2DFromFile(textureFile, usage, devMemProps);

        assert(texture.GetImage() != VK_NULL_HANDLE);
        m_materialManager.AddTexture(textureFile, texture);
    }
    // マテリアルを準備する.
    auto matFloor = std::make_shared<Material>(L"Floor");
    matFloor->SetTexture(m_materialManager.GetTexture(floorTexFile));
    matFloor->SetDiffuse(glm::vec3(1.0f));
    matFloor->SetType(static_cast<int>(MaterialType::LAMBERT));
    auto matFence = std::make_shared<Material>(L"Fence");
    matFence->SetTexture(m_materialManager.GetTexture(fenceTexFile));
    matFence->SetDiffuse(glm::vec3(1.0f));
    matFence->SetType(static_cast<int>(MaterialType::LAMBERT));

    auto matAABBAnalytic = std::make_shared<Material>(L"AABB_Analytic");
    matAABBAnalytic->SetDiffuse(glm::vec3(0.8f, 0.3f, 0.1f));

    auto matAABBSDF = std::make_shared<Material>(L"AABB_SDF");
    matAABBSDF->SetDiffuse(glm::vec3(0.1f, 0.3f, 0.8f));

    // 床平面の準備.
    {
        m_meshPlane = std::make_shared<SimplePolygonMesh>();
        util::primitive::GetPlane(vertices, indices);

        SimplePolygonMesh::CreateInfo ci;
        ci.srcVertices = vertices.data();
        ci.srcIndices = indices.data();
        ci.vbSize = vstride * vertices.size();
        ci.ibSize = istride * indices.size();
        ci.indexCount = uint32_t(indices.size());
        ci.vertexCount = uint32_t(vertices.size());
        ci.stride = vstride;
        ci.material = matFloor;
        m_meshPlane->Create(m_device, ci, m_materialManager);
        m_meshPlane->SetHitShader(AppHitShaderGroups::GroupHitPlane);
    }

    // フェンスの準備.
    {
        m_meshFence = std::make_shared<SimplePolygonMesh>();
        util::primitive::GetPlaneXY(vertices, indices);
        std::transform(vertices.begin(), vertices.end(), vertices.begin(), [](auto v) { v.Position.x *= 4; return v; });

        SimplePolygonMesh::CreateInfo ci;
        ci.srcVertices = vertices.data();
        ci.srcIndices = indices.data();
        ci.vbSize = vstride * vertices.size();
        ci.ibSize = istride * indices.size();
        ci.indexCount = uint32_t(indices.size());
        ci.vertexCount = uint32_t(vertices.size());
        ci.stride = vstride;
        ci.material = matFence;
        m_meshFence->Create(m_device, ci, m_materialManager);
        m_meshFence->SetHitShader(AppHitShaderGroups::GroupHitFence);
    }
    // AABB Analytic用
    {
        m_meshAABB = std::make_shared<ProcedualMesh>();
        struct AABBData {
            glm::vec3 minimum;
            glm::vec3 maximum;
        };
        AABBData data{
            glm::vec3(-0.5f),
            glm::vec3(+0.5f)
        };

        ProcedualMesh::CreateInfo ci;
        ci.data = &data;
        ci.size = sizeof(AABBData);;
        ci.stride = sizeof(AABBData);
        ci.material = matAABBAnalytic;

        m_meshAABB->Create(m_device, ci, m_materialManager);
        m_meshAABB->SetHitShader(AppHitShaderGroups::GroupHitAnalytic);
    }

    // AABB SDF用
    {
        m_meshAABBSDF = std::make_shared<ProcedualMesh>();
        struct AABBData {
            glm::vec3 minimum;
            glm::vec3 maximum;
        };
        AABBData data{
            glm::vec3(-0.5f),
            glm::vec3(+0.5f)
        };

        ProcedualMesh::CreateInfo ci;
        ci.data = &data;
        ci.size = sizeof(AABBData);;
        ci.stride = sizeof(AABBData);
        ci.material = matAABBSDF;

        m_meshAABBSDF->Create(m_device, ci, m_materialManager);
        m_meshAABBSDF->SetHitShader(AppHitShaderGroups::GroupHitSdf);
    }

    m_defaultSampler = m_device->CreateSampler(
        VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_REPEAT);
}

void IntersectionScene::CreateSceneBLAS()
{
    VkBuildAccelerationStructureFlagsKHR buildFlags = 0;
    buildFlags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;

    // Plane BLAS の生成.
    m_meshPlane->BuildAS(m_device, buildFlags);
        
    // Fence BLAS の生成.
    m_meshFence->SetGometryFlags(0); // AnyHitシェーダーを起動するため VK_GEOMETRY_OPAQUE_BIT_KHR を設定しない.
    m_meshFence->BuildAS(m_device, buildFlags);

    // AABB BLAS の生成.
    m_meshAABB->BuildAS(m_device, buildFlags);

    // AABB-SDF BLAS の生成.
    m_meshAABBSDF->BuildAS(m_device, buildFlags);
}

void IntersectionScene::CreateSceneTLAS()
{
    auto asInstances = CreateAccelerationStructureIncenceFromSceneObjects();

    auto instancesBufferSize = sizeof(VkAccelerationStructureInstanceKHR) * asInstances.size();
    auto usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    auto hostMemProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    m_instancesBuffer.Initialize(m_device, instancesBufferSize, usage);
    memcpy(m_instancesBuffer.Map(0), asInstances.data(), instancesBufferSize);

    VkDeviceOrHostAddressConstKHR instanceDataDeviceAddress{};
    instanceDataDeviceAddress.deviceAddress = m_instancesBuffer.GetDeviceAddress(0);

    VkAccelerationStructureGeometryKHR asGeometry{};
    asGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    asGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    asGeometry.flags = 0; // AnyHitシェーダーを使うため.
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
    VkBuildAccelerationStructureFlagsKHR buildFlags = VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    m_topLevelAS.BuildAS(m_device, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, tlasInput, buildFlags);

    m_topLevelAS.DestroyScratchBuffer(m_device);
}

void IntersectionScene::CreateRaytracedBuffer()
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

void IntersectionScene::CreateRaytracePipeline()
{
    // レイトレーシングのシェーダーを読み込む.
    m_shaderGroupHelper.LoadShader(m_device, "rgs", L"shaders/raygen.rgen.spv");
    m_shaderGroupHelper.LoadShader(m_device, "miss", L"shaders/miss.rmiss.spv");
    m_shaderGroupHelper.LoadShader(m_device, "shadowMiss", L"shaders/shadowMiss.rmiss.spv");
    m_shaderGroupHelper.LoadShader(m_device, "rchitDefault", L"shaders/chitPlane.rchit.spv");
    m_shaderGroupHelper.LoadShader(m_device, "rahitFence", L"shaders/ahitFence.rahit.spv");
    m_shaderGroupHelper.LoadShader(m_device, "chitAABB", L"shaders/chitAABB.rchit.spv");
    m_shaderGroupHelper.LoadShader(m_device, "isectAABB", L"shaders/intersectAABB.rint.spv");
    m_shaderGroupHelper.LoadShader(m_device, "isectSDF" , L"shaders/intersectSDF.rint.spv");

    // シェーダーグループを構成する.
    m_shaderGroupHelper.AddShaderGroupRayGeneration(AppHitShaderGroups::GroupRgs,"rgs");
    m_shaderGroupHelper.AddShaderGroupMiss(AppHitShaderGroups::GroupMiss, "miss");
    m_shaderGroupHelper.AddShaderGroupMiss(AppHitShaderGroups::GroupMissShadow, "shadowMiss");

    m_shaderGroupHelper.AddShaderGroupHit(AppHitShaderGroups::GroupHitPlane, "rchitDefault");
    m_shaderGroupHelper.AddShaderGroupHit(
        AppHitShaderGroups::GroupHitFence, "rchitDefault", "rahitFence"
    );
    m_shaderGroupHelper.AddShaderGroupHit(
        AppHitShaderGroups::GroupHitAnalytic, "chitAABB", nullptr, "isectAABB"
    );
    m_shaderGroupHelper.AddShaderGroupHit(
        AppHitShaderGroups::GroupHitSdf, "chitAABB", nullptr, "isectSDF"
    );

    // レイトレーシングパイプラインの生成.
    VkRayTracingPipelineCreateInfoKHR rtPipelineCI{};

    rtPipelineCI.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    rtPipelineCI.stageCount = m_shaderGroupHelper.GetShaderStagesCount();
    rtPipelineCI.pStages = m_shaderGroupHelper.GetShaderStages();
    rtPipelineCI.groupCount = m_shaderGroupHelper.GetShaderGroupsCount();
    rtPipelineCI.pGroups = m_shaderGroupHelper.GetShaderGroups();
    rtPipelineCI.maxPipelineRayRecursionDepth = APP_RAYTRACE_RECURSIVE;
    rtPipelineCI.layout = m_pipelineLayout;

    vkCreateRayTracingPipelinesKHR(
        m_device->GetDevice(), VK_NULL_HANDLE, VK_NULL_HANDLE,
        1, &rtPipelineCI, nullptr, &m_raytracePipeline);

}

void IntersectionScene::CreateShaderBindingTable()
{
    auto memProps = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    auto usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    m_sbtHelper.Setup(m_device, m_raytracePipeline);
    m_shaderGroupHelper.LoadShaderGroupHandles(m_device, m_raytracePipeline);

    // RayGeneration
    m_sbtHelper.AddRayGenerationShader(AppHitShaderGroups::GroupRgs, { });
    // Miss
    m_sbtHelper.AddMissShader(AppHitShaderGroups::GroupMiss, { });
    m_sbtHelper.AddMissShader(AppHitShaderGroups::GroupMissShadow, { });
    
    // Hit
    uint32_t recordOffset = 0;
    for (auto& obj : m_sceneObjects) {
        auto name = obj->GetHitShader();
        obj->SetHitShaderOffset(recordOffset);
        auto count = uint32_t(obj->GetSceneObjectParameters().size());
        for (const auto& v : obj->GetSceneObjectParameters()) {
            std::vector<uint64_t> recordData{
                v.indexBuffer,
                v.vertexPosition,
            };
            m_sbtHelper.AddHitShader(name.c_str(), recordData);
        }
        recordOffset += count;
    }

    auto sbtSize = m_sbtHelper.ComputeShaderBindingTableSize();
    
    m_shaderBindingTable = m_device->CreateBuffer(
        sbtSize, usage, memProps);

    // 登録したエントリを書き込む.
    m_sbtHelper.Build(
        m_device,
        m_shaderBindingTable,
        m_shaderGroupHelper
    );
}

void IntersectionScene::CreateLayouts()
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

    VkDescriptorSetLayoutBinding layoutAnalyticAABBUBO{};
    layoutAnalyticAABBUBO.binding = 3;
    layoutAnalyticAABBUBO.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    layoutAnalyticAABBUBO.descriptorCount = 1;
    layoutAnalyticAABBUBO.stageFlags = VK_SHADER_STAGE_ALL;

    VkDescriptorSetLayoutBinding layoutSdfAabbUBO{};
    layoutSdfAabbUBO.binding = 4;
    layoutSdfAabbUBO.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    layoutSdfAabbUBO.descriptorCount = 1;
    layoutSdfAabbUBO.stageFlags = VK_SHADER_STAGE_ALL;

    VkDescriptorSetLayoutBinding layoutObjectParamSBO{};
    layoutObjectParamSBO.binding = 5;
    layoutObjectParamSBO.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    layoutObjectParamSBO.descriptorCount = 1;
    layoutObjectParamSBO.stageFlags = VK_SHADER_STAGE_ALL;

    VkDescriptorSetLayoutBinding layoutMaterialSBO{};
    layoutMaterialSBO.binding = 6;
    layoutMaterialSBO.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    layoutMaterialSBO.descriptorCount = 1;
    layoutMaterialSBO.stageFlags = VK_SHADER_STAGE_ALL;

    VkDescriptorSetLayoutBinding layoutTextures{};
    layoutTextures.binding = 7;
    layoutTextures.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutTextures.descriptorCount = m_materialManager.GetTextureCount();
    layoutTextures.stageFlags = VK_SHADER_STAGE_ALL;

    std::vector<VkDescriptorSetLayoutBinding> bindings({
        layoutAS, layoutRtImage, layoutSceneUBO, 
        layoutAnalyticAABBUBO, layoutSdfAabbUBO,
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

void IntersectionScene::CreateDescriptorSets()
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

    VkDescriptorBufferInfo aabbAnalyticUboDescriptor{};
    aabbAnalyticUboDescriptor.buffer = m_analyticUBO.GetBuffer();
    aabbAnalyticUboDescriptor.range = m_analyticUBO.GetBlockSize();

    VkWriteDescriptorSet aabbAnalyticUboWrite{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
    };
    aabbAnalyticUboWrite.dstSet = m_descriptorSet;
    aabbAnalyticUboWrite.dstBinding = 3;
    aabbAnalyticUboWrite.descriptorCount = 1;
    aabbAnalyticUboWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    aabbAnalyticUboWrite.pBufferInfo = &aabbAnalyticUboDescriptor;

    VkDescriptorBufferInfo aabbSdfUboDescriptor{};
    aabbSdfUboDescriptor.buffer = m_sdfUBO.GetBuffer();
    aabbSdfUboDescriptor.range = m_sdfUBO.GetBlockSize();

    VkWriteDescriptorSet aabbSdfUboWrite{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
    };
    aabbSdfUboWrite.dstSet = m_descriptorSet;
    aabbSdfUboWrite.dstBinding = 4;
    aabbSdfUboWrite.descriptorCount = 1;
    aabbSdfUboWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    aabbSdfUboWrite.pBufferInfo = &aabbSdfUboDescriptor;


    VkDescriptorBufferInfo objectInfoDescriptor{};
    objectInfoDescriptor.buffer = m_objectsSBO.GetBuffer();
    objectInfoDescriptor.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet objectInfoWrite{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
    };
    objectInfoWrite.dstSet = m_descriptorSet;
    objectInfoWrite.dstBinding = 5;
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
    materialInfoWrite.dstBinding = 6;
    materialInfoWrite.descriptorCount = 1;
    materialInfoWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    materialInfoWrite.pBufferInfo = &materialInfoDescriptor;

    std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
        asWrite, imageWrite, sceneUboWrite,
        aabbAnalyticUboWrite, aabbSdfUboWrite,
        objectInfoWrite, materialInfoWrite,
    };
    vkUpdateDescriptorSets(
        m_device->GetDevice(),
        uint32_t(writeDescriptorSets.size()),
        writeDescriptorSets.data(),
        0,
        nullptr);

    // 各モデル用のテクスチャをディスクリプタに書き込む.
    auto textureDescriptors = m_materialManager.GetTextureDescriptors();
    VkWriteDescriptorSet texturesImageWrite{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
    };
    texturesImageWrite.dstSet = m_descriptorSet;
    texturesImageWrite.dstBinding = 7;
    texturesImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texturesImageWrite.descriptorCount = uint32_t(textureDescriptors.size());
    texturesImageWrite.pImageInfo = textureDescriptors.data();

    vkUpdateDescriptorSets(
        m_device->GetDevice(),
        1, &texturesImageWrite, 0, nullptr);
}

void IntersectionScene::InitializeImGui()
{
    // 先にレンダーパスを準備する.
    {
        // DXR の描画結果の上に重ねて書くことに注意.
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

void IntersectionScene::DestroyImGui()
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

void IntersectionScene::UpdateHUD()
{
    // ImGui
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Information");
    auto framerate = ImGui::GetIO().Framerate;
    ImGui::Text("frametime %.3f ms", 1000.0f / framerate);
    ImGui::Combo("AABB Type", &m_guiParams.aabbObjectType, "CUBE\0SPHERE\0\0");
    ImGui::Combo("SDF Type", &m_guiParams.sdfObjectType, "CUBE\0Sphere\0TORUS\0\0");
    ImGui::InputFloat3("Box Extent", (float*)&m_guiParams.sdfBoxExtent);
    ImGui::SliderFloat("Radius", &m_guiParams.sdfRadius, 0.1f, 0.4f);
    ImGui::End();
}

void IntersectionScene::UpdateSceneTLAS()
{
    auto command = m_device->GetCurrentFrameCommandBuffer();
    auto frameIndex = m_device->GetCurrentFrameIndex();

    // VkAccelerationStructureInstanceKHR 配列を取得して書き込む.
    auto asInstances = CreateAccelerationStructureIncenceFromSceneObjects();
    auto instancesBufferSize = sizeof(VkAccelerationStructureInstanceKHR) * asInstances.size();
    memcpy(m_instancesBuffer.Map(frameIndex), asInstances.data(), instancesBufferSize);


    VkDeviceOrHostAddressConstKHR instanceDataDeviceAddress{};
    instanceDataDeviceAddress.deviceAddress = m_instancesBuffer.GetDeviceAddress(frameIndex);

    VkAccelerationStructureGeometryKHR asGeometry{};
    asGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    asGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    asGeometry.flags = 0;
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

    VkBuildAccelerationStructureFlagsKHR buildFlags = 0;
    buildFlags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    m_topLevelAS.Update(
        command,
        VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        tlasInput,
        buildFlags
    );
}

void IntersectionScene::DeployObjects()
{
    // 床を配置.
    m_meshPlane->SetWorldMatrix(glm::mat4(1.0f));

    // フェンスを配置.
    m_meshFence->SetWorldMatrix(glm::translate(glm::vec3(0, 1, 1)));

    // AABB Analytic モデル.
    auto trans = glm::translate(glm::vec3(1.25f, 0.5f, -0.5f)) * glm::rotate(glm::radians(30.f), glm::vec3(0, 1, 0));
    m_meshAABB->SetWorldMatrix(trans);

    // AABB SDF モデル.
    trans = glm::translate(glm::vec3(-1.25f, 0.5f, -0.2f));
    m_meshAABBSDF->SetWorldMatrix(trans);
}

void IntersectionScene::CreateSceneList()
{
    m_sceneObjects.clear();
    // 床.
    m_sceneObjects.push_back(m_meshPlane);

    // フェンス.
    m_sceneObjects.push_back(m_meshFence);

    // AABB
    m_sceneObjects.push_back(m_meshAABB);

    // AABB(SDF)
    m_sceneObjects.push_back(m_meshAABBSDF);

}

void IntersectionScene::CreateSceneBuffers()
{
    std::vector<ObjectParam> objParameters{};

    for (const auto& obj : m_sceneObjects) {
        auto objParams = obj->GetSceneObjectParameters();
        for (auto& v : objParams) {
            ObjectParam objParam;
            objParam.materialIndex = v.materialIndex;
            objParameters.emplace_back(objParam);
        }
    }

    auto devMemProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    auto usage = \
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | \
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    auto materialData = m_materialManager.GetMaterialData();
    auto materialBufSize = sizeof(Material) * materialData.size();

    m_materialsSBO = m_device->CreateBuffer(
        materialBufSize, usage, devMemProps
    );
    m_device->WriteToBuffer(m_materialsSBO, materialData.data(), materialBufSize);

    auto objectBufSize = sizeof(ObjectParam) * objParameters.size();
    m_objectsSBO = m_device->CreateBuffer(
        objectBufSize, usage, devMemProps);
    m_device->WriteToBuffer(m_objectsSBO, objParameters.data(), objectBufSize);
}

std::vector<VkAccelerationStructureInstanceKHR> IntersectionScene::CreateAccelerationStructureIncenceFromSceneObjects()
{
    std::vector<VkAccelerationStructureInstanceKHR> asInstances;

    int customIndex = 0;
    for (auto& obj : m_sceneObjects) {
        asInstances.emplace_back(
            obj->GetAccelerationStructureInstance()
        );
        auto& asInstance = asInstances.back();
        asInstance.instanceCustomIndex = customIndex;
        customIndex += obj->GetSubMeshCount();
    }
    return asInstances;
}
