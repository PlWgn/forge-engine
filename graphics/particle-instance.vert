#version 330 core
layout(location=0) in vec2 a_corner;
layout(location=1) in vec3 a_position;
layout(location=2) in vec4 a_uv_rect;
layout(location=3) in vec4 a_color;
layout(location=4) in vec2 a_size_angle;
uniform mat4 u_view;
uniform vec3 u_right, u_up;
out vec2 v_uv;
out vec4 v_color;
void main(){
    float angle=radians(a_size_angle.y), c=cos(angle), s=sin(angle);
    vec2 offset=a_corner*a_size_angle.x;
    offset=mat2(c,s,-s,c)*offset;
    vec3 position=a_position+u_right*offset.x+u_up*offset.y;
    v_uv=a_uv_rect.xy+(a_corner+vec2(.5))*a_uv_rect.zw;
    v_color=a_color;
    gl_Position=u_view*vec4(position,1);
}
