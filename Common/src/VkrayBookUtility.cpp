#include "VkrayBookUtility.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <fstream>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

bool util::LoadFile(std::vector<char>& out, const std::wstring& fileName)
{
    std::ifstream infile(fileName, std::ifstream::binary);
    if (!infile) {
        return false;
    }
    out.resize(infile.seekg(0, std::ifstream::end).tellg());
    infile.seekg(0, std::ifstream::beg).read(out.data(), out.size());

    return true;
}

VkPipelineShaderStageCreateInfo util::LoadShader(std::unique_ptr<vk::GraphicsDevice>& device, const std::wstring& fileName, VkShaderStageFlagBits stage)
{
    VkPipelineShaderStageCreateInfo shaderStage{
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr
    };

    std::vector<char> shaderSpv;
    if (!LoadFile(shaderSpv, fileName)) {
        return shaderStage;
    }

    VkShaderModuleCreateInfo moduleCI{
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO, nullptr
    };
    moduleCI.codeSize = uint32_t(shaderSpv.size());
    moduleCI.pCode = reinterpret_cast<const uint32_t*>(shaderSpv.data());

    VkShaderModule shaderModule;
    vkCreateShaderModule(device->GetDevice(), &moduleCI, nullptr, &shaderModule);

    shaderStage.stage = stage;
    shaderStage.pName = "main";
    shaderStage.module = shaderModule;

    return shaderStage;
}

VkTransformMatrixKHR util::ConvertTransform(const glm::mat4x3& m)
{
    VkTransformMatrixKHR mtx{};
    auto mT = glm::transpose(m);
    memcpy(&mtx.matrix[0], &mT[0], sizeof(float) * 4);
    memcpy(&mtx.matrix[1], &mT[1], sizeof(float) * 4);
    memcpy(&mtx.matrix[2], &mT[2], sizeof(float) * 4);
    return mtx;
}

std::wstring util::ConvertFromUTF8(const std::string& s)
{
    DWORD dwRet = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, NULL, 0);
    std::vector<wchar_t> buf(dwRet);
    dwRet = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, buf.data(), int(buf.size() - 1));
    return std::wstring(buf.data());
}

// ------------------------------------------
// Helper Function
// ------------------------------------------

VkRayTracingShaderGroupCreateInfoKHR util::CreateShaderGroupRayGeneration(uint32_t shaderIndex)
{
    VkRayTracingShaderGroupCreateInfoKHR shaderGroupCI{
        VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR
    };
    shaderGroupCI.generalShader = shaderIndex;
    shaderGroupCI.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    shaderGroupCI.closestHitShader = VK_SHADER_UNUSED_KHR;
    shaderGroupCI.anyHitShader = VK_SHADER_UNUSED_KHR;
    shaderGroupCI.intersectionShader = VK_SHADER_UNUSED_KHR;
    return shaderGroupCI;
}

VkRayTracingShaderGroupCreateInfoKHR util::CreateShaderGroupMiss(uint32_t shaderIndex)
{
    VkRayTracingShaderGroupCreateInfoKHR shaderGroupCI{
        VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR
    };
    shaderGroupCI.generalShader = shaderIndex;
    shaderGroupCI.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_GENERAL_KHR;
    shaderGroupCI.closestHitShader = VK_SHADER_UNUSED_KHR;
    shaderGroupCI.anyHitShader = VK_SHADER_UNUSED_KHR;
    shaderGroupCI.intersectionShader = VK_SHADER_UNUSED_KHR;
    return shaderGroupCI;
}

VkRayTracingShaderGroupCreateInfoKHR util::CreateShaderGroupHit(
    uint32_t closestHitShaderIndex, uint32_t anyHitShaderIndex, uint32_t intersectionShaderIndex)
{
    VkRayTracingShaderGroupCreateInfoKHR shaderGroupCI{
        VK_STRUCTURE_TYPE_RAY_TRACING_SHADER_GROUP_CREATE_INFO_KHR
    };
    shaderGroupCI.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_TRIANGLES_HIT_GROUP_KHR;

    if (intersectionShaderIndex != VK_SHADER_UNUSED_KHR) {
        shaderGroupCI.type = VK_RAY_TRACING_SHADER_GROUP_TYPE_PROCEDURAL_HIT_GROUP_KHR;
    }

    shaderGroupCI.generalShader = VK_SHADER_UNUSED_KHR;
    shaderGroupCI.closestHitShader = closestHitShaderIndex;
    shaderGroupCI.anyHitShader = anyHitShaderIndex;
    shaderGroupCI.intersectionShader = intersectionShaderIndex;
    return shaderGroupCI;
}




bool util::DynamicBuffer::Initialize(Device& device, size_t requestSize, VkBufferUsageFlags usage)
{
    auto bufferAlignment = device->GetUniformBufferAlignment();
    VkMemoryPropertyFlags memProps = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

    if (usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) {
        bufferAlignment = device->GetStorageBufferAlignment();
    }
    if (usage & VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR) {
        bufferAlignment = device->GetStorageBufferAlignment();
    }
    m_blockSize = (requestSize + bufferAlignment - 1) & ~(bufferAlignment - 1);
    const auto backBufferCount = device->GetBackBufferCount();
    auto bufferSizeWhole = backBufferCount * m_blockSize;

    m_buffer = device->CreateBuffer(
        bufferSizeWhole, usage, memProps);
    m_mappedPtr = device->Map(m_buffer);
    
    return m_mappedPtr != nullptr;
}

void util::DynamicBuffer::Destroy(Device& device)
{
    device->DestroyBuffer(m_buffer);
}

VkDeviceAddress util::DynamicBuffer::GetDeviceAddress(uint32_t bufferIndex) const
{
    return m_buffer.GetDeviceAddress() + GetOffset(bufferIndex);
}

void* util::DynamicBuffer::Map(uint32_t bufferIndex)
{
    return static_cast<uint8_t*>(m_mappedPtr) + GetOffset(bufferIndex);
}

VkDescriptorBufferInfo util::DynamicBuffer::GetDescriptor() const
{
    return VkDescriptorBufferInfo{
        m_buffer.GetBuffer(), 0, m_blockSize,
    };
}

uint32_t util::DynamicBuffer::GetOffset(uint32_t index) const
{
    return uint32_t(index * m_blockSize);
}


void util::primitive::GetPlane(std::vector<VertexPNC>& vertices, std::vector<uint32_t>& indices, float size)
{
    const auto white = vec4(1, 1, 1, 1);
    VertexPNC srcVertices[] = {
        VertexPNC{ {-1.0f, 0.0f,-1.0f }, { 0.0f, 1.0f, 0.0f }, white },
        VertexPNC{ {-1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f }, white },
        VertexPNC{ { 1.0f, 0.0f,-1.0f }, { 0.0f, 1.0f, 0.0f }, white },
        VertexPNC{ { 1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f }, white },
    };
    vertices.resize(4);
    std::transform(
        std::begin(srcVertices), std::end(srcVertices), vertices.begin(),
        [=](auto v) {
            v.Position.x *= size;
            v.Position.z *= size;
            return v;
        }
    );
    indices = { 0, 1, 2, 2, 1, 3 };
}

void util::primitive::GetPlane(std::vector<VertexPNT>& vertices, std::vector<uint32_t>& indices, float size)
{
    const auto white = vec4(1, 1, 1, 1);
    VertexPNT srcVertices[] = {
        VertexPNT{ {-1.0f, 0.0f,-1.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f} },
        VertexPNT{ {-1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f} },
        VertexPNT{ { 1.0f, 0.0f,-1.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f} },
        VertexPNT{ { 1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f} },
    };
    vertices.resize(4);
    std::transform(
        std::begin(srcVertices), std::end(srcVertices), vertices.begin(),
        [=](auto v) {
            v.Position.x *= size;
            v.Position.z *= size;
            return v;
        }
    );
    indices = { 0, 1, 2, 2, 1, 3 };
}

void util::primitive::GetPlane(std::vector<vec3>* positions, std::vector<vec3>* normals, std::vector<vec2>* texcoords, std::vector<uint32_t>& indices, float size)
{
    std::vector<VertexPNT> vertices;
    GetPlane(vertices, indices, size);
    if (positions) {
        (*positions).clear();
        for (const auto& v : vertices) {
            positions->push_back(v.Position);
        }
    }
    if (normals) {
        (*normals).clear();
        for (const auto& v : vertices) {
            normals->push_back(v.Normal);
        }
    }
    if (texcoords) {
        (*texcoords).clear();
        for (const auto& v : vertices) {
            texcoords->push_back(v.UV);
        }
    }
}


void util::primitive::GetColoredCube(std::vector<VertexPNC>& vertices, std::vector<uint32_t>& indices, float size)
{
    vertices.clear();
    indices.clear();

    const vec4 red(1.0f, 0.0f, 0.0f, 1.0f);
    const vec4 green(0.0f, 1.0f, 0.0f, 1.0f);
    const vec4 blue(0.0f, 0.0f, 1.0f, 1.0);
    const vec4 white(1.0f, 1.0f, 1.0f, 1.0f);
    const vec4 black(0.0f, 0.0f, 0.0f, 1.0f);
    const vec4 yellow(1.0f, 1.0f, 0.0f, 1.0f);
    const vec4 magenta(1.0f, 0.0f, 1.0f, 1.0f);
    const vec4 cyan(0.0f, 1.0f, 1.0f, 1.0f);

    vertices = {
        // ó†
        { {-1.0f,-1.0f,-1.0f}, { 0.0f, 0.0f, -1.0f }, red },
        { {-1.0f, 1.0f,-1.0f}, { 0.0f, 0.0f, -1.0f }, yellow },
        { { 1.0f, 1.0f,-1.0f}, { 0.0f, 0.0f, -1.0f }, white },
        { { 1.0f,-1.0f,-1.0f}, { 0.0f, 0.0f, -1.0f }, magenta },
        // âE
        { { 1.0f,-1.0f,-1.0f}, { 1.0f, 0.0f, 0.0f }, magenta },
        { { 1.0f, 1.0f,-1.0f}, { 1.0f, 0.0f, 0.0f }, white},
        { { 1.0f, 1.0f, 1.0f}, { 1.0f, 0.0f, 0.0f }, cyan},
        { { 1.0f,-1.0f, 1.0f}, { 1.0f, 0.0f, 0.0f }, blue},
        // ç∂
        { {-1.0f,-1.0f, 1.0f}, { -1.0f, 0.0f, 0.0f }, black},
        { {-1.0f, 1.0f, 1.0f}, { -1.0f, 0.0f, 0.0f }, green},
        { {-1.0f, 1.0f,-1.0f}, { -1.0f, 0.0f, 0.0f }, yellow},
        { {-1.0f,-1.0f,-1.0f}, { -1.0f, 0.0f, 0.0f }, red},
        // ê≥ñ 
        { { 1.0f,-1.0f, 1.0f}, { 0.0f, 0.0f, 1.0f}, blue},
        { { 1.0f, 1.0f, 1.0f}, { 0.0f, 0.0f, 1.0f}, cyan},
        { {-1.0f, 1.0f, 1.0f}, { 0.0f, 0.0f, 1.0f}, green},
        { {-1.0f,-1.0f, 1.0f}, { 0.0f, 0.0f, 1.0f}, black},
        // è„
        { {-1.0f, 1.0f,-1.0f}, { 0.0f, 1.0f, 0.0f}, yellow},
        { {-1.0f, 1.0f, 1.0f}, { 0.0f, 1.0f, 0.0f}, green },
        { { 1.0f, 1.0f, 1.0f}, { 0.0f, 1.0f, 0.0f}, cyan },
        { { 1.0f, 1.0f,-1.0f}, { 0.0f, 1.0f, 0.0f}, white},
        // íÍ
        { {-1.0f,-1.0f, 1.0f}, { 0.0f, -1.0f, 0.0f}, black},
        { {-1.0f,-1.0f,-1.0f}, { 0.0f, -1.0f, 0.0f}, red},
        { { 1.0f,-1.0f,-1.0f}, { 0.0f, -1.0f, 0.0f}, magenta},
        { { 1.0f,-1.0f, 1.0f}, { 0.0f, -1.0f, 0.0f}, blue},
    };
    indices = {
        0, 1, 2, 2, 3,0,
        4, 5, 6, 6, 7,4,
        8, 9, 10, 10, 11, 8,
        12,13,14, 14,15,12,
        16,17,18, 18,19,16,
        20,21,22, 22,23,20,
    };

    std::transform(
        vertices.begin(), vertices.end(), vertices.begin(),
        [=](auto v) {
            v.Position.x *= size;
            v.Position.y *= size;
            v.Position.z *= size;
            return v;
        }
    );
}

static void SetSphereVertex(
    util::primitive::VertexPN& vert,
    const glm::vec3& position, const glm::vec3& normal, const glm::vec2& uv)
{
    vert.Position = position;
    vert.Normal = normal;
}
static void SetSphereVertex(
    util::primitive::VertexPNT& vert,
    const glm::vec3& position, const glm::vec3& normal, const glm::vec2& uv)
{
    vert.Position = position;
    vert.Normal = normal;
    vert.UV = uv;
}

template<class T>
static void CreateSphereVertices(std::vector<T>& vertices, float radius, int slices, int stacks)
{
    using namespace glm;

    vertices.clear();
    const auto SLICES = float(slices);
    const auto STACKS = float(stacks);
    for (int stack = 0; stack <= stacks; ++stack) {
        for (int slice = 0; slice <= slices; ++slice) {
            vec3 p;
            p.y = 2.0f * stack / STACKS - 1.0f;
            float r = std::sqrtf(1 - p.y * p.y);
            float theta = 2.0f * glm::pi<float>() * slice / SLICES;
            p.x = r * std::sinf(theta);
            p.z = r * std::cosf(theta);

            vec3 v = p * radius;
            vec3 n = normalize(v);
            vec2 uv = {
                float(slice) / SLICES,
                1.0f - float(stack) / STACKS,
            };

            T vtx{};
            SetSphereVertex(vtx, v, n, uv);
            vertices.push_back(vtx);
        }
    }
}
static void CreateSphereIndices(std::vector<uint32_t>& indices, int slices, int stacks)
{
    for (int stack = 0; stack < stacks; ++stack) {
        const int sliceMax = slices + 1;
        for (int slice = 0; slice < slices; ++slice) {
            int idx = stack * sliceMax;
            int i0 = idx + (slice + 0) % sliceMax;
            int i1 = idx + (slice + 1) % sliceMax;
            int i2 = i0 + sliceMax;
            int i3 = i1 + sliceMax;

            indices.push_back(i0); indices.push_back(i1); indices.push_back(i2);
            indices.push_back(i2); indices.push_back(i1); indices.push_back(i3);
        }
    }
}


void util::primitive::GetSphere(std::vector<VertexPN>& vertices, std::vector<uint32_t>& indices, float radius, int slices, int stacks)
{
    vertices.clear();
    indices.clear();
    CreateSphereVertices(vertices, radius, slices, stacks);
    CreateSphereIndices(indices, slices, stacks);
}

void util::primitive::GetSphere(std::vector<VertexPNT>& vertices, std::vector<uint32_t>& indices, float radius, int slices, int stacks)
{
    vertices.clear();
    indices.clear();
    CreateSphereVertices(vertices, radius, slices, stacks);
    CreateSphereIndices(indices, slices, stacks);
}

void util::primitive::GetPlaneXY(std::vector<VertexPNT>& vertices, std::vector<uint32_t>& indices, float size)
{
    const auto white = vec4(1, 1, 1, 1);
    VertexPNT srcVertices[] = {
        VertexPNT{ {-1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f} },
        VertexPNT{ {-1.0f,-1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f} },
        VertexPNT{ { 1.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f} },
        VertexPNT{ { 1.0f,-1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f} },
    };
    vertices.resize(4);
    std::transform(
        std::begin(srcVertices), std::end(srcVertices), vertices.begin(),
        [=](auto v) {
            v.Position.x *= size;
            v.Position.y *= size;
            return v;
        }
    );
    indices = { 0, 1, 2, 2, 1, 3 };
}
