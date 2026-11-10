// Sprite pixel shader: the texel times the tint. Bindings follow the SDL GPU rules for
// fragment shaders: sampled textures and samplers in (t, space2) and (s, space2).

Texture2D<float4> sprite_texture : register(t0, space2);
SamplerState sprite_sampler : register(s0, space2);

struct Input
{
    float2 uv : TEXCOORD0;
    float4 tint : TEXCOORD1;
};

float4 main(Input input) : SV_Target0
{
    return sprite_texture.Sample(sprite_sampler, input.uv) * input.tint;
}
