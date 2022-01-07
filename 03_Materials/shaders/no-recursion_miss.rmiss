#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_GOOGLE_include_directive : enable

#include "rtcommon.glsl"

struct HitPayloadWithState {
  vec3 color;
  float contributionFactor;
  vec3 nextRayOrigin;
  vec3 nextRayDirection;
};
layout(location = 0) rayPayloadInEXT HitPayloadWithState payload;

void main()
{
    const vec3 worldRayDirection = gl_WorldRayDirectionEXT;
    payload.color = texture(backgroundCube, worldRayDirection).xyz;
    
    payload.nextRayOrigin = vec3(0);
    payload.nextRayDirection = vec3(0);
    payload.contributionFactor = 0;
}
