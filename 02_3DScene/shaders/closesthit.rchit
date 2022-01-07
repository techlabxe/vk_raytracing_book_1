#version 460
#extension GL_EXT_ray_tracing : enable
#extension GL_EXT_buffer_reference : enable
#extension GL_EXT_scalar_block_layout : enable

hitAttributeEXT vec3 attribs;

layout(location = 0) rayPayloadInEXT vec3 hitValue;

layout(binding=2, set=0) uniform SceneParameters {
    mat4 mtxView;
    mat4 mtxProj;
    mat4 mtxViewInv;
    mat4 mtxProjInv;
    vec4 lightDirection;
    vec4 lightColor;
    vec4 ambientColor;
} sceneParams;

struct Vertex {
  vec3 Position;
  vec3 Normal;
  vec4 Color;
};

layout(buffer_reference, scalar) buffer VertexBuffer {
    Vertex v[];
};
layout(buffer_reference, scalar) buffer IndexBuffer {
    uvec3 i[];
};
layout(shaderRecordEXT,std430) buffer SBTData {
    IndexBuffer  indices;
    VertexBuffer verts;
};

Vertex CalcVertex(vec3 barys) {
  const uvec3 idx = indices.i[gl_PrimitiveID];
  Vertex v0 = verts.v[idx.x];
  Vertex v1 = verts.v[idx.y];
  Vertex v2 = verts.v[idx.z];

  Vertex v;
  v.Position = v0.Position * barys.x + v1.Position * barys.y + v2.Position * barys.z;
  v.Normal = normalize(v0.Normal * barys.x + v1.Normal * barys.y + v2.Normal * barys.z);
  v.Color = v0.Color * barys.x + v1.Color * barys.y + v2.Color * barys.z;
  return v;
}

void main() {
  const vec3 barys = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
  Vertex vtx = CalcVertex(barys);
  vec3 worldNormal = mat3(gl_ObjectToWorldEXT) * vtx.Normal;

  const vec3 toLightDir = normalize(-sceneParams.lightDirection.xyz);
  const vec3 lightColor = sceneParams.lightColor.xyz;
  
  const vec3 vtxcolor = vtx.Color.xyz;
  float dotNL = max(dot(worldNormal, toLightDir), 0.0);
  hitValue =  vtxcolor * dotNL * lightColor;
  hitValue += vtxcolor * sceneParams.ambientColor.xyz;
}
