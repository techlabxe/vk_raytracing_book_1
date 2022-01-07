#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_scalar_block_layout : enable
#extension GL_EXT_shader_explicit_arithmetic_types : enable
#extension GL_EXT_nonuniform_qualifier : enable

//---------------------------
// Descriptor Binding List
//---------------------------
#define BIND_TLAS           (0)
#define BIND_IMAGE          (1)
#define BIND_SCENEPARAM     (2)
#define BIND_AABB_ANALYTIC  (3)
#define BIND_AABB_SDF       (4)
#define BIND_OBJECTLIST     (5)
#define BIND_MATERIALLIST   (6)
#define BIND_TEXTURELIST    (7)

#define MODE_RADEON

//---------------------------
// Structures
//---------------------------
struct MyHitPayload {
  vec3 hitValue;
  vec3 rayOrigin;
  vec3 rayDirection;
};
struct MyShadowPayload {
  bool isHit;
};

struct ObjectParameters {
    int32_t materialIndex;
    uint32_t padding0;
    uint32_t padding1;
    uint32_t padding2;
};
struct Material {
    vec4   diffuse;
    vec4   specular;
    int32_t  type;
    int32_t  textureIndex;
    int32_t  padd0;
    int32_t  padd1;
};


//---------------------------
// Descriptor Bindings
//---------------------------
layout(binding = BIND_TLAS, set = 0) uniform accelerationStructureEXT topLevelAS;
layout(binding = BIND_IMAGE, set = 0, rgba8) uniform image2D image;
layout(binding = BIND_SCENEPARAM, set = 0) uniform SceneParameters {
    mat4 mtxView;
    mat4 mtxProj;
    mat4 mtxViewInv;
    mat4 mtxProjInv;
    vec4 lightDirection;
    vec4 lightColor;
    vec4 ambientColor;
} sceneParams;

layout(binding = BIND_AABB_ANALYTIC, set = 0) uniform AnalyticGeomParameters {
    vec3     diffuse;
    uint32_t objectType;  // 0:BOX, 1:Sphere
} analyticGeomParams;

layout(binding = BIND_AABB_SDF, set = 0) uniform SDFGeomParameters {
    vec3     diffuse;
    uint32_t objectType;  // 0:Cube, 1:Sphere, 2:Torus
    vec3     extent;
    float    radius;
} sdfGeomParams;

layout(binding = BIND_OBJECTLIST, set = 0) readonly buffer _ObjectBuffer { ObjectParameters objParams[]; };
layout(binding = BIND_MATERIALLIST, set = 0) readonly buffer _MaterialBuffer { Material materials[]; };
layout(binding = BIND_TEXTURELIST, set=0) uniform sampler2D textures[];
