#version 460
#extension GL_GOOGLE_include_directive : enable

#include "rtcommon.glsl"
#include "fetchVertex.glsl"
#include "calcLighting.glsl"

struct HitPayloadWithState {
  vec3 color;
  float contributionFactor;
  vec3 nextRayOrigin;
  vec3 nextRayDirection;
};
layout(location = 0) rayPayloadInEXT HitPayloadWithState payload;

hitAttributeEXT vec3 attribs;

void main() {

  const vec3 barys = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
  ObjectParameters objParam = objParams[gl_InstanceID];

  VertexPNT vtx = FetchVertexInterleavedPNT(
    barys, objParam.indexBuffer, objParam.vertexBuffer
  );

  vec3 worldPosition = gl_ObjectToWorldEXT * vec4(vtx.Position, 1);
  vec3 worldNormal = mat3(gl_ObjectToWorldEXT) * vtx.Normal;

  // 使用するマテリアルを求める.  
  uint32_t materialIndex = objParams[gl_InstanceID].materialIndex;
  Material material = materials[nonuniformEXT(materialIndex)];

  // Lighting.
  const vec3 toLightDir = normalize(-sceneParams.lightDirection.xyz);
  const vec3 lightColor = sceneParams.lightColor.xyz;
  float dotNL = max(dot(worldNormal, toLightDir), 0.0);

  vec3 albedo = material.diffuse.xyz;
  if(material.textureIndex > -1) {
    albedo = texture(textures[nonuniformEXT(material.textureIndex)], vtx.Texcoord).xyz;
  }

  vec3 lambert = LambertLight(worldNormal, toLightDir, albedo, lightColor, sceneParams.ambientColor.xyz);
  payload.color = lambert;

  // 少し反射を混ぜたいので次のレイを設定する.
  payload.nextRayOrigin = worldPosition;
  payload.nextRayDirection = reflect(gl_WorldRayDirectionEXT, worldNormal);
  payload.contributionFactor = 0.15; // 反射成分は 15% にしておく.

}
