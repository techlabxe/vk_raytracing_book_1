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

        // �s�񑀍�.
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

    // �|���S�����b�V�����.
    class MeshInfo {
    public:
        void SetBlasMatrixIndex(int index) { m_blasMatrixIndex = index; }
        void SetMaterialIndex(int index) { m_materialIndex = index; }

        void SetVertexOffset(uint64_t offset) { m_vertexOffset = offset; }
        void SetIndexOffset(uint64_t offset) { m_indexOffset = offset; }
        void SetVertexCount(uint32_t count) { m_vertexCount = count; }
        void SetIndexCount(uint32_t count) { m_indexCount = count; }

        // �e�o�b�t�@���Z�b�g.
        void SetPositionBuffer(VkDeviceAddress addr) { m_vbAttribPosition = addr; }
        void SetNormalBuffer(VkDeviceAddress addr) { m_vbAttribNormal = addr; }
        void SetTexcoordBuffer(VkDeviceAddress addr) { m_vbAttribTexcoord = addr; }
        void SetIndexBuffer(VkDeviceAddress addr) { m_indexBuffer = addr; }

        int GetBlasMatrixIndex() const { return m_blasMatrixIndex; }
        int GetMaterialIndex() const { return m_materialIndex; }

        // �e�o�b�t�@�̊Y�����ʂ��f�o�C�X�A�h���X�Ŏ擾.
        VkDeviceAddress GetPositionOffseted() const { return m_vbAttribPosition + m_vertexOffset * strideP; }
        VkDeviceAddress GetNormalOffseted() const { return m_vbAttribNormal + m_vertexOffset * strideN; }
        VkDeviceAddress GetTexcoordOffseted()const { return m_vbAttribTexcoord + m_vertexOffset * strideT; }
        VkDeviceAddress GetIndexOffseted() const { return m_indexBuffer + m_indexOffset * strideIdx; }

        // �{���b�V���Ɋ܂܂�钸�_��.
        uint32_t GetVertexCount()const { return m_vertexCount; }

        // �{���b�V���Ɋ܂܂��C���f�b�N�X��.
        uint32_t GetIndexCount() const { return m_indexCount; }

        // �{���b�V�����Q�Ƃ��钸�_�f�[�^�ɂ�����I�t�Z�b�g�l.
        uint64_t GetVertexOffset() const { return m_vertexOffset; }

        // �{���b�V�����Q�Ƃ���C���f�b�N�X�f�[�^�ɂ�����I�t�Z�b�g�l.
        uint64_t GetIndexOffset() const { return m_indexOffset; }
    private:
        VkDeviceAddress m_vbAttribPosition;
        VkDeviceAddress m_vbAttribNormal;
        VkDeviceAddress m_vbAttribTexcoord;
        VkDeviceAddress m_indexBuffer;
        uint64_t m_vertexOffset = 0;
        uint64_t m_indexOffset = 0;

        int m_blasMatrixIndex = 0;  // BLAS transform �Őݒ肷��s��.
        int m_materialIndex = 0;    // �`��Ŏg�p����}�e���A���̃C���f�b�N�X�l.
        uint32_t m_vertexCount = 0;
        uint32_t m_indexCount = 0;

        const size_t strideP = sizeof(glm::vec3);
        const size_t strideN = sizeof(glm::vec3);
        const size_t strideT = sizeof(glm::vec2);
        const size_t strideIdx = sizeof(uint32_t);
    };

    // �e�q���m�[�h�̃��[���h�s����X�V����.
    void UpdateMatrices();

    // �e�s��̕ύX��GPU�̃o�b�t�@�֔��f����.
    void ApplyTransform(VkGraphicsDevice& device);

    // ApplyTransform �̓��e�� BLAS ���X�V.
    void UpdateBlas(VkCommandBuffer command);

    // �w�肳�ꂽ�m�[�h������.
    std::shared_ptr<ModelNode> SearchNode(const std::wstring& name) const;

    // ����� BLAS �̐����擾.
    virtual int GetSubMeshCount() const override;

    // �X�L�j���O���f���ł��邩.
    bool IsSkinned() const { return m_isSkinned; }

    // �X�L�j���O���_�����擾.
    int  GetSkinnedVertexCount() const { return m_skinVertexCount; }

    // �X�L�j���O�v�Z�Ŏg�p����.
    // �v�Z�̓R���s���[�g�V�F�[�_�[�ɂ�点��.
    vk::BufferResource GetPositionBufferSrc() const;    // �ό`�O�ʒu.
    vk::BufferResource GetNormalBufferSrc() const;      // �ό`�O�@��.
    vk::BufferResource GetJointIndicesBuffer() const;   // �W���C���g�̃C���f�b�N�X�l.
    vk::BufferResource GetJointWeightsBuffer() const;   // �W���C���g�̃E�F�C�g�l.
    const util::DynamicBuffer& GetJointMatricesBuffer() const;  // �W���C���g�̍s��(�X�L�j���O�s��)�o�b�t�@.
    vk::BufferResource GetPositionTransformedBuffer() const;    // �ό`��̒��_�ʒu�o�b�t�@.
    vk::BufferResource GetNormalTransformedBuffer() const;      // �ό`��̒��_�@���o�b�t�@.
private:
    void CreateNodes(const util::VkrModel* model);
    void CreateTextures(VkGraphicsDevice& device, const std::vector<util::VkrModel::ImageInfo>& images, MaterialManager& materialManager);
    void CreateMaterials(const util::VkrModel* model, MaterialManager& materialManager);

    void AllocateBlasTransformMatrices(VkGraphicsDevice& device, const util::VkrModel* model);
    void AllocateTransformedBuffer(VkGraphicsDevice& device, uint64_t size);

    std::vector<std::shared_ptr<ModelNode>> m_nodes;
    std::vector<std::shared_ptr<ModelNode>> m_blasNodes;    // BLAS�\�z���ɎQ�Ƃ���m�[�h.
    std::vector<std::shared_ptr<Material>> m_materials;
    std::vector<std::shared_ptr<ModelNode>> m_skinJoints;   // �X�L�j���O�Ɋ֘A����W���C���g(�m�[�h) �̎Q��.
    std::vector<glm::mat4> m_invBindMatrices;               // �X�L�j���O�o�C���h�t�s��.

    std::vector<MeshInfo> m_meshes;
    util::DynamicBuffer m_blasTransformMatrices;// BLAS �̐������Ɏg���s��w��o�b�t�@.


    // --- ���f���N���X����̏����R�s�[ (�{�N���X�ŉ���s�v) ---
    vk::BufferResource m_positionBuffer;        // ���_�ʒu�o�b�t�@.
    vk::BufferResource m_normalBuffer;          // ���_�@���o�b�t�@.
    vk::BufferResource m_texcoordBuffer;        // ���_UV�o�b�t�@.
    vk::BufferResource m_jointWeightsBuffer;    // �X�L�j���O.�W���C���g�d�݃o�b�t�@.
    vk::BufferResource m_jointIndicesBuffer;    // �X�L�j���O.�W���C���g�C���f�b�N�X�o�b�t�@.
    vk::BufferResource m_indexBuffer;           // �C���f�b�N�X�o�b�t�@.

    // --- �X�L�j���O���f���p. �ʂ̃��\�[�X�ɂȂ�̂Ŗ{�N���X�ŉ���K�v ---
    vk::BufferResource m_positionTransformed;   // �ό`�㒸�_�ʒu�o�b�t�@.
    vk::BufferResource m_normalTransformed;     // �ό`��@���o�b�t�@.
    util::DynamicBuffer m_jointMatricesBuffer;   // �X�L�j���O�v�Z�̍s��i�[�o�b�t�@.

    bool m_isSkinned = false;
    int m_skinVertexCount = 0;
    VkBuildAccelerationStructureFlagsKHR m_blasBuildFlags = 0;
};