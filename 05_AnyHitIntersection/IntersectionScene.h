#pragma once

#include "BookFramework.h"
#include <glm/glm.hpp>

#include "AccelerationStructure.h"
#include "Camera.h"

#include "scene/SimplePolygonMesh.h"
#include "scene/ProcedualMesh.h"
#include "ShaderGroupHelper.h"
#include "MaterialManager.h"

// 使用可能なヒットシェーダーの名前.
namespace AppHitShaderGroups {
    static const char* GroupRgs = "groupRayGen";
    static const char* GroupMiss = "groupMiss";
    static const char* GroupMissShadow = "groupMissShadow";
    static const char* GroupHitPlane = "groupHitPlane";
    static const char* GroupHitFence = "groupHitFence";
    static const char* GroupHitAnalytic = "groupHitAnalytic";
    static const char* GroupHitSdf = "groupHitSdf";
}


class IntersectionScene : public BookFramework {
public:
    IntersectionScene() : BookFramework("Intersection and AnyHit Scene") {}

protected:
    void OnInit() override;
    void OnDestroy() override;

    void OnUpdate() override;
    void OnRender() override;

    void OnMouseDown(MouseButton button) override;
    void OnMouseUp(MouseButton button) override;
    void OnMouseMove() override;

private:
    // シーンに配置するジオメトリを準備します.
    void CreateSceneGeometries();

    // 各ジオメトリの BLAS を構築します.
    void CreateSceneBLAS();

    // BLAS を束ねてシーンの TLAS を構築します.
    void CreateSceneTLAS();

    // レイトレーシング結果書き込み用バッファを準備します.
    void CreateRaytracedBuffer();

    // レイトレーシングパイプラインを構築します.
    void CreateRaytracePipeline();

    // レイトレーシングで使用する ShaderBindingTable を構築します.
    void CreateShaderBindingTable();

    // レイアウトの作成.
    void CreateLayouts();

    // ディスクリプタセットの準備・書き込み.
    void CreateDescriptorSets();

    // ImGui
    void InitializeImGui();
    void DestroyImGui();

    void UpdateHUD();
    void UpdateSceneTLAS();

    struct SceneParam
    {
        glm::mat4 mtxView;
        glm::mat4 mtxProj;
        glm::mat4 mtxViewInv;
        glm::mat4 mtxProjInv;
        glm::vec4 lightDirection;
        glm::vec4 lightColor;
        glm::vec4 ambientColor;
    };

    enum class MaterialType {
        LAMBERT = 0,
    };

    // 各インスタンスごとの情報.
    struct ObjectParam
    {
        int32_t materialIndex = -1;
        uint32_t padding0 = 0;
        uint32_t padding1 = 0;
        uint32_t padding2 = 0;
    };

    enum class AABBTypes : uint32_t {
        CUBE = 0,
        SPHERE,
    };
    enum class SDFTypes : uint32_t {
        CUBE = 0,
        SPHERE,
        TORUS,
    };


    // シーンにオブジェクトを配置する.
    void DeployObjects();

    // 配置したオブジェクトからシーンを構成する.
    void CreateSceneList();
    
    // シーン全体用のバッファを準備する.
    void CreateSceneBuffers();

    // シーンに配置したオブジェクト情報から,
    // TLAS に必要な VkAccelerationStructureInstanceKHR配列を生成する.
    std::vector<VkAccelerationStructureInstanceKHR> CreateAccelerationStructureIncenceFromSceneObjects();
private:
    // ジオメトリ情報.
    std::shared_ptr<SimplePolygonMesh> m_meshPlane;
    std::shared_ptr<SimplePolygonMesh> m_meshFence;
    std::shared_ptr<ProcedualMesh> m_meshAABB;
    std::shared_ptr<ProcedualMesh> m_meshAABBSDF;

    util::DynamicBuffer  m_instancesBuffer;
    AccelerationStructure m_topLevelAS;
    VkDescriptorSetLayout m_dsLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    vk::ImageResource   m_raytracedImage;

    VkPipeline m_raytracePipeline;
    VkDescriptorSet m_descriptorSet;

    // シェーダーグループ(m_shaderGroups)に対し、この場所で各シェーダーを登録する.
    enum ShaderGroups {
        GroupRayGenShader = 0,
        GroupMiss,
        GroupPlaneHit,
        GroupShadowMiss,
        GroupHitFence,
        GroupHitAABB,
        GroupHitSDF,
        MaxShaderGroup
    };
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> m_shaderGroups;
    vk::BufferResource  m_shaderBindingTable;

    SceneParam m_sceneParam;
    struct AnalyticGeometryParam
    {
        glm::vec3 diffuse = glm::vec3(1.0f);
        AABBTypes  objectType = AABBTypes::CUBE;
    } m_analyticGeomParam;
    struct SDFGeometryParam {
        glm::vec3 diffuse = glm::vec3(0.2f, 0.4f, 1.0f);
        SDFTypes objectType = SDFTypes::CUBE;
        glm::vec3 extent = glm::vec3(0.25f, 0.5f, 0.4f);
        float radius = 0.4f;
    } m_sdfGeomParam;

  
    util::DynamicBuffer m_sceneUBO;
    vk::BufferResource  m_materialsSBO;
    vk::BufferResource  m_objectsSBO;

    util::DynamicBuffer m_analyticUBO;
    util::DynamicBuffer m_sdfUBO;

    vk::ImageResource m_cubemap;
    VkSampler m_cubemapSampler = VK_NULL_HANDLE;
    VkSampler m_defaultSampler = VK_NULL_HANDLE;
    Camera m_camera;

    // ラスタライズ描画用.
    VkRenderPass m_renderPass;
    std::vector<VkFramebuffer > m_framebuffers;

    const int MaxTextureCount = 65536;
    std::vector<std::shared_ptr<SceneObject>> m_sceneObjects;
    MaterialManager m_materialManager;

    struct GUIParams {
        int  aabbObjectType = static_cast<int>(AABBTypes::CUBE);
        int  sdfObjectType = static_cast<int>(SDFTypes::CUBE);
        glm::vec3 sdfBoxExtent = { 0.25f, 0.5f, 0.4f };
        float sdfRadius = 0.4f;
    } m_guiParams;

    util::ShaderGroupHelper m_shaderGroupHelper;
    util::ShaderBindingTableHelper m_sbtHelper;
};
