#include "util/VkrModel.h"
#include "scene/ModelMesh.h"
#include <glm/gtx/transform.hpp>

#include <sstream>

#if _DEBUG
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif

void ModelMesh::ModelNode::AddChild(std::shared_ptr<ModelNode> node)
{
    m_children.push_back(node);
}

void ModelMesh::ModelNode::SetParent(std::shared_ptr<ModelNode> parent)
{
    m_parent = parent;
}

std::shared_ptr<ModelMesh::ModelNode> ModelMesh::ModelNode::SearchNode(const std::wstring& name) const
{
    for (const auto& node : m_children) {
        if (node->GetName() == name) {
            return node;
        }
        auto ret = node->SearchNode(name);
        if (ret != nullptr) {
            return ret;
        }
    }
    return nullptr;
}

void ModelMesh::ModelNode::UpdateLocalMatrix()
{
    auto mtxT = glm::translate(m_translation);
    auto mtxR = glm::toMat4(m_rotation);
    auto mtxS = glm::scale(m_scale);
    m_mtxLocal = mtxT * mtxR * mtxS;
}

void ModelMesh::ModelNode::UpdateWorldMatrix(glm::mat4 mtxParent)
{
    m_mtxWorld = mtxParent * m_mtxLocal;
}

void ModelMesh::ModelNode::UpdateMatrices(glm::mat4 mtxParent)
{
    UpdateLocalMatrix();
    UpdateWorldMatrix(mtxParent);
    for (auto& child : m_children) {
        child->UpdateMatrices(GetWorldMatrix());
    }
}


void ModelMesh::Create(VkGraphicsDevice& device, const CreateInfo& createInfo, MaterialManager& materialManager)
{
    const auto model = createInfo.model;
    assert(model != nullptr);

    // 親子ノードを構築する.
    CreateNodes(model);

    // 本モデルのテクスチャを準備する.
    CreateTextures(device, model->GetImages(), materialManager);

    // マテリアルを生成.
    CreateMaterials(model, materialManager);

    SetWorldMatrix(glm::mat4(1.0f));
    UpdateMatrices();

    // BLAS 構築時に使う行列バッファを準備.
    AllocateBlasTransformMatrices(device, model);

    // BLAS に設定する行列を持つノードの集合を準備.
    const auto blasGroup = model->GetMeshGroups();
    for (auto group : blasGroup) {
        auto node = model->GetNode(group.GetNode());
        auto target = SearchNode(node->GetName());
        assert(target != nullptr);
        m_blasNodes.push_back(target);
    }

    m_isSkinned = model->IsSkinned();

    m_positionBuffer = model->GetPositionBuffer();
    m_normalBuffer = model->GetNormalBuffer();
    m_texcoordBuffer = model->GetTexcoordBuffer();
    m_indexBuffer = model->GetIndexBuffer();

    auto deviceAddrPos = m_positionBuffer.GetDeviceAddress();
    auto deviceAddrNrm = m_normalBuffer.GetDeviceAddress();
    auto deviceAddrTex = m_texcoordBuffer.GetDeviceAddress();
    auto deviceAddrIdx = m_indexBuffer.GetDeviceAddress();

    if (model->IsSkinned()) {
        m_jointWeightsBuffer = model->GetJointWeightsBuffer();
        m_jointIndicesBuffer = model->GetJointIndicesBuffer();

        // スキニングモデルでは、変形後のバッファを描画で使う.
        m_skinVertexCount = model->GetSkinnedVertexCount();
        auto bufferSize = sizeof(glm::vec3) * m_skinVertexCount;
        AllocateTransformedBuffer(device, bufferSize);
        deviceAddrPos = GetPositionTransformedBuffer().GetDeviceAddress();
        deviceAddrNrm = GetNormalTransformedBuffer().GetDeviceAddress();
    }

    const auto materials = model->GetMaterials();
    for (int i = 0; i<int(model->GetMeshGroupCount()); ++i) {
        const auto subBlasIndex = i;
        const auto group = model->GetMeshGroups()[subBlasIndex];
        for (const auto& mesh : group.GetMeshes()) {
            const auto& material = m_materials[mesh.materialIndex];
            auto materialRegistered = materialManager.GetMaterialIndex(material->GetName());
            if (materialRegistered < 0) {
                materialRegistered = materialManager.AddMaterial(material);
            }
            
            MeshInfo meshInfo{};
            meshInfo.SetBlasMatrixIndex(subBlasIndex);
            meshInfo.SetMaterialIndex(materialRegistered);
            meshInfo.SetPositionBuffer(deviceAddrPos);
            meshInfo.SetNormalBuffer(deviceAddrNrm);
            meshInfo.SetTexcoordBuffer(deviceAddrTex);
            meshInfo.SetIndexBuffer(deviceAddrIdx);

            meshInfo.SetVertexOffset(mesh.vertexStart);
            meshInfo.SetIndexOffset(mesh.indexStart);
            meshInfo.SetVertexCount(mesh.vertexCount);
            meshInfo.SetIndexCount(mesh.indexCount);

            m_meshes.emplace_back(meshInfo);
        }
    }

    if (model->IsSkinned()) {
        // スキニングモデルではスキニング行列計算のための行列バッファが必要.
        const auto jointList = model->GetJointNodeNames();
        auto jointCount = jointList.size();
        auto size = jointCount * sizeof(glm::mat4);
        auto usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        m_jointMatricesBuffer.Initialize(device, size, usage);

        // スキニングモデルの行列計算のための情報をセット.
        for (const auto& nodeName : jointList) {
            auto nodeTarget = SearchNode(nodeName);
            m_skinJoints.push_back(nodeTarget);
        }
        m_invBindMatrices = model->GetInvBindMatrices();
    }

#if 0 // 描画前に変形バッファを更新するので,ここは無くても動く.
    if (IsSkinned()) {
        // バッファをコピーしておく.
        auto stride = sizeof(glm::vec3);
        auto command = device->CreateCommandBuffer();
        // 初期値をコピーする.
        VkBufferCopy region{};
        region.size = GetSkinnedVertexCount() * stride;

        vkCmdCopyBuffer(
            command, m_positionBuffer.GetBuffer(), m_positionTransformed.GetBuffer(), 1, &region);
        vkCmdCopyBuffer(
            command, m_normalBuffer.GetBuffer(), m_normalTransformed.GetBuffer(), 1, &region);
        vkEndCommandBuffer(command);
        device->SubmitAndWait(command);
        device->DestroyCommandBuffer(command);
    }
#endif

    // 行列を各バッファに適用・反映する.
    ApplyTransform(device);
}

void ModelMesh::Destroy(std::unique_ptr<vk::GraphicsDevice>& device)
{
    m_blas.Destroy(device);
    m_blasTransformMatrices.Destroy(device);
    m_jointMatricesBuffer.Destroy(device);
    
    if (IsSkinned()) {
        device->DestroyBuffer(m_positionTransformed);
        device->DestroyBuffer(m_normalTransformed);
    }
}

void ModelMesh::BuildAS(VkGraphicsDevice& device, VkBuildAccelerationStructureFlagsKHR buildFlags)
{
    AccelerationStructure::Input blasInput;
    blasInput.asGeometry = GetAccelerationStructureGeometry();
    blasInput.asBuildRangeInfo = GetAccelerationStructureBuildRangeInfo();

    m_blas.BuildAS(device, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, blasInput, buildFlags);
    m_blas.DestroyScratchBuffer(device);

    m_asInstance.accelerationStructureReference = m_blas.GetDeviceAddress();
    m_blasBuildFlags = buildFlags;
}

std::vector<VkAccelerationStructureGeometryKHR> ModelMesh::GetAccelerationStructureGeometry(int frameIndex)
{
    std::vector<VkAccelerationStructureGeometryKHR> asGeometries;

    for (const auto& mesh : m_meshes) {
        VkAccelerationStructureGeometryKHR asGeometry{
            VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR
        };
        asGeometry.flags = m_geometryFlags;
        asGeometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
        // VkAccelerationStructureGeometryTrianglesDataKHR
        auto& triangles = asGeometry.geometry.triangles;
        triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
        triangles.vertexStride = sizeof(glm::vec3);
        triangles.vertexData.deviceAddress = mesh.GetPositionOffseted();
        triangles.maxVertex = mesh.GetVertexCount();

        triangles.indexType = VK_INDEX_TYPE_UINT32;
        triangles.indexData.deviceAddress = mesh.GetIndexOffseted();
        triangles.transformData.deviceAddress = m_blasTransformMatrices.GetDeviceAddress(frameIndex);

        asGeometries.emplace_back(asGeometry);
    }

    return asGeometries;
}

std::vector<VkAccelerationStructureBuildRangeInfoKHR> ModelMesh::GetAccelerationStructureBuildRangeInfo()
{
    std::vector<VkAccelerationStructureBuildRangeInfoKHR> asBuildRanges;

    for (const auto& mesh : m_meshes) {
        VkAccelerationStructureBuildRangeInfoKHR asBuildRangeInfo{};
        asBuildRangeInfo.primitiveCount = mesh.GetIndexCount() / 3;
        asBuildRangeInfo.primitiveOffset = 0;
        asBuildRangeInfo.firstVertex = 0;
        asBuildRangeInfo.transformOffset = mesh.GetBlasMatrixIndex() * sizeof(glm::mat3x4);
        asBuildRanges.emplace_back(asBuildRangeInfo);
    }
    return asBuildRanges;
}

std::vector<SceneObject::SceneObjectParameter> ModelMesh::GetSceneObjectParameters()
{
    std::vector<SceneObjectParameter> params;

    for (const auto& m : m_meshes) {
        SceneObjectParameter objParam{};
        objParam.blasMatrixIndex = m.GetBlasMatrixIndex();
        objParam.materialIndex = m.GetMaterialIndex();
        objParam.vertexPosition = m.GetPositionOffseted();
        objParam.vertexNormal = m.GetNormalOffseted();
        objParam.vertexTexcoord = m.GetTexcoordOffseted();
        objParam.indexBuffer = m.GetIndexOffseted();
        objParam.blasTransformMatrices = m_blasTransformMatrices.GetDeviceAddress(0);
        objParam.blasMatrixStride = GetSubMeshCount() * sizeof(glm::mat3x4);

        params.emplace_back(objParam);
    }
    return params;
}

void ModelMesh::UpdateMatrices()
{
    for (auto& node : m_nodes) {
        node->UpdateMatrices(m_transform);
    }
}

void ModelMesh::ApplyTransform(VkGraphicsDevice& device)
{
    auto frameIndex = device->GetCurrentFrameIndex();
    if (IsSkinned()) {
        const auto jointCount = m_skinJoints.size();
        auto meshAttached = m_blasNodes[0];
        auto meshInvMatrix = glm::inverse(meshAttached->GetWorldMatrix());

        std::vector<glm::mat4> matrices(jointCount);
        for (int i = 0; i<int(jointCount); ++i) {
            auto node = m_skinJoints[i];
            auto mtx = meshInvMatrix * node->GetWorldMatrix() * m_invBindMatrices[i];
            matrices[i] = mtx;
        }

        // スキニング行列をバッファへ反映.
        auto dst = m_jointMatricesBuffer.Map(frameIndex);
        memcpy(dst, matrices.data(), sizeof(glm::mat4) * jointCount);
    }

    // BLAS 生成・更新で使用する行列バッファを更新する.
    std::vector<glm::mat3x4> blasMatices;
    for (auto node : m_blasNodes) {
        auto mtx = node->GetWorldMatrix();
        if (IsSkinned()) {
            blasMatices.emplace_back(glm::transpose(mtx));
        } else {
            // TLAS で設定した行列分を打ち消すために設定.
            auto invRoot = glm::inverse(m_transform);
            blasMatices.emplace_back(
                glm::transpose(invRoot * mtx) );
        }
    }

    auto size = sizeof(glm::mat3x4) * blasMatices.size();
    auto dst = m_blasTransformMatrices.Map(frameIndex);
    memcpy(dst, blasMatices.data(), size);
}

void ModelMesh::UpdateBlas(VkCommandBuffer command)
{
    AccelerationStructure::Input blasInput;
    blasInput.asGeometry = GetAccelerationStructureGeometry();
    blasInput.asBuildRangeInfo = GetAccelerationStructureBuildRangeInfo();

    m_blas.Update(command, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, blasInput, m_blasBuildFlags);
}

std::shared_ptr<ModelMesh::ModelNode> ModelMesh::SearchNode(const std::wstring& name) const
{
    for (const auto node : m_nodes) {
        if (node->GetName() == name) {
            return node;
        }
        auto ret = node->SearchNode(name);
        if (ret != nullptr) {
            return ret;
        }
    }
    return nullptr;
}

int ModelMesh::GetSubMeshCount() const
{
    return int(m_blasNodes.size());
}

vk::BufferResource ModelMesh::GetPositionBufferSrc() const
{
    return m_positionBuffer;
}

vk::BufferResource ModelMesh::GetNormalBufferSrc() const
{
    return m_normalBuffer;
}

vk::BufferResource ModelMesh::GetJointIndicesBuffer() const
{
    return m_jointIndicesBuffer;
}

vk::BufferResource ModelMesh::GetJointWeightsBuffer() const
{
    return m_jointWeightsBuffer;
}

const util::DynamicBuffer& ModelMesh::GetJointMatricesBuffer() const
{
    return m_jointMatricesBuffer;
}

vk::BufferResource ModelMesh::GetPositionTransformedBuffer() const
{
    return m_positionTransformed;
}

vk::BufferResource ModelMesh::GetNormalTransformedBuffer() const
{
    return m_normalTransformed;
}

void ModelMesh::CreateNodes(const util::VkrModel* model)
{
    const auto nodeCount = int(model->GetNodeCount());
    std::vector<std::shared_ptr<ModelNode>> nodes;
    nodes.resize(nodeCount);

    for (auto i = 0; i<nodeCount; ++i) {
        const auto& src = model->GetNode(i);
        nodes[i] = std::make_shared<ModelNode>(src->GetName());
        nodes[i]->SetTranslation(src->GetTranslation());
        nodes[i]->SetRotation(src->GetRotation());
        nodes[i]->SetScale(src->GetScale());
    }
    // 親子解決.
    for (auto i = 0; i < nodeCount; ++i) {
        const auto& src = model->GetNode(i);
        auto node = nodes[i];
        for (auto idx : src->GetChildren()) {
            auto child = nodes[idx];
            node->AddChild(child);
            child->SetParent(node);
        }
    }
    for (auto i : model->GetRootNodes()) {
        m_nodes.push_back(nodes[i]);
    }
}

void ModelMesh::CreateTextures(VkGraphicsDevice& device, const std::vector<util::VkrModel::ImageInfo>& images, MaterialManager& materialManager)
{
    auto usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    auto memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    for (auto img : images) {
        auto id = materialManager.GetTexture(img.fileName);
        if (id < 0) {
            auto texture = device->CreateTexture2DFromMemory(
                img.imageBuffer.data(), img.imageBuffer.size(), usage, memProps
            );
            // マネージャーに登録.
            materialManager.AddTexture(img.fileName, texture);

#if _DEBUG
            std::wostringstream ss;
            ss << L"Load Texture file (in model): " << img.fileName << std::endl;
            OutputDebugStringW(ss.str().c_str());
#endif
        }
    }
}

void ModelMesh::CreateMaterials(const util::VkrModel* model, MaterialManager& materialManager)
{
    const auto modelTextures = model->GetTextures();
    const auto modelImages = model->GetImages();
    for (auto& m : model->GetMaterials()) {
        m_materials.emplace_back(std::make_shared<Material>(m.GetName().c_str()));
        auto& material = m_materials.back();

        material->SetDiffuse(m.GetDiffuseColor());
        material->SetSpecular(glm::vec3(1.0f));     // スペキュラカラーは固定.
        material->SetSpecularPower(50.0f);          // スペキュラパワーは固定.
        material->SetType(1);   // Phong を指定の意味.

        // テクスチャの名前からマネージャーに登録済みのインデックスを取得する.
        auto textureIndex = m.GetTextureIndex();
        if (textureIndex >= 0) {
            auto ti = modelTextures[textureIndex];
            auto imageInfo = modelImages[ti.imageIndex];

            auto indexRegistered= materialManager.GetTexture(imageInfo.fileName);
            material->SetTexture(indexRegistered);
        }
    }
}

void ModelMesh::AllocateBlasTransformMatrices(VkGraphicsDevice& device, const util::VkrModel* model)
{
    auto matrixCount = model->GetMeshGroupCount();
    auto usage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    auto size = sizeof(glm::mat3x4) * matrixCount;
    m_blasTransformMatrices.Initialize(device, size, usage);
}

void ModelMesh::AllocateTransformedBuffer(VkGraphicsDevice& device, uint64_t size)
{
    auto memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    auto usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    m_positionTransformed = device->CreateBuffer(size, usage, memProps);
    m_normalTransformed = device->CreateBuffer(size, usage, memProps);
}

