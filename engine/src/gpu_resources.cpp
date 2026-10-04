#include <forge/engine.hpp>
#include <forge/gpu_resources.hpp>
#include <forge/gl.hpp>
namespace forge {
Mesh upload(const std::vector<Vertex> &vertices) {
    Mesh m;
    m.count = static_cast<int>(vertices.size());
    gl::GenVertexArrays(1, &m.vao);
    gl::GenBuffers(1, &m.vbo);
    gl::BindVertexArray(m.vao);
    gl::BindBuffer(gl::ARRAY_BUFFER, m.vbo);
    gl::BufferData(gl::ARRAY_BUFFER, sizeof(Vertex) * vertices.size(), vertices.data(), gl::STATIC_DRAW);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 3, gl::FLOAT, 0, sizeof(Vertex),
                            reinterpret_cast<void *>(offsetof(Vertex, p)));
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 2, gl::FLOAT, 0, sizeof(Vertex),
                            reinterpret_cast<void *>(offsetof(Vertex, uv)));
    gl::EnableVertexAttribArray(2);
    gl::VertexAttribPointer(2, 3, gl::FLOAT, 0, sizeof(Vertex),
                            reinterpret_cast<void *>(offsetof(Vertex, n)));
    gl::EnableVertexAttribArray(3);
    gl::VertexAttribIPointer(3, 4, 0x1404, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, bones)));
    gl::EnableVertexAttribArray(4);
    gl::VertexAttribPointer(4, 4, gl::FLOAT, 0, sizeof(Vertex),
                            reinterpret_cast<void *>(offsetof(Vertex, weights)));
    gl::EnableVertexAttribArray(5);
    gl::VertexAttribPointer(5,4,gl::FLOAT,0,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,color)));
    return m;
}
static unsigned compile(unsigned kind, const std::string &source, const std::string &name) {
    auto shader = gl::CreateShader(kind);
    const char *s = source.c_str();
    gl::ShaderSource(shader, 1, &s, nullptr);
    gl::CompileShader(shader);
    int ok;
    gl::GetShaderiv(shader, gl::COMPILE_STATUS, &ok);
    if (!ok) {
        char log[8192]{};
        gl::GetShaderInfoLog(shader, sizeof(log), nullptr, log);
        gl::DeleteShader(shader);
        throw std::runtime_error("Shader " + name + ":\n" + log);
    }
    return shader;
}
unsigned texture(const unsigned char *data, int width, int height) {
    unsigned id;
    gl::GenTextures(1, &id);
    gl::BindTexture(gl::TEXTURE_2D, id);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA, width, height, 0, gl::RGBA, gl::UNSIGNED_BYTE, data);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    return id;
}
unsigned linkProgram(const std::string &vertex, const std::string &fragment, const std::string &name) {
    auto vs = compile(gl::VERTEX_SHADER, vertex, name + ".vertex");
    unsigned fs = 0, p = 0;
    try {
        fs = compile(gl::FRAGMENT_SHADER, fragment, name + ".fragment");
        p = gl::CreateProgram();
        gl::AttachShader(p, vs);
        gl::AttachShader(p, fs);
        gl::LinkProgram(p);
        int ok;
        gl::GetProgramiv(p, gl::LINK_STATUS, &ok);
        if (!ok) {
            char log[8192]{};
            gl::GetProgramInfoLog(p, sizeof(log), nullptr, log);
            throw std::runtime_error(name + " linking: " + log);
        }
    } catch (...) {
        gl::DeleteShader(vs);
        if (fs)
            gl::DeleteShader(fs);
        if (p)
            gl::DeleteProgram(p);
        throw;
    }
    gl::DeleteShader(vs);
    gl::DeleteShader(fs);
    return p;
}
void uniforms(unsigned program, const Json &values) {
    for (auto it = values.begin(); it != values.end(); ++it) {
        auto location = gl::GetUniformLocation(program, it.key().c_str());
        auto value = it.value();
        if (value.is_boolean() || value.is_number_integer())
            gl::Uniform1i(location, value.is_boolean() ? int(value.get<bool>()) : value.get<int>());
        else if (value.is_array()) {
            float v[4]{};
            for (size_t i = 0; i < value.size(); ++i)
                v[i] = finiteNumber(value[i], it.key());
            switch (value.size()) {
            case 1:
                gl::Uniform1f(location, v[0]);
                break;
            case 2:
                gl::Uniform2fv(location, 1, v);
                break;
            case 3:
                gl::Uniform3fv(location, 1, v);
                break;
            case 4:
                gl::Uniform4fv(location, 1, v);
                break;
            default:
                throw std::runtime_error("Invalid shader uniform");
            }
        } else
            gl::Uniform1f(location, finiteNumber(value, it.key()));
    }
}
}
