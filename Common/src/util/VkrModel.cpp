#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include "util/VkrModel.h"

#include <filesystem>
#include <fstream>
#include <vector>
#include <queue>

#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>

#define TINYGLTF_IMPLEMENTATION
//#define STB_IMAGE_IMPLEMENTATION   // Device クラス側で実装しているため.
#define TINYGLTF_NO_STB_IMAGE_WRITE  // 書き出しを使用しないため.
#include "tiny_gltf.h"


namespace util {
    using namespace glm;
    using namespace tinygltf;

    static vec3 makeFloat3(const double* in)
    {
        vec3 v{
            static_cast<float>(in[0]),
            static_cast<float>(in[1]),
            static_cast<float>(in[2]),
        };
        return v;
    }

    static quat makeQuat(const double* in)
    {
        quat q{
            static_cast<float>(in[3]),
            static_cast<float>(in[0]),
            static_cast<float>(in[1]),
            static_cast<float>(in[2]),
        };
        return q;
    }
    
    VkrModel::Node::Node()
    {
        translation = vec3(0.0f);
        rotation = quat(1.0f, 0.0f,0.0f,0.0f);
        scale = vec3(1.0f);
        mtxLocal = mat4(1.0f);
        mtxWorld = mat4(1.0f);
        name = L"";
        parent = nullptr;
    }
    VkrModel::Node::~Node()
    {
    }


    VkrModel::VkrModel()
    {
    }

    VkrModel::~VkrModel()
    {
    }

    void VkrModel::Destroy(VkGraphicsDevice& device)
    {
        m_nodes.clear();
        m_images.clear();
        m_textures.clear();
        m_materials.clear();

        device->DestroyBuffer(m_vertexAttrib.position);
        device->DestroyBuffer(m_vertexAttrib.normal);
        device->DestroyBuffer(m_vertexAttrib.texcoord);
        device->DestroyBuffer(m_vertexAttrib.jointIndices);
        device->DestroyBuffer(m_vertexAttrib.jointWeights);
        device->DestroyBuffer(m_indexBuffer);

    }

    bool VkrModel::LoadFromGltf(
        const std::wstring& fileName, VkGraphicsDevice& device)
    {
        std::filesystem::path filePath(fileName);
        std::vector<char> buffer;
        util::LoadFile(buffer, fileName);

        std::string baseDir;
        if (filePath.is_relative()) {
            auto current = std::filesystem::current_path();
            current /= filePath;
            current.swap(filePath);
        }
        baseDir = filePath.parent_path().string();

        std::string err, warn;
        TinyGLTF loader;
        Model model;
        bool result = false;
        if (filePath.extension() == L".glb") {
            result = loader.LoadBinaryFromMemory(&model, &err, &warn,
                reinterpret_cast<const uint8_t*>(buffer.data()),
                uint32_t(buffer.size()), baseDir);
        }
        if (!warn.empty()) {
            OutputDebugStringA(warn.c_str());
        }
        if (!err.empty()) {
            OutputDebugStringA(err.c_str());
        }
        if (!result) {
            return false;
        }

        VertexAttributeVisitor visitor;
        const auto& scene = model.scenes[0];
        for (const auto& nodeIndex : scene.nodes) {
            m_rootNodes.push_back(nodeIndex);
        }

        LoadNode(model);
        LoadMesh(model, visitor);
        LoadSkin(model);
        LoadMaterial(model);

        auto memProps = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        auto usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | 
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
            | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        auto sizePos = sizeof(vec3) * visitor.positionBuffer.size();
        auto sizeNrm = sizeof(vec3) * visitor.normalBuffer.size();
        auto sizeTex = sizeof(vec2) * visitor.texcoordBuffer.size();

        // 頂点データの生成.
        m_vertexAttrib.position = device->CreateBuffer(sizePos, usage, memProps);
        device->WriteToBuffer(m_vertexAttrib.position, visitor.positionBuffer.data(), sizePos);

        m_vertexAttrib.normal = device->CreateBuffer(sizeNrm, usage, memProps);
        device->WriteToBuffer(m_vertexAttrib.normal, visitor.normalBuffer.data(), sizeNrm);

        m_vertexAttrib.texcoord = device->CreateBuffer(sizeTex, usage, memProps);
        device->WriteToBuffer(m_vertexAttrib.texcoord, visitor.texcoordBuffer.data(), sizeTex);

        // インデックスバッファ.
        auto sizeIdx = sizeof(uint32_t) * visitor.indexBuffer.size();
        m_indexBuffer = device->CreateBuffer(sizeIdx, usage, memProps);
        device->WriteToBuffer(m_indexBuffer, visitor.indexBuffer.data(), sizeIdx);

        // スキニングモデル用.
        if (m_hasSkin) {
            auto sizeJoint = sizeof(uvec4) * visitor.jointBuffer.size();
            auto sizeWeight = sizeof(vec4) * visitor.weightBuffer.size();
            m_vertexAttrib.jointIndices = device->CreateBuffer(sizeJoint, usage, memProps);
            device->WriteToBuffer(m_vertexAttrib.jointIndices, visitor.jointBuffer.data(), sizeJoint);

            m_vertexAttrib.jointWeights = device->CreateBuffer(sizeWeight, usage, memProps);
            device->WriteToBuffer(m_vertexAttrib.jointWeights, visitor.weightBuffer.data(), sizeWeight);

            // 個数をスキニングで使用する頂点数とする.
            //   (Position と同じ個数となっているものを対象としているのでこれでよい)
            m_skinInfo.skinVertexCount = UINT(visitor.jointBuffer.size());
        }


        for (auto& image : model.images) {
            auto fileName = util::ConvertFromUTF8(image.name);
            auto view = model.bufferViews[image.bufferView];
            auto offsetBytes = view.byteOffset;
            const void* src = &model.buffers[view.buffer].data[offsetBytes];

            m_images.emplace_back();
            auto& info = m_images.back();
            info.fileName = fileName;
            info.imageBuffer.resize(view.byteLength);
            memcpy(info.imageBuffer.data(), src, view.byteLength);
        }

        for (auto& texture : model.textures) {
            m_textures.emplace_back();
            auto& info = m_textures.back();
            info.imageIndex = texture.source;   // 参照する画像データへのインデックス.
        }

        return true;
    }

    std::vector<std::wstring> VkrModel::GetJointNodeNames() const
    {
        std::vector<std::wstring> nameList;
        for (auto nodeIndex : m_skinInfo.joints) {
            nameList.emplace_back(m_nodes[nodeIndex]->GetName());
        }
        return nameList;
    }

    std::vector<glm::mat4> VkrModel::GetInvBindMatrices() const
    {
        return m_skinInfo.invBindMatrices;
    }

    void VkrModel::LoadNode(const tinygltf::Model& inModel)
    {
        for (auto& inNode : inModel.nodes) {
            m_nodes.emplace_back(new Node());
            auto node = m_nodes.back();

            node->name = util::ConvertFromUTF8(inNode.name);
            if (!inNode.translation.empty()) {
                node->translation = makeFloat3(inNode.translation.data());
            }
            if (!inNode.scale.empty()) {
                node->scale = makeFloat3(inNode.scale.data());
            }
            if (!inNode.rotation.empty()) {
                node->rotation = makeQuat(inNode.rotation.data());
            }
            for (auto& c : inNode.children) {
                node->children.push_back(c);
            }
            node->meshIndex = inNode.mesh;
        }
    }

    void VkrModel::LoadMesh(const tinygltf::Model& inModel, VertexAttributeVisitor& visitor)
    {
        auto& indexBuffer = visitor.indexBuffer;
        auto& positionBuffer = visitor.positionBuffer;
        auto& normalBuffer = visitor.normalBuffer;
        auto& texcoordBuffer = visitor.texcoordBuffer;
        auto& jointBuffer = visitor.jointBuffer;
        auto& weightBuffer = visitor.weightBuffer;
        for (auto& inMesh : inModel.meshes) {
            m_meshGroups.emplace_back(MeshGroup());
            auto& meshgrp = m_meshGroups.back();

            for (auto& primitive : inMesh.primitives) {
                auto indexStart = static_cast<UINT>(indexBuffer.size());
                auto vertexStart = static_cast<UINT>(positionBuffer.size());
                UINT indexCount = 0, vertexCount = 0;
                bool hasSkin = false;

                const auto& notfound = primitive.attributes.end();
                if (auto attr = primitive.attributes.find("POSITION"); attr != notfound) {
                    auto& acc = inModel.accessors[attr->second];
                    auto& view = inModel.bufferViews[acc.bufferView];
                    auto offsetBytes = acc.byteOffset + view.byteOffset;
                    const auto* src = reinterpret_cast<const vec3*>(&(inModel.buffers[view.buffer].data[offsetBytes]));

                    vertexCount = UINT(acc.count);
                    for (UINT i = 0; i < vertexCount; ++i) {
                        positionBuffer.push_back(src[i]);
                    }
                }
                if (auto attr = primitive.attributes.find("NORMAL"); attr != notfound) {
                    auto& acc = inModel.accessors[attr->second];
                    auto& view = inModel.bufferViews[acc.bufferView];
                    auto offsetBytes = acc.byteOffset + view.byteOffset;
                    const auto* src = reinterpret_cast<const vec3*>(&(inModel.buffers[view.buffer].data[offsetBytes]));

                    vertexCount = UINT(acc.count);
                    for (UINT i = 0; i < vertexCount; ++i) {
                        normalBuffer.push_back(src[i]);
                    }
                }
                if (auto attr = primitive.attributes.find("TEXCOORD_0"); attr != notfound) {
                    auto& acc = inModel.accessors[attr->second];
                    auto& view = inModel.bufferViews[acc.bufferView];
                    auto offsetBytes = acc.byteOffset + view.byteOffset;
                    const auto* src = reinterpret_cast<const vec2*>(&(inModel.buffers[view.buffer].data[offsetBytes]));

                    for (UINT i = 0; i < vertexCount; ++i) {
                        texcoordBuffer.push_back(src[i]);
                    }
                } else {
                    // UV データが無い場合には、他のものと合わせるべくゼロで埋めておく.
                    for (UINT i = 0; i < vertexCount; ++i) {
                        texcoordBuffer.push_back(vec2(0.0f, 0.0f));
                    }
                }

                // スキニング用のジョイント(インデックス)番号とウェイト値を読み取る.
                if (auto attr = primitive.attributes.find("JOINTS_0"); attr != notfound) {
                    auto& acc = inModel.accessors[attr->second];
                    auto& view = inModel.bufferViews[acc.bufferView];
                    auto offsetBytes = acc.byteOffset + view.byteOffset;
                    const auto* src = reinterpret_cast<const uint16_t*>(&(inModel.buffers[view.buffer].data[offsetBytes]));

                    for (UINT i = 0; i < vertexCount; ++i) {
                        auto idx = i * 4;
                        auto v = uvec4(
                            src[idx + 0], src[idx + 1], src[idx + 2], src[idx + 3]);
                        jointBuffer.push_back(v);
                    }
                }
                if (auto attr = primitive.attributes.find("WEIGHTS_0"); attr != notfound) {
                    auto& acc = inModel.accessors[attr->second];
                    auto& view = inModel.bufferViews[acc.bufferView];
                    auto offsetBytes = acc.byteOffset + view.byteOffset;
                    const auto* src = reinterpret_cast<const vec4*>(&(inModel.buffers[view.buffer].data[offsetBytes]));

                    for (UINT i = 0; i < vertexCount; ++i) {
                        weightBuffer.push_back(src[i]);
                    }
                }

                //　インデックスバッファ用.
                {
                    auto& acc = inModel.accessors[primitive.indices];
                    const auto& view = inModel.bufferViews[acc.bufferView];
                    const auto& buffer = inModel.buffers[view.buffer];
                    indexCount = UINT(acc.count);
                    auto offsetBytes = acc.byteOffset + view.byteOffset;
                    if (acc.componentType == TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT) {
                        auto src = reinterpret_cast<const uint32_t*>(&(buffer.data[offsetBytes]));

                        for (size_t index = 0; index < acc.count; index++)                         {
                            indexBuffer.push_back(src[index]);
                        }
                    }
                    if (acc.componentType == TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT) {
                        auto src = reinterpret_cast<const uint16_t*>(&(buffer.data[offsetBytes]));
                        for (size_t index = 0; index < acc.count; index++)                         {
                            indexBuffer.push_back(src[index]);
                        }
                    }
                }

                meshgrp.m_meshes.emplace_back(Mesh());
                auto& mesh = meshgrp.m_meshes.back();
                mesh.indexStart = indexStart;
                mesh.vertexStart = vertexStart;
                mesh.indexCount = indexCount;
                mesh.vertexCount = vertexCount;
                mesh.materialIndex = primitive.material;
            }
        }

        for (UINT nodeIndex = 0; nodeIndex < UINT(inModel.nodes.size()); ++nodeIndex) {
            auto meshIndex = inModel.nodes[nodeIndex].mesh;
            if (meshIndex < 0) {
                continue;
            }
            m_meshGroups[meshIndex].m_nodeIndex = nodeIndex;
        }
    }

    void VkrModel::LoadSkin(const tinygltf::Model& inModel)
    {
        if (inModel.skins.empty()) {
            m_hasSkin = false;
            return;
        }
        m_hasSkin = true;
        // 本処理ではスキンデータの最初の1つのみを取り扱う.
        const auto& inSkin = inModel.skins[0];

        m_skinInfo.name = ConvertFromUTF8(inSkin.name);
        m_skinInfo.joints.assign(inSkin.joints.begin(), inSkin.joints.end());

        if (inSkin.inverseBindMatrices > -1) {
            const auto& acc = inModel.accessors[inSkin.inverseBindMatrices];
            const auto& view = inModel.bufferViews[acc.bufferView];
            const auto& buffer = inModel.buffers[view.buffer];
            m_skinInfo.invBindMatrices.resize(acc.count);

            auto offsetBytes = acc.byteOffset + view.byteOffset;
            memcpy(
                m_skinInfo.invBindMatrices.data(),
                &buffer.data[offsetBytes],
                acc.count * sizeof(mat4));
        }
    }

    void VkrModel::LoadMaterial(const tinygltf::Model& inModel)
    {
        for (const auto& inMaterial : inModel.materials) {
            m_materials.emplace_back(Material());
            auto& material = m_materials.back();
            material.m_name = util::ConvertFromUTF8(inMaterial.name);

            for (auto& value : inMaterial.values) {
                auto valueName = value.first;
                if (valueName == "baseColorTexture") {
                    auto textureIndex = value.second.TextureIndex();
                    material.m_textureIndex = textureIndex;
                }
                if (valueName == "normalTexture") {
                    auto textureIndex = value.second.TextureIndex();
                }
                if (valueName == "baseColorFactor") {
                    auto color = value.second.ColorFactor();
                    material.m_diffuseColor = vec3(
                        float(color[0]), float(color[1]), float(color[2])
                    );
                }
            }
        }
    }
}
