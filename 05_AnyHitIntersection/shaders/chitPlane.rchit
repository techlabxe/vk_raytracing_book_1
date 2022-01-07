#version 460
#extension GL_GOOGLE_include_directive : enable

#include "rtcommon.glsl"
#include "rayhitPayload.glsl"
#include "fetchVertex.glsl"
#include "calcLighting.glsl"

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

  // 使用するマテリアルを求める.  
  int index = gl_InstanceID;
  ObjectParameters objParams = objParams[index];
  Material material = materials[objParams.materialIndex];

  vec3 albedo = material.diffuse.xyz;
  if(material.textureIndex > -1) {
    albedo = texture(textures[nonuniformEXT(material.textureIndex)], vtx.Texcoord).xyz;
  }

  vec3 worldPosition = gl_ObjectToWorldEXT * vec4(vtx.Position, 1);
  vec3 worldNormal = mat3(gl_ObjectToWorldEXT) * vtx.Normal;

  // Lighting.
  vec3 toLightDir = normalize(-sceneParams.lightDirection.xyz);
  payload.hitValue = LambertLight(
    worldNormal, toLightDir, albedo,
    sceneParams.lightColor.xyz, sceneParams.ambientColor.xyz);
  
  // 影を判定するための情報をセット.
  payload.rayOrigin = worldPosition;
  payload.rayDirection = toLightDir;
}
