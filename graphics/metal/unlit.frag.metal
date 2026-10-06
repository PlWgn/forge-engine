// Optional unlit scene shader. Keep GLSL settings for the OpenGL backend.
#include <metal_stdlib>
using namespace metal;
struct Material {float4 u_color;int u_textured;int u_alpha_mode;float u_alpha_cutoff;};
struct Pixel {float4 position [[position]];float2 uv;float4 color;};
fragment float4 fragment_main(Pixel input [[stage_in]],constant Material& material [[buffer(0)]],
                             texture2d<float> u_texture [[texture(0)]],sampler u_texture_sampler [[sampler(0)]]) {
    float4 color=material.u_color*input.color;
    if(material.u_textured==1)color*=u_texture.sample(u_texture_sampler,input.uv);
    if(material.u_alpha_mode==1 && color.a<material.u_alpha_cutoff)discard_fragment();
    if(material.u_alpha_mode==0)color.a=1;
    return color;
}
