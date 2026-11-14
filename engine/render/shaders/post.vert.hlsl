// Post-processing vertex shader: one triangle that covers the whole target, from the
// vertex index alone (no vertex buffer). The pixel shaders find their pixel from
// SV_Position.

float4 main(uint id : SV_VertexID) : SV_Position
{
    const float2 corner = float2((id << 1) & 2, id & 2); // (0, 0), (2, 0), (0, 2)
    return float4(corner * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
