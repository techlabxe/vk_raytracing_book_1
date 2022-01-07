#pragma once

#include <memory>
#include <unordered_map>
#include "GraphicsDevice.h"

namespace util {

    class ShaderGroupHelper
    {
    public:
        using VkGraphicsDevice = std::unique_ptr<vk::GraphicsDevice>;

        enum ShaderStage {
            Auto = -1,
            RayGeneration,
            Miss,
            ClosestHit,
            AnyHit,
            Intersect,
            StageMax,
        };
        bool LoadShader(VkGraphicsDevice& device, const char* shaderName, const wchar_t* fileName, ShaderStage stage = Auto);

        // 名前から本クラスで登録されているシェーダーのインデックスを取得.
        int GetShader(const char* shaderName) const;

        // 名前を付けてシェーダーグループを登録.
        bool AddShaderGroupRayGeneration(const char* name, const char* rgenShaderName);
        bool AddShaderGroupMiss(const char* name, const char* missShaderName);
        bool AddShaderGroupHit(const char* name, const char* chitShaderName, const char* ahitShaderName = nullptr, const char* rintShaderName = nullptr);

        const VkPipelineShaderStageCreateInfo* GetShaderStages() const;
        uint32_t GetShaderStagesCount() const;

        const VkRayTracingShaderGroupCreateInfoKHR* GetShaderGroups() const;
        uint32_t GetShaderGroupsCount() const;

        void LoadShaderGroupHandles(VkGraphicsDevice& device, VkPipeline rtPipeline);

        // 名前からシェーダーグループのハンドルを得る.
        const void* GetShaderGroupHandle(const std::string& groupName) const;
        // シェーダーグループのハンドルサイズ.
        uint32_t GetShaderGroupHandleSize() const;

        void Destroy(VkGraphicsDevice& device);
    private:
        struct ShaderData {
            VkPipelineShaderStageCreateInfo shader;
        };
        // 名前からシェーダーグループ内のインデックス値を得る.
        int GetGroupIndex(const std::string& groupName) const;

        std::vector<VkPipelineShaderStageCreateInfo> m_shaders;
        std::unordered_map<std::string, int> m_shaderMap;

        std::vector<VkRayTracingShaderGroupCreateInfoKHR> m_shaderGroups;
        std::unordered_map<std::string, int> m_shaderGroupMap;

        uint32_t m_shaderGroupHandleSize = 0;
        uint32_t m_shaderGroupHandleAligned = 0;
        std::vector<uint8_t> m_shaderGroupHandleStorage;
    };

    class ShaderBindingTableHelper
    {
    public:
        using VkGraphicsDevice = std::unique_ptr<vk::GraphicsDevice>;

        void Setup(VkGraphicsDevice& device, VkPipeline rtPipeline);
        void Reset();

        void AddRayGenerationShader(const char* name, const std::vector<uint64_t>& sbtRecordData);
        void AddMissShader(const char* name, const std::vector<uint64_t>& sbtRecordData);
        void AddHitShader(const char* name, const std::vector<uint64_t>& sbtRecordData);

        uint64_t ComputeShaderBindingTableSize();

        uint32_t GetRayGenRegionSize() const;
        uint32_t GetRayGenEntrySize() const;

        uint32_t GetMissRegionSize() const;
        uint32_t GetMissEntrySize() const;

        uint32_t GetHitRegionSize() const;
        uint32_t GetHitEntrySize() const;

        void Build(VkGraphicsDevice& device, vk::BufferResource sbtBuffer, const ShaderGroupHelper& groups);

        const VkStridedDeviceAddressRegionKHR* GetRaygenRegion() const;
        const VkStridedDeviceAddressRegionKHR* GetMissRegion() const;
        const VkStridedDeviceAddressRegionKHR* GetHitRegion() const;
    private:
        struct Entry {
            Entry(std::string name, std::vector<uint64_t> data);
            const std::string m_name;
            const std::vector<uint64_t> m_sbtLocalData;
        };
        uint32_t GetEntrySize(const std::vector<Entry>& entries);
        void WriteData(
            uint8_t* dst, const std::vector<Entry>& shaders, uint32_t entrySize, const ShaderGroupHelper& groups);

        std::vector<Entry> m_raygenEntries;
        std::vector<Entry> m_missEntries;
        std::vector<Entry> m_hitEntries;

        VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_rtPipelineProps;
        uint32_t m_raygenEntrySize = 0;
        uint32_t m_missEntrySize = 0;
        uint32_t m_hitEntrySize = 0;
        uint32_t m_raygenRegionSize = 0;
        uint32_t m_missRegionSize = 0;
        uint32_t m_hitRegionSize = 0;

        uint32_t m_shaderHandleSize;
        uint32_t m_uniformBufferAlignment = 256;

        VkStridedDeviceAddressRegionKHR m_regionRgen = { };
        VkStridedDeviceAddressRegionKHR m_regionMiss = { };
        VkStridedDeviceAddressRegionKHR m_regionHit = { };
    };

} // util
