// fetch vertex functions
//*****************************************
// Common
//*****************************************
//---------------------------
// Buffer references
//---------------------------
layout(buffer_reference, scalar) readonly buffer Indices
{
    uvec3 i[];
};

layout(buffer_reference, scalar) readonly buffer Matrices { mat3x4 m[]; };

// BLAS に指定した行列を考慮してワールド空間に変換する行列を求める.
mat4 GetObjectToWorld(uint64_t blasMatrixAddr, uint blasMatrixIndex) {
  if(blasMatrixAddr == 0 ) { // BLAS 行列未設定の場合.
    return mat4(gl_ObjectToWorldEXT);
  }

  Matrices matrices = Matrices(blasMatrixAddr);
  mat4 mtxBlas = mat4(transpose(matrices.m[blasMatrixIndex]));
  mat4 mtxObjectToWorld = mat4(gl_ObjectToWorldEXT) * mtxBlas;
  return mtxObjectToWorld;
}


//*****************************************
// Interleaved vertices.
//*****************************************
struct VertexPNT {
  vec3 Position;
  vec3 Normal;
  vec2 Texcoord;
};

//---------------------------
// Buffer references
//---------------------------
layout(buffer_reference, buffer_reference_align = 4, scalar)
readonly buffer VerticesPNT
{
    VertexPNT v[];
};

VertexPNT FetchVertexInterleavedPNT(
  vec3 barys,
  uint64_t indexBuffer,
  uint64_t vertexBufferPNT)
{
  Indices indices = Indices(indexBuffer);
  VerticesPNT verts = VerticesPNT(vertexBufferPNT);

  const uvec3 idx = indices.i[gl_PrimitiveID];
  VertexPNT v0 = verts.v[idx.x];
  VertexPNT v1 = verts.v[idx.y];
  VertexPNT v2 = verts.v[idx.z];

  VertexPNT v = VertexPNT(vec3(0), vec3(0), vec2(0));
  // Position
  v.Position += v0.Position * barys.x;
  v.Position += v1.Position * barys.y;
  v.Position += v2.Position * barys.z;

  // Normal
  v.Normal += v0.Normal * barys.x;
  v.Normal += v1.Normal * barys.y;
  v.Normal += v2.Normal * barys.z;

  // Texcoord
  v.Texcoord += v0.Texcoord * barys.x;
  v.Texcoord += v1.Texcoord * barys.y;
  v.Texcoord += v2.Texcoord * barys.z;

  return v;
}

//*****************************************
// Separated VertexStreams
//*****************************************

//---------------------------
// Buffer references
//---------------------------
layout(buffer_reference, scalar) readonly buffer VertexPos { vec3 v[];};
layout(buffer_reference, scalar) readonly buffer VertexNormal { vec3 n[];};
layout(buffer_reference, scalar) readonly buffer VertexTexcoord { vec2 t[];};

VertexPNT FetchVertexPNT(
  vec3 barys,
  uint64_t indexBuffer,
  uint64_t vertexBufferPos,
  uint64_t vertexBufferNormal,
  uint64_t vertexBufferTexcoord
  )
{
  Indices indices = Indices(indexBuffer);
  VertexPos vbPos = VertexPos(vertexBufferPos);
  VertexNormal vbNormal = VertexNormal(vertexBufferNormal);
  VertexTexcoord vbTex = VertexTexcoord(vertexBufferTexcoord);

  const uvec3 idx = indices.i[gl_PrimitiveID];

  VertexPNT v = VertexPNT(vec3(0), vec3(0), vec2(0));
  // Position
  v.Position += vbPos.v[idx.x] * barys.x;
  v.Position += vbPos.v[idx.y] * barys.y;
  v.Position += vbPos.v[idx.z] * barys.z;

  // Normal
  v.Normal += vbNormal.n[idx.x] * barys.x;
  v.Normal += vbNormal.n[idx.y] * barys.y;
  v.Normal += vbNormal.n[idx.z] * barys.z;

  // Texcoord
  v.Texcoord += vbTex.t[idx.x] * barys.x;
  v.Texcoord += vbTex.t[idx.y] * barys.y;
  v.Texcoord += vbTex.t[idx.z] * barys.z;

  return v;
}