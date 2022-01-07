#include "ModelScene.h"
#include <glm/gtx/transform.hpp>
#include <random>
#include <numeric>

// For ImGui
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

void ModelScene::OnInit()
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

    // レイトレーシングパイプラインを構築する.
    CreateRaytracePipeline();

    // スキニング計算のパイプラインを構築する.
    CreateComputeSkinningPipeline();

    // シェーダーバインディングテーブルを構築する.
    CreateShaderBindingTable();

    // ディスクリプタの準備・書き込み.
    CreateDescriptorSets();
    CreateDescriptorSetsSkinned();

    // ImGui の初期化.
    InitializeImGui();

    // 初期パラメータ設定.
    auto eye = glm::vec3(0.0f, 2.0f, 3.0f);
    auto target = glm::vec3(0.0f, 1.4f, 0.0f);
    
    m_camera.SetLookAt(eye, target);

    m_camera.SetPerspective(
        glm::radians(60.0f),
        GetAspect(),
        0.1f,
        100.f
    );

    m_sceneParam.lightColor = glm::vec4(1.0f);
    m_sceneParam.lightDirection = glm::vec4(0.5f, -0.75f, -1.0f, 0.0f);
    m_sceneParam.ambientColor = glm::vec4(0.15f);
}

void ModelScene::OnDestroy()
{
    m_sceneUBO.Destroy(m_device);
    m_device->DestroyBuffer(m_objectsSBO);
    m_device->DestroyBuffer(m_materialsSBO);
    m_instancesBuffer.Destroy(m_device);
    m_topLevelAS.Destroy(m_device);

    m_actorTable->Destroy(m_device);
    m_actorTeapot0->Destroy(m_device);
    m_actorTeapot1->Destroy(m_device);
    m_actorChara->Destroy(m_device);
    
    m_meshPlane->Destroy(m_device);

    m_modelTable.Destroy(m_device);
    m_modelTeapot.Destroy(m_device);
    m_modelChara.Destroy(m_device);
    m_meshPlane.reset();
    
    m_device->DestroyImage(m_raytracedImage);
    m_device->DestroyBuffer(m_shaderBindingTable);
    m_materialManager.Destroy(m_device);

    m_device->DeallocateDescriptorSet(m_descriptorSet);
    m_device->DeallocateDescriptorSet(m_descriptorSetCompute);

    m_shaderGroupHelper.Destroy(m_device);

    DestroyImGui();

    auto device = m_device->GetDevice();
    vkDestroyPipeline(device, m_raytracePipeline, nullptr);
    vkDestroyPipeline(device, m_computeSkiningPipeline, nullptr);
    vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    vkDestroyPipelineLayout(device, m_pipelineLayoutSkinned, nullptr);
    vkDestroyDescriptorSetLayout(device, m_dsLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, m_dsLayoutSkinned, nullptr);
}

void ModelScene::OnUpdate()
{
    UpdateHUD();
    m_sceneParam.mtxView = m_camera.GetViewMatrix();
    m_sceneParam.mtxProj = m_camera.GetProjectionMatrix();
    m_sceneParam.mtxViewInv = glm::inverse(m_sceneParam.mtxView);
    m_sceneParam.mtxProjInv = glm::inverse(m_sceneParam.mtxProj);
    m_sceneParam.cameraPosition = m_camera.GetPosition();

    if (m_actorChara) {
        auto node = m_actorChara->SearchNode(L"ひじ.L");
        if (node) {
            auto mtx = glm::rotate(glm::radians(m_guiParams.elbowL), glm::vec3(0,0,1));
            node->SetRotation(glm::quat_cast(mtx));
        }
        node = m_actorChara->SearchNode(L"ひじ.R");
        if (node) {
            auto mtx = glm::rotate(glm::radians(m_guiParams.elbowR), glm::vec3(0, 0, 1));
            node->SetRotation(glm::quat_cast(mtx));
        }
        node = m_actorChara->SearchNode(L"首");
        if (node) {
            auto mtx = glm::rotate(glm::radians(m_guiParams.neck), glm::vec3(1, 0, 0));
            node->SetRotation(glm::quat_cast(mtx));
        }
    }

    // 配置情報更新.
    DeployObjects();
}

void ModelScene::OnRender()
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

    // 行列の更新.
    m_actorTable->ApplyTransform(m_device);
    m_actorTeapot0->ApplyTransform(m_device);
    m_actorTeapot1->ApplyTransform(m_device);
    m_actorChara->ApplyTransform(m_device);

    // スキニングによる頂点変形.
    if (m_actorChara) {
        std::vector<uint32_t> offsets = {
            uint32_t(m_actorChara->GetJointMatricesBuffer().GetBlockSize()) * frameIndex
        };

        // Graphics キューを使ってComputeのパイプラインを実行する.
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, m_computeSkiningPipeline);
        vkCmdBindDescriptorSets(
            command, VK_PIPELINE_BIND_POINT_COMPUTE,
            m_pipelineLayoutSkinned, 0, 
            1, &m_descriptorSetCompute,
            uint32_t(offsets.size()),
            offsets.data()
        );

        // スキニング計算をコンピュートシェーダーで実行.
        // * この実装の並列性はよくない 
        auto vertexCount = m_actorChara->GetSkinnedVertexCount();
        vkCmdDispatch(command, vertexCount, 1, 1);

        // この計算結果で BLAS 更新をするため、バリアを設定する.
        VkMemoryBarrier barrier{
            VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        };
        barrier.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        vkCmdPipelineBarrier(
            command,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 1, &barrier,
            0, nullptr,
            0, nullptr
        );
    }

    // BLAS 更新.
    m_actorTable->UpdateBlas(command);
    m_actorChara->UpdateBlas(command);

    // TLAS を更新する.
    UpdateSceneTLAS();

    // レイトレーシングを行う.
    uint32_t offsets[] = {
        uint32_t(m_sceneUBO.GetBlockSize() * frameIndex),
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

void ModelScene::OnMouseDown(MouseButton button)
{
    if (ImGui::GetIO().WantCaptureMouse) {
        return;
    }
    m_camera.OnMouseButtonDown(int(button));
}

void ModelScene::OnMouseUp(MouseButton button)
{
    if (ImGui::GetIO().WantCaptureMouse) {
        return;
    }
    m_camera.OnMouseButtonUp();
}

void ModelScene::OnMouseMove()
{
    if (ImGui::GetIO().WantCaptureMouse) {
        return;
    }
    auto mouseDelta = ImGui::GetIO().MouseDelta;
    float dx = float(mouseDelta.x) / GetWidth();
    float dy = float(mouseDelta.y) / GetHeight();
    m_camera.OnMouseMove(-dx, dy);
}

void ModelScene::CreateSceneGeometries()
{
    std::vector<util::primitive::VertexPNT> vertices;
    std::vector<uint32_t> indices;
    const auto vstride = uint32_t(sizeof(util::primitive::VertexPNT));
    const auto istride = uint32_t(sizeof(uint32_t));

    // 先にマテリアル用のテクスチャを読み込んでおく.
    const auto floorTexFile = L"textures/trianglify-lowres.png";
    for (const auto* textureFile : { floorTexFile }) {
        auto usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        auto devMemProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        auto texture = m_device->CreateTexture2DFromFile(textureFile, usage, devMemProps);

        assert(texture.GetImage() != VK_NULL_HANDLE);
        m_materialManager.AddTexture(textureFile, texture);
    }

    // マテリアルを準備する.
    auto matFloor = std::make_shared<Material>(L"Floor");
    matFloor->SetTexture(m_materialManager.GetTexture(floorTexFile));
    matFloor->SetType(static_cast<int>(MaterialType::LAMBERT));
    matFloor->SetDiffuse(glm::vec3(1.0f, 0.2f, 0.3f));
    matFloor->SetSpecularPower(5.0f);


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

    // モデルをロードする.
    {
        m_modelTable.LoadFromGltf(L"models/table.glb", m_device);

        m_actorTable = std::make_shared<ModelMesh>();
        ModelMesh::CreateInfo ci;
        ci.model = &m_modelTable;
        m_actorTable->Create(m_device, ci, m_materialManager);
        m_actorTable->SetHitShader(AppHitShaderGroups::GroupHitModel);
    }
    {
        m_modelTeapot.LoadFromGltf(L"models/teapot.glb", m_device);
        ModelMesh::CreateInfo ci;
        ci.model = &m_modelTeapot;

        m_actorTeapot0 = std::make_shared<ModelMesh>();
        m_actorTeapot0->Create(m_device, ci, m_materialManager);
        m_actorTeapot0->SetHitShader(AppHitShaderGroups::GroupHitModel);

        m_actorTeapot1 = std::make_shared<ModelMesh>();
        m_actorTeapot1->Create(m_device, ci, m_materialManager);
        m_actorTeapot1->SetHitShader(AppHitShaderGroups::GroupHitModel);
    }
    {
        m_modelChara.LoadFromGltf(L"models/alicia.glb", m_device);

        m_actorChara = std::make_shared<ModelMesh>();
        ModelMesh::CreateInfo ci;
        ci.model = &m_modelChara;
        m_actorChara->Create(m_device, ci, m_materialManager);
        m_actorChara->SetHitShader(AppHitShaderGroups::GroupHitModel);
    }

}

void ModelScene::CreateSceneBLAS()
{
    VkBuildAccelerationStructureFlagsKHR buildFlags = 0;
    buildFlags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildFlags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;

    // Plane BLAS の生成.
    m_meshPlane->BuildAS(m_device, buildFlags);
   
    // Table BLAS
    m_actorTable->BuildAS(m_device, buildFlags);

    // Teapot BLAS
    m_actorTeapot0->BuildAS(m_device, buildFlags);
    m_actorTeapot1->BuildAS(m_device, buildFlags);

    // Character BLAS
    m_actorChara->BuildAS(m_device, buildFlags);
}

void ModelScene::CreateSceneTLAS()
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

void ModelScene::CreateRaytracedBuffer()
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

void ModelScene::CreateRaytracePipeline()
{
    // レイトレーシングのシェーダーを読み込む.
    // シェーダーをロード.
    m_shaderGroupHelper.LoadShader(m_device, "rgs", L"shaders/raygen.rgen.spv");
    m_shaderGroupHelper.LoadShader(m_device, "miss", L"shaders/miss.rmiss.spv");
    m_shaderGroupHelper.LoadShader(m_device, "shadowMiss", L"shaders/shadowMiss.rmiss.spv");
    m_shaderGroupHelper.LoadShader(m_device, "rchitDefault", L"shaders/chitPlane.rchit.spv");
    m_shaderGroupHelper.LoadShader(m_device, "rchitModel", L"shaders/chitModel.rchit.spv");
 
    // シェーダーグループを構成する.
    m_shaderGroupHelper.AddShaderGroupRayGeneration(AppHitShaderGroups::GroupRgs,"rgs");
    m_shaderGroupHelper.AddShaderGroupMiss(AppHitShaderGroups::GroupMiss, "miss");
    m_shaderGroupHelper.AddShaderGroupMiss(AppHitShaderGroups::GroupMissShadow, "shadowMiss");

    m_shaderGroupHelper.AddShaderGroupHit(AppHitShaderGroups::GroupHitPlane, "rchitDefault");
    m_shaderGroupHelper.AddShaderGroupHit(AppHitShaderGroups::GroupHitModel, "rchitModel");

    // レイトレーシングパイプラインの生成.
    VkRayTracingPipelineCreateInfoKHR rtPipelineCI{};

    rtPipelineCI.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    rtPipelineCI.stageCount = m_shaderGroupHelper.GetShaderStagesCount();
    rtPipelineCI.pStages = m_shaderGroupHelper.GetShaderStages();
    rtPipelineCI.groupCount = m_shaderGroupHelper.GetShaderGroupsCount();
    rtPipelineCI.pGroups = m_shaderGroupHelper.GetShaderGroups();
    rtPipelineCI.maxPipelineRayRecursionDepth = 1;
    rtPipelineCI.layout = m_pipelineLayout;

    vkCreateRayTracingPipelinesKHR(
        m_device->GetDevice(), VK_NULL_HANDLE, VK_NULL_HANDLE,
        1, &rtPipelineCI, nullptr, &m_raytracePipeline);
}

void ModelScene::CreateComputeSkinningPipeline()
{
    // スキニング計算のためのコンピュートシェーダーを読み込む.
    auto shaderStage = util::LoadShader(m_device, L"shaders/computeSkinning.comp.spv", VK_SHADER_STAGE_COMPUTE_BIT);

    VkComputePipelineCreateInfo compPipelineCI{
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO
    };
    compPipelineCI.layout = m_pipelineLayoutSkinned;
    compPipelineCI.stage = shaderStage;

    vkCreateComputePipelines(m_device->GetDevice(), VK_NULL_HANDLE, 1, &compPipelineCI, nullptr, &m_computeSkiningPipeline);
    vkDestroyShaderModule(m_device->GetDevice(), shaderStage.module, nullptr);
}



void ModelScene::CreateShaderBindingTable()
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
    for( auto& obj : m_sceneObjects) {
        auto name = obj->GetHitShader();
        obj->SetHitShaderOffset(recordOffset);
        auto count = obj->GetSceneObjectParameters().size();
        for (const auto& v : obj->GetSceneObjectParameters()) {
            std::vector<uint64_t> recordData{
                v.indexBuffer,
                v.vertexPosition,
                v.vertexNormal,
                v.vertexTexcoord,
                v.blasTransformMatrices,
            };
            m_sbtHelper.AddHitShader(name.c_str(),recordData);
        }

        recordOffset += uint32_t(count);
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

void ModelScene::CreateLayouts()
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

    VkDescriptorSetLayoutBinding layoutObjectParamSBO{};
    layoutObjectParamSBO.binding = 3;
    layoutObjectParamSBO.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    layoutObjectParamSBO.descriptorCount = 1;
    layoutObjectParamSBO.stageFlags = VK_SHADER_STAGE_ALL;

    VkDescriptorSetLayoutBinding layoutMaterialSBO{};
    layoutMaterialSBO.binding = 4;
    layoutMaterialSBO.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    layoutMaterialSBO.descriptorCount = 1;
    layoutMaterialSBO.stageFlags = VK_SHADER_STAGE_ALL;

    VkDescriptorSetLayoutBinding layoutTextures{};
    layoutTextures.binding = 5;
    layoutTextures.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    layoutTextures.descriptorCount = m_materialManager.GetTextureCount();
    layoutTextures.stageFlags = VK_SHADER_STAGE_ALL;

    std::vector<VkDescriptorSetLayoutBinding> bindings({
        layoutAS, layoutRtImage, layoutSceneUBO, 
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


    // Skinning 計算用レイアウトの準備.
    auto makeSkinComputeDSLayout = [](
        int bindingIndex, 
        VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
    {
        auto ret = VkDescriptorSetLayoutBinding{ 0, descriptorType , 1, VK_SHADER_STAGE_COMPUTE_BIT, VK_NULL_HANDLE };
        ret.binding = bindingIndex;
        return ret;
    };

    bindings.clear();
    bindings.emplace_back(makeSkinComputeDSLayout(0)); // SrcPosition
    bindings.emplace_back(makeSkinComputeDSLayout(1)); // SrcNormal
    bindings.emplace_back(makeSkinComputeDSLayout(2)); // SrcJointWeights
    bindings.emplace_back(makeSkinComputeDSLayout(3)); // SrcJointIndices
    bindings.emplace_back(makeSkinComputeDSLayout(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)); // SkinnedMatrices
    bindings.emplace_back(makeSkinComputeDSLayout(5)); // DstPosition
    bindings.emplace_back(makeSkinComputeDSLayout(6)); // DstNormal

    dsLayoutCI.bindingCount = uint32_t(bindings.size());
    dsLayoutCI.pBindings = bindings.data();

    vkCreateDescriptorSetLayout(
        m_device->GetDevice(), &dsLayoutCI, nullptr, &m_dsLayoutSkinned);

    // Skinned 用 Pipeline Layout.
    layouts = { m_dsLayoutSkinned };
    pipelineLayoutCI.setLayoutCount = uint32_t(layouts.size());
    pipelineLayoutCI.pSetLayouts = layouts.data();
    vkCreatePipelineLayout(m_device->GetDevice(),
        &pipelineLayoutCI, nullptr, &m_pipelineLayoutSkinned);
}

void ModelScene::CreateDescriptorSets()
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


    VkDescriptorBufferInfo objectInfoDescriptor{};
    objectInfoDescriptor.buffer = m_objectsSBO.GetBuffer();
    objectInfoDescriptor.range = VK_WHOLE_SIZE;

    VkWriteDescriptorSet objectInfoWrite{
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
    };
    objectInfoWrite.dstSet = m_descriptorSet;
    objectInfoWrite.dstBinding = 3;
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
    materialInfoWrite.dstBinding = 4;
    materialInfoWrite.descriptorCount = 1;
    materialInfoWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    materialInfoWrite.pBufferInfo = &materialInfoDescriptor;

    std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
        asWrite, imageWrite, sceneUboWrite,
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
    texturesImageWrite.dstBinding = 5;
    texturesImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texturesImageWrite.descriptorCount = uint32_t(textureDescriptors.size());
    texturesImageWrite.pImageInfo = textureDescriptors.data();

    vkUpdateDescriptorSets(
        m_device->GetDevice(),
        1, &texturesImageWrite, 0, nullptr);
}

void ModelScene::CreateDescriptorSetsSkinned()
{
    m_descriptorSetCompute = m_device->AllocateDescriptorSet(m_dsLayoutSkinned);
    auto srcPosDescriptor = m_actorChara->GetPositionBufferSrc().GetDescriptor();
    auto srcNormalDescriptor = m_actorChara->GetNormalBufferSrc().GetDescriptor();
    auto srcJointWeightsDescriptor = m_actorChara->GetJointWeightsBuffer().GetDescriptor();
    auto srcJointIndicesDescriptor = m_actorChara->GetJointIndicesBuffer().GetDescriptor();
    auto srcJointMatricesDescriptor = m_actorChara->GetJointMatricesBuffer().GetDescriptor();
    auto dstPosDescriptor = m_actorChara->GetPositionTransformedBuffer().GetDescriptor();
    auto dstNormalDescriptor = m_actorChara->GetNormalTransformedBuffer().GetDescriptor();

    auto makeWriteDescriptorSet = [](
        VkDescriptorSet dstSet, int binding, const VkDescriptorBufferInfo* pBufferInfo, VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)
    {
        VkWriteDescriptorSet write{
            VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET
        };
        write.descriptorType = descriptorType;
        write.descriptorCount = 1;
        write.dstSet = dstSet;
        write.dstBinding = binding;
        write.pBufferInfo = pBufferInfo;
        return write;
    };
    auto dstDS = m_descriptorSetCompute;
    std::vector<VkWriteDescriptorSet> writes = {
        makeWriteDescriptorSet(dstDS, 0, &srcPosDescriptor),
        makeWriteDescriptorSet(dstDS, 1, &srcNormalDescriptor),
        makeWriteDescriptorSet(dstDS, 2, &srcJointWeightsDescriptor),
        makeWriteDescriptorSet(dstDS, 3, &srcJointIndicesDescriptor),
        makeWriteDescriptorSet(
            dstDS, 4, &srcJointMatricesDescriptor, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC),
        makeWriteDescriptorSet(dstDS, 5, &dstPosDescriptor),
        makeWriteDescriptorSet(dstDS, 6, &dstNormalDescriptor),
    };
    vkUpdateDescriptorSets(m_device->GetDevice(), uint32_t(writes.size()), writes.data(), 0, nullptr);
}

void ModelScene::InitializeImGui()
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

void ModelScene::DestroyImGui()
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

void ModelScene::UpdateHUD()
{
    // ImGui
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Information");
    ImGui::Text("%s", m_device->GetDeviceName());
    auto framerate = ImGui::GetIO().Framerate;
    ImGui::Text("frametime %.3f ms", 1000.0f / framerate);

    ImGui::SliderFloat("Elbow L", &m_guiParams.elbowL, 0.0f, 150.0f, "%.1f");
    ImGui::SliderFloat("Elbow R", &m_guiParams.elbowR, 0.0f, 150.0f, "%.1f");
    ImGui::SliderFloat("Neck", &m_guiParams.neck, -30.0f, 60.0f, "%.1f");
    ImGui::End();
}

void ModelScene::UpdateSceneTLAS()
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



void ModelScene::DeployObjects()
{
    // 床を配置.
    m_meshPlane->SetWorldMatrix(glm::mat4(1.0f));

    auto mtxTrans = glm::translate(glm::vec3(0, 0, -1)) * glm::rotate(glm::radians(90.0f), glm::vec3(0, 1, 0));
    m_actorTable->SetWorldMatrix(mtxTrans);
    m_actorTable->UpdateMatrices();

    // teapot配置.
    mtxTrans = glm::translate(glm::vec3(1.0f, 1.04f, -1.0f));
    m_actorTeapot0->SetWorldMatrix(mtxTrans);
    m_actorTeapot0->UpdateMatrices();

    mtxTrans = glm::translate(glm::vec3(-1.0f, 1.04f, -1.0f));
    m_actorTeapot1->SetWorldMatrix(mtxTrans);
    m_actorTeapot1->UpdateMatrices();

    // キャラクター配置.
    static int count = 0;
    glm::vec3 trans(0.0f);
    trans.x = 0.75f * sinf(count * 0.01f);
    trans.z = 0.25f * cosf(count * 0.01f) + 0.75f;
    m_actorChara->SetWorldMatrix(glm::translate(trans));
    m_actorChara->UpdateMatrices();
    count++;
}

void ModelScene::CreateSceneList()
{
    m_sceneObjects.clear();
    // 床.
    m_sceneObjects.push_back(m_meshPlane);

    m_sceneObjects.push_back(m_actorTable);

    m_sceneObjects.push_back(m_actorTeapot0);
    m_sceneObjects.push_back(m_actorTeapot1);

    m_sceneObjects.push_back(m_actorChara);
}

void ModelScene::CreateSceneBuffers()
{
    std::vector<ObjectParam> objParameters{};

    for (auto& obj : m_sceneObjects) {
        auto objParams = obj->GetSceneObjectParameters();
        for (auto& v : objParams) {
            ObjectParam oParam;
            oParam.materialIndex = v.materialIndex;
            oParam.blasMatrixIndex = v.blasMatrixIndex;
            oParam.blasMatrixStride = v.blasMatrixStride;
            objParameters.emplace_back(oParam);
        }
    }

    auto devMemProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    auto usage = \
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | \
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    auto materialData = m_materialManager.GetMaterialData();
    auto materialBufSize = materialData.size() * sizeof(Material::DataBlock);
    m_materialsSBO = m_device->CreateBuffer(
        materialBufSize, usage, devMemProps
    );
    m_device->WriteToBuffer(m_materialsSBO, materialData.data(), materialBufSize);

    auto objectBufSize = sizeof(ObjectParam) * objParameters.size();
    m_objectsSBO = m_device->CreateBuffer(
        objectBufSize, usage, devMemProps);
    m_device->WriteToBuffer(m_objectsSBO, objParameters.data(), objectBufSize);
}

std::vector<VkAccelerationStructureInstanceKHR> ModelScene::CreateAccelerationStructureIncenceFromSceneObjects()
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
