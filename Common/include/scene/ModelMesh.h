#pragma once

#include "GraphicsDevice.h"
#include "scene/SceneObject.h"
#include "util/VkrModel.h"
#include "MaterialManager.h"
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

class ModelMesh : public SceneObject {
public:
    struct CreateInfo {
        const util::VkrModel* model;
    };
    void Create(VkGraphicsDevice& device, const CreateInfo& createInfo, MaterialManager& materialManager);

    virtual void Destroy(std::unique_ptr<vk::GraphicsDevice>& device) override;

    void BuildAS(VkGraphicsDevice& device, VkBuildAccelerationStructureFlagsKHR buildFlags = 0);

    virtual std::vector<VkAccelerationStructureGeometryKHR> GetAccelerationStructureGeometry(int frameIndex = 0) override;
    virtual std::vector<VkAccelerationStructureBuildRangeInfoKHR> GetAccelerationStructureBuildRangeInfo() override;
    virtual std::vector<SceneObjectParameter> GetSceneObjectParameters() override;

    class ModelNode {
    public:
        ModelNode(const std::wstring& name) : m_name(name) {}
        void SetTranslation(glm::vec3 t) { m_translation = t; }
        void SetRotation(glm::quat q) { m_rotation = q; }
        void SetScale(glm::vec3 s) { m_scale = s; }
        glm::vec3 GetTranslation() const { return m_translation; }
        glm::vec3 GetScale() const { return m_scale; }
        glm::quat GetRotation() const { return m_rotation; }

        void AddChild(std::shared_ptr<ModelNode> node);
        void SetParent(std::shared_ptr<ModelNode> parent);
        std::shared_ptr<ModelNode> SearchNode(const std::wstring& name) const;

        std::wstring GetName() const { return m_name; }
        glm::mat4 GetWorldMatrix() const { return m_mtxWorld; }

        // 行列操作.
        void UpdateLocalMatrix();
        void UpdateWorldMatrix(glm::mat4 mtxParent);
        void UpdateMatrices(glm::mat4 mtxParent);
    private:
        glm::vec3   m_translation = glm::vec3(0.0f);
        glm::vec3   m_scale = glm::vec3(1.0f);
        glm::quat   m_rotation = glm::quat(1.0f, 0.0f,0.0f,0.0f);
        std::vector<std::shared_ptr<ModelNode>> m_children;
        std::weak_ptr<ModelNode> m_parent;
        std::wstring m_name;

        glm::mat4   m_mtxLocal;
        glm::mat4   m_mtxWorld;
    };

    // ポリゴンメッシュ情報.
    class MeshInfo {
    public:
        void SetBlasMatrixIndex(int index) { m_blasMatrixIndex = index; }
        void SetMaterialIndex(int index) { m_materialIndex = index; }

        void SetVertexOffset(uint64_t offset) { m_vertexOffset = offset; }
        void SetIndexOffset(uint64_t offset) { m_indexOffset = offset; }
        void SetVertexCount(uint32_t count) { m_vertexCount = count; }
        void SetIndexCount(uint32_t count) { m_indexCount = count; }

        // 各バッファをセット.
        void SetPositionBuffer(VkDeviceAddress addr) { m_vbAttribPosition = addr; }
        void SetNormalBuffer(VkDeviceAddress addr) { m_vbAttribNormal = addr; }
        void SetTexcoordBuffer(VkDeviceAddress addr) { m_vbAttribTexcoord = addr; }
        void SetIndexBuffer(VkDeviceAddress addr) { m_indexBuffer = addr; }

        int GetBlasMatrixIndex() const { return m_blasMatrixIndex; }
        int GetMaterialIndex() const { return m_materialIndex; }

        // 各バッファの該当部位をデバイスアドレスで取得.
        VkDeviceAddress GetPositionOffseted() const { return m_vbAttribPosition + m_vertexOffset * strideP; }
        VkDeviceAddress GetNormalOffseted() const { return m_vbAttribNormal + m_vertexOffset * strideN; }
        VkDeviceAddress GetTexcoordOffseted()const { return m_vbAttribTexcoord + m_vertexOffset * strideT; }
        VkDeviceAddress GetIndexOffseted() const { return m_indexBuffer + m_indexOffset * strideIdx; }

        // 本メッシュに含まれる頂点数.
        uint32_t GetVertexCount()const { return m_vertexCount; }

        // 本メッシュに含まれるインデックス数.
        uint32_t GetIndexCount() const { return m_indexCount; }

        // 本メッシュが参照する頂点データにおけるオフセット値.
        uint64_t GetVertexOffset() const { return m_vertexOffset; }

        // 本メッシュが参照するインデックスデータにおけるオフセット値.
        uint64_t GetIndexOffset() const { return m_indexOffset; }
    private:
        VkDeviceAddress m_vbAttribPosition;
        VkDeviceAddress m_vbAttribNormal;
        VkDeviceAddress m_vbAttribTexcoord;
        VkDeviceAddress m_indexBuffer;
        uint64_t m_vertexOffset = 0;
        uint64_t m_indexOffset = 0;

        int m_blasMatrixIndex = 0;  // BLAS transform で設定する行列.
        int m_materialIndex = 0;    // 描画で使用するマテリアルのインデックス値.
        uint32_t m_vertexCount = 0;
        uint32_t m_indexCount = 0;

        const size_t strideP = sizeof(glm::vec3);
        const size_t strideN = sizeof(glm::vec3);
        const size_t strideT = sizeof(glm::vec2);
        const size_t strideIdx = sizeof(uint32_t);
    };

    // 各子供ノードのワールド行列を更新する.
    void UpdateMatrices();

    // 各行列の変更をGPUのバッファへ反映する.
    void ApplyTransform(VkGraphicsDevice& device);

    // ApplyTransform の内容で BLAS を更新.
    void UpdateBlas(VkCommandBuffer command);

    // 指定されたノードを検索.
    std::shared_ptr<ModelNode> SearchNode(const std::wstring& name) const;

    // 内包する BLAS の数を取得.
    virtual int GetSubMeshCount() const override;

    // スキニングモデルであるか.
    bool IsSkinned() const { return m_isSkinned; }

    // スキニング頂点数を取得.
    int  GetSkinnedVertexCount() const { return m_skinVertexCount; }

    // スキニング計算で使用する.
    // 計算はコンピュートシェーダーにやらせる.
    vk::BufferResource GetPositionBufferSrc() const;    // 変形前位置.
    vk::BufferResource GetNormalBufferSrc() const;      // 変形前法線.
    vk::BufferResource GetJointIndicesBuffer() const;   // ジョイントのインデックス値.
    vk::BufferResource GetJointWeightsBuffer() const;   // ジョイントのウェイト値.
    const util::DynamicBuffer& GetJointMatricesBuffer() const;  // ジョイントの行列(スキニング行列)バッファ.
    vk::BufferResource GetPositionTransformedBuffer() const;    // 変形後の頂点位置バッファ.
    vk::BufferResource GetNormalTransformedBuffer() const;      // 変形後の頂点法線バッファ.
private:
    void CreateNodes(const util::VkrModel* model);
    void CreateTextures(VkGraphicsDevice& device, const std::vector<util::VkrModel::ImageInfo>& images, MaterialManager& materialManager);
    void CreateMaterials(const util::VkrModel* model, MaterialManager& materialManager);

    void AllocateBlasTransformMatrices(VkGraphicsDevice& device, const util::VkrModel* model);
    void AllocateTransformedBuffer(VkGraphicsDevice& device, uint64_t size);

    std::vector<std::shared_ptr<ModelNode>> m_nodes;
    std::vector<std::shared_ptr<ModelNode>> m_blasNodes;    // BLAS構築時に参照するノード.
    std::vector<std::shared_ptr<Material>> m_materials;
    std::vector<std::shared_ptr<ModelNode>> m_skinJoints;   // スキニングに関連するジョイント(ノード) の参照.
    std::vector<glm::mat4> m_invBindMatrices;               // スキニングバインド逆行列.

    std::vector<MeshInfo> m_meshes;
    util::DynamicBuffer m_blasTransformMatrices;// BLAS の生成時に使う行列指定バッファ.


    // --- モデルクラスからの情報をコピー (本クラスで解放不要) ---
    vk::BufferResource m_positionBuffer;        // 頂点位置バッファ.
    vk::BufferResource m_normalBuffer;          // 頂点法線バッファ.
    vk::BufferResource m_texcoordBuffer;        // 頂点UVバッファ.
    vk::BufferResource m_jointWeightsBuffer;    // スキニング.ジョイント重みバッファ.
    vk::BufferResource m_jointIndicesBuffer;    // スキニング.ジョイントインデックスバッファ.
    vk::BufferResource m_indexBuffer;           // インデックスバッファ.

    // --- スキニングモデル用. 個別のリソースになるので本クラスで解放必要 ---
    vk::BufferResource m_positionTransformed;   // 変形後頂点位置バッファ.
    vk::BufferResource m_normalTransformed;     // 変形後法線バッファ.
    util::DynamicBuffer m_jointMatricesBuffer;   // スキニング計算の行列格納バッファ.

    bool m_isSkinned = false;
    int m_skinVertexCount = 0;
    VkBuildAccelerationStructureFlagsKHR m_blasBuildFlags = 0;
};