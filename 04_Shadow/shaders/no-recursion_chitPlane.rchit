#version 460
#extension GL_GOOGLE_include_directive : enable

#include "rtcommon.glsl"
#include "no-recursion_rayhitPayload.glsl"
#include "fetchVertex.glsl"
#include "calcLighting.glsl"
#include "shootSecondRays.glsl"
#include "shadowUtil.glsl"

hitAttributeEXT DefaultHitAttribute myHitAttribute;

layout(shaderRecordEXT) buffer sbt {
    uint64_t indexBuffer;
    uint64_t vertexBufferPNT;
};

void main() {
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
  payload.color = lambert;

  payload.shadowRayOrigin = worldPosition;
  payload.shadowRayDirection = toLightDir;
}
