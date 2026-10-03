#version 330 core
in vec2 v_uv;
in vec3 v_normal;
uniform vec4 u_color;
uniform sampler2D u_texture;
uniform int u_textured;
uniform float u_lit;
out vec4 out_color;
void main() {
    vec4 color = u_color;
    if (u_textured == 1) color *= texture(u_texture, v_uv);
    float diffuse = 0.3 + 0.7 * max(dot(normalize(v_normal), normalize(vec3(0.5, 1.0, 0.8))), 0.0);
    out_color = vec4(color.rgb * mix(1.0, diffuse, u_lit), color.a);
    if (out_color.a < 0.01) discard;
}
