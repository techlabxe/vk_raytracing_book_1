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
    uint64_t vertexBufferReserved0; // ���ł� VB Normal �Ƃ��Ďg�p.
    uint64_t vertexBufferReserved1; // ���ł� VB Texcoord �Ƃ��Ďg�p.
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

  // �g�p����}�e���A�������߂�.  
  uint32_t materialIndex = objParams[gl_InstanceID].materialIndex;
  Material material = materials[materialIndex];

  vec3 albedo = material.diffuse.xyz;
  if(material.textureIndex > -1) {
    albedo *= texture(textures[nonuniformEXT(material.textureIndex)], vtx.Texcoord).xyz;
  }

#if 01
    // �s���͗l������Ă݂�.
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

  // ���s�����ŉe�𔻒�.
  float dotNL = dot(worldNormal, toLightDir);
  if(dotNL > 0) {
    payload.rayOrigin = worldPosition;
    payload.rayDirection = toLightDir; 
  } else {
    // �A�e�ŉA�ƂȂ镔���ɂ̓V���h�E���C���΂��Ȃ��ł���.
    payload.rayOrigin = vec3(0);
    payload.rayDirection = vec3(0);
  }
}
