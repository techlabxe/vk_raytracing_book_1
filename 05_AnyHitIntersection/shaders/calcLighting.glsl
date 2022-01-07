
vec3 LambertLight(vec3 worldNormal, vec3 toLightDir, vec3 albedo, vec3 lightColor, vec3 ambientColor) {
  float dotNL = max(dot(worldNormal, toLightDir), 0.0);
  vec3 color = dotNL * lightColor * albedo;
  color += ambientColor * albedo;
  return color;
}

vec3 PhongSpecular(vec3 worldNormal, vec3 incidentLightRay, vec3 toEyeDir, vec3 albedo, vec4 specular) {
  // for Specular
  float specularPower = specular.w;
  vec3 specularColor = specular.xyz;
  vec3 reflectedLightRay = normalize(reflect(incidentLightRay, worldNormal));
  
  float specularCoef = pow(
    max(0, dot(reflectedLightRay, toEyeDir)),
    specularPower);
  vec3 color = specularCoef * specularColor;
  return color;
}
