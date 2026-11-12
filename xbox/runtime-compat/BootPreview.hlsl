// R5F local owner-image preview. The game engine is NOT executing.
// Fullscreen textured rectangle: user supplies a 16:9 PNG in LocalState.
// No Nintendo image or program code is embedded in the public project.
Texture2D screenshot : register(t0);
SamplerState linearSampler : register(s0);
struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };
VSOut VSMain(uint id : SV_VertexID) {
    const float2 p[6] = {
        float2(-1,  1), float2( 1,  1), float2(-1, -1),
        float2(-1, -1), float2( 1,  1), float2( 1, -1)
    };
    const float2 uv[6] = {
        float2(0,0), float2(1,0), float2(0,1),
        float2(0,1), float2(1,0), float2(1,1)
    };
    VSOut o;
    o.pos = float4(p[id], 0, 1);
    o.uv = uv[id];
    return o;
}
float4 PSMain(VSOut i) : SV_TARGET {
    return screenshot.Sample(linearSampler, i.uv);
}
