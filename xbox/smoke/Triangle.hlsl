// Original vertex/pixel shader pair; no game content.
struct VSOut { float4 Position : SV_POSITION; float3 Color : COLOR0; };
VSOut VSMain(uint vertexId : SV_VertexID) {
  const float2 pos[3] = { float2(0, 0.8), float2(0.82, -0.8), float2(-0.82, -0.8) };
  const float3 col[3] = { float3(1, 0.2, 0.37), float3(0.16, 1, 0.52), float3(0.29, 0.61, 1) };
  VSOut o;
  o.Position = float4(pos[vertexId], 0, 1);
  o.Color = col[vertexId];
  return o;
}
float4 PSMain(VSOut input) : SV_TARGET { return float4(input.Color, 1); }
