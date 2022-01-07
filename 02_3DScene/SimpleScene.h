#pragma once

#include "BookFramework.h"
#include <glm/glm.hpp>

#include "AccelerationStructure.h"
#include "Camera.h"

// �g�p�\�ȃq�b�g�V�F�[�_�[�̃C���f�b�N�X�l.
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

    // �g�p����q�b�g�V�F�[�_�[�̃C���f�b�N�X�l.
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

    // �V�[���ɃI�u�W�F�N�g��z�u����.
    void DeployObjects(std::vector<VkAccelerationStructureInstanceKHR>& instances);

    // �q�b�g�V�F�[�_�[��SBTData����������.
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
    // �W�I���g�����.
    PolygonMesh m_meshPlane;
    PolygonMesh m_meshCube;
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
        GroupHitShader = 2,
        MaxShaderGroup
    };
    std::vector<VkRayTracingShaderGroupCreateInfoKHR> m_shaderGroups;
    vk::BufferResource  m_shaderBindingTable;
    ShaderBindingTableInfo m_sbtInfo;

    SceneParam m_sceneParam;
    util::DynamicBuffer m_sceneUBO;
    Camera m_camera;

    // ���X�^���C�Y�`��p.
    VkRenderPass m_renderPass;
    std::vector<VkFramebuffer > m_framebuffers;
};
