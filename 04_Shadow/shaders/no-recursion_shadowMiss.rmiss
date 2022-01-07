#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_GOOGLE_include_directive : enable

#include "rtcommon.glsl"
layout(location = 1) rayPayloadInEXT MyShadowPayload shadowPayload;

void main()
{
    shadowPayload.isHit = false;
}
