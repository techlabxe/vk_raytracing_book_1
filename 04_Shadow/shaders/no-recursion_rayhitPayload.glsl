//---------------------------
// Attribute / HitPayload
//---------------------------
layout(location = 0) rayPayloadInEXT MyHitPayload_NR payload;
layout(location = 1) rayPayloadInEXT MyShadowPayload shadowPayload;

//---------------------------
// Structures
//---------------------------
struct DefaultHitAttribute {
  vec2 attribs;
};

struct MyIntersectAttribute {
  vec3 normal;
};



