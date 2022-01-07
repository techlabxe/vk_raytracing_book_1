#version 460
#extension GL_GOOGLE_include_directive : enable
#include "rtcommon.glsl"
#include "no-recursion_rayhitPayload.glsl"
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

  vec3 worldPosition = gl_ObjectToWorldEXT * vec4(vtx.Position, 1);
  vec3 worldNormal = mat3(gl_ObjectToWorldEXT) * vtx.Normal;

  // 使用するマテリアルを求める.  
  uint32_t materialIndex = objParams[gl_InstanceID].materialIndex;
  Material material = materials[nonuniformEXT(materialIndex)];

  vec3 albedo = material.diffuse.xyz;
  if(material.textureIndex > -1) {
    albedo *= texture(textures[nonuniformEXT(material.textureIndex)], vtx.Texcoord).xyz;
  }

  // Lighting.
  bool usePointLight = sceneParams.shaderFlags.y > 0;
  vec3 toLightDir = normalize(-sceneParams.lightDirection.xyz);
  if(usePointLight) {
    toLightDir = normalize(sceneParams.pointLightPosition.xyz - worldPosition);
  }
  
  vec3 color = vec3(0);
  if(material.materialKind == 0 ) {
    color = LambertLight(
      worldNormal, toLightDir, albedo, 
      sceneParams.lightColor.xyz, sceneParams.ambientColor.xyz
    );
    vec3 toEyeDir = normalize(sceneParams.cameraPosition.xyz - worldPosition);
    color += PhongSpecular(worldNormal, -toLightDir, toEyeDir, albedo, material.specular);
  }

  // Emissive
  if(material.materialKind == 1) {
    color = sceneParams.lightColor.xyz;
  }
  payload.color = color;

}
