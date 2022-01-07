#define LIGHT_OBJECT_MASK 0x01

//---------------------------
// Functions
//---------------------------
vec3 Reflection(vec3 worldPosition, vec3 worldNormal, vec3 incidentRay) {
  worldNormal = normalize(worldNormal);
  vec3 reflectDir = reflect(incidentRay, worldNormal);

  float tmin = 0.001;
  float tmax = 10000.0;

  traceRayEXT(
    topLevelAS,
    gl_RayFlagsOpaqueEXT,
    0xff,
    0,
    0,
    0,
    worldPosition,
    tmin,
    reflectDir,
    tmax,
    0
  );
  return payload.hitValue;
}

vec3 Refraction(vec3 worldPosition, vec3 worldNormal, vec3 incidentRay) {
  worldNormal = normalize(worldNormal);
  const float refractValue = 1.4;
  float nr = dot(worldNormal, incidentRay);
  vec3 refracted;
  vec3 orientingNormal;
  if(nr < 0) {
    // •\–Ê. ‹ó‹C’† -> ‹üÜ”}Ž¿.
    float eta = 1.0 / refractValue;
    refracted = refract(incidentRay, worldNormal, eta);
    orientingNormal = worldNormal;
  } else {
    // — –Ê. ‹üÜ”}Ž¿ -> ‹ó‹C’†.  
    float eta = refractValue / 1.0;
    refracted = refract(incidentRay, -worldNormal, eta);
    orientingNormal = -worldNormal;
  }


  if(length(refracted)<0.01) {
     return Reflection(worldPosition, orientingNormal, incidentRay );
  }

  float tmin = 0.00001;
  float tmax = 10000.0;
  uint rayFlags = gl_RayFlagsOpaqueEXT;
  uint cullMask = 0xFF;
  traceRayEXT(
    topLevelAS,
    rayFlags, cullMask,
    0, 0, 0,
    worldPosition, tmin, refracted, tmax,
    0 // PayloadLocation
  );
  return payload.hitValue;
}
