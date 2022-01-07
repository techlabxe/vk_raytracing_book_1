#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_GOOGLE_include_directive : enable

#include "rtcommon.glsl"
#include "no-recursion_rayhitPayload.glsl"

void main()
{
  payload.color = vec3(0.1, 0.1, 0.12);
  payload.shadowRayOrigin = vec3(0);
  payload.shadowRayDirection = vec3(0);
}
