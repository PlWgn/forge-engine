#include <forge/engine.hpp>
#include <forge/gl.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#define STBI_WINDOWS_UTF8
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#include <array>
#include <sstream>
#include <cmath>
namespace forge {
namespace {
std::string textFile(const fs::path& p) { std::ifstream s(p,std::ios::binary); if(!s) throw std::runtime_error("Cannot read: "+p.u8string()); return {std::istreambuf_iterator<char>(s),{}}; }
const char* vertexDefault=R"(#version 330 core
layout(location=0) in vec3 a_position;
layout(location=1) in vec2 a_uv;
layout(location=2) in vec3 a_normal;
uniform mat4 u_mvp; uniform mat4 u_model;
out vec2 v_uv; out vec3 v_normal;
void main(){v_uv=a_uv;v_normal=mat3(transpose(inverse(u_model)))*a_normal;gl_Position=u_mvp*vec4(a_position,1.0);})";
const char* fragmentDefault=R"(#version 330 core
in vec2 v_uv; in vec3 v_normal;
uniform vec4 u_color; uniform sampler2D u_texture; uniform int u_textured; uniform float u_lit;
out vec4 out_color;
void main(){vec4 c=u_color;if(u_textured==1)c*=texture(u_texture,v_uv);float light=mix(1.0,0.3+0.7*max(dot(normalize(v_normal),normalize(vec3(0.5,1.0,0.8))),0.0),u_lit);out_color=vec4(c.rgb*light,c.a);if(out_color.a<0.01)discard;})";
struct Vertex { glm::vec3 p; glm::vec2 uv; glm::vec3 n; };
struct Mesh { unsigned vao=0,vbo=0; int count=0; };
Mesh upload(const std::vector<Vertex>& vertices) {
    Mesh m; m.count=static_cast<int>(vertices.size()); gl::GenVertexArrays(1,&m.vao); gl::GenBuffers(1,&m.vbo);
    gl::BindVertexArray(m.vao); gl::BindBuffer(gl::ARRAY_BUFFER,m.vbo); gl::BufferData(gl::ARRAY_BUFFER,sizeof(Vertex)*vertices.size(),vertices.data(),gl::STATIC_DRAW);
    gl::EnableVertexAttribArray(0); gl::VertexAttribPointer(0,3,gl::FLOAT,0,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,p)));
    gl::EnableVertexAttribArray(1); gl::VertexAttribPointer(1,2,gl::FLOAT,0,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,uv)));
    gl::EnableVertexAttribArray(2); gl::VertexAttribPointer(2,3,gl::FLOAT,0,sizeof(Vertex),reinterpret_cast<void*>(offsetof(Vertex,n)));
    return m;
}
unsigned compile(unsigned kind,const std::string& source,const std::string& name) {
    auto shader=gl::CreateShader(kind); const char* s=source.c_str(); gl::ShaderSource(shader,1,&s,nullptr); gl::CompileShader(shader);
    int ok; gl::GetShaderiv(shader,gl::COMPILE_STATUS,&ok);
    if(!ok) { char log[8192]{}; gl::GetShaderInfoLog(shader,sizeof(log),nullptr,log); gl::DeleteShader(shader); throw std::runtime_error("Shader "+name+":\n"+log); }
    return shader;
}
unsigned texture(const unsigned char* data,int width,int height) {
    unsigned id; gl::GenTextures(1,&id); gl::BindTexture(gl::TEXTURE_2D,id); gl::PixelStorei(gl::UNPACK_ALIGNMENT,1);
    gl::TexImage2D(gl::TEXTURE_2D,0,gl::RGBA,width,height,0,gl::RGBA,gl::UNSIGNED_BYTE,data);
    gl::TexParameteri(gl::TEXTURE_2D,gl::TEXTURE_MIN_FILTER,gl::LINEAR); gl::TexParameteri(gl::TEXTURE_2D,gl::TEXTURE_MAG_FILTER,gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_2D,gl::TEXTURE_WRAP_S,gl::CLAMP_TO_EDGE); gl::TexParameteri(gl::TEXTURE_2D,gl::TEXTURE_WRAP_T,gl::CLAMP_TO_EDGE); return id;
}
std::vector<Vertex> obj(const fs::path& file) {
    std::istringstream in(textFile(file)); std::vector<glm::vec3> positions,normals; std::vector<glm::vec2> uvs; std::vector<Vertex> out; std::string line;
    int lineNumber=0;
    while(std::getline(in,line)) { ++lineNumber; std::istringstream s(line); std::string type; s>>type;
        if(type=="v") { glm::vec3 v; if(!(s>>v.x>>v.y>>v.z)) throw std::runtime_error("Invalid OBJ vertex: "+file.u8string()); positions.push_back(v); }
        else if(type=="vn") { glm::vec3 v; if(!(s>>v.x>>v.y>>v.z)) throw std::runtime_error("Invalid OBJ normal"); normals.push_back(v); }
        else if(type=="vt") { glm::vec2 v; if(!(s>>v.x>>v.y)) throw std::runtime_error("Invalid OBJ UV"); v.y=1-v.y; uvs.push_back(v); }
        else if(type=="f") {
            std::vector<Vertex> face; std::string token;
            auto index=[](int i,size_t size) { int result=i>0?i-1:static_cast<int>(size)+i; if(i==0 || result<0 || result>=static_cast<int>(size)) throw std::runtime_error("OBJ index out of range"); return result; };
            while(s>>token) { if(token[0]=='#') break; std::array<int,3> ids{}; size_t start=0; int component=0;
                while(start<=token.size() && component<3) { auto end=token.find('/',start); auto part=token.substr(start,end==std::string::npos?end:end-start); if(!part.empty()) ids[component]=std::stoi(part); ++component; if(end==std::string::npos) break; start=end+1; }
                Vertex v; v.p=positions.at(index(ids[0],positions.size())); v.uv=ids[1]?uvs.at(index(ids[1],uvs.size())):glm::vec2(0); v.n=ids[2]?normals.at(index(ids[2],normals.size())):glm::vec3(0); face.push_back(v);
            }
            if(face.size()<3) throw std::runtime_error("OBJ face needs 3 vertices at line "+std::to_string(lineNumber));
            for(size_t i=1;i+1<face.size();++i) { Vertex tri[]={face[0],face[i],face[i+1]}; auto normal=glm::cross(tri[1].p-tri[0].p,tri[2].p-tri[0].p); normal=glm::length(normal)>0?glm::normalize(normal):glm::vec3(0,1,0); for(auto& v:tri) { if(glm::length(v.n)==0) v.n=normal; out.push_back(v); } }
        }
    }
    if(out.empty()) throw std::runtime_error("OBJ contains no faces: "+file.u8string()); return out;
}
std::vector<int> unicode(const std::string& s) {
    std::vector<int> out;
    for(size_t i=0;i<s.size();) { unsigned char c=s[i++]; int code=c, count=0;
        if(c>=0xf0) {code=c&7;count=3;} else if(c>=0xe0) {code=c&15;count=2;} else if(c>=0xc0) {code=c&31;count=1;}
        for(int k=0;k<count && i<s.size();++k) code=(code<<6)|(static_cast<unsigned char>(s[i++])&63); out.push_back(code);
    } return out;
}
int keyCode(std::string name) {
    for(auto& c:name) c=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if(name.size()==1) return name[0];
    static const std::map<std::string,int> keys={{"PAGEUP",GLFW_KEY_PAGE_UP},{"PAGEDOWN",GLFW_KEY_PAGE_DOWN},{"SPACE",GLFW_KEY_SPACE},{"ESCAPE",GLFW_KEY_ESCAPE},{"ENTER",GLFW_KEY_ENTER},{"TAB",GLFW_KEY_TAB},{"BACKSPACE",GLFW_KEY_BACKSPACE},{"LEFT",GLFW_KEY_LEFT},{"RIGHT",GLFW_KEY_RIGHT},{"UP",GLFW_KEY_UP},{"DOWN",GLFW_KEY_DOWN},{"SHIFT",GLFW_KEY_LEFT_SHIFT},{"CTRL",GLFW_KEY_LEFT_CONTROL},{"ALT",GLFW_KEY_LEFT_ALT}};
    if(name.size()>=2 && name[0]=='F') { int n=std::stoi(name.substr(1)); if(n>=1 && n<=25) return GLFW_KEY_F1+n-1; }
    auto it=keys.find(name); if(it==keys.end()) throw std::runtime_error("Unknown key: "+name); return it->second;
}
}
struct Renderer::Impl {
    fs::path screenshotPath, previousScreenshot;
    bool previousCaptured=false;
    GLFWwindow* window=nullptr; const Config* config=nullptr;
    unsigned program=0,whiteTexture=0; std::map<std::string,Mesh> meshes; std::map<std::string,unsigned> textures;
    std::array<bool,GLFW_KEY_LAST+1> previous{},keys{};
    glm::vec2 mousePosition{0},mouseDelta{0},mouseScroll{0}; bool firstMouse=true,captured=false;
    std::vector<unsigned char> fontData; stbtt_fontinfo font{}; bool fontReady=false;
    struct Glyph { unsigned texture; int w,h,x,y,advance; }; std::map<std::pair<int,int>,Glyph> glyphs;
    float density=1;
    ~Impl() { if(program || whiteTexture || !meshes.empty() || !textures.empty() || !glyphs.empty())clear(); if(window) { glfwDestroyWindow(window); glfwTerminate(); } }
    void clear() { for(auto& [name,m]:meshes) { gl::DeleteBuffers(1,&m.vbo); gl::DeleteVertexArrays(1,&m.vao); } meshes.clear(); for(auto& [name,t]:textures) gl::DeleteTextures(1,&t); textures.clear(); for(auto& [code,g]:glyphs) gl::DeleteTextures(1,&g.texture); glyphs.clear(); if(whiteTexture)gl::DeleteTextures(1,&whiteTexture);whiteTexture=0; if(program) gl::DeleteProgram(program); program=0; }
    void setup() {
        auto options=config->data.value("renderer",Json::object());
        std::string vertex=options.contains("vertex_shader")?textFile(config->asset("graphics",options["vertex_shader"])):vertexDefault;
        std::string fragment=options.contains("fragment_shader")?textFile(config->asset("graphics",options["fragment_shader"])):fragmentDefault;
        unsigned vs=compile(gl::VERTEX_SHADER,vertex,options.value("vertex_shader","builtin.vertex")); unsigned ps;
        try {ps=compile(gl::FRAGMENT_SHADER,fragment,options.value("fragment_shader","builtin.fragment"));} catch(...) {gl::DeleteShader(vs);throw;}
        program=gl::CreateProgram(); gl::AttachShader(program,vs); gl::AttachShader(program,ps); gl::LinkProgram(program); gl::DeleteShader(vs); gl::DeleteShader(ps);
        int ok; gl::GetProgramiv(program,gl::LINK_STATUS,&ok); if(!ok) {char log[8192]{};gl::GetProgramInfoLog(program,sizeof(log),nullptr,log);throw std::runtime_error(std::string("Shader linking: ")+log);}
        meshes["sprite"]=upload({{{-.5f,-.5f,0},{0,0},{0,0,1}},{{.5f,-.5f,0},{1,0},{0,0,1}},{{.5f,.5f,0},{1,1},{0,0,1}},{{-.5f,-.5f,0},{0,0},{0,0,1}},{{.5f,.5f,0},{1,1},{0,0,1}},{{-.5f,.5f,0},{0,1},{0,0,1}}});
        std::vector<Vertex> cube;
        for(int axis=0;axis<3;++axis) for(int sign:{-1,1}) { glm::vec3 n(0),u(0),v(0); n[axis]=float(sign);u[(axis+1)%3]=1;v[(axis+2)%3]=float(sign); auto center=n*.5f; glm::vec3 p[]={center-(u+v)*.5f,center+(u-v)*.5f,center+(u+v)*.5f,center+(-u+v)*.5f}; glm::vec2 uv[]={{0,0},{1,0},{1,1},{0,1}}; for(int i:{0,1,2,0,2,3}) cube.push_back({p[i],uv[i],n}); }
        meshes["cube"]=upload(cube);
        const unsigned char white[]={255,255,255,255};whiteTexture=texture(white,1,1);
        auto file=config->asset("graphics",options.value("font","font.ttf"));
        if(!fs::exists(file)) throw std::runtime_error("Font missing: "+file.u8string()+"; run tools/dependencies.py or set renderer.font");
        auto bytes=textFile(file); fontData.assign(bytes.begin(),bytes.end()); fontReady=stbtt_InitFont(&font,fontData.data(),stbtt_GetFontOffsetForIndex(fontData.data(),0));
        if(!fontReady) throw std::runtime_error("Invalid TrueType font: "+file.u8string());
    }
    unsigned image(const std::string& name) {
        if(name.empty()) return 0; auto it=textures.find(name); if(it!=textures.end()) return it->second;
        int w,h,channels; auto file=config->asset("textures",name); auto filename=file.u8string();
        auto* pixels=stbi_load(filename.c_str(),&w,&h,&channels,4);
        if(!pixels) throw std::runtime_error("Cannot load texture "+filename+": "+stbi_failure_reason());
        auto id=texture(pixels,w,h); stbi_image_free(pixels); textures[name]=id; return id;
    }
    void draw(const Mesh& mesh,const glm::mat4& model,const glm::mat4& view,const glm::vec4& color,unsigned tex,bool lit) {
        gl::UseProgram(program); auto mvp=view*model;
        gl::UniformMatrix4fv(gl::GetUniformLocation(program,"u_mvp"),1,0,glm::value_ptr(mvp));
        gl::UniformMatrix4fv(gl::GetUniformLocation(program,"u_model"),1,0,glm::value_ptr(model));
        gl::Uniform4fv(gl::GetUniformLocation(program,"u_color"),1,glm::value_ptr(color));
        gl::Uniform1i(gl::GetUniformLocation(program,"u_textured"),tex?1:0); gl::Uniform1f(gl::GetUniformLocation(program,"u_lit"),lit?1.0f:0.0f);
        gl::ActiveTexture(gl::TEXTURE0); gl::BindTexture(gl::TEXTURE_2D,tex?tex:whiteTexture); gl::Uniform1i(gl::GetUniformLocation(program,"u_texture"),0);
        gl::BindVertexArray(mesh.vao); gl::DrawArrays(gl::TRIANGLES,0,mesh.count);
    }
    void text(const Entity& e,const glm::mat4& projection) {
        if(!fontReady || e.fontSize<=0) return;
        int rasterSize=glyphRasterSize(e.fontSize,std::max(std::abs(e.scale.x),std::abs(e.scale.y)),density);
        float baseScale=stbtt_ScaleForPixelHeight(&font,float(rasterSize)); int ascent,descent,gap; stbtt_GetFontVMetrics(&font,&ascent,&descent,&gap);
        float ratio=e.fontSize/rasterSize, x=0,y=ascent*baseScale*ratio; int previousCode=0;
        auto origin=glm::translate(glm::mat4(1),e.position); origin=glm::rotate(origin,glm::radians(e.rotation.z),glm::vec3(0,0,1)); origin=glm::scale(origin,e.scale);
        for(auto code:unicode(e.text)) {
            if(code=='\n') {x=0;y+=(ascent-descent+gap)*baseScale*ratio;previousCode=0;continue;}
            if(previousCode) x+=stbtt_GetCodepointKernAdvance(&font,previousCode,code)*baseScale*ratio;
            auto key=std::make_pair(rasterSize,code);auto it=glyphs.find(key);
            if(it==glyphs.end()) { Glyph g{};int bearing;stbtt_GetCodepointHMetrics(&font,code,&g.advance,&bearing);auto pixels=stbtt_GetCodepointBitmap(&font,0,baseScale,code,&g.w,&g.h,&g.x,&g.y);
                if(pixels && g.w>0 && g.h>0) {std::vector<unsigned char> rgba(g.w*g.h*4,255);for(int i=0;i<g.w*g.h;++i) rgba[i*4+3]=pixels[i];g.texture=texture(rgba.data(),g.w,g.h);} stbtt_FreeBitmap(pixels,nullptr); it=glyphs.emplace(key,g).first;
            }
            auto& g=it->second; if(g.texture) {auto model=glm::translate(origin,glm::vec3(x+(g.x+g.w*.5f)*ratio,y+(g.y+g.h*.5f)*ratio,0)); model=glm::scale(model,glm::vec3(g.w*ratio,g.h*ratio,1)); draw(meshes.at("sprite"),model,projection,e.color,g.texture,false);}
            x+=g.advance*baseScale*ratio;previousCode=code;
        }
    }
};
std::array<float,3> measureText(const Config& config,const std::string& value,float size) {
    if(!std::isfinite(size) || size<=0)throw std::runtime_error("Text size must be positive and finite");
    struct Metrics { fs::path path; fs::file_time_type stamp{}; std::vector<unsigned char> bytes; stbtt_fontinfo font{}; };
    static Metrics metrics;
    auto options=config.data.value("renderer",Json::object());auto path=config.asset("graphics",options.value("font","font.ttf"));
    auto stamp=fs::last_write_time(path);
    if(metrics.path!=path || metrics.stamp!=stamp){auto bytes=textFile(path);std::vector<unsigned char> data(bytes.begin(),bytes.end());stbtt_fontinfo font{};
        if(!stbtt_InitFont(&font,data.data(),stbtt_GetFontOffsetForIndex(data.data(),0)))throw std::runtime_error("Invalid TrueType font: "+path.u8string());
        metrics.bytes=std::move(data);metrics.font=font;metrics.path=path;metrics.stamp=stamp;
    }
    auto& font=metrics.font;float scale=stbtt_ScaleForPixelHeight(&font,size);int ascent,descent,gap;stbtt_GetFontVMetrics(&font,&ascent,&descent,&gap);
    float lineHeight=(ascent-descent+gap)*scale,x=0,width=0;int previous=0,lines=1;
    for(auto code:unicode(value)){if(code=='\n'){width=std::max(width,x);x=0;previous=0;++lines;continue;}int advance,bearing;stbtt_GetCodepointHMetrics(&font,code,&advance,&bearing);
        if(previous)x+=stbtt_GetCodepointKernAdvance(&font,previous,code)*scale;x+=advance*scale;previous=code;
    }
    return {std::max(width,x),lines*lineHeight,lineHeight};
}
void validateMedia(const Config& c) {
    const std::set<std::string> extensions={".png",".jpg",".jpeg",".bmp",".tga",".gif",".psd",".hdr",".pic",".pnm"};
    for(auto& entry:fs::recursive_directory_iterator(c.paths.at("textures")))if(entry.is_regular_file() && extensions.count(entry.path().extension().u8string())) {
        int w,h,n;auto filename=entry.path().u8string();auto* bytes=stbi_load(filename.c_str(),&w,&h,&n,4);
        if(!bytes)throw std::runtime_error("Invalid texture "+filename+": "+stbi_failure_reason());stbi_image_free(bytes);
    }
    for(auto& entry:fs::recursive_directory_iterator(c.paths.at("models")))if(entry.is_regular_file() && entry.path().extension()==".obj")obj(entry.path());
}
Renderer::Renderer():impl(std::make_unique<Impl>()){} Renderer::~Renderer()=default;
void Renderer::init(const Config& c,World& world) {
    impl->config=&c;
    glfwSetErrorCallback([](int code,const char* message){logger.write("ERROR","GLFW "+std::to_string(code)+": "+message);});
    if(!glfwInit()) throw std::runtime_error("GLFW initialization failed");
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE); glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,GLFW_TRUE);
    auto options=c.data.value("window",Json::object()); world.width=options.value("width",1280); world.height=options.value("height",720);
    glfwWindowHint(GLFW_RESIZABLE,options.value("resizable",true));
    impl->window=glfwCreateWindow(world.width,world.height,c.data["project"]["name"].get<std::string>().c_str(),options.value("fullscreen",false)?glfwGetPrimaryMonitor():nullptr,nullptr);
    if(!impl->window) {glfwTerminate();throw std::runtime_error("Cannot create OpenGL 3.3 window");}
    glfwSetWindowUserPointer(impl->window,impl.get());glfwSetScrollCallback(impl->window,[](GLFWwindow* win,double x,double y){static_cast<Impl*>(glfwGetWindowUserPointer(win))->mouseScroll+=glm::vec2(x,y);});
    glfwMakeContextCurrent(impl->window);gl::load();glfwSwapInterval(options.value("vsync",true)?1:0);
#ifndef __APPLE__
    if(c.data["project"].contains("icon")) {auto iconPath=c.resolve(c.data["project"]["icon"]).u8string();GLFWimage icon{};int channels;icon.pixels=stbi_load(iconPath.c_str(),&icon.width,&icon.height,&channels,4);if(!icon.pixels)throw std::runtime_error("Cannot decode window icon");glfwSetWindowIcon(impl->window,1,&icon);stbi_image_free(icon.pixels);}
#endif
    impl->setup();gl::Enable(gl::BLEND);gl::BlendFunc(gl::SRC_ALPHA,gl::ONE_MINUS_SRC_ALPHA);
}
void Renderer::stage(){staged=std::make_unique<Impl>();staged->config=impl->config;try{staged->setup();}catch(...){staged.reset();throw;}}
void Renderer::commit(){
    if(!staged)return;
    std::swap(impl->program,staged->program);std::swap(impl->whiteTexture,staged->whiteTexture);
    impl->meshes.swap(staged->meshes);impl->textures.swap(staged->textures);impl->glyphs.swap(staged->glyphs);
    impl->fontData.swap(staged->fontData);std::swap(impl->font,staged->font);std::swap(impl->fontReady,staged->fontReady);
    staged.reset();
}
void Renderer::discard(){staged.reset();}
void Renderer::checkpointInput(){impl->previousCaptured=impl->captured;impl->previousScreenshot=impl->screenshotPath;}
void Renderer::rollbackInput(){capture(impl->previousCaptured);impl->screenshotPath=impl->previousScreenshot;}
void Renderer::invalidate(){stage();commit();}
void Renderer::validateWorld(const World& world){
    auto& resources=staged?*staged:*impl;
    for(auto& ptr:world.entities){auto& e=*ptr;if(!e.alive || e.kind=="empty")continue;
        if(e.kind=="text"){if(!std::isfinite(e.fontSize) || e.fontSize<=0)throw std::runtime_error("Text size must be positive: "+e.id);continue;}
        if(e.scale.x==0 || e.scale.y==0 || e.scale.z==0)throw std::runtime_error("Drawable scale cannot be zero: "+e.id);
        resources.image(e.texture);
        if(e.kind=="mesh"){auto key="obj:"+e.model;if(!resources.meshes.count(key))resources.meshes.emplace(key,upload(obj(resources.config->asset("models",e.model))));}
    }
}
void Renderer::render(World& world) {
    int w,h;glfwGetFramebufferSize(impl->window,&w,&h);glfwGetWindowSize(impl->window,&world.width,&world.height);if(w<=0 || h<=0 || world.width<=0 || world.height<=0)return;
    impl->density=std::max(float(w)/world.width,float(h)/world.height);
    if(impl->glyphs.size()>4096){for(auto& [key,g]:impl->glyphs)gl::DeleteTextures(1,&g.texture);impl->glyphs.clear();}
    gl::Viewport(0,0,w,h);auto bg=world.background;gl::Disable(gl::SCISSOR_TEST);gl::ClearColor(bg.r,bg.g,bg.b,bg.a);gl::Clear(gl::COLOR|gl::DEPTH);
    auto ortho=glm::ortho(0.0f,float(world.width),float(world.height),0.0f,-10000.0f,10000.0f);
    auto view=world.is3d?glm::perspective(glm::radians(world.fov),float(w)/h,.05f,10000.0f)*glm::lookAt(world.cameraPosition,world.cameraTarget,glm::vec3(0,1,0)):ortho*glm::translate(glm::mat4(1),glm::vec3(-world.cameraPosition.x,-world.cameraPosition.y,0));
    auto entities=world.entities;
    std::stable_sort(entities.begin(),entities.end(),[&](const auto& a,const auto& b){if(a->screen!=b->screen)return !a->screen;return (a->screen || !world.is3d) && a->position.z<b->position.z;});
    // Overlay entities are drawn last with no depth test.
    for(int pass=0;pass<2;++pass) for(auto& ptr:entities) {auto& e=*ptr;if(!e.alive || !e.visible || e.kind=="empty" || int(e.screen)!=pass)continue;
        if(e.clipped && e.screen){auto c=e.clip;gl::Enable(gl::SCISSOR_TEST);gl::Scissor(int(c.x*w/world.width),int((world.height-c.y-c.w)*h/world.height),std::max(0,int(c.z*w/world.width)),std::max(0,int(c.w*h/world.height)));}else gl::Disable(gl::SCISSOR_TEST);
        if(world.is3d && !e.screen)gl::Enable(gl::DEPTH_TEST);else gl::Disable(gl::DEPTH_TEST);
        auto projection=e.screen?ortho:view;if(e.kind=="text"){impl->text(e,projection);continue;}
        if(e.scale.x==0 || e.scale.y==0 || e.scale.z==0)throw std::runtime_error("Drawable scale cannot be zero: "+e.id);
        auto key=e.kind=="mesh"?"obj:"+e.model:e.kind;
        auto mesh=impl->meshes.find(key);if(mesh==impl->meshes.end()){if(e.kind!="mesh")throw std::runtime_error("Unknown drawable: "+e.kind);mesh=impl->meshes.emplace(key,upload(obj(impl->config->asset("models",e.model)))).first;}
        auto model=glm::translate(glm::mat4(1),e.position);model=glm::rotate(model,glm::radians(e.rotation.x),glm::vec3(1,0,0));model=glm::rotate(model,glm::radians(e.rotation.y),glm::vec3(0,1,0));model=glm::rotate(model,glm::radians(e.rotation.z),glm::vec3(0,0,1));model=glm::scale(model,e.scale);
        impl->draw(mesh->second,model,projection,e.color,impl->image(e.texture),world.is3d && !e.screen);
    }
    gl::Disable(gl::SCISSOR_TEST);
    if(!impl->screenshotPath.empty()) {
        std::vector<unsigned char> pixels(static_cast<size_t>(w)*h*3);gl::PixelStorei(gl::PACK_ALIGNMENT,1);gl::ReadPixels(0,0,w,h,gl::RGB,gl::UNSIGNED_BYTE,pixels.data());
        auto path=impl->screenshotPath;fs::create_directories(path.parent_path());std::ofstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Cannot save screenshot: "+path.u8string());
        file<<"P6\n"<<w<<" "<<h<<"\n255\n";for(int y=h-1;y>=0;--y)file.write(reinterpret_cast<char*>(pixels.data()+static_cast<size_t>(y)*w*3),w*3);if(!file)throw std::runtime_error("Screenshot write failed");
        impl->screenshotPath.clear();logger.write("INFO","Screenshot saved: "+path.u8string());
    }
    glfwSwapBuffers(impl->window);
}
void Renderer::poll(){impl->previous=impl->keys;impl->mouseScroll=glm::vec2(0);glfwPollEvents();for(int i=GLFW_KEY_SPACE;i<=GLFW_KEY_LAST;++i)impl->keys[i]=glfwGetKey(impl->window,i)==GLFW_PRESS;double x,y;glfwGetCursorPos(impl->window,&x,&y);auto p=glm::vec2(x,y);impl->mouseDelta=impl->firstMouse?glm::vec2(0):p-impl->mousePosition;impl->firstMouse=false;impl->mousePosition=p;}
bool Renderer::closing()const{return glfwWindowShouldClose(impl->window);}
bool Renderer::key(const std::string& s)const{return impl->keys.at(keyCode(s));}
bool Renderer::pressed(const std::string& s)const{auto k=keyCode(s);return impl->keys.at(k) && !impl->previous.at(k);}
bool Renderer::mouse(int button)const{if(button<0 || button>7)throw std::runtime_error("Mouse button must be 0..7");return glfwGetMouseButton(impl->window,button)==GLFW_PRESS;}
glm::vec2 Renderer::scroll()const{return impl->mouseScroll;}
glm::vec2 Renderer::cursor()const{return impl->mousePosition;} glm::vec2 Renderer::delta()const{return impl->mouseDelta;}
void Renderer::capture(bool enabled){if(impl->captured==enabled)return;impl->captured=enabled;glfwSetInputMode(impl->window,GLFW_CURSOR,enabled?GLFW_CURSOR_DISABLED:GLFW_CURSOR_NORMAL);impl->firstMouse=true;}
void Renderer::screenshot(const fs::path& path){impl->screenshotPath=path;}
void Renderer::quit(){glfwSetWindowShouldClose(impl->window,1);}
}
