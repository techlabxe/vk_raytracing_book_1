#include "MaterialManager.h"
#include "GraphicsDevice.h"

void MaterialManager::Create(VkGraphicsDevice& device, int maxTextures)
{
    m_textures.reserve(maxTextures);
    m_defaultSampler = device->CreateSampler(
        VK_FILTER_LINEAR, VK_FILTER_LINEAR, VK_SAMPLER_MIPMAP_MODE_LINEAR,
        VK_SAMPLER_ADDRESS_MODE_REPEAT,
        VK_SAMPLER_ADDRESS_MODE_REPEAT);
}

void MaterialManager::Destroy(VkGraphicsDevice& device)
{
    for (auto t : m_textures) {
        device->DestroyImage(t);
    }
    device->DestroySampler(m_defaultSampler);
}

void MaterialManager::Reset()
{
    m_textureMap.clear();
    m_textures.clear();
}

int MaterialManager::AddTexture(const std::wstring& name, vk::ImageResource texture)
{
    auto textureIndex = GetTexture(name);
    if (textureIndex < 0) {
        textureIndex = int(m_textures.size());
        m_textureMap.insert(std::make_pair(name, textureIndex));
        m_textures.push_back(texture);
    }
    return textureIndex;
}

int MaterialManager::GetTexture(const std::wstring& name) const
{
    auto itr = m_textureMap.find(name);
    if (itr != m_textureMap.end()) {
        return itr->second;
    }
    return -1;
}

int MaterialManager::AddMaterial(std::shared_ptr<Material> mate)
{
    auto materialIndex = GetMaterialIndex(mate->GetName());
    if (materialIndex < 0) {
        materialIndex = int(m_materials.size());
        m_materialMap.insert(std::make_pair(mate->GetName(), materialIndex));
        m_materials.push_back(mate);
    }
    return materialIndex;
}


int MaterialManager::GetMaterialIndex(const std::wstring& name) const
{
    auto itr = m_materialMap.find(name);
    if (itr != m_materialMap.end()) {
        return itr->second;
    }
    return -1;
}

std::vector<VkDescriptorImageInfo> MaterialManager::GetTextureDescriptors() const
{
    std::vector<VkDescriptorImageInfo> descriptors;
    for (const auto& texture : m_textures) {
        VkDescriptorImageInfo info{};
        info.imageView = texture.GetImageView();
        info.imageLayout = texture.GetImageLayout();
        info.sampler = m_defaultSampler;
        descriptors.push_back(info);
    }
    return descriptors;
}

std::vector<Material::DataBlock> MaterialManager::GetMaterialData() const
{
    std::vector<Material::DataBlock> blocks;
    for (const auto& m : m_materials) {
        blocks.emplace_back(m->Get());
    }
    return blocks;
}

