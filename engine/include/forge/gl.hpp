#pragma once
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cstddef>
#include <stdexcept>
#ifdef _WIN32
#define FORGE_GL_CALL __stdcall
#else
#define FORGE_GL_CALL
#endif
namespace forge::gl {
using U=unsigned int; using I=int; using F=float; using B=unsigned char; using S=std::ptrdiff_t;
constexpr U COLOR=0x4000,DEPTH=0x100,DEPTH_TEST=0xB71,SCISSOR_TEST=0xC11,BLEND=0xBE2,SRC_ALPHA=0x302,ONE_MINUS_SRC_ALPHA=0x303,
ARRAY_BUFFER=0x8892,STATIC_DRAW=0x88E4,FLOAT=0x1406,TRIANGLES=4,VERTEX_SHADER=0x8B31,FRAGMENT_SHADER=0x8B30,
COMPILE_STATUS=0x8B81,LINK_STATUS=0x8B82,TEXTURE_2D=0xDE1,TEXTURE0=0x84C0,RGBA=0x1908,RGB=0x1907,
UNSIGNED_BYTE=0x1401,TEXTURE_MIN_FILTER=0x2801,TEXTURE_MAG_FILTER=0x2800,LINEAR=0x2601,
TEXTURE_WRAP_S=0x2802,TEXTURE_WRAP_T=0x2803,CLAMP_TO_EDGE=0x812F,UNPACK_ALIGNMENT=0xCF5,PACK_ALIGNMENT=0xD05;
constexpr U FRAMEBUFFER=0x8D40,COLOR_ATTACHMENT0=0x8CE0,DEPTH_ATTACHMENT=0x8D00,FRAMEBUFFER_COMPLETE=0x8CD5,DEPTH_COMPONENT=0x1902,DEPTH_COMPONENT24=0x81A6,UNSIGNED_INT=0x1405,STREAM_DRAW=0x88E0,MAX_TEXTURE_SIZE=0xD33;
#define GL_FUNC(ret,name,args) using name##Fn=ret(FORGE_GL_CALL*)args; inline name##Fn name=nullptr;
GL_FUNC(void,Viewport,(I,I,I,I)) GL_FUNC(void,Scissor,(I,I,I,I)) GL_FUNC(void,ClearColor,(F,F,F,F)) GL_FUNC(void,Clear,(U))
GL_FUNC(void,Enable,(U)) GL_FUNC(void,Disable,(U)) GL_FUNC(void,BlendFunc,(U,U))
GL_FUNC(void,DepthMask,(B))
GL_FUNC(void,GenBuffers,(I,U*)) GL_FUNC(void,BindBuffer,(U,U)) GL_FUNC(void,BufferData,(U,S,const void*,U)) GL_FUNC(void,DeleteBuffers,(I,const U*))
GL_FUNC(void,GenVertexArrays,(I,U*)) GL_FUNC(void,BindVertexArray,(U)) GL_FUNC(void,DeleteVertexArrays,(I,const U*))
GL_FUNC(void,EnableVertexAttribArray,(U)) GL_FUNC(void,VertexAttribPointer,(U,I,U,B,I,const void*))
GL_FUNC(void,DisableVertexAttribArray,(U))
GL_FUNC(void,VertexAttribIPointer,(U,I,U,I,const void*))
GL_FUNC(U,CreateShader,(U)) GL_FUNC(void,ShaderSource,(U,I,const char*const*,const I*)) GL_FUNC(void,CompileShader,(U))
GL_FUNC(void,GetShaderiv,(U,U,I*)) GL_FUNC(void,GetShaderInfoLog,(U,I,I*,char*)) GL_FUNC(void,DeleteShader,(U))
GL_FUNC(U,CreateProgram,()) GL_FUNC(void,AttachShader,(U,U)) GL_FUNC(void,LinkProgram,(U))
GL_FUNC(void,GetProgramiv,(U,U,I*)) GL_FUNC(void,GetProgramInfoLog,(U,I,I*,char*)) GL_FUNC(void,DeleteProgram,(U))
GL_FUNC(void,UseProgram,(U)) GL_FUNC(I,GetUniformLocation,(U,const char*)) GL_FUNC(void,UniformMatrix4fv,(I,I,B,const F*))
GL_FUNC(void,Uniform4fv,(I,I,const F*)) GL_FUNC(void,Uniform1i,(I,I)) GL_FUNC(void,Uniform1f,(I,F)) GL_FUNC(void,DrawArrays,(U,I,I))
GL_FUNC(void,Uniform2fv,(I,I,const F*)) GL_FUNC(void,Uniform3fv,(I,I,const F*)) GL_FUNC(void,GetIntegerv,(U,I*))
GL_FUNC(void,ReadPixels,(I,I,I,I,U,U,void*))
GL_FUNC(void,GenTextures,(I,U*)) GL_FUNC(void,BindTexture,(U,U)) GL_FUNC(void,TexImage2D,(U,I,I,I,I,I,U,U,const void*))
GL_FUNC(void,TexParameteri,(U,U,I)) GL_FUNC(void,DeleteTextures,(I,const U*)) GL_FUNC(void,PixelStorei,(U,I)) GL_FUNC(void,ActiveTexture,(U))
GL_FUNC(void,GenerateMipmap,(U))
GL_FUNC(void,TexSubImage2D,(U,I,I,I,I,I,U,U,const void*))
GL_FUNC(void,GenFramebuffers,(I,U*)) GL_FUNC(void,BindFramebuffer,(U,U)) GL_FUNC(void,FramebufferTexture2D,(U,U,U,U,I)) GL_FUNC(U,CheckFramebufferStatus,(U)) GL_FUNC(void,DeleteFramebuffers,(I,const U*))
#undef GL_FUNC
inline void load() {
#define LOAD(name) name=reinterpret_cast<name##Fn>(glfwGetProcAddress("gl" #name)); if(!name) throw std::runtime_error("Missing OpenGL function gl" #name);
LOAD(Scissor) LOAD(Viewport) LOAD(ClearColor) LOAD(Clear) LOAD(Enable) LOAD(Disable) LOAD(BlendFunc)
LOAD(DepthMask)
LOAD(GenBuffers) LOAD(BindBuffer) LOAD(BufferData) LOAD(DeleteBuffers) LOAD(GenVertexArrays) LOAD(BindVertexArray) LOAD(DeleteVertexArrays)
LOAD(DisableVertexAttribArray)
LOAD(EnableVertexAttribArray) LOAD(VertexAttribPointer) LOAD(CreateShader) LOAD(ShaderSource) LOAD(CompileShader) LOAD(GetShaderiv)
LOAD(GetShaderInfoLog) LOAD(DeleteShader) LOAD(CreateProgram) LOAD(AttachShader) LOAD(LinkProgram) LOAD(GetProgramiv) LOAD(GetProgramInfoLog)
LOAD(DeleteProgram) LOAD(UseProgram) LOAD(GetUniformLocation) LOAD(UniformMatrix4fv) LOAD(Uniform4fv) LOAD(Uniform1i) LOAD(Uniform1f) LOAD(DrawArrays)
LOAD(GenerateMipmap) LOAD(ReadPixels) LOAD(GenTextures) LOAD(BindTexture) LOAD(TexImage2D) LOAD(TexParameteri) LOAD(DeleteTextures) LOAD(PixelStorei) LOAD(ActiveTexture)
LOAD(VertexAttribIPointer) LOAD(Uniform2fv) LOAD(Uniform3fv) LOAD(GetIntegerv) LOAD(TexSubImage2D)
LOAD(GenFramebuffers) LOAD(BindFramebuffer) LOAD(FramebufferTexture2D) LOAD(CheckFramebufferStatus) LOAD(DeleteFramebuffers)
#undef LOAD
}
}
