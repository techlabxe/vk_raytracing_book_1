#version 460
#extension GL_GOOGLE_include_directive : enable
#include "rtcommon.glsl"
#include "rayhitPayload.glsl"
#include "fetchVertex.glsl"
#include "calcLighting.glsl"
#include "shootSecondRays.glsl"

hitAttributeEXT vec3 attribs;

void main() {
  payload.recursive = payload.recursive - 1;
  if (payload.recursive < 0) {
    payload.hitValue = vec3(0,0,0);
    return;
  }

  vec3 barys = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
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
  float dotNL = dot(worldNormal, toLightDir);

  vec3 albedo = material.diffuse.xyz;
  if(material.textureIndex > -1) {
    albedo = texture(textures[nonuniformEXT(material.textureIndex)], vtx.Texcoord).xyz;
  }

  vec3 color = vec3(0);
  if(material.materialKind == 0) {
    vec3 toEyeDir = normalize(sceneParams.cameraPosition.xyz - worldPosition);
    color = LambertLight(worldNormal, toLightDir, albedo, lightColor, sceneParams.ambientColor.xyz);
    if(dotNL>0) {
      color+= PhongSpecular(worldNormal, -toLightDir, toEyeDir, albedo, material.specular);
    }
  }
  if(material.materialKind == 1) {
    /*Metal*/
	color = Reflection(worldPosition, worldNormal, gl_WorldRayDirectionEXT);
  }
  if(material.materialKind == 2) {
    /*Glass*/
    color = Refraction(worldPosition, worldNormal, gl_WorldRayDirectionEXT);
  }
  payload.hitValue = color;

}
