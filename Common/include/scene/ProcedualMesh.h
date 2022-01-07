#pragma once

#include "scene/SceneObject.h"
#include "MaterialManager.h"

class ProcedualMesh : public SceneObject {
public:
    struct CreateInfo {
        uint32_t stride = 0;
        const void* data = nullptr;
        size_t   size = 0;

        std::shared_ptr<Material> material;
    };
    void Create(VkGraphicsDevice& device, const CreateInfo& createInfo, MaterialManager& materialManager);

    virtual void Destroy(std::unique_ptr<vk::GraphicsDevice>& device) override
    {
        device->DestroyBuffer(aabbBuffer);
        m_blas.Destroy(device);
    }

    void BuildAS(VkGraphicsDevice& device, VkBuildAccelerationStructureFlagsKHR buildFlags = 0);

    virtual std::vector<VkAccelerationStructureGeometryKHR> GetAccelerationStructureGeometry(int frameIndex=0) override;
    virtual std::vector<VkAccelerationStructureBuildRangeInfoKHR> GetAccelerationStructureBuildRangeInfo() override;

    virtual uint64_t GetDeviceAddressVB() const { return aabbBuffer.GetDeviceAddress(); }

    virtual std::vector<SceneObjectParameter> GetSceneObjectParameters();

    std::shared_ptr<Material> GetMaterial() const { return m_material; }

    virtual int GetSubMeshCount() const { return 1; }
public:
    vk::BufferResource aabbBuffer;
    uint32_t dataStride;
    std::shared_ptr<Material> m_material = nullptr;
    int m_materialIndex = -1;
};


