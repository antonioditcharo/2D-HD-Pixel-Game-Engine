// World vertex shader: transforms 3D positions by the view-projection matrix and passes the
// distance from the eye on for fog. The matrix arrives as four rows and is applied with dot
// products, so its layout means the same thing in SPIR-V and DXIL. Uniforms are in
// (b, space1), as SDL GPU requires for vertex shaders.

cbuffer Camera : register(b0, space1)
{
    float4 row0;
    float4 row1;
    float4 row2;
    float4 row3;
    float4 eye; // xyz: the camera's position
};

struct Input
{
    float3 position : TEXCOORD0; // world units
    float2 uv : TEXCOORD1;
    float4 color : TEXCOORD2;    // lighting times tint
};

struct Output
{
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
    float distance : TEXCOORD2;  // from the eye, for fog
    float4 position : SV_Position;
};

Output main(Input input)
{
    const float4 p = float4(input.position, 1.0);
    Output output;
    output.position = float4(dot(row0, p), dot(row1, p), dot(row2, p), dot(row3, p));
    output.uv = input.uv;
    output.color = input.color;
    output.distance = length(input.position - eye.xyz);
    return output;
}
