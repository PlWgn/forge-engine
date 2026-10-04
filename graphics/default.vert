#version 330 core
layout(location=0) in vec3 a_position;
layout(location=1) in vec2 a_uv;
layout(location=2) in vec3 a_normal;
layout(location=3) in ivec4 a_bones;
layout(location=4) in vec4 a_weights;
layout(location=5) in vec4 a_color;
out vec4 v_vertex_color;
uniform mat4 u_mvp, u_model, u_bones[128];
uniform int u_skinned, u_flip_y;
uniform vec4 u_uv_rect;
out vec2 v_uv;
out vec3 v_normal, v_position;
void main() {
    mat4 skin=mat4(1.0);
    if(u_skinned==1 && dot(a_weights,vec4(1))>0.0)
        skin=u_bones[a_bones.x]*a_weights.x+u_bones[a_bones.y]*a_weights.y+
             u_bones[a_bones.z]*a_weights.z+u_bones[a_bones.w]*a_weights.w;
    v_vertex_color=a_color;
    vec4 p=skin*vec4(a_position,1);
    v_uv=u_uv_rect.xy+a_uv*u_uv_rect.zw;
    if(u_flip_y==1) v_uv.y=1.0-v_uv.y;
    v_position=(u_model*p).xyz;
    v_normal=mat3(transpose(inverse(u_model)))*mat3(skin)*a_normal;
    gl_Position=u_mvp*p;
}
