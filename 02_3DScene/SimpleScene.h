#pragma once

#include "BookFramework.h"
#include <glm/glm.hpp>

#include "AccelerationStructure.h"
#include "Camera.h"

// 使用可能なヒットシェーダーのインデックス値.
namespace AppHitShaderGroups {
    const uint32_t PlaneHitShader = 0;
    const uint32_t CubeHitShader = 1;
}

struct PolygonMesh {
    vk::BufferResource vertexBuffer;
    vk::BufferResource indexBuffer;

    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t vertexStride = 0;

    // BLAS.
    AccelerationStructure blas;

    // 使用するヒットシェーダーのインデックス値.
    uint32_t hitShaderIndex = 0;
};


class SimpleScene : public BookFramework {
public:
    SimpleScene() : BookFramework("SimpleScene") {}

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

    // シーンにオブジェクトを配置する.
    void DeployObjects(std::vector<VkAccelerationStructureInstanceKHR>& instances);

    // ヒットシェーダーのSBTDataを書き込む.
    void WriteSBTDataForHitShader(void* dst, const PolygonMesh& mesh);

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
    };

private:
    // ジオメトリ情報.
    PolygonMesh m_meshPlane;
    PolygonMesh m_meshCube;
    vk::BufferResource  m_instancesBuffer;
    AccelerationStructure m_topLevelAS;
    VkDescriptorSetLayout m_dsLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    vk::ImageResource   m_raytracedImage;

    VkPipeline m_raytracePipeline;
    VkDescriptorSet m_descriptorSet;

    // シェーダーグループ(m_shaderGroups)に対し、この場所で各シェーダーを登録する.
    enum ShaderGroups {
        GroupRayGenShader = 0,
        GroupMissShader = 1,
        GroupHitShader = 2,
        MaxShaderGroup
    };
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> m_shaderGroups;
    vk::BufferResource  m_shaderBindingTable;
    ShaderBindingTableInfo m_sbtInfo;

    SceneParam m_sceneParam;
    util::DynamicBuffer m_sceneUBO;
    Camera m_camera;

    // ラスタライズ描画用.
    VkRenderPass m_renderPass;
    std::vector<VkFramebuffer > m_framebuffers;
};
