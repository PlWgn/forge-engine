// Editable example: an unlit, textured scene with alpha mask/blend support.
Texture2D u_texture : register(t0);
SamplerState u_texture_sampler : register(s0);
cbuffer Material : register(b0) {
    float4 u_color;
    int u_textured;
    int u_alpha_mode;
    float u_alpha_cutoff;
};
float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0,
            float4 vertex_color : TEXCOORD1) : SV_Target {
    float4 color = u_color * vertex_color;
    if (u_textured == 1) color *= u_texture.Sample(u_texture_sampler, uv);
    if (u_alpha_mode == 1 && color.a < u_alpha_cutoff) discard;
    if (u_alpha_mode == 0) color.a = 1;
    else if (color.a < 0.01) discard;
    return color;
}
