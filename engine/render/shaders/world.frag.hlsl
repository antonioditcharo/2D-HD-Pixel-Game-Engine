// World pixel shader: the texel times the vertex colour plus the point lights, then linear
// distance fog. Opaque quads cut alpha out, so pixel art keeps hard edges and sorts by
// depth; see-through quads (the blended pipeline sets fog_range.w) keep the texel's alpha
// times the colour's, to blend over what is behind them.
// Textures and samplers are in (t, space2) and (s, space2), uniforms in (b, space3), as SDL
// GPU requires.

Texture2D<float4> world_texture : register(t0, space2);
SamplerState world_sampler : register(s0, space2);

#define MAX_POINT_LIGHTS 16 // render::max_point_lights

cbuffer Frame : register(b0, space3)
{
    float4 fog_color;  // rgb
    float4 fog_range;  // x: start distance, y: 1 / (end - start), z: 1 if fog is on,
                       // w: 1 for see-through quads
    float4 light_count; // x
    float4 light_position[MAX_POINT_LIGHTS]; // xyz: where, w: radius
    float4 light_color[MAX_POINT_LIGHTS];    // rgb
};

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
    const float4 texel = world_texture.Sample(world_sampler, input.uv);
    const bool blended = fog_range.w > 0.5;
    const float alpha = blended ? texel.a * input.color.a : 1.0;
    if ((blended && alpha < 0.002) || (!blended && texel.a < 0.5))
    {
        discard;
    }
    float3 light = input.color.rgb;
    const float normal_length = length(input.surface.xyz);
    if (normal_length > 0.0001)
    {
        const float3 normal = input.surface.xyz / normal_length;
        const float wrap = input.surface.w;
        const int count = (int)light_count.x;
        [loop]
        for (int i = 0; i < count; ++i)
        {
            const float3 to_light = light_position[i].xyz - input.world;
            const float distance = length(to_light);
            const float radius = light_position[i].w;
            if (distance >= radius)
            {
                continue;
            }
            // render::light_falloff(): (1 - (d / r)^2)^2.
            const float x = distance / radius;
            const float falloff = (1.0 - x * x) * (1.0 - x * x);
            const float facing = distance > 0.0001 ? dot(normal, to_light / distance) : 1.0;
            const float diffuse = saturate((facing + wrap) / (1.0 + wrap));
            light += input.albedo * light_color[i].rgb * (falloff * diffuse);
        }
    }
    float3 rgb = texel.rgb * light;
    if (fog_range.z > 0.5)
    {
        const float amount = saturate((input.distance - fog_range.x) * fog_range.y);
        rgb = lerp(rgb, fog_color.rgb, amount);
    }
    return float4(rgb, alpha);
}
