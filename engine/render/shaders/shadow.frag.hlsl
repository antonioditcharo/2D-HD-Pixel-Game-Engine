// Shadow decals: soft dark shapes laid on what is already drawn, such as the blob under a
// character. The output multiplies the target (blending: destination times source), so
// they only darken. The mask's alpha shapes the shadow; the vertex colour's red is how
// dark its middle is.

Texture2D<float4> mask : register(t0, space2);
SamplerState mask_sampler : register(s0, space2);

struct Input
{
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    float distance : TEXCOORD2;
    float3 world : TEXCOORD3;
    float4 surface : TEXCOORD4;
    float3 albedo : TEXCOORD5;
};

float4 main(Input input) : SV_Target0
{
    const float shade = 1.0 - saturate(input.color.r) * mask.Sample(mask_sampler, input.uv).a;
    return float4(shade, shade, shade, 1.0);
}
