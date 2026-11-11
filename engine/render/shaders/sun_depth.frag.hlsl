// The sun's depth pass: what casts shadows, seen from the sun. Only depth is written;
// texels with alpha below one half are cut out, as in the world shader, so a picture's
// clear pixels cast nothing.

Texture2D<float4> world_texture : register(t0, space2);
SamplerState world_sampler : register(s0, space2);

struct Input
{
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    float distance : TEXCOORD2;
    float3 world : TEXCOORD3;
    float4 surface : TEXCOORD4;
    float3 albedo : TEXCOORD5;
};

void main(Input input)
{
    if (world_texture.Sample(world_sampler, input.uv).a < 0.5)
    {
        discard;
    }
}
