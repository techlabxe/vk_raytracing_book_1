#pragma once

#include "BookFramework.h"
#include <glm/glm.hpp>

#include "scene/SimplePolygonMesh.h"
#include "MaterialManager.h"
#include "ShaderGroupHelper.h"
#include "Camera.h"

// �g�p�\�ȃq�b�g�V�F�[�_�[�̃C���f�b�N�X�l.
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

    // �}�e���A�����.
    enum class MaterialType {
        LAMBERT = 0,
        EMISSIVE,
        MATERIAL_KIND_MAX
    };

    // �e�C���X�^���X���Ƃ̏��.
    struct ObjectParam
    {
        uint64_t addressIndexBuffer;
        uint64_t addressVertexBuffer;
        uint32_t materialIndex;
        uint32_t padding0 = 0;
    };

    // �V�[���ɃI�u�W�F�N�g��z�u����.
    void DeployObjects();

    // �z�u�����I�u�W�F�N�g����V�[�����\������.
    void CreateSceneList();
    
    // �V�[���S�̗p�̃o�b�t�@����������.
    void CreateSceneBuffers();

    // �V�[���ɔz�u�����I�u�W�F�N�g��񂩂�,
    // TLAS �ɕK�v�� VkAccelerationStructureInstanceKHR�z��𐶐�����.
    std::vector<VkAccelerationStructureInstanceKHR> CreateAccelerationStructureIncenceFromSceneObjects();
private:
    // �W�I���g�����.
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

    // ���X�^���C�Y�`��p.
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
