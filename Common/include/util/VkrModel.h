#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <stdexcept>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "GraphicsDevice.h"
#include "VkrayBookUtility.h"
#include "AccelerationStructure.h"

namespace tinygltf {
    class Node;
    class Model;
    struct Mesh;
}

namespace util {

    // ���f���f�[�^��\������N���X.
    class VkrModel {
    public:
        using vec2 = glm::vec2;
        using vec3 = glm::vec3;
        using vec4 = glm::vec4;
        using quat = glm::quat;
        using mat4 = glm::mat4;
        using uvec4 = glm::uvec4;

        using VkGraphicsDevice = std::unique_ptr<vk::GraphicsDevice>;


        VkrModel();
        ~VkrModel();

        void Destroy(VkGraphicsDevice& device);

        // ���f�������[�h����.
        bool LoadFromGltf(
            const std::wstring& fileName,
            VkGraphicsDevice& device);

        // �e�K�w��֐߂�\������m�[�h�N���X.
        class Node {
        public:
            Node();
            ~Node();

            std::wstring GetName() const { return name; }

            glm::vec3 GetTranslation() const { return translation; }
            glm::quat GetRotation() const { return rotation; }
            glm::vec3 GetScale() const { return scale; }

            std::vector<int> GetChildren() const { return children; }
        private:
            std::wstring name;
            vec3 translation;
            quat rotation;
            vec3 scale;
            mat4 mtxLocal;
            mat4 mtxWorld;
            std::vector<int> children;
            Node* parent = nullptr;
            int meshIndex = -1;

            friend class VkrModel;
        };

        // �|���S�����.
        struct Mesh {
            uint32_t indexStart = 0;
            uint32_t vertexStart = 0;
            uint32_t indexCount = 0;
            uint32_t vertexCount = 0;
            uint32_t materialIndex = 0;
        };

        // �����m�[�h�Ɋ֘A����|���S�����b�V���𑩂˂��f�[�^.
        class MeshGroup {
        public:
            int GetNode() const { return m_nodeIndex; }
            std::vector<Mesh> GetMeshes() const { return m_meshes; }
        private:
            std::vector<Mesh> m_meshes;
            int m_nodeIndex;
            friend class VkrModel;
        };

        // �X�L�j���O�p���.
        class Skin {
        private:
            std::wstring name;
            std::vector<int> m_joints;
            std::vector<glm::mat4> invBindMatrices;
            friend class VkrModel;
        };

        // �}�e���A��.
        class Material {
        public:
            Material() : m_name(), m_textureIndex(-1), m_diffuseColor(1.0f) {}
            std::wstring GetName() const { return m_name; }
            int GetTextureIndex() const { return m_textureIndex; }
            vec3 GetDiffuseColor() const { return m_diffuseColor; }

        private:
            std::wstring m_name;
            int m_textureIndex;
            glm::vec3 m_diffuseColor;
            friend class VkrModel;
        };

        struct ImageInfo {
            std::vector<uint8_t> imageBuffer;
            std::wstring fileName;
        };
        struct TextureInfo {
            int imageIndex;
        };

        // �ʒu���o�b�t�@�̎擾.
        vk::BufferResource GetPositionBuffer() const { return m_vertexAttrib.position; }

        // �@�����o�b�t�@�̎擾.
        vk::BufferResource GetNormalBuffer() const { return m_vertexAttrib.normal; }

        // �e�N�X�`�����W���o�b�t�@�̎擾.
        vk::BufferResource GetTexcoordBuffer() const { return m_vertexAttrib.texcoord; }

        // �C���f�b�N�X�o�b�t�@�̎擾.
        vk::BufferResource GetIndexBuffer() const { return m_indexBuffer; }

        // �W���C���g�p�C���f�b�N�X�o�b�t�@�̎擾.
        vk::BufferResource GetJointIndicesBuffer() const { return m_vertexAttrib.jointIndices; }

        // �W���C���g�p�E�F�C�g�o�b�t�@�̎擾.
        vk::BufferResource GetJointWeightsBuffer() const { return m_vertexAttrib.jointWeights; }
    
        // �e�N�X�`����.
        int GetTextureCount() const  { return int(m_textures.size()); }
        std::vector<TextureInfo> GetTextures() const { return m_textures; }

        // �摜�f�[�^��.
        int GetImageCount() const { return int(m_images.size()); }
        std::vector<ImageInfo> GetImages() const { return m_images; }

        // �m�[�h��.
        int GetNodeCount() const { return int(m_nodes.size()); }
        std::shared_ptr<Node> GetNode(int index) const { return m_nodes[index]; }

        // ���[�g�m�[�h(�̃C���f�b�N�X)����擾.
        std::vector<int> GetRootNodes() const { return m_rootNodes; }

        // �}�e���A���擾.
        std::vector<Material> GetMaterials()const { return m_materials; }

        // ���b�V���O���[�v.
        int GetMeshGroupCount() const { return int(m_meshGroups.size()); }
        std::vector<MeshGroup> GetMeshGroups() const { return m_meshGroups; }

        // �X�L�j���O���f���ł��邩.
        bool IsSkinned() const { return m_hasSkin; }

        // �X�L�j���O�v�Z�Ŏg�p����W���C���g�̖��O���X�g���擾.
        std::vector<std::wstring> GetJointNodeNames() const;

        // �X�L�j���O�v�Z�Ŏg�p����o�C���h�t�s��̃��X�g���擾.
        std::vector<glm::mat4> GetInvBindMatrices()const;

        // �X�L�j���O���_�̌�.
        int GetSkinnedVertexCount() const { return m_skinInfo.skinVertexCount; }
    private:
        struct VertexAttributeVisitor {
            std::vector<uint32_t> indexBuffer;
            std::vector<vec3> positionBuffer;
            std::vector<vec3> normalBuffer;
            std::vector<vec2> texcoordBuffer;

            std::vector<uvec4> jointBuffer;
            std::vector<vec4>  weightBuffer;
        };
        void LoadNode(const tinygltf::Model& inModel);
        void LoadMesh(const tinygltf::Model& inModel, VertexAttributeVisitor& visitor);
        void LoadSkin(const tinygltf::Model& inModel);
        void LoadMaterial(const tinygltf::Model& inModel);

        // �e���_�������Ƃ̃o�b�t�@(�X�g���[��)
        struct VertexAttribute {
            vk::BufferResource position;
            vk::BufferResource normal;
            vk::BufferResource texcoord;
            vk::BufferResource jointIndices;
            vk::BufferResource jointWeights;
        } m_vertexAttrib;
        vk::BufferResource m_indexBuffer;

        std::vector<MeshGroup> m_meshGroups;
        std::vector<Material> m_materials;
        std::vector<std::shared_ptr<Node>> m_nodes;
        std::vector<int> m_rootNodes;

        struct SkinInfo {
            std::wstring name;
            std::vector<int> joints;
            std::vector<glm::mat4> invBindMatrices;
            uint32_t skinVertexCount;
        } m_skinInfo;
        bool m_hasSkin = false;

        std::vector<ImageInfo> m_images;
        std::vector<TextureInfo> m_textures;
        
        friend class VkrModelActor;
    };

}

