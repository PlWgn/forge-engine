#version 330 core
in vec2 v_uv;
in vec4 v_color;
uniform sampler2D u_texture;
out vec4 out_color;
void main() {
    out_color=v_color*texture(u_texture,v_uv);
    if(out_color.a<0.005) discard;
}
