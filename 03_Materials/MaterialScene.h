#pragma once

#include "BookFramework.h"
#include <glm/glm.hpp>

#include "AccelerationStructure.h"
#include "Camera.h"

// �g�p�\�ȃq�b�g�V�F�[�_�[�̃C���f�b�N�X�l.
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
    // �V�[���ɔz�u����W�I���g�����������܂�.
    void CreateSceneGeometries();

    // �e�W�I���g���� BLAS ���\�z���܂�.
    void CreateSceneBLAS();

    // BLAS �𑩂˂ăV�[���� TLAS ���\�z���܂�.
    void CreateSceneTLAS();

    // ���C�g���[�V���O���ʏ������ݗp�o�b�t�@���������܂�.
    void CreateRaytracedBuffer();

    // ���C�g���[�V���O�p�C�v���C�����\�z���܂�.
    void CreateRaytracePipeline();

    // ���C�g���[�V���O�Ŏg�p���� ShaderBindingTable ���\�z���܂�.
    void CreateShaderBindingTable();

    // ���C�A�E�g�̍쐬.
    void CreateLayouts();

    // �f�B�X�N���v�^�Z�b�g�̏����E��������.
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

    // �}�e���A�����.
    enum MaterialKind {
        LAMBERT = 0,
        METAL,
        GLASS,
        MATERIAL_KIND_MAX
    };
    // �}�e���A�����.
    struct Material
    {
        glm::vec4   diffuse = glm::vec4(1.0f);
        glm::vec4   specular = glm::vec4(1.0f, 1.0f, 1.0f, 20.0f);
        int32_t  materialKind = LAMBERT;
        int32_t  textureIndex = -1;
        int32_t  padding0[2] = { 0 };
    };
    // �e�C���X�^���X���Ƃ̏��.
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
    // �I�u�W�F�N�g�̈ʒu�����Z�b�g.
    void DeployObjects();

    // �z�u�����I�u�W�F�N�g����V�[���ɓo�^.
    void CreateSceneList();
    
    // �V�[���S�̂Ŏg���I�u�W�F�N�g�o�b�t�@�A�}�e���A���o�b�t�@������.
    void CreateSceneBuffers();

    // �V�[���ɔz�u�����I�u�W�F�N�g��񂩂�,
    // TLAS �ɕK�v�� VkAccelerationStructureInstanceKHR�z��𐶐�����.
    std::vector<VkAccelerationStructureInstanceKHR> CreateAccelerationStructureIncenceFromSceneObjects();
private:
    // �W�I���g�����.
    PolygonMesh m_meshPlane;
    PolygonMesh m_meshSphere;

    // �V�[���z�u�C���X�^���X.
    SceneObject m_floor;
    std::vector<SceneObject> m_spheres;

    vk::BufferResource  m_instancesBuffer;
    AccelerationStructure m_topLevelAS;
    VkDescriptorSetLayout m_dsLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    vk::ImageResource   m_raytracedImage;

    VkPipeline m_raytracePipeline;
    VkDescriptorSet m_descriptorSet;

    // �V�F�[�_�[�O���[�v(m_shaderGroups)�ɑ΂��A���̏ꏊ�Ŋe�V�F�[�_�[��o�^����.
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

    // �e�N�X�`��ID
    enum TextureID {
        TexID_Floor = 0,
        TexID_Sphere,
    };

    // �V�[���Ŏg�p����e�N�X�`���W��.
    std::vector<vk::ImageResource> m_textures;

    // �V�[�����\������C���X�^���X�̏W��.
    std::vector<SceneObject> m_sceneObjects;

    vk::ImageResource m_cubemap;
    VkSampler m_cubemapSampler = VK_NULL_HANDLE;
    VkSampler m_defaultSampler = VK_NULL_HANDLE;
    Camera m_camera;

    // ���X�^���C�Y�`��p.
    VkRenderPass m_renderPass;
    std::vector<VkFramebuffer > m_framebuffers;

    const int SphereCount = 36;
    bool m_useNoRecursiveRT = false;
};
