// Sprite vertex shader: places quad corners on screen and passes texture coordinates and
// tint through. Bindings follow the SDL GPU rules for vertex shaders: uniform buffers in
// (b, space1).

cbuffer Camera : register(b0, space1)
{
    // clip.xy = position.xy * transform.xy + transform.zw
    float4 transform;
};

struct Input
{
    float2 position : TEXCOORD0; // pixels
    float2 uv : TEXCOORD1;
    float4 tint : TEXCOORD2;
};

struct Output
{
    float2 uv : TEXCOORD0;
    float4 tint : TEXCOORD1;
    float4 position : SV_Position;
};

Output main(Input input)
{
    Output output;
    output.position = float4(input.position * transform.xy + transform.zw, 0.0, 1.0);
    output.uv = input.uv;
    output.tint = input.tint;
    return output;
}
