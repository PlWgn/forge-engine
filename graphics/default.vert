#version 330 core
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec3 a_normal;
uniform mat4 u_mvp;
uniform mat4 u_model;
out vec2 v_uv;
out vec3 v_normal;
void main() {
    v_uv = a_uv;
    v_normal = mat3(transpose(inverse(u_model))) * a_normal;
    gl_Position = u_mvp * vec4(a_position, 1.0);
}
