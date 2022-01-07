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
  vec3 barys = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
  ObjectParameters objParam = objParams[gl_InstanceID];

  VertexPNT vtx = FetchVertexInterleavedPNT(
    barys, objParam.indexBuffer, objParam.vertexBuffer
  );

  vec3 worldPosition = gl_ObjectToWorldEXT * vec4(vtx.Position, 1);
  vec3 worldNormal = mat3(gl_ObjectToWorldEXT) * vtx.Normal;

  // 使用するマテリアルを求める.  
  uint32_t materialIndex = objParams[nonuniformEXT(gl_InstanceID)].materialIndex;
  Material material = materials[nonuniformEXT(materialIndex)];

  // Lighting.
  const vec3 toLightDir = normalize(-sceneParams.lightDirection.xyz);
  const vec3 lightColor = sceneParams.lightColor.xyz;
  float dotNL = dot(worldNormal, toLightDir);

  vec3 albedo = material.diffuse.xyz;
  if(material.textureIndex > -1) {
    albedo = texture(textures[nonuniformEXT(material.textureIndex)], vtx.Texcoord).xyz;
  }

  vec3 lambert = LambertLight(worldNormal, toLightDir, albedo, lightColor, sceneParams.ambientColor.xyz);
  vec3 toEyeDir = normalize(sceneParams.cameraPosition.xyz - worldPosition);

  const vec3 incidentRay =  gl_WorldRayDirectionEXT;
  if(material.materialKind == 0 ) {
    payload.color = lambert;
    if( dotNL > 0 ) {
      payload.color += PhongSpecular(worldNormal, -toLightDir, toEyeDir, albedo, material.specular);
    }

    // 次回の反射はなし.
    payload.nextRayOrigin = vec3(0);
    payload.nextRayDirection = vec3(0);
    payload.contributionFactor = 0;
  }
  if(material.materialKind == 1) {
    payload.color = vec3(0);
    payload.nextRayOrigin = worldPosition;
    payload.nextRayDirection = reflect(incidentRay, worldNormal);
    payload.contributionFactor = 1;
  }
  if(material.materialKind == 2) {
    
    const float refractValue = 1.33;
    float nr = dot(worldNormal, incidentRay);
    vec3 refractDir;
    vec3 orientingNormal;
    if( nr < 0) {
      // 表面. 空気中 -> 屈折媒質.
      float eta = 1.0 / refractValue;
      refractDir = refract(incidentRay, worldNormal, eta);
      orientingNormal = worldNormal;
    } else {
      // 裏面. 屈折媒質 -> 空気中.  
      float eta = refractValue / 1.0;
      refractDir = refract(incidentRay, -worldNormal, eta);
      orientingNormal = -worldNormal;
    }

    payload.color = vec3(0);
    payload.nextRayOrigin = worldPosition;
    payload.nextRayDirection = refractDir;
    payload.contributionFactor = 1;

    if(length(refractDir)<0.01) {
      // 全反射している.
      payload.nextRayDirection = reflect(incidentRay, orientingNormal);
    }
  }
}
