// Post-processing, first pass: blurs each row of the scene horizontally by the tilt-shift
// radius of that row, and the scene's bright parts by the bloom radius, into two targets.
// Pixels are read with Load, so neutral settings copy them exactly. Bindings follow SDL
// GPU's rules for pixel shaders: textures in (t, space2), uniforms in (b, space3).

Texture2D<float4> scene : register(t0, space2);
SamplerState scene_sampler : register(s0, space2);

cbuffer Look : register(b0, space3)
{
    float4 size;  // x: width, y: height
    float4 tilt;  // x: focus centre, y: half the sharp band, z: strongest blur (pixels), w: ramp
    float4 bloom; // x: threshold, y: intensity, z: radius (pixels)
    float4 grade; // x: exposure, y: contrast, z: saturation, w: vignette
};

struct Output
{
    float4 blurred : SV_Target0;
    float4 bright : SV_Target1;
};

/// The tilt-shift blur radius for row y: 0 inside the sharp band, growing over the ramp
/// to the strongest blur.
float tilt_radius(float y)
{
    const float from_focus = abs((y + 0.5) / size.y - tilt.x) - tilt.y;
    return from_focus <= 0.0 ? 0.0 : saturate(from_focus / max(tilt.w, 0.0001)) * tilt.z;
}

float3 bright_part(float3 c)
{
    const float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
    return c * saturate((luma - bloom.x) / max(1.0 - bloom.x, 0.0001));
}

float4 at(int x, int y)
{
    return scene.Load(int3(clamp(x, 0, (int)size.x - 1), y, 0));
}

Output main(float4 position : SV_Position)
{
    const int x = (int)position.x;
    const int y = (int)position.y;
    Output output;

    const float radius = tilt_radius((float)y);
    if (radius < 0.5)
    {
        output.blurred = at(x, y);
    }
    else
    {
        const int reach = min((int)ceil(radius), 12);
        const float sigma = radius / 2.0;
        float3 sum = 0.0;
        float total = 0.0;
        [loop]
        for (int i = -reach; i <= reach; ++i)
        {
            const float w = exp(-(float)(i * i) / (2.0 * sigma * sigma));
            sum += at(x + i, y).rgb * w;
            total += w;
        }
        output.blurred = float4(sum / total, 1.0);
    }

    output.bright = float4(0.0, 0.0, 0.0, 1.0);
    if (bloom.y > 0.0)
    {
        const int reach = clamp((int)ceil(bloom.z), 0, 16);
        const float sigma = max(bloom.z / 2.0, 0.5);
        float3 sum = 0.0;
        float total = 0.0;
        [loop]
        for (int i = -reach; i <= reach; ++i)
        {
            const float w = exp(-(float)(i * i) / (2.0 * sigma * sigma));
            sum += bright_part(at(x + i, y).rgb) * w;
            total += w;
        }
        output.bright = float4(sum / total, 1.0);
    }
    return output;
}
