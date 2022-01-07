#include "GraphicsDevice.h"
#include "AccelerationStructure.h"
#include "scene/SceneObject.h"

#include "VkrayBookUtility.h"

void SceneObject::SetWorldMatrix(glm::mat4 m)
{
    m_asInstance.transform = util::ConvertTransform(m);
}

void SceneObject::SetHitShaderOffset(uint32_t offset)
{
    m_asInstance.instanceShaderBindingTableRecordOffset = offset;
}

void SceneObject::SetMask(uint32_t mask)
{
    m_asInstance.mask = mask;
}

void SceneObject::SetCustomIndex(uint32_t customIndex)
{
    m_asInstance.instanceCustomIndex = customIndex;
}

void SceneObject::SetGeometryInstanceFlags(VkGeometryInstanceFlagsKHR flags)
{
    m_asInstance.flags = flags;
}

VkAccelerationStructureInstanceKHR SceneObject::GetAccelerationStructureInstance() const
{
    return m_asInstance;
}
