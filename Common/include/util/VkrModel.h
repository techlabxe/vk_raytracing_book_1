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

    // モデルデータを表現するクラス.
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

        // モデルをロードする.
        bool LoadFromGltf(
            const std::wstring& fileName,
            VkGraphicsDevice& device);

        // 各階層や関節を表現するノードクラス.
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

        // ポリゴン情報.
        struct Mesh {
            uint32_t indexStart = 0;
            uint32_t vertexStart = 0;
            uint32_t indexCount = 0;
            uint32_t vertexCount = 0;
            uint32_t materialIndex = 0;
        };

        // 同じノードに関連するポリゴンメッシュを束ねたデータ.
        class MeshGroup {
        public:
            int GetNode() const { return m_nodeIndex; }
            std::vector<Mesh> GetMeshes() const { return m_meshes; }
        private:
            std::vector<Mesh> m_meshes;
            int m_nodeIndex;
            friend class VkrModel;
        };

        // スキニング用情報.
        class Skin {
        private:
            std::wstring name;
            std::vector<int> m_joints;
            std::vector<glm::mat4> invBindMatrices;
            friend class VkrModel;
        };

        // マテリアル.
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

        // 位置情報バッファの取得.
        vk::BufferResource GetPositionBuffer() const { return m_vertexAttrib.position; }

        // 法線情報バッファの取得.
        vk::BufferResource GetNormalBuffer() const { return m_vertexAttrib.normal; }

        // テクスチャ座標情報バッファの取得.
        vk::BufferResource GetTexcoordBuffer() const { return m_vertexAttrib.texcoord; }

        // インデックスバッファの取得.
        vk::BufferResource GetIndexBuffer() const { return m_indexBuffer; }

        // ジョイント用インデックスバッファの取得.
        vk::BufferResource GetJointIndicesBuffer() const { return m_vertexAttrib.jointIndices; }

        // ジョイント用ウェイトバッファの取得.
        vk::BufferResource GetJointWeightsBuffer() const { return m_vertexAttrib.jointWeights; }
    
        // テクスチャ数.
        int GetTextureCount() const  { return int(m_textures.size()); }
        std::vector<TextureInfo> GetTextures() const { return m_textures; }

        // 画像データ数.
        int GetImageCount() const { return int(m_images.size()); }
        std::vector<ImageInfo> GetImages() const { return m_images; }

        // ノード数.
        int GetNodeCount() const { return int(m_nodes.size()); }
        std::shared_ptr<Node> GetNode(int index) const { return m_nodes[index]; }

        // ルートノード(のインデックス)列を取得.
        std::vector<int> GetRootNodes() const { return m_rootNodes; }

        // マテリアル取得.
        std::vector<Material> GetMaterials()const { return m_materials; }

        // メッシュグループ.
        int GetMeshGroupCount() const { return int(m_meshGroups.size()); }
        std::vector<MeshGroup> GetMeshGroups() const { return m_meshGroups; }

        // スキニングモデルであるか.
        bool IsSkinned() const { return m_hasSkin; }

        // スキニング計算で使用するジョイントの名前リストを取得.
        std::vector<std::wstring> GetJointNodeNames() const;

        // スキニング計算で使用するバインド逆行列のリストを取得.
        std::vector<glm::mat4> GetInvBindMatrices()const;

        // スキニング頂点の個数.
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

        // 各頂点属性ごとのバッファ(ストリーム)
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

