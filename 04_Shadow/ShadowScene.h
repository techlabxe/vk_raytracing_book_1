#pragma once

#include "BookFramework.h"
#include <glm/glm.hpp>

#include "scene/SimplePolygonMesh.h"
#include "MaterialManager.h"
#include "ShaderGroupHelper.h"
#include "Camera.h"

// 使用可能なヒットシェーダーのインデックス値.
namespace AppHitShaderGroups {
    static const char* GroupRgs = "groupRayGen";
    static const char* GroupMiss = "groupMiss";
    static const char* GroupMissShadow = "groupMissShadow";
    static const char* GroupHitPlane = "groupHitPlane";
    static const char* GroupHitSphere = "groupHitSphere";
}

class ShadowScene : public BookFramework {
public:
    ShadowScene() : BookFramework("ShadowScene") {}

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

    struct ShaderBindingTableInfo {
        VkStridedDeviceAddressRegionKHR rgen = { };
        VkStridedDeviceAddressRegionKHR miss = { };
        VkStridedDeviceAddressRegionKHR hit = { };
    };
    struct SceneParam
    {
        glm::mat4 mtxView;
        glm::mat4 mtxProj;
        glm::mat4 mtxViewInv;
        glm::mat4 mtxProjInv;
        glm::vec4 lightDirection;
        glm::vec4 lightColor;
        glm::vec4 ambientColor;
        glm::vec3 cameraPosition;
        int frameIndex;
        //
        glm::vec4 pointLightPosition;
        glm::uvec4 shaderFlags = glm::uvec4(0);
    };

    // マテリアル種類.
    enum class MaterialType {
        LAMBERT = 0,
        EMISSIVE,
        MATERIAL_KIND_MAX
    };

    // 各インスタンスごとの情報.
    struct ObjectParam
    {
        uint64_t addressIndexBuffer;
        uint64_t addressVertexBuffer;
        uint32_t materialIndex;
        uint32_t padding0 = 0;
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
    std::shared_ptr<SimplePolygonMesh> m_meshLightSphere;
    std::vector<std::shared_ptr<SimplePolygonMesh>> m_meshSpheres;

    util::DynamicBuffer m_instancesBuffer;
    AccelerationStructure m_topLevelAS;
    VkDescriptorSetLayout m_dsLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    vk::ImageResource   m_raytracedImage;

    VkPipeline m_raytracePipeline;
    VkDescriptorSet m_descriptorSet;

    vk::BufferResource  m_shaderBindingTable;

    SceneParam m_sceneParam;

    util::DynamicBuffer m_sceneUBO;
    vk::BufferResource  m_materialsSBO;
    vk::BufferResource  m_objectsSBO;

    Camera m_camera;

    // ラスタライズ描画用.
    VkRenderPass m_renderPass;
    std::vector<VkFramebuffer > m_framebuffers;

    const int SphereCount = 12;
    const int MaxTextureCount = 65536;
    std::vector<std::shared_ptr<SceneObject>> m_sceneObjects;
    MaterialManager m_materialManager;

    const uint32_t PointLightMask = 0x01;

    struct GUIParams {
        glm::vec3 pointLightPosition = { 0.0f, 6.0f, 2.5f };
        int  shadowRayCount = 1;
        float distanceFactor = 1.0f;
        bool usePointLightShadow = false;
    } m_guiParams;

    util::ShaderGroupHelper m_shaderGroupHelper;
    util::ShaderBindingTableHelper m_sbtHelper;

    bool m_useNoRecursiveRaytrace;
};
