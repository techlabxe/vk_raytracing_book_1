#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_GOOGLE_include_directive : enable

#include "rtcommon.glsl"

layout(location = 0) rayPayloadInEXT MyHitPayload payload;

void main()
{
    payload.hitValue = vec3(0.1, 0.1, 0.12);
}
