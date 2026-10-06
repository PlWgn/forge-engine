// Optional example, not the default. TEXCOORDn corresponds to GL attribute n.
// Native HLSL uses ordinary column-major matrices and mul(matrix, vector).
cbuffer Scene : register(b0) {
    float4x4 u_mvp;
    float4x4 u_bones[128];
    float4 u_uv_rect;
    int u_skinned;
    int u_flip_y;
};
struct Vertex {
    float3 position : TEXCOORD0;
    float2 uv : TEXCOORD1;
    int4 bones : TEXCOORD3;
    float4 weights : TEXCOORD4;
    float4 color : TEXCOORD5;
};
struct Pixel {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : TEXCOORD1;
};
// Forge retains GL-oriented coordinates/targets, even with Direct3D.
float4 forge_clip_position(float4 value) {
    value.y = -value.y;
    value.z = (value.z + value.w) * 0.5;
    return value;
}
Pixel main(Vertex input) {
    float4 position = float4(input.position, 1);
    if (u_skinned == 1 && dot(input.weights, float4(1, 1, 1, 1)) > 0) {
        float4x4 skin = u_bones[input.bones.x] * input.weights.x + u_bones[input.bones.y] * input.weights.y +
                        u_bones[input.bones.z] * input.weights.z + u_bones[input.bones.w] * input.weights.w;
        position = mul(skin, position);
    }
    Pixel result;
    result.position = forge_clip_position(mul(u_mvp, position));
    result.uv = u_uv_rect.xy + input.uv * u_uv_rect.zw;
    if (u_flip_y == 1) result.uv.y = 1 - result.uv.y;
    result.color = input.color;
    return result;
}
