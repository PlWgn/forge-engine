// Editable native Metal example. Attribute numbers match Forge's portable layout.
#include <metal_stdlib>
using namespace metal;
struct Scene {
    float4x4 u_mvp;
    float4x4 u_bones[128];
    float4 u_uv_rect;
    int u_skinned;
    int u_flip_y;
};
struct Vertex {
    float3 position [[attribute(0)]];
    float2 uv [[attribute(1)]];
    int4 bones [[attribute(3)]];
    float4 weights [[attribute(4)]];
    float4 color [[attribute(5)]];
};
struct Pixel {float4 position [[position]];float2 uv;float4 color;};
float4 forge_clip_position(float4 value) {
    value.y=-value.y;
    value.z=(value.z+value.w)*0.5;
    return value;
}
vertex Pixel vertex_main(Vertex input [[stage_in]],constant Scene& scene [[buffer(0)]]) {
    float4 position=float4(input.position,1);
    if(scene.u_skinned==1 && dot(input.weights,float4(1))>0){
        float4x4 skin=scene.u_bones[input.bones.x]*input.weights.x+scene.u_bones[input.bones.y]*input.weights.y+
                      scene.u_bones[input.bones.z]*input.weights.z+scene.u_bones[input.bones.w]*input.weights.w;
        position=skin*position;
    }
    Pixel result;result.position=forge_clip_position(scene.u_mvp*position);
    result.uv=scene.u_uv_rect.xy+input.uv*scene.u_uv_rect.zw;
    if(scene.u_flip_y==1)result.uv.y=1-result.uv.y;
    result.color=input.color;return result;
}
