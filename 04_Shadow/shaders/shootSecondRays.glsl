#define LIGHT_OBJECT_MASK 0x01

//---------------------------
// Functions
//---------------------------
bool ShootShadowRay(vec3 worldPosition, vec3 rayDirection, uint rayFlags) {
  // �����Ƀq�b�g����΂��̎��_�ŉe���m��.
  // �q�b�g�ۂ������d�v�Ȃ̂Ńq�b�g�V�F�[�_�[���Ăяo���K�v�͂Ȃ�.
  rayFlags |= gl_RayFlagsSkipClosestHitShaderEXT;
  rayFlags |= gl_RayFlagsTerminateOnFirstHitEXT;

  uint cullMask = ~(LIGHT_OBJECT_MASK); // ���C�g�p�I�u�W�F�N�g�Ƀq�b�g�����Ȃ�����.

  const int shadowMissIndex = 1;
  const int shadowPayloadLocation = 1;

  float tmin = 0.001;
  float tmax = 10000.0;
  // �e����p���C���΂�.
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
