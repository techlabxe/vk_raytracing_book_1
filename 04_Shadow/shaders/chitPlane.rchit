#version 460
#extension GL_GOOGLE_include_directive : enable

#include "rtcommon.glsl"
#include "rayhitPayload.glsl"
#include "fetchVertex.glsl"
#include "calcLighting.glsl"
#include "shootSecondRays.glsl"


uint randomU(vec2 uv)
{
    float r = dot(uv, vec2(127.1, 311.7));
    return uint(12345 * fract(sin(r) * 43758.5453123));
}


float nextRand(inout uint s) {
    s = (1664525u * s + 1013904223u);
    return float(s & 0x00FFFFFF) / float(0x01000000);
}

// Rotation with angle (in radians) and axis
mat3 angleAxis3x3(float angle, vec3 axis) {
    float c, s;
    s = sin(angle);
    c = cos(angle);

    float t = 1 - c;
    float x = axis.x;
    float y = axis.y;
    float z = axis.z;

    return mat3(
        t * x * x + c, t * x * y - s * z, t * x * z + s * y,
        t * x * y + s * z, t * y * y + c, t * y * z - s * x,
        t * x * z - s * y, t * y * z + s * x, t * z * z + c
        );
}

// Returns a random direction vector inside a cone
// Angle defined in radians
// Example: direction=(0,1,0) and angle=pi returns ([-1,1],[0,1],[-1,1])
vec3 getConeSample(inout uint randSeed, vec3 direction, float coneAngle) {
    float cosAngle = cos(coneAngle);
    const float PI = 3.1415926535;

    // Generate points on the spherical cap around the north pole [1].
    // [1] See https://math.stackexchange.com/a/205589/81266
    float z = nextRand(randSeed) * (1.0f - cosAngle) + cosAngle;
    float phi = nextRand(randSeed) * 2.0f * PI;

    float x = sqrt(1.0f - z * z) * cos(phi);
    float y = sqrt(1.0f - z * z) * sin(phi);
    vec3 north = vec3(0.f, 0.f, 1.f);

    // Find the rotation axis `u` and rotation angle `rot` [1]
    vec3 axis = normalize(cross(normalize(direction), north));

    float angle = acos(dot(normalize(direction), north));

    // Convert rotation axis and angle to 3x3 rotation matrix [2]
    mat3 R = angleAxis3x3(angle, axis);

    return R * vec3(x, y, z);
}

hitAttributeEXT DefaultHitAttribute myHitAttribute;

layout(shaderRecordEXT) buffer sbt {
    uint64_t indexBuffer;
    uint64_t vertexBufferPNT;
};

void main() {
  payload.recursive = payload.recursive - 1;
  if (payload.recursive < 0) {
    payload.hitValue = vec3(0,0,0);
    return;
  }

  const vec2 attribs = myHitAttribute.attribs;
  const vec3 barys = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
  VertexPNT vtx = FetchVertexInterleavedPNT(
    barys, indexBuffer, vertexBufferPNT);

  vec3 worldPosition = gl_ObjectToWorldEXT * vec4(vtx.Position, 1);
  vec3 worldNormal = mat3(gl_ObjectToWorldEXT) * vtx.Normal;

  // 使用するマテリアルを求める.  
  uint32_t materialIndex = objParams[gl_InstanceID].materialIndex;
  Material material = materials[nonuniformEXT(materialIndex)];

  vec3 albedo = material.diffuse.xyz;
  if(material.textureIndex > -1) {
    albedo = texture(textures[nonuniformEXT(material.textureIndex)], vtx.Texcoord).xyz;
  }

  bool usePointLight = sceneParams.shaderFlags.y > 0;
  vec3 pointLightPosition = sceneParams.pointLightPosition.xyz;

  // Lighting.
  vec3 toLightDir = normalize(-sceneParams.lightDirection.xyz);
  if(usePointLight) {
    toLightDir = normalize(pointLightPosition - worldPosition);
  }

  vec3 lambert = LambertLight(
    worldNormal, toLightDir, albedo,
    sceneParams.lightColor.xyz, sceneParams.ambientColor.xyz
  );

  // 影を判定する.
  vec3 rayDirection = toLightDir;
  uint shadowRayFlags = 0;

  bool isShadow = false;
  if(usePointLight == false) {
    // 平行光源でシャドウ判定.
    isShadow = ShootShadowRay(worldPosition, toLightDir, shadowRayFlags);
  } else {
    // ポイントライトでシャドウ判定.
    vec3 toPointLightDir = normalize(pointLightPosition - worldPosition.xyz);
#if 0 // ハードシャドウのとき.
    isShadow = ShootShadowRay(worldPosition, toPointLightDir, shadowRayFlags);
#else
    // 光源に向けて散らしたベクトルを生成.
    vec3 perpL = cross(toPointLightDir, vec3(0, 1, 0));
    if( all( equal(perpL, vec3(0.0)) ) ) {
        perpL.x = 1.0;
    }
    float radius = 1.0;
    vec3 toLightEdge = normalize((pointLightPosition + perpL * radius) - worldPosition.xyz);
    float coneAngle = acos(dot(toPointLightDir, toLightEdge));
    uint randSeed = randomU(worldPosition.xz * 0.1);
    uint shadowRayCount = sceneParams.shaderFlags.z;
    for(int i=0;i<shadowRayCount;++i) {
       rayDirection = getConeSample(randSeed, toPointLightDir, coneAngle);
       isShadow = isShadow || ShootShadowRay(worldPosition, rayDirection, shadowRayFlags);
    }
#endif
  }

  if( isShadow ) {
    lambert *= 0.8;
  }
  payload.hitValue = lambert;
}
