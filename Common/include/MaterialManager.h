#pragma once

#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

namespace vk {
    class GraphicsDevice;
    class ImageResource;
}

class Material {
public:
    Material(const wchar_t* name) : m_name(name) {}
    // �T�C�Y�� 16 byte �A���C�����g���Ă���.
    // (UBO�Ɋi�[���z��A�N�Z�X�����邽��)
    struct DataBlock {
        glm::vec4   diffuse;
        glm::vec4   specular;
        int type = 0;
        int textureIndex;
        int padd0 = 0;
        int padd1 = 0;
    };
    DataBlock Get() const
    {
        return { glm::vec4(m_diffuse, 0.0f), glm::vec4(m_specular, m_specPower), m_type, m_textureIndex };
    }

    void SetTexture(int textureIndex) { m_textureIndex = textureIndex; }
    void SetType(int type) { m_type = type; }
    void SetDiffuse(glm::vec3 diffuse) { m_diffuse = diffuse; }
    void SetSpecular(glm::vec3 specular) { m_specular = specular; }
    void SetSpecularPower(float specPower) { m_specPower = specPower; }

    std::wstring GetName() const { return m_name; }
private:
    // �Q�Ƃ���e�N�X�`���̃C���f�b�N�X.
    int m_textureIndex = -1;
    int m_type = 0;
    glm::vec3 m_diffuse = glm::vec3(1.0f);
    glm::vec3 m_specular = glm::vec3(0.5f);
    float m_specPower = 20.0f;
    std::wstring m_name;
};

class MaterialManager {
public:
    using VkGraphicsDevice = std::unique_ptr<vk::GraphicsDevice>;

    void Create(VkGraphicsDevice& device, int maxTextures = 1024);
    void Destroy(VkGraphicsDevice& device);

    // ���Z�b�g.
    void Reset();

    // �e�N�X�`����o�^.
    int AddTexture(const std::wstring& name, vk::ImageResource texture);

    // �e�N�X�`��������.
    int GetTexture(const std::wstring& name) const;

    // �}�e���A����o�^.
    int AddMaterial(std::shared_ptr<Material> mate);

    // �}�e���A�����Ō���.
    int GetMaterialIndex(const std::wstring& name) const;

    // �o�^�ς݃e�N�X�`���̃f�B�X�N���v�^�z���Ԃ�.
    std::vector<VkDescriptorImageInfo> GetTextureDescriptors() const;

    int GetMaxTextureCount() const { return static_cast<int>(m_textures.capacity()); }
    int GetTextureCount() const { return static_cast<int>(m_textures.size()); }

    // UBO�ɏ����}�e���A�����z����擾.
    std::vector<Material::DataBlock> GetMaterialData() const;
private:
    std::vector<vk::ImageResource> m_textures;
    std::vector<std::shared_ptr<Material>> m_materials;

    std::unordered_map<std::wstring, int> m_textureMap;
    std::unordered_map<std::wstring, int> m_materialMap;
    VkSampler m_defaultSampler;
};
