#include <forge/particle_render.hpp>
#include <forge/gl.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
namespace forge {
namespace {
struct Instance {glm::vec3 position;glm::vec4 uv,color;glm::vec2 sizeAngle;};
struct Vertex {glm::vec3 position;glm::vec2 uv;glm::vec4 color;};
const glm::vec2 corners[]={{-.5f,-.5f},{.5f,-.5f},{.5f,.5f},{-.5f,-.5f},{.5f,.5f},{-.5f,.5f}};
}
struct ParticleRenderer::Impl {
    bool instanced;
    unsigned vao=0,stream=0,quad=0;
    size_t allocated=0;
    struct Reference {const ParticleDraw* particle;float depth;};
    std::vector<Reference> order;
    std::vector<Instance> instances;
    std::vector<Vertex> vertices;
    explicit Impl(bool value):instanced(value){}
    ~Impl(){if(stream)gl::DeleteBuffers(1,&stream);if(quad)gl::DeleteBuffers(1,&quad);if(vao)gl::DeleteVertexArrays(1,&vao);}
    void initialize(const std::function<void(size_t)>& reserve){
        if(vao)return;
        if(instanced)reserve(sizeof(corners));
        gl::GenVertexArrays(1,&vao);gl::BindVertexArray(vao);
        gl::GenBuffers(1,&stream);
        if(instanced){
            gl::GenBuffers(1,&quad);gl::BindBuffer(gl::ARRAY_BUFFER,quad);
            gl::BufferData(gl::ARRAY_BUFFER,sizeof(corners),corners,gl::STATIC_DRAW);
            gl::EnableVertexAttribArray(0);gl::VertexAttribPointer(0,2,gl::FLOAT,0,sizeof(glm::vec2),nullptr);
            gl::BindBuffer(gl::ARRAY_BUFFER,stream);
            const int sizes[]={3,4,4,2};
            const size_t offsets[]={offsetof(Instance,position),offsetof(Instance,uv),offsetof(Instance,color),offsetof(Instance,sizeAngle)};
            for(unsigned i=0;i<4;++i){gl::EnableVertexAttribArray(i+1);gl::VertexAttribPointer(i+1,sizes[i],gl::FLOAT,0,sizeof(Instance),reinterpret_cast<void*>(offsets[i]));gl::VertexAttribDivisor(i+1,1);}
        }else{
            gl::BindBuffer(gl::ARRAY_BUFFER,stream);
            const int sizes[]={3,2,4};const size_t offsets[]={offsetof(Vertex,position),offsetof(Vertex,uv),offsetof(Vertex,color)};
            for(unsigned i=0;i<3;++i){gl::EnableVertexAttribArray(i);gl::VertexAttribPointer(i,sizes[i],gl::FLOAT,0,sizeof(Vertex),reinterpret_cast<void*>(offsets[i]));}
        }
    }
};
ParticleRenderer::ParticleRenderer(bool value):impl(std::make_unique<Impl>(value)){}
ParticleRenderer::~ParticleRenderer()=default;
ParticleRenderStats ParticleRenderer::draw(const std::vector<ParticleDraw>& source,const glm::mat4& view,
        bool is3d,bool screen,unsigned layers,unsigned program,unsigned white,
        const std::function<unsigned(const std::string&)>& texture,const std::function<void(size_t)>& reserve){
    ParticleRenderStats stats;
    auto& state=*impl;state.order.clear();
    auto started=std::chrono::steady_clock::now();
    for(auto& p:source){
        if(p.screen!=screen || !(p.layer&layers) || p.size<=0 || p.color.a<=0)continue;
        auto clip=view*glm::vec4(p.position,1);
        float depth=is3d && !screen && std::abs(clip.w)>1e-8?-clip.z/clip.w:p.position.z;
        state.order.push_back({&p,depth});
    }
    auto less=[](const auto& a,const auto& b){
        auto& p=*a.particle;auto& q=*b.particle;
        if(p.additive!=q.additive)return !p.additive;
        if(p.additive)return p.texture<q.texture; // additive blending needs grouping, not depth sorting
        return a.depth<b.depth;
    };
    if(!std::is_sorted(state.order.begin(),state.order.end(),less))
        std::stable_sort(state.order.begin(),state.order.end(),less);
    stats.sortMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    if(state.order.empty())return stats;
    state.initialize(reserve);
    glm::vec3 right(1,0,0),up(0,1,0);
    if(is3d && !screen){
        auto inverse=glm::inverse(view);
        auto unproject=[&](float x,float y){auto p=inverse*glm::vec4(x,y,0,1);return glm::vec3(p)/p.w;};
        auto center=unproject(0,0);right=glm::normalize(unproject(1,0)-center);up=glm::normalize(unproject(0,1)-center);
    }
    gl::Disable(gl::SCISSOR_TEST);
    if(is3d && !screen)gl::Enable(gl::DEPTH_TEST);else gl::Disable(gl::DEPTH_TEST);
    gl::DepthMask(0);
    state.instances.clear();state.vertices.clear();
    unsigned currentTexture=0;bool additive=false,prepared=false;
    auto flush=[&](){
        size_t count=state.instanced?state.instances.size():state.vertices.size()/6;
        if(!count)return;
        size_t bytes=state.instanced?count*sizeof(Instance):state.vertices.size()*sizeof(Vertex);
        if(bytes>state.allocated){reserve(bytes-state.allocated);state.allocated=bytes;}
        gl::UseProgram(program);
        gl::UniformMatrix4fv(gl::GetUniformLocation(program,"u_view"),1,0,glm::value_ptr(view));
        gl::Uniform3fv(gl::GetUniformLocation(program,"u_right"),1,glm::value_ptr(right));
        gl::Uniform3fv(gl::GetUniformLocation(program,"u_up"),1,glm::value_ptr(up));
        gl::Uniform1i(gl::GetUniformLocation(program,"u_texture"),0);
        gl::ActiveTexture(gl::TEXTURE0);gl::BindTexture(gl::TEXTURE_2D,currentTexture?currentTexture:white);
        gl::BlendFunc(gl::SRC_ALPHA,additive?1:gl::ONE_MINUS_SRC_ALPHA);
        gl::BindVertexArray(state.vao);gl::BindBuffer(gl::ARRAY_BUFFER,state.stream);
        // Keep allocated capacity stable; only upload the live prefix.
        gl::BufferData(gl::ARRAY_BUFFER,state.allocated,nullptr,gl::STREAM_DRAW);
        gl::BufferSubData(gl::ARRAY_BUFFER,0,bytes,state.instanced?static_cast<const void*>(state.instances.data()):state.vertices.data());
        if(state.instanced)gl::DrawArraysInstanced(gl::TRIANGLES,0,6,int(count));
        else gl::DrawArrays(gl::TRIANGLES,0,int(count*6));
        ++stats.calls;stats.quads+=unsigned(count);stats.uploadBytes+=bytes;
        state.instances.clear();state.vertices.clear();
    };
    try{
        for(auto& ref:state.order){
            auto& p=*ref.particle;auto image=texture(p.texture);
            size_t count=state.instanced?state.instances.size():state.vertices.size()/6;
            if(prepared && (image!=currentTexture || additive!=p.additive || count>=4096))flush();
            prepared=true;currentTexture=image;additive=p.additive;
            if(state.instanced)state.instances.push_back({p.position,p.uv,p.color,{p.size,p.angle}});
            else{
                float angle=glm::radians(p.angle),c=std::cos(angle),s=std::sin(angle);
                for(auto& corner:corners){
                    auto offset=corner*p.size;float x=offset.x*c-offset.y*s,y=offset.x*s+offset.y*c;
                    auto uv=corner+glm::vec2(.5f);
                    state.vertices.push_back({p.position+right*x+up*y,{p.uv.x+uv.x*p.uv.z,p.uv.y+uv.y*p.uv.w},p.color});
                }
            }
        }
        flush();
    }catch(...){gl::DepthMask(1);gl::BlendFunc(gl::SRC_ALPHA,gl::ONE_MINUS_SRC_ALPHA);throw;}
    gl::DepthMask(1);gl::BlendFunc(gl::SRC_ALPHA,gl::ONE_MINUS_SRC_ALPHA);
    return stats;
}
}
