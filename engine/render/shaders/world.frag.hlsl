// World pixel shader: the texel times the vertex colour, with alpha cut out so pixel art
// keeps hard edges and sorts by depth, then linear distance fog. Textures and samplers are
// in (t, space2) and (s, space2), uniforms in (b, space3), as SDL GPU requires.

Texture2D<float4> world_texture : register(t0, space2);
SamplerState world_sampler : register(s0, space2);

cbuffer Fog : register(b0, space3)
{
    float4 fog_color;  // rgb
    float4 fog_range;  // x: start distance, y: 1 / (end - start), z: 1 if fog is on
};

struct Input
{
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    float distance : TEXCOORD2;
};

float4 main(Input input) : SV_Target0
{
    const float4 texel = world_texture.Sample(world_sampler, input.uv);
    if (texel.a < 0.5)
    {
        discard;
    }
    float3 rgb = texel.rgb * input.color.rgb;
    if (fog_range.z > 0.5)
    {
        const float amount = saturate((input.distance - fog_range.x) * fog_range.y);
        rgb = lerp(rgb, fog_color.rgb, amount);
    }
    return float4(rgb, 1.0);
}
