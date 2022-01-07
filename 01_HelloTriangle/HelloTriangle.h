#pragma once

#include "BookFramework.h"
#include <glm/glm.hpp>

class HelloTriangle : public BookFramework {
public:
    HelloTriangle() : BookFramework("HelloTriangle") {};

    struct Vertex {
        glm::vec3 Position;
    };
protected:
    void OnInit() override;
    void OnDestroy() override;

    void OnUpdate() override;
    void OnRender() override;

private:
    // 3�p�`�f�[�^�ɑ΂��� BLAS ���\�z���܂�.
    void CreateTriangleBLAS();

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

    struct AccelerationStructure {
        VkAccelerationStructureKHR handle = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkBuffer buffer = VK_NULL_HANDLE;
        uint64_t deviceAddress = 0;
    };
    AccelerationStructure CreateAccelerationStructureBuffer(VkAccelerationStructureBuildSizesInfoKHR buildSizeInfo);
    void DestroyAcceleratioStructureBuffer(AccelerationStructure& as);

    struct RayTracingScratchBuffer {
        VkBuffer handle = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        uint64_t deviceAddress = 0;
    };
    // �X�N���b�`�o�b�t�@���m�ۂ���.
    RayTracingScratchBuffer CreateScratchBuffer(VkDeviceSize size);

private:
    vk::BufferResource  m_vertexBuffer;
    vk::BufferResource  m_instancesBuffer;
    AccelerationStructure m_bottomLevelAS;
    AccelerationStructure m_topLevelAS;
    VkDescriptorSetLayout m_dsLayout = VK_NULL_HANDLE;
    VkPipelineLayout      m_pipelineLayout = VK_NULL_HANDLE;
    vk::ImageResource     m_raytracedImage;

    VkPipeline  m_raytracePipeline;
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

    VkStridedDeviceAddressRegionKHR m_regionRaygen;
    VkStridedDeviceAddressRegionKHR m_regionMiss;
    VkStridedDeviceAddressRegionKHR m_regionHit;
};