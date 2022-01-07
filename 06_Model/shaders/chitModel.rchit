#version 460
#extension GL_GOOGLE_include_directive : enable
#include "rtcommon.glsl"
#include "rayhitPayload.glsl"
#include "fetchVertex.glsl"
#include "calcLighting.glsl"

hitAttributeEXT DefaultHitAttribute myHitAttribute;

struct ModelMaterial {
    vec4 diffuse;
    vec4 specular;
    int32_t type;
    int32_t textureIndex;
    int32_t padd0;
    int32_t padd1;
};

layout(buffer_reference, scalar) readonly buffer Materials { ModelMaterial m[];};


layout(shaderRecordEXT) buffer sbt {
    uint64_t indexBuffer;
    uint64_t vertexBufferPos;
    uint64_t vertexBufferNormal;
    uint64_t vertexBufferTexcoord;
    uint64_t blasTransformMatrices;
};

void main() {
  int index = gl_InstanceCustomIndexEXT + gl_GeometryIndexEXT;
  ObjectParameters objParam = objParams[index];
  uint32_t materialIndex = objParam.materialIndex;
  Material material = materials[nonuniformEXT(materialIndex)];

  const vec2 attribs = myHitAttribute.attribs;
  const vec3 barys = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);

  // 各デバイスアドレスはオフセットを考慮してセット済み.
  VertexPNT v = FetchVertexPNT(
    barys,
    indexBuffer,
    vertexBufferPos,
    vertexBufferNormal,
    vertexBufferTexcoord
  );

  uint64_t matrixOffset = objParam.blasMatrixStride * sceneParams.frameIndex;
  mat4 mtxObjectToWorld = GetObjectToWorld(
    blasTransformMatrices + matrixOffset,
    objParam.blasMatrixIndex
    );

  vec3 worldPosition = (mtxObjectToWorld * vec4(v.Position, 1)).xyz;
  vec3 worldNormal = mat3(mtxObjectToWorld) * v.Normal;

  vec3 albedo = material.diffuse.xyz;
  if(material.textureIndex > -1 ) {
    albedo *= texture(textures[nonuniformEXT(material.textureIndex)], v.Texcoord).xyz;
  }

  // Lighting.
  vec3 toLightDir = normalize(-sceneParams.lightDirection.xyz);
  vec3 color = LambertLight(worldNormal, toLightDir, albedo,
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
