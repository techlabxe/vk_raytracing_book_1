#pragma once

#include "BookFramework.h"
#include <glm/glm.hpp>

#include "AccelerationStructure.h"
#include "Camera.h"

// 使用可能なヒットシェーダーのインデックス値.
namespace AppHitShaderGroups {
    const uint32_t PlaneHitShader = 0;
    const uint32_t SphereHitShader = 1;
}

struct PolygonMesh {
    vk::BufferResource vertexBuffer;
    vk::BufferResource indexBuffer;

    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t vertexStride = 0;

    // BLAS.
    AccelerationStructure blas;
};


class MaterialScene : public BookFramework {
public:
    MaterialScene() : BookFramework("MaterialScene") {}

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
    };

    // マテリアル種類.
    enum MaterialKind {
        LAMBERT = 0,
        METAL,
        GLASS,
        MATERIAL_KIND_MAX
    };
    // マテリアル情報.
    struct Material
    {
        glm::vec4   diffuse = glm::vec4(1.0f);
        glm::vec4   specular = glm::vec4(1.0f, 1.0f, 1.0f, 20.0f);
        int32_t  materialKind = LAMBERT;
        int32_t  textureIndex = -1;
        int32_t  padding0[2] = { 0 };
    };
    // 各インスタンスごとの情報.
    struct ObjectParam
    {
        uint64_t addressIndexBuffer;
        uint64_t addressVertexBuffer;
        uint32_t materialIndex;
        uint32_t padding0 = 0;
        uint64_t padding1 = 0;
    };

    struct SceneObject {
        PolygonMesh* meshRef = nullptr;
        glm::mat4    transform = glm::mat4(1.0f);
        Material material;

        uint32_t sbtOffset = 0;
        uint32_t customIndex = 0;
    };
    // オブジェクトの位置情報をセット.
    void DeployObjects();

    // 配置したオブジェクトからシーンに登録.
    void CreateSceneList();
    
    // シーン全体で使うオブジェクトバッファ、マテリアルバッファを準備.
    void CreateSceneBuffers();

    // シーンに配置したオブジェクト情報から,
    // TLAS に必要な VkAccelerationStructureInstanceKHR配列を生成する.
    std::vector<VkAccelerationStructureInstanceKHR> CreateAccelerationStructureIncenceFromSceneObjects();
private:
    // ジオメトリ情報.
    PolygonMesh m_meshPlane;
    PolygonMesh m_meshSphere;

    // シーン配置インスタンス.
    SceneObject m_floor;
    std::vector<SceneObject> m_spheres;

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
        GroupPlaneHit = 2,
        GroupSphereHit=3,
        MaxShaderGroup
    };
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> m_shaderGroups;
    vk::BufferResource  m_shaderBindingTable;
    ShaderBindingTableInfo m_sbtInfo;

    SceneParam m_sceneParam;

    util::DynamicBuffer m_sceneUBO;
    vk::BufferResource  m_materialsSBO;
    vk::BufferResource  m_objectsSBO;

    // テクスチャID
    enum TextureID {
        TexID_Floor = 0,
        TexID_Sphere,
    };

    // シーンで使用するテクスチャ集合.
    std::vector<vk::ImageResource> m_textures;

    // シーンを構成するインスタンスの集合.
    std::vector<SceneObject> m_sceneObjects;

    vk::ImageResource m_cubemap;
    VkSampler m_cubemapSampler = VK_NULL_HANDLE;
    VkSampler m_defaultSampler = VK_NULL_HANDLE;
    Camera m_camera;

    // ラスタライズ描画用.
    VkRenderPass m_renderPass;
    std::vector<VkFramebuffer > m_framebuffers;

    const int SphereCount = 36;
    bool m_useNoRecursiveRT = false;
};
