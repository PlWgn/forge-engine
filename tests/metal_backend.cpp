// Optional real-device checks of the public graphics command adapter. No game
// runtime is needed; the probe supplies terminal logging only.
#include <array>
#include <forge/gl.hpp>
#include <forge/logger.hpp>
#include <forge/metal.hpp>
#include <iostream>
namespace forge {
Logger logger;
void Logger::write(const std::string &level, const std::string &text) {
    std::cerr << level << ": " << text << '\n';
}
} // namespace forge
using namespace forge;
void check(bool value, const char *reason) {
    if (!value)
        throw std::runtime_error(reason);
}
struct Mesh {
    unsigned vao, buffer;
};
Mesh upload(const std::vector<float> &data, int stride, size_t position, size_t color) {
    Mesh result{};
    gl::GenVertexArrays(1, &result.vao);
    gl::GenBuffers(1, &result.buffer);
    gl::BindVertexArray(result.vao);
    gl::BindBuffer(gl::ARRAY_BUFFER, result.buffer);
    gl::BufferData(gl::ARRAY_BUFFER, gl::S(data.size() * sizeof(float)), data.data(),
                   gl::STATIC_DRAW);
    gl::EnableVertexAttribArray(0);
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(0, 2, gl::FLOAT, 0, stride, reinterpret_cast<void *>(position));
    gl::VertexAttribPointer(1, 4, gl::FLOAT, 0, stride, reinterpret_cast<void *>(color));
    return result;
}
std::vector<float> triangle(float r, float g, float b, bool padded = false) {
    std::vector<float> result;
    for (auto p : {std::array<float, 2>{-1, -1}, {3, -1}, {-1, 3}}) {
        if (padded)
            result.insert(result.end(), {123, 456});
        result.insert(result.end(), {p[0], p[1], r, g, b, 1});
    }
    return result;
}
void discard(Mesh mesh) {
    gl::DeleteVertexArrays(1, &mesh.vao);
    gl::DeleteBuffers(1, &mesh.buffer);
}
void draw(int x, int y = 0) {
    gl::Viewport(x, y, 80, 80);
    gl::DrawArrays(gl::TRIANGLES, 0, 3);
}
void color(const std::vector<unsigned char> &image, int x, int y,
           std::array<unsigned char, 3> expected) {
    size_t offset = (size_t(y) * 640 + x) * 4;
    for (size_t i = 0; i < 3; ++i)
        check(image[offset + i] == expected[i], "Encoded draw data/pixel changed");
}
void test(Metal &metal) {
    const std::string vertex = R"(#include <metal_stdlib>
using namespace metal;
struct Input {float2 p [[attribute(0)]];float4 color [[attribute(1)]];};
struct Output {float4 p [[position]];float4 color;};
vertex Output v(Input i [[stage_in]]) {return {float4(i.p,0,1),i.color};}
)";
    auto program = metal.mslProgram(vertex, R"(#include <metal_stdlib>
using namespace metal;
struct Output {float4 p [[position]];float4 color;};
fragment float4 f(Output i [[stage_in]]) {return i.color;}
)",
                                    "v", "f", "Layout test");
    gl::UseProgram(program);
    gl::ClearColor(0, 0, 0, 1);
    gl::Clear(gl::COLOR | gl::DEPTH);
    auto a = upload(triangle(1, 0, 0), 24, 0, 8);
    draw(0);
    auto green = triangle(0, 1, 0);
    gl::BufferSubData(gl::ARRAY_BUFFER, 0, gl::S(green.size() * sizeof(float)), green.data());
    draw(80);
    discard(a);
    auto b = upload(triangle(0, 0, 1), 24, 0, 8);
    draw(160);
    check(metal.diagnostics()["draw_pipeline_builds"] == 1,
          "Equivalent VAO layouts rebuilt a pipeline");
    discard(b);
    auto c = upload(triangle(1, 0, 0, true), 32, 8, 16);
    draw(240);
    check(metal.diagnostics()["draw_pipeline_builds"] == 2,
          "Stride/offset change reused a stale pipeline");
    gl::VertexAttribDivisor(1, 1);
    draw(320);
    check(metal.diagnostics()["draw_pipeline_builds"] == 3,
          "Instance stepping reused a stale pipeline");
    gl::DisableVertexAttribArray(1);
    draw(400);
    check(metal.diagnostics()["draw_pipeline_builds"] == 4,
          "Disabled attribute reused a stale pipeline");
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribDivisor(1, 0);
    draw(480);
    check(metal.diagnostics()["draw_pipeline_builds"] == 4,
          "Restored layout did not reuse its pipeline");
    gl::VertexAttribPointer(1, 3, gl::FLOAT, 0, 32, reinterpret_cast<void *>(16));
    draw(560);
    check(metal.diagnostics()["draw_pipeline_builds"] == 5,
          "Attribute format change reused a stale pipeline");
    discard(c);
    std::vector<unsigned char> image(640 * 480 * 4);
    gl::ReadPixels(0, 0, 640, 480, gl::RGBA, gl::UNSIGNED_BYTE, image.data());
    color(image, 20, 20, {255, 0, 0});
    color(image, 100, 20, {0, 255, 0});
    color(image, 180, 20, {0, 0, 255});
    for (int x : {260, 340, 500, 580})
        color(image, x, 20, {255, 0, 0});
    color(image, 420, 20, {0, 0, 0});

    auto textureProgram = metal.mslProgram(vertex, R"(#include <metal_stdlib>
using namespace metal;
struct Output {float4 p [[position]];float4 color;};
fragment float4 f(Output i [[stage_in]],texture2d<float> image [[texture(0)]],sampler sampling [[sampler(0)]]) {
    return image.sample(sampling,float2(.5,.5));
}
)",
                                           "v", "f", "Texture version test");
    gl::UseProgram(textureProgram);
    auto mesh = upload(triangle(1, 1, 1), 24, 0, 8);
    unsigned texture;
    gl::GenTextures(1, &texture);
    gl::BindTexture(gl::TEXTURE_2D, texture);
    std::array<unsigned char, 4> red = {255, 0, 0, 255}, g = {0, 255, 0, 255},
                                 blue = {0, 0, 255, 255};
    auto baseline = metal.diagnostics()["synchronous_flushes"];
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA, 1, 1, 0, gl::RGBA, gl::UNSIGNED_BYTE, red.data());
    draw(0, 100);
    gl::TexSubImage2D(gl::TEXTURE_2D, 0, 0, 0, 1, 1, gl::RGBA, gl::UNSIGNED_BYTE, g.data());
    draw(80, 100);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA, 1, 1, 0, gl::RGBA, gl::UNSIGNED_BYTE, blue.data());
    draw(160, 100);
    check(metal.diagnostics()["synchronous_flushes"] == baseline,
          "Texture writes synchronously drained the GPU");
    gl::DeleteTextures(1, &texture);
    discard(mesh);
    gl::ReadPixels(0, 0, 640, 480, gl::RGBA, gl::UNSIGNED_BYTE, image.data());
    color(image, 20, 120, {255, 0, 0});
    color(image, 100, 120, {0, 255, 0});
    color(image, 180, 120, {0, 0, 255});
    metal.present(false);
    gl::DeleteProgram(program);
    gl::DeleteProgram(textureProgram);
    check(metal.diagnostics()["draw_pipeline_cache_entries"] == 0,
          "Deleted program retained pipelines");
    auto uniforms = metal.mslProgram(vertex, R"(#include <metal_stdlib>
using namespace metal;
struct Output {float4 p [[position]];float4 color;};
struct Globals {float4 u_tint;};
fragment float4 f(Output i [[stage_in]],constant Globals& values [[buffer(0)]]) {
    return values.u_tint;
}
)",
                                     "v", "f", "Uniform lifetime test");
    auto uniformMesh = upload(triangle(1, 1, 1), 24, 0, 8);
    gl::UseProgram(uniforms);
    auto location = gl::GetUniformLocation(uniforms, "u_tint");
    check(location >= 0, "Uniform reflection failed");
    for (int frame = 0; frame < 24; ++frame) {
        metal.beginFrame(640, 480);
        gl::Clear(gl::COLOR | gl::DEPTH);
        for (int i = 0; i < 40; ++i) {
            std::array<float, 4> tint = {float((frame + i) % 3 == 0), float((frame + i) % 3 == 1),
                                         float((frame + i) % 3 == 2), 1};
            gl::Uniform4fv(location, 1, tint.data());
            draw((i % 8) * 80, (i / 8) * 80);
        }
        auto info = metal.diagnostics();
        check(info["uniform_arena_bytes"] <= info["uniform_budget_bytes"],
              "Pending arenas bypassed budget");
        check(info["peak_inflight_command_buffers"] <= 3, "Unbounded command submissions");
        if (frame == 23) {
            gl::ReadPixels(0, 0, 640, 480, gl::RGBA, gl::UNSIGNED_BYTE, image.data());
            color(image, 20, 20, {0, 0, 255});
            color(image, 100, 20, {255, 0, 0});
            color(image, 180, 20, {0, 255, 0});
        }
        metal.present(false);
    }
    discard(uniformMesh);
    gl::DeleteProgram(uniforms);
    // Relinking the same program must discard layout variants and reflection,
    // while draws already encoded with the old program retain their state.
    metal.beginFrame(640, 480);
    unsigned linked = gl::CreateProgram(), vs = gl::CreateShader(gl::VERTEX_SHADER),
             fs = gl::CreateShader(gl::FRAGMENT_SHADER);
    const char *vertexSource =
        "#version 330 core\nlayout(location=0) in vec2 p;void main(){gl_Position=vec4(p,0,1);}";
    const char *fragmentSource =
        "#version 330 core\nuniform vec4 u_color;out vec4 c;void main(){c=u_color;}";
    gl::ShaderSource(vs, 1, &vertexSource, nullptr);
    gl::CompileShader(vs);
    gl::ShaderSource(fs, 1, &fragmentSource, nullptr);
    gl::CompileShader(fs);
    gl::AttachShader(linked, vs);
    gl::AttachShader(linked, fs);
    gl::LinkProgram(linked);
    int status = 0;
    gl::GetProgramiv(linked, gl::LINK_STATUS, &status);
    check(status == 1, "Initial GLSL link failed");
    auto linkedMesh = upload(triangle(1, 1, 1), 24, 0, 8);
    gl::UseProgram(linked);
    std::array<float, 4> tint = {1, 0, 0, 1};
    gl::Uniform4fv(gl::GetUniformLocation(linked, "u_color"), 1, tint.data());
    draw(0, 100);
    auto builds = metal.diagnostics()["draw_pipeline_builds"].get<uint64_t>();
    fragmentSource = "#version 330 core\nout vec4 c;void main(){c=vec4(0,1,0,1);}";
    gl::ShaderSource(fs, 1, &fragmentSource, nullptr);
    gl::CompileShader(fs);
    gl::LinkProgram(linked);
    gl::GetProgramiv(linked, gl::LINK_STATUS, &status);
    check(status == 1, "GLSL relink failed");
    check(gl::GetUniformLocation(linked, "u_color") == -1,
          "Relink retained retired uniform reflection");
    draw(80, 100);
    check(metal.diagnostics()["draw_pipeline_builds"] == builds + 1,
          "Relink reused a stale shader pipeline");
    gl::DeleteProgram(linked);
    gl::DeleteShader(vs);
    gl::DeleteShader(fs);
    discard(linkedMesh);
    gl::ReadPixels(0, 0, 640, 480, gl::RGBA, gl::UNSIGNED_BYTE, image.data());
    color(image, 20, 120, {255, 0, 0});
    color(image, 100, 120, {0, 255, 0});
    metal.finish();
    check(metal.diagnostics()["inflight_command_buffers"] == 0,
          "Explicit finish left pending work");
    std::cout << "Metal layout variants and encoded buffer/texture/uniform versions passed\n";
}
int main() {
    GLFWwindow *window = nullptr;
    try {
        check(glfwInit(), "GLFW initialization failed");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        window = glfwCreateWindow(640, 480, "Metal regressions", nullptr, nullptr);
        check(window != nullptr, "Window creation failed");
        {
            Metal metal(window, {{"uniform_budget_bytes", 65536}});
            metal.beginFrame(640, 480);
            metal.renderFrame([&] { test(metal); });
        }
        glfwDestroyWindow(window);
        glfwTerminate();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        if (window)
            glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
}
