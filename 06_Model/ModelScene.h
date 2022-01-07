#pragma once

#include "BookFramework.h"
#include <glm/glm.hpp>

#include "AccelerationStructure.h"
#include "Camera.h"

#include "ShaderGroupHelper.h"
#include "scene/SceneObject.h"
#include "util/VkrModel.h"

#include "MaterialManager.h"
#include "scene/SimplePolygonMesh.h"
#include "scene/ModelMesh.h"

// 使用可能なヒットシェーダーの名前.
namespace AppHitShaderGroups {
    static const char* GroupRgs = "groupRayGen";
    static const char* GroupMiss = "groupMiss";
    static const char* GroupMissShadow = "groupMissShadow";
    static const char* GroupHitPlane = "groupHitPlane";
    static const char* GroupHitModel = "groupHitModel";
}

class ModelScene : public BookFramework {
public:
    ModelScene() : BookFramework("Model Scene") {}

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

    // スキニング計算用のパイプラインを構築します.
    void CreateComputeSkinningPipeline();

    // レイトレーシングで使用する ShaderBindingTable を構築します.
    void CreateShaderBindingTable();

    // レイアウトの作成.
    void CreateLayouts();

    // ディスクリプタセットの準備・書き込み.
    void CreateDescriptorSets();

    // スキニングモデル用のディスクリプタセットの準備・書き込み.
    void CreateDescriptorSetsSkinned();

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
        glm::vec3 cameraPosition;
        uint32_t  frameIndex;
    };
    struct ObjectParam
    {
        int materialIndex = -1;
        int blasMatrixIndex = 0;
        int blasMatrixStride = 0;
        int padd0 = 0;
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

    enum class MaterialType {
        LAMBERT = 0,
        PHONG = 1,
    };
private:
    util::DynamicBuffer  m_instancesBuffer;
    AccelerationStructure m_topLevelAS;

    VkDescriptorSetLayout m_dsLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_dsLayoutSkinned = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayoutSkinned = VK_NULL_HANDLE;
    vk::ImageResource   m_raytracedImage;

    VkPipeline m_raytracePipeline;
    VkPipeline m_computeSkiningPipeline;
    VkDescriptorSet m_descriptorSet;
    VkDescriptorSet m_descriptorSetCompute;

    vk::BufferResource  m_shaderBindingTable;

    SceneParam m_sceneParam;

    util::DynamicBuffer m_sceneUBO;
    vk::BufferResource  m_materialsSBO;
    vk::BufferResource  m_objectsSBO;

    Camera m_camera;

    // ラスタライズ描画用.
    VkRenderPass m_renderPass;
    std::vector<VkFramebuffer > m_framebuffers;

    // シーン描画.
    std::vector<std::shared_ptr<SceneObject>> m_sceneObjects;


    util::VkrModel m_modelTable;
    util::VkrModel m_modelTeapot;
    util::VkrModel m_modelChara;

    std::shared_ptr<SimplePolygonMesh> m_meshPlane;
    std::shared_ptr<ModelMesh> m_actorTable;
    std::shared_ptr<ModelMesh> m_actorTeapot0;
    std::shared_ptr<ModelMesh> m_actorTeapot1;
    std::shared_ptr<ModelMesh> m_actorChara;

    struct GUIParams {
        float elbowL = 0.0f;
        float elbowR = 0.0f;
        float neck = 0.0f;
    } m_guiParams;

    util::ShaderGroupHelper m_shaderGroupHelper;
    util::ShaderBindingTableHelper m_sbtHelper;

    MaterialManager m_materialManager;
};
