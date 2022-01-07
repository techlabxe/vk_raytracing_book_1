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
    uint64_t vertexBufferReserved0; // 他では VB Normal として使用.
    uint64_t vertexBufferReserved1; // 他では VB Texcoord として使用.
    uint64_t blasTransformMatrices;
};

void main() {
  int index = gl_InstanceCustomIndexEXT + gl_GeometryIndexEXT;
  ObjectParameters objParam = objParams[index];

  const vec2 attribs = myHitAttribute.attribs;
  const vec3 barys = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
  VertexPNT vtx = FetchVertexInterleavedPNT(
    barys, indexBuffer, vertexBufferPNT);

  uint64_t matrixOffset = objParam.blasMatrixStride * sceneParams.frameIndex;
  mat4 mtxObjectToWorld = GetObjectToWorld(
    blasTransformMatrices + matrixOffset,
    objParam.blasMatrixIndex
  );

  vec3 worldPosition = (mtxObjectToWorld * vec4(vtx.Position, 1)).xyz;
  vec3 worldNormal = mat3(gl_ObjectToWorldEXT) * vtx.Normal;

  // 使用するマテリアルを求める.  
  uint32_t materialIndex = objParams[gl_InstanceID].materialIndex;
  Material material = materials[materialIndex];

  vec3 albedo = material.diffuse.xyz;
  if(material.textureIndex > -1) {
    albedo *= texture(textures[nonuniformEXT(material.textureIndex)], vtx.Texcoord).xyz;
  }

#if 01
    // 市松模様を作ってみる.
    vec2 v = step(0, sin(worldPosition.xz*1.5)) * 0.5;
    float v2 = fract(v.x + v.y);
    albedo.xyz = (v2.xxx * 2+0.3);
#endif

  // Lighting.
  vec3 toLightDir = normalize(-sceneParams.lightDirection.xyz); 
  vec3 color = LambertLight(
    worldNormal, toLightDir, albedo,
    sceneParams.lightColor.xyz, sceneParams.ambientColor.xyz);

  vec3 specularColor = vec3(0);
  if(material.type == 1) {
    vec3 toEyeDir = normalize(sceneParams.cameraPosition - worldPosition);
    specularColor = PhongSpecular(worldNormal, -toLightDir, toEyeDir, albedo, material.specular);
  }
  payload.hitValue = color;
  payload.specular = specularColor;

  // 平行光源で影を判定.
  float dotNL = dot(worldNormal, toLightDir);
  if(dotNL > 0) {
    payload.rayOrigin = worldPosition;
    payload.rayDirection = toLightDir; 
  } else {
    // 陰影で陰となる部分にはシャドウレイを飛ばさないでおく.
    payload.rayOrigin = vec3(0);
    payload.rayDirection = vec3(0);
  }
}
