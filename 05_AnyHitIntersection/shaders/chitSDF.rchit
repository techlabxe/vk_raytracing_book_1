#version 460
#extension GL_GOOGLE_include_directive : enable
#include "rtcommon.glsl"
#include "rayhitPayload.glsl"
#include "calcLighting.glsl"

hitAttributeEXT MyIntersectAttribute myHitAttribute;

void main() {
  vec3 worldPosition = gl_WorldRayOriginEXT + gl_WorldRayDirectionEXT * gl_HitTEXT;
  vec3 worldNormal = mat3(gl_ObjectToWorldEXT) * myHitAttribute.normal;

  // �g�p����}�e���A�������߂�. 
  int index = gl_InstanceID;
  ObjectParameters objParams = objParams[index];
  Material material = materials[nonuniformEXT(objParams.materialIndex)];

  vec3 albedo = material.diffuse.xyz;

  // Lighting.
  vec3 toLightDir = normalize(-sceneParams.lightDirection.xyz);
  payload.hitValue = LambertLight(
    worldNormal, toLightDir, albedo,
    sceneParams.lightColor.xyz, sceneParams.ambientColor.xyz);

  // �e�𔻒肷�邽�߂̏����Z�b�g.
  payload.rayOrigin = worldPosition;
  payload.rayDirection = toLightDir;
}
