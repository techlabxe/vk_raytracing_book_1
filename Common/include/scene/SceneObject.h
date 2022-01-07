#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <cstdint>

#include "AccelerationStructure.h"

class Material;

class SceneObject {
public:
    using VkGraphicsDevice = std::unique_ptr<vk::GraphicsDevice>;

    void SetGometryFlags(VkGeometryFlagsKHR flags) { m_geometryFlags = flags; }
    VkAccelerationStructureKHR GetHandle() const { return m_blas.GetHandle(); }
    uint64_t GetBlasDeviceAddress() const { return m_blas.GetDeviceAddress(); }

    virtual void Destroy(std::unique_ptr<vk::GraphicsDevice>& device) = 0;
    virtual std::vector<VkAccelerationStructureGeometryKHR> GetAccelerationStructureGeometry(int frameIndex =0) = 0;
    virtual std::vector<VkAccelerationStructureBuildRangeInfoKHR> GetAccelerationStructureBuildRangeInfo() = 0;

    virtual uint64_t GetDeviceAddressVB() const { return 0; }
    virtual uint64_t GetDeviceAddressIB() const { return 0; }
    virtual int GetSubMeshCount() const = 0;

    struct SceneObjectParameter {
        uint64_t indexBuffer;
        uint64_t vertexPosition;

        uint64_t vertexNormal;
        uint64_t vertexTexcoord;

        uint64_t blasTransformMatrices;
        int blasMatrixIndex;
        int blasMatrixStride;

        int materialIndex;
    };
    virtual std::vector<SceneObjectParameter> GetSceneObjectParameters() = 0;

    virtual void SetWorldMatrix(glm::mat4 m);
    void SetHitShaderOffset(uint32_t offset);
    void SetMask(uint32_t mask);
    void SetCustomIndex(uint32_t customIndex);
    void SetGeometryInstanceFlags(VkGeometryInstanceFlagsKHR flags);

    VkAccelerationStructureInstanceKHR GetAccelerationStructureInstance() const;

    void SetHitShader(const std::string& name) { m_hitShaderName = name; }
    std::string GetHitShader()const { return m_hitShaderName; }
protected:
    // BLAS.
    AccelerationStructure m_blas;
    VkGeometryFlagsKHR m_geometryFlags = VK_GEOMETRY_OPAQUE_BIT_KHR;

    // îzíuÇÃÇΩÇﬂÇÃçsóÒ.
    glm::mat4   m_transform = glm::mat4(1.0f);

    VkAccelerationStructureInstanceKHR m_asInstance = {
        VkTransformMatrixKHR(),
        0,
        0xFFu,
        0,
        0,
        0,
    };

    std::string m_hitShaderName;
};
