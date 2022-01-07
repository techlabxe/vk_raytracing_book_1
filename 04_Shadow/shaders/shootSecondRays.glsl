#define LIGHT_OBJECT_MASK 0x01

//---------------------------
// Functions
//---------------------------
bool ShootShadowRay(vec3 worldPosition, vec3 rayDirection, uint rayFlags) {
  // 何かにヒットすればその時点で影が確定.
  // ヒット可否だけが重要なのでヒットシェーダーを呼び出す必要はない.
  rayFlags |= gl_RayFlagsSkipClosestHitShaderEXT;
  rayFlags |= gl_RayFlagsTerminateOnFirstHitEXT;

  uint cullMask = ~(LIGHT_OBJECT_MASK); // ライト用オブジェクトにヒットさせないため.

  const int shadowMissIndex = 1;
  const int shadowPayloadLocation = 1;

  float tmin = 0.001;
  float tmax = 10000.0;
  // 影判定用レイを飛ばす.
  shadowPayload.isHit = true;
  traceRayEXT(
    topLevelAS,
    rayFlags,
    cullMask,
    0,
    0,
    shadowMissIndex,
    worldPosition,
    tmin,
    rayDirection,
    tmax,
    shadowPayloadLocation
  );
  return shadowPayload.isHit;
}
