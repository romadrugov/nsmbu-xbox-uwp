// Original RGBA8 texture sampling test. No Nintendo assets.
Texture2D rgbaTexture : register(t0);
SamplerState pointSampler : register(s0);
struct VSOut { float4 Position : SV_POSITION; float2 UV : TEXCOORD0; };
VSOut VSMain(uint id : SV_VertexID) {
    // Roughly a square on a 1280x720 screen.
    const float2 pos[6] = {
        float2(-0.48f,  0.86f), float2( 0.48f,  0.86f),
        float2(-0.48f, -0.86f), float2(-0.48f, -0.86f),
        float2( 0.48f,  0.86f), float2( 0.48f, -0.86f)
    };
    const float2 uv[6] = {
        float2(0, 0), float2(1, 0), float2(0, 1),
        float2(0, 1), float2(1, 0), float2(1, 1)
    };
    VSOut o;
    o.Position = float4(pos[id], 0, 1);
    o.UV = uv[id];
    return o;
}
float4 PSMain(VSOut input) : SV_TARGET {
    return rgbaTexture.Sample(pointSampler, input.UV);
}
