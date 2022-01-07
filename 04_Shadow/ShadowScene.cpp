#include "ShadowScene.h"
#include <glm/gtx/transform.hpp>
#include <random>

// For ImGui
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

void ShadowScene::OnInit()
{
    m_materialManager.Create(m_device, MaxTextureCount);

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

void ShadowScene::OnDestroy()
{
    m_sceneUBO.Destroy(m_device);
    m_device->DestroyBuffer(m_objectsSBO);
    m_device->DestroyBuffer(m_materialsSBO);
    m_instancesBuffer.Destroy(m_device);
    m_topLevelAS.Destroy(m_device);

    m_meshPlane->Destroy(m_device);
    m_meshLightSphere->Destroy(m_device);
    for (auto& v : m_meshSpheres) {
        v->Destroy(m_device);
    }
    m_meshSpheres.clear();
    m_meshPlane.reset();
    m_meshLightSphere.reset();

    m_device->DestroyImage(m_raytracedImage);
    m_device->DestroyBuffer(m_shaderBindingTable);

    m_device->DeallocateDescriptorSet(m_descriptorSet);

    m_shaderGroupHelper.Destroy(m_device);
    m_materialManager.Destroy(m_device);
    DestroyImGui();

    auto device = m_device->GetDevice();
    vkDestroyPipeline(device, m_raytracePipeline, nullptr);
    vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, m_dsLayout, nullptr);
}

void ShadowScene::OnUpdate()
{
    UpdateHUD();
    m_sceneParam.mtxView = m_camera.GetViewMatrix();
    m_sceneParam.mtxProj = m_camera.GetProjectionMatrix();
    m_sceneParam.mtxViewInv = glm::inverse(m_sceneParam.mtxView);
    m_sceneParam.mtxProjInv = glm::inverse(m_sceneParam.mtxProj);
    m_sceneParam.cameraPosition = m_camera.GetPosition();

    // ライトの位置を更新する.
    glm::vec3 lightPos = m_guiParams.pointLightPosition;
    lightPos *= m_guiParams.distanceFactor;
    m_sceneParam.pointLightPosition = glm::vec4(lightPos, 0.0f);

    // 配置情報更新.
    DeployObjects();
}

void ShadowScene::OnRender()
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

    // TLAS を更新する.
    //  ポイントライトの位置の変更に対応.
    UpdateSceneTLAS();

    // レイトレーシングを行う.
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

void ShadowScene::OnMouseDown(MouseButton button)
{
    if (ImGui::GetIO().WantCaptureMouse) {
        return;
    }
    m_camera.OnMouseButtonDown(int(button));
}

void ShadowScene::OnMouseUp(MouseButton button)
{
    if (ImGui::GetIO().WantCaptureMouse) {
        return;
    }
    m_camera.OnMouseButtonUp();
}

void ShadowScene::OnMouseMove()
{
    if (ImGui::GetIO().WantCaptureMouse) {
        return;
    }
    auto mouseDelta = ImGui::GetIO().MouseDelta;
    float dx = float(mouseDelta.x) / GetWidth();
    float dy = float(mouseDelta.y) / GetHeight();
    m_camera.OnMouseMove(-dx, dy);
}

void ShadowScene::CreateSceneGeometries()
{
    auto hostMemProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    auto usageForRT = \
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | \
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR;
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

    auto matLight = std::make_shared<Material>(L"Light");
    matLight->SetType(static_cast<int>(MaterialType::EMISSIVE));

    std::vector<std::shared_ptr<Material>> matSpheres;
    std::vector<glm::vec3> colorTable = {
        glm::vec3(1.0f, 1.0f, 1.0f),
        glm::vec3(0.5f, 0.8f, 0.4f),
        glm::vec3(0.7f, 0.6f, 0.2f),
        glm::vec3(0.2f, 0.3f, 0.6f),
        glm::vec3(0.1f, 0.8f, 0.9f),
    };
    for (int i=0;i<int(colorTable.size());++i) {
        auto name = std::wstring(L"SphereMaterial_") + std::to_wstring(i);
        auto m = std::make_shared<Material>(name.c_str());
        m->SetDiffuse(colorTable[i]);
        m->SetType(static_cast<int>(MaterialType::LAMBERT));
        matSpheres.emplace_back(std::move(m));
    }

    // 床平面の準備.
    {
        util::primitive::GetPlane(vertices, indices);

        auto vbPlaneSize = vstride * vertices.size();
        auto ibPlaneSize = istride * indices.size();

        SimplePolygonMesh::CreateInfo ci;
        ci.srcVertices = vertices.data();
        ci.srcIndices = indices.data();
        ci.vbSize = vstride * vertices.size();
        ci.ibSize = istride * indices.size();
        ci.indexCount = uint32_t(indices.size());
        ci.vertexCount = uint32_t(vertices.size());
        ci.stride = vstride;
        ci.material = matFloor;

        m_meshPlane = std::make_shared<SimplePolygonMesh>();
        m_meshPlane->Create(m_device, ci, m_materialManager);
        m_meshPlane->SetHitShader(AppHitShaderGroups::GroupHitPlane);
    }
    // ライト用Sphereの準備.
    {
        util::primitive::GetSphere(vertices, indices, 2.0f, 8, 12);
        auto vbSphereSize = vstride * vertices.size();
        auto ibSphereSize = istride * indices.size();

        SimplePolygonMesh::CreateInfo ci;
        ci.srcVertices = vertices.data();
        ci.srcIndices = indices.data();
        ci.vbSize = vstride * vertices.size();
        ci.ibSize = istride * indices.size();
        ci.indexCount = uint32_t(indices.size());
        ci.vertexCount = uint32_t(vertices.size());
        ci.stride = vstride;
        ci.material = matLight;

        m_meshLightSphere = std::make_shared<SimplePolygonMesh>();
        m_meshLightSphere->Create(m_device, ci, m_materialManager);
        m_meshLightSphere->SetHitShader(AppHitShaderGroups::GroupHitSphere);
    }

    // Sphereの準備.
    for(int i=0;i<SphereCount;++i) {
        util::primitive::GetSphere(vertices, indices, 0.5f);
        auto vbSphereSize = vstride * vertices.size();
        auto ibSphereSize = istride * indices.size();

        SimplePolygonMesh::CreateInfo ci;
        ci.srcVertices = vertices.data();
        ci.srcIndices = indices.data();
        ci.vbSize = vstride * vertices.size();
        ci.ibSize = istride * indices.size();
        ci.indexCount = uint32_t(indices.size());
        ci.vertexCount = uint32_t(vertices.size());
        ci.stride = vstride;
        ci.material = matSpheres[ i % matSpheres.size() ];
        m_meshSpheres.emplace_back(std::make_shared<SimplePolygonMesh>());
        auto meshSphere = m_meshSpheres.back();

        meshSphere->Create(m_device, ci, m_materialManager);
        meshSphere->SetHitShader(AppHitShaderGroups::GroupHitSphere);
    }
}

void ShadowScene::CreateSceneBLAS()
{
    VkBuildAccelerationStructureFlagsKHR buildFlags = 0;
    buildFlags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;

    // Plane BLAS の生成.
    m_meshPlane->BuildAS(m_device, buildFlags);

    // LightSphere BLAS の生成.
    m_meshLightSphere->BuildAS(m_device, buildFlags);

    // Spheres BLAS の生成.
    for (const auto& v : m_meshSpheres) {
        v->BuildAS(m_device, buildFlags);
    }
}

void ShadowScene::CreateSceneTLAS()
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
    VkBuildAccelerationStructureFlagsKHR buildFlags = 0;
    buildFlags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    buildFlags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    m_topLevelAS.BuildAS(m_device, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, tlasInput, buildFlags);

    m_topLevelAS.DestroyScratchBuffer(m_device);
}

void ShadowScene::CreateRaytracedBuffer()
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

void ShadowScene::CreateRaytracePipeline()
{
    bool useNoRecursiveRaytrace = false;
    const std::string deviceName = m_device->GetDeviceName();
    m_useNoRecursiveRaytrace = deviceName.find("Radeon") != deviceName.npos;
    m_useNoRecursiveRaytrace = true;
    std::wstring shaderFiles[] = {
        L"shaders/raygen.rgen.spv",
        L"shaders/miss.rmiss.spv",
        L"shaders/shadowMiss.rmiss.spv",
        L"shaders/chitPlane.rchit.spv",
        L"shaders/chitSphere.rchit.spv"
    };
    if (m_useNoRecursiveRaytrace) {
        shaderFiles[0] = L"shaders/no-recursion_raygen.rgen.spv";
        shaderFiles[1] = L"shaders/no-recursion_miss.rmiss.spv";
        shaderFiles[2] = L"shaders/no-recursion_shadowMiss.rmiss.spv";
        shaderFiles[3] = L"shaders/no-recursion_chitPlane.rchit.spv";
        shaderFiles[4] = L"shaders/no-recursion_chitSphere.rchit.spv";
    }

    // レイトレーシングのシェーダーを読み込む.
    m_shaderGroupHelper.LoadShader(m_device, "rgs", shaderFiles[0].c_str());
    m_shaderGroupHelper.LoadShader(m_device, "miss", shaderFiles[1].c_str());
    m_shaderGroupHelper.LoadShader(m_device, "shadowMiss", shaderFiles[2].c_str());
    m_shaderGroupHelper.LoadShader(m_device, "rchitPlane", shaderFiles[3].c_str());
    m_shaderGroupHelper.LoadShader(m_device, "rchitSphere", shaderFiles[4].c_str());

    // シェーダーグループを構成する.
    m_shaderGroupHelper.AddShaderGroupRayGeneration(AppHitShaderGroups::GroupRgs, "rgs");
    m_shaderGroupHelper.AddShaderGroupMiss(AppHitShaderGroups::GroupMiss, "miss");
    m_shaderGroupHelper.AddShaderGroupMiss(AppHitShaderGroups::GroupMissShadow, "shadowMiss");
    m_shaderGroupHelper.AddShaderGroupHit(AppHitShaderGroups::GroupHitPlane, "rchitPlane");
    m_shaderGroupHelper.AddShaderGroupHit(AppHitShaderGroups::GroupHitSphere, "rchitSphere");

    // レイトレーシングパイプラインの生成.
    VkRayTracingPipelineCreateInfoKHR rtPipelineCI{};

    rtPipelineCI.sType = VK_STRUCTURE_TYPE_RAY_TRACING_PIPELINE_CREATE_INFO_KHR;
    rtPipelineCI.stageCount = m_shaderGroupHelper.GetShaderStagesCount();
    rtPipelineCI.pStages = m_shaderGroupHelper.GetShaderStages();
    rtPipelineCI.groupCount = m_shaderGroupHelper.GetShaderGroupsCount();
    rtPipelineCI.pGroups = m_shaderGroupHelper.GetShaderGroups();
    rtPipelineCI.maxPipelineRayRecursionDepth = m_useNoRecursiveRaytrace ? 1 : 2;
    rtPipelineCI.layout = m_pipelineLayout;

    vkCreateRayTracingPipelinesKHR(
        m_device->GetDevice(), VK_NULL_HANDLE, VK_NULL_HANDLE,
        1, &rtPipelineCI, nullptr, &m_raytracePipeline);
}

void ShadowScene::CreateShaderBindingTable()
{
    auto memProps = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    auto usage = VK_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    m_sbtHelper.Setup(m_device, m_raytracePipeline);
    m_shaderGroupHelper.LoadShaderGroupHandles(m_device, m_raytracePipeline);

    // Raygeneration
    m_sbtHelper.AddRayGenerationShader(AppHitShaderGroups::GroupRgs, { });
    // Miss
    m_sbtHelper.AddMissShader(AppHitShaderGroups::GroupMiss, { });
    m_sbtHelper.AddMissShader(AppHitShaderGroups::GroupMissShadow, { });

    // Hit
    uint32_t recordOffset = 0;
    for (auto& obj : m_sceneObjects) {
        auto name = obj->GetHitShader();
        obj->SetHitShaderOffset(recordOffset);
        for (const auto& v : obj->GetSceneObjectParameters()) {
            std::vector<uint64_t> recordData{
                v.indexBuffer,
                v.vertexPosition
            };
            m_sbtHelper.AddHitShader(name.c_str(), recordData);
            recordOffset++;
        }
    }
    auto sbtSize = m_sbtHelper.ComputeShaderBindingTableSize();
    m_shaderBindingTable = m_device->CreateBuffer(
        sbtSize, usage, memProps);

    // 登録したエントリを書き込む.
    m_sbtHelper.Build(m_device, m_shaderBindingTable, m_shaderGroupHelper);
}

void ShadowScene::CreateLayouts()
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
    layoutTextures.descriptorCount = m_materialManager.GetTextureCount();
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

void ShadowScene::CreateDescriptorSets()
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
    texturesImageWrite.dstBinding = 6;
    texturesImageWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    texturesImageWrite.descriptorCount = uint32_t(textureDescriptors.size());
    texturesImageWrite.pImageInfo = textureDescriptors.data();

    vkUpdateDescriptorSets(
        m_device->GetDevice(),
        1, &texturesImageWrite, 0, nullptr);
}

void ShadowScene::InitializeImGui()
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

void ShadowScene::DestroyImGui()
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

void ShadowScene::UpdateHUD()
{
    // ImGui
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("Information");
    ImGui::Text(m_device->GetDeviceName());
    if (m_useNoRecursiveRaytrace) {
        ImGui::Text("Mode: No Recursive Raytrace");
    } else {
        ImGui::Text("Mode: Recursive Raytrace");
    }
    auto framerate = ImGui::GetIO().Framerate;
    ImGui::Text("frametime %.3f ms", 1000.0f / framerate);
    ImGui::Checkbox("PointLightShadow", &m_guiParams.usePointLightShadow);
    ImGui::SliderInt("ShadowRayCount", &m_guiParams.shadowRayCount, 1, 5);
    ImGui::InputFloat3("PointLightPos", (float*)&m_guiParams.pointLightPosition);
    ImGui::SliderFloat("DistanceFactor", &m_guiParams.distanceFactor, 1.0f, 10.0f);
    ImGui::End();

    m_sceneParam.shaderFlags.y = m_guiParams.usePointLightShadow != 0 ? 1 : 0;
    m_sceneParam.shaderFlags.z = m_guiParams.shadowRayCount;

}

void ShadowScene::UpdateSceneTLAS()
{
    auto command = m_device->GetCurrentFrameCommandBuffer();
    // VkAccelerationStructureInstanceKHR 配列を取得して書き込む.
    auto asInstances = CreateAccelerationStructureIncenceFromSceneObjects();
    auto instancesBufferSize = sizeof(VkAccelerationStructureInstanceKHR) * asInstances.size();
    auto frameIndex = m_device->GetCurrentFrameIndex();
    memcpy(m_instancesBuffer.Map(frameIndex), asInstances.data(), instancesBufferSize);

    VkDeviceOrHostAddressConstKHR instanceDataDeviceAddress{};
    instanceDataDeviceAddress.deviceAddress = m_instancesBuffer.GetDeviceAddress(frameIndex);

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

    VkBuildAccelerationStructureFlagsKHR buildFlags = 0;
    buildFlags |= VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;
    buildFlags |= VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    m_topLevelAS.Update(
        command,
        VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        tlasInput,
        buildFlags
    );
}

void ShadowScene::DeployObjects()
{
    // 床を配置.
    m_meshPlane->SetWorldMatrix(glm::mat4(1.0f));

    // Sphereを配置.
    std::mt19937 mt;
    std::uniform_int_distribution rnd(-9, 9);

    for (int i = 0; i < SphereCount; ++i) {
        auto pos = glm::vec3(rnd(mt) + 0.5f, 0.5f, rnd(mt) + 0.5f);
        m_meshSpheres[i]->SetWorldMatrix(glm::translate(pos));
    }

    // ライト位置.
    auto lightPos = m_guiParams.pointLightPosition;
    lightPos *= m_guiParams.distanceFactor;
    m_meshLightSphere->SetWorldMatrix(glm::translate(lightPos));
    m_meshLightSphere->SetMask(PointLightMask);
}

void ShadowScene::CreateSceneList()
{
    m_sceneObjects.clear();
    // 床.
    m_sceneObjects.push_back(m_meshPlane);

    // ライト.
    m_sceneObjects.push_back(m_meshLightSphere);

    // 球体.
    for (auto& s : m_meshSpheres) {
        m_sceneObjects.push_back(s);
    }
}

void ShadowScene::CreateSceneBuffers()
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

std::vector<VkAccelerationStructureInstanceKHR> ShadowScene::CreateAccelerationStructureIncenceFromSceneObjects()
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
