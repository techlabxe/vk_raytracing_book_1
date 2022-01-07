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
    // 3角形データに対する BLAS を構築します.
    void CreateTriangleBLAS();

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
    // スクラッチバッファを確保する.
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

    // シェーダーグループ(m_shaderGroups)に対し、この場所で各シェーダーを登録する.
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