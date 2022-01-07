#version 460
#extension GL_GOOGLE_include_directive : enable
#include "rtcommon.glsl"
#include "rayhitPayload.glsl"
#include "calcLighting.glsl"

struct HitAttribute {
  vec3 normal;
};
hitAttributeEXT HitAttribute myHitAttribute;

layout(shaderRecordEXT) buffer sbt {
    uint64_t indexBuffer;
    uint64_t vertexBufferPNT;
};

void main() {
  vec3 worldPosition = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
  vec3 worldNormal = mat3(gl_ObjectToWorldEXT) * myHitAttribute.normal;

  // 使用するマテリアルを求める.  
  uint32_t materialIndex = objParams[gl_InstanceID].materialIndex;
  Material material = materials[nonuniformEXT(materialIndex)];

  vec3 albedo = material.diffuse.xyz;

  // Lighting.
  vec3 toLightDir = normalize(-sceneParams.lightDirection.xyz);
  payload.hitValue = LambertLight(
    worldNormal, toLightDir, albedo,
    sceneParams.lightColor.xyz, sceneParams.ambientColor.xyz );

  // 影を判定するための情報をセット.
  payload.rayOrigin = worldPosition;
  payload.rayDirection = toLightDir;
}
