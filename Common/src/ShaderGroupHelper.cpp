#include "GraphicsDevice.h"
#include "ShaderGroupHelper.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

#include "VkrayBookUtility.h"

#define ALIGN(x, a) ( (x + a -1) & ~(a-1) )

namespace util {
    bool ShaderGroupHelper::LoadShader(VkGraphicsDevice& device, const char* shaderName, const wchar_t* fileName, ShaderStage stage)
    {
        VkShaderStageFlagBits shaderStage = VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
        const VkShaderStageFlagBits stages[] = {
            VK_SHADER_STAGE_RAYGEN_BIT_KHR,
            VK_SHADER_STAGE_MISS_BIT_KHR,
            VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
            VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
            VK_SHADER_STAGE_INTERSECTION_BIT_KHR,
        };
        static_assert(StageMax == _countof(stages));

        if (stage == ShaderGroupHelper::Auto) {
            auto wstrFileName = std::wstring(fileName);
            const auto npos = wstrFileName.npos;

            const std::wstring suffix[] = {
                L".rgen.spv", L".rmiss.spv", L".rchit.spv", L".rahit.spv", L".rint.spv"
            };
            static_assert(_countof(suffix) == _countof(stages));
            std::transform(wstrFileName.begin(), wstrFileName.end(), wstrFileName.begin(), towlower);

            for (int i = 0; i < _countof(stages); ++i) {
                if (wstrFileName.rfind(suffix[i]) != npos) {
                    shaderStage = stages[i];
                    break;
                }
            }
            if (shaderStage == VK_SHADER_STAGE_FLAG_BITS_MAX_ENUM) { return false; }
        } else {
            shaderStage = stages[stage];
        }

        auto shaderIndex = int(m_shaders.size());
        auto shaderGroupCI = util::LoadShader(device, fileName, shaderStage);
        if (shaderGroupCI.module == VK_NULL_HANDLE) {
            return false;
        }
        m_shaders.emplace_back(shaderGroupCI);
        if (m_shaderMap.find(shaderName) != m_shaderMap.end()) {
            // Already registered.
            return false;
        }
        auto result = m_shaderMap.insert(std::make_pair(shaderName, shaderIndex));
        return result.second;
    }

    int ShaderGroupHelper::GetShader(const char* shaderName) const
    {
        if (shaderName == nullptr) {
            return -1;
        }
        auto key = std::string(shaderName);
        auto itr = m_shaderMap.find(key);
        if (itr != m_shaderMap.end()) {
            return itr->second;
        }
        return -1;
    }

    bool ShaderGroupHelper::AddShaderGroupRayGeneration(const char* name, const char* rgenShaderName)
    {
        int rgen = GetShader(rgenShaderName);
        if (rgen < 0) {
            return false;
        }
        int groupIndex = int(m_shaderGroups.size());
        m_shaderGroups.emplace_back(util::CreateShaderGroupRayGeneration(rgen));
        auto result = m_shaderGroupMap.insert(std::make_pair(name, groupIndex));
        return result.second;
    }

    bool ShaderGroupHelper::AddShaderGroupMiss(const char* name, const char* missShaderName)
    {
        auto miss = GetShader(missShaderName);
        if (miss < 0) {
            return false;
        }
        int groupIndex = int(m_shaderGroups.size());
        m_shaderGroups.emplace_back(util::CreateShaderGroupMiss(miss));
        auto result = m_shaderGroupMap.insert(std::make_pair(name, groupIndex));
        return result.second;
    }

    bool ShaderGroupHelper::AddShaderGroupHit(const char* name, const char* chitShaderName, const char* ahitShaderName, const char* rintShaderName)
    {
        auto closestHit = GetShader(chitShaderName);
        auto anyHit = GetShader(ahitShaderName);
        auto intersect = GetShader(rintShaderName);

        if (closestHit < 0 && anyHit < 0 && intersect < 0) {
            return false;
        }
        int groupIndex = int(m_shaderGroups.size());
        m_shaderGroups.emplace_back(util::CreateShaderGroupHit(closestHit, anyHit, intersect));
        auto result = m_shaderGroupMap.insert(std::make_pair(name, groupIndex));
        return result.second;
    }

    const VkPipelineShaderStageCreateInfo* ShaderGroupHelper::GetShaderStages() const
    {
        return m_shaders.data();
    }

    uint32_t ShaderGroupHelper::GetShaderStagesCount() const
    {
        return uint32_t(m_shaders.size());
    }

    const VkRayTracingShaderGroupCreateInfoKHR* ShaderGroupHelper::GetShaderGroups() const
    {
        return m_shaderGroups.data();
    }

    uint32_t ShaderGroupHelper::GetShaderGroupsCount() const
    {
        return uint32_t(m_shaderGroups.size());
    }

    void ShaderGroupHelper::LoadShaderGroupHandles(VkGraphicsDevice& device, VkPipeline rtPipeline)
    {
        auto rtPipelineProps = device->GetRayTracingPipelineProperties();
        auto handleAlignment = rtPipelineProps.shaderGroupHandleAlignment;
        m_shaderGroupHandleSize = rtPipelineProps.shaderGroupHandleSize;
        m_shaderGroupHandleAligned = (m_shaderGroupHandleSize + handleAlignment - 1) & ~(handleAlignment - 1);

        auto storageSize = GetShaderGroupsCount() * m_shaderGroupHandleAligned;
        m_shaderGroupHandleStorage.resize(storageSize);
        vkGetRayTracingShaderGroupHandlesKHR(
            device->GetDevice(),
            rtPipeline,
            0,
            GetShaderGroupsCount(),
            m_shaderGroupHandleStorage.size(),
            m_shaderGroupHandleStorage.data()
        );
    }

    const void* ShaderGroupHelper::GetShaderGroupHandle(const std::string& groupName) const
    {
        auto groupIndex = GetGroupIndex(groupName);
        if (groupIndex < 0) {
            return nullptr;
        }
        assert(!m_shaderGroupHandleStorage.empty()); // データがロード済みか?.
        auto storage = static_cast<const uint8_t*>(m_shaderGroupHandleStorage.data());
        return storage + groupIndex * m_shaderGroupHandleAligned;
    }

    uint32_t ShaderGroupHelper::GetShaderGroupHandleSize() const
    {
        return m_shaderGroupHandleSize;
    }

    void ShaderGroupHelper::Destroy(VkGraphicsDevice& device)
    {
        for (auto shader : m_shaders) {
            vkDestroyShaderModule(device->GetDevice(), shader.module, nullptr);
        }
    }

    int ShaderGroupHelper::GetGroupIndex(const std::string& groupName) const
    {
        auto itr = m_shaderGroupMap.find(groupName);
        if (itr != m_shaderGroupMap.end()) {
            return itr->second;
        }
        return -1;
    }


    void ShaderBindingTableHelper::Setup(VkGraphicsDevice& device, VkPipeline rtPipeline)
    {
        m_uniformBufferAlignment = static_cast<uint32_t>(device->GetUniformBufferAlignment());
        m_rtPipelineProps = device->GetRayTracingPipelineProperties();
    }

    void ShaderBindingTableHelper::Reset()
    {
        m_raygenEntries.clear();
        m_missEntries.clear();
        m_hitEntries.clear();

        m_raygenEntrySize = 0;
        m_missEntrySize = 0;
        m_hitEntrySize = 0;
    }

    void ShaderBindingTableHelper::AddRayGenerationShader(const char* name, const std::vector<uint64_t>& sbtRecordData)
    {
        assert(name != nullptr);
        m_raygenEntries.emplace_back(Entry(name, sbtRecordData));
    }

    void ShaderBindingTableHelper::AddMissShader(const char* name, const std::vector<uint64_t>& sbtRecordData)
    {
        m_missEntries.emplace_back(Entry(name, sbtRecordData));
    }

    void ShaderBindingTableHelper::AddHitShader(const char* name, const std::vector<uint64_t>& sbtRecordData)
    {
        m_hitEntries.emplace_back(Entry(name, sbtRecordData));
    }

    uint64_t ShaderBindingTableHelper::ComputeShaderBindingTableSize()
    {
        const auto shaderGroupBaseAlignment = m_rtPipelineProps.shaderGroupBaseAlignment;
        // それぞれのタイプごとに登録済みエントリを検索し、パラメータの最大サイズでエントリサイズを決定.
        m_raygenEntrySize = GetEntrySize(m_raygenEntries);
        m_raygenRegionSize = m_raygenEntrySize * uint32_t(m_raygenEntries.size());
        m_raygenRegionSize = ALIGN(m_raygenRegionSize, shaderGroupBaseAlignment);

        m_missEntrySize = GetEntrySize(m_missEntries);
        m_missRegionSize = m_missEntrySize * uint32_t(m_missEntries.size());
        m_missRegionSize = ALIGN(m_missRegionSize, shaderGroupBaseAlignment);

        m_hitEntrySize = GetEntrySize(m_hitEntries);
        m_hitRegionSize = m_hitEntrySize * uint32_t(m_hitEntries.size());
        m_hitRegionSize = ALIGN(m_hitRegionSize, shaderGroupBaseAlignment);

        uint64_t sbtSize = 0;
        sbtSize += m_raygenRegionSize;
        sbtSize += m_missRegionSize;
        sbtSize += m_hitRegionSize;

        // バッファサイズを切り上げ.
        sbtSize = (sbtSize + m_uniformBufferAlignment - 1) & ~(static_cast<uint64_t>(m_uniformBufferAlignment - 1));
        return sbtSize;
    }

    uint32_t ShaderBindingTableHelper::GetRayGenRegionSize() const
    {
        return m_raygenRegionSize;
    }

    uint32_t ShaderBindingTableHelper::GetRayGenEntrySize() const
    {
        return m_raygenEntrySize;
    }

    uint32_t ShaderBindingTableHelper::GetMissRegionSize() const
    {
        return m_missRegionSize;
    }

    uint32_t ShaderBindingTableHelper::GetMissEntrySize() const
    {
        return m_missEntrySize;
    }

    uint32_t ShaderBindingTableHelper::GetHitRegionSize() const
    {
        return m_hitRegionSize;
    }

    uint32_t ShaderBindingTableHelper::GetHitEntrySize() const
    {
        return m_hitEntrySize;
    }

    void ShaderBindingTableHelper::Build(VkGraphicsDevice& device, vk::BufferResource sbtBuffer, const ShaderGroupHelper& groups)
    {
        m_shaderHandleSize = m_rtPipelineProps.shaderGroupHandleSize;
        assert(m_shaderHandleSize != 0);

        auto p = static_cast<uint8_t*>(device->Map(sbtBuffer));
        if (p == nullptr) {
            throw std::logic_error("could not map the shader binding table");
        }

        uint32_t offset = 0;
        WriteData(p, m_raygenEntries, m_raygenEntrySize, groups);
        p += GetRayGenRegionSize();

        WriteData(p, m_missEntries, m_missEntrySize, groups);
        p += GetMissRegionSize();

        WriteData(p, m_hitEntries, m_hitEntrySize, groups);

        device->Unmap(sbtBuffer);

        m_regionRgen.deviceAddress = sbtBuffer.GetDeviceAddress();
        m_regionRgen.stride = GetRayGenEntrySize();
        m_regionRgen.size = m_regionRgen.stride;    // Rgen 用特別対応.

        m_regionMiss.deviceAddress = sbtBuffer.GetDeviceAddress() + GetRayGenRegionSize();
        m_regionMiss.stride = GetMissEntrySize();
        m_regionMiss.size = GetMissRegionSize();

        m_regionHit.deviceAddress = m_regionMiss.deviceAddress + GetMissRegionSize();
        m_regionHit.stride = GetHitEntrySize();
        m_regionHit.size = GetHitRegionSize();

    }

    const VkStridedDeviceAddressRegionKHR* ShaderBindingTableHelper::GetRaygenRegion() const
    {
        return &m_regionRgen;
    }

    const VkStridedDeviceAddressRegionKHR* ShaderBindingTableHelper::GetMissRegion() const
    {
        return &m_regionMiss;
    }

    const VkStridedDeviceAddressRegionKHR* ShaderBindingTableHelper::GetHitRegion() const
    {
        return &m_regionHit;
    }

    // エントリのサイズを求める.
    uint32_t ShaderBindingTableHelper::GetEntrySize(const std::vector<Entry>& entries)
    {
        const auto shaderHandleSize = m_rtPipelineProps.shaderGroupHandleSize;
        const auto handleAlign = m_rtPipelineProps.shaderGroupHandleAlignment;

        size_t maxCount = 0;
        for (const auto& v : entries) {
            maxCount = std::max(maxCount, v.m_sbtLocalData.size());
        }
        // 今はシェーダーレコードの固有データは64bit 値のみと限定しているため、簡単に計算できる.
        auto entrySize = static_cast<uint32_t>(maxCount * sizeof(uint64_t));
        entrySize += shaderHandleSize;

        // シェーダーハンドルのアライメント制約に合わせてサイズ調整.
        entrySize = (entrySize + handleAlign - 1) & ~(handleAlign - 1);
        return entrySize;
    }

    void ShaderBindingTableHelper::WriteData(uint8_t* dst, const std::vector<Entry>& shaders, uint32_t entrySize, const ShaderGroupHelper& groups)
    {
        for (const auto& shader : shaders) {
            auto id = groups.GetShaderGroupHandle(shader.m_name);
            assert(id != nullptr);

            memcpy(dst, id, m_shaderHandleSize);
            auto copySize = shader.m_sbtLocalData.size() * sizeof(uint64_t);
            if (copySize>0) {
                memcpy(dst + m_shaderHandleSize, shader.m_sbtLocalData.data(), copySize);
            }
            dst += entrySize;
        }
    }

    ShaderBindingTableHelper::Entry::Entry(std::string name, std::vector<uint64_t> data)
        : m_name(std::move(name)), m_sbtLocalData(std::move(data))
    {
    }


}
