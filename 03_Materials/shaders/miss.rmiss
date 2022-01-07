#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_GOOGLE_include_directive : enable

#include "rtcommon.glsl"

layout(location = 0) rayPayloadInEXT MyHitPayload payload;

void main()
{
    const vec3 worldRayDirection = gl_WorldRayDirectionEXT;
    payload.hitValue = texture(backgroundCube, worldRayDirection).xyz;
}
