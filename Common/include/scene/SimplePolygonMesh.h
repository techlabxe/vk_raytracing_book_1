#pragma once

#include "scene/SceneObject.h"
#include "MaterialManager.h"

class SimplePolygonMesh : public SceneObject {
public:
    struct CreateInfo {
        const void* srcVertices = nullptr;
        const void* srcIndices = nullptr;
        uint64_t vbSize = 0;
        uint64_t ibSize = 0;

        uint32_t stride = 0;
        uint32_t vertexCount = 0;
        uint32_t indexCount = 0;

        std::shared_ptr<Material> material;
    };
    void Create(VkGraphicsDevice& device, const CreateInfo& createInfo, MaterialManager& materialManager);

    virtual void Destroy(VkGraphicsDevice& device) override
    {
        device->DestroyBuffer(vertexBuffer);
        device->DestroyBuffer(indexBuffer);
        m_blas.Destroy(device);
    }

    void BuildAS(VkGraphicsDevice& device, VkBuildAccelerationStructureFlagsKHR buildFlags = 0);

    virtual std::vector<VkAccelerationStructureGeometryKHR> GetAccelerationStructureGeometry(int frameIndex=0) override;
    virtual std::vector<VkAccelerationStructureBuildRangeInfoKHR> GetAccelerationStructureBuildRangeInfo() override;

    virtual uint64_t GetDeviceAddressVB() const { return vertexBuffer.GetDeviceAddress(); }
    virtual uint64_t GetDeviceAddressIB() const { return indexBuffer.GetDeviceAddress(); }

    virtual std::vector<SceneObjectParameter> GetSceneObjectParameters();
    virtual int GetSubMeshCount() const { return 1; }

    uint32_t GetVertexCount() const { return vertexCount; }
    uint32_t GetIndexCount() const { return indexCount; }

    std::shared_ptr<Material> GetMaterial() const { return m_material; }
public:
    vk::BufferResource vertexBuffer;
    vk::BufferResource indexBuffer;

    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t vertexStride = 0;

    std::shared_ptr<Material> m_material = nullptr;
    int m_materialIndex = -1;
};

#if 0
class ProcedualMesh : public SceneObject {
public:
    struct CreateInfo {
        uint32_t stride = 0;
        const void* data = nullptr;
        size_t   size = 0;
    };
    void Create(VkGraphicsDevice& device, const CreateInfo& createInfo);

    virtual void Destroy(std::unique_ptr<vk::GraphicsDevice>& device) override
    {
        device->DestroyBuffer(aabbBuffer);
        m_blas.Destroy(device);
    }

    void BuildAS(VkGraphicsDevice& device, VkBuildAccelerationStructureFlagsKHR buildFlags = 0);

    virtual std::vector<VkAccelerationStructureGeometryKHR> GetAccelerationStructureGeometry();
    virtual std::vector<VkAccelerationStructureBuildRangeInfoKHR> GetAccelerationStructureBuildRangeInfo();

    virtual uint64_t GetDeviceAddressVB() const { return aabbBuffer.GetDeviceAddress(); }

    virtual std::vector<SceneObjectParameter> GetSceneObjectParameters();

    void SetMaterial(std::shared_ptr<Material> material) { m_material = material; }
    std::shared_ptr<Material> GetMaterial() const { return m_material; }

    virtual int GetBlasCount() const { return 1; }
public:
    vk::BufferResource aabbBuffer;
    uint32_t dataStride;
    std::shared_ptr<Material> m_material = nullptr;
};


#endif