#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#include <forge/engine.hpp>
namespace forge {
struct Audio::Impl {
    ma_engine engine{};
    bool initialized=false;
    std::vector<std::unique_ptr<ma_sound>> sounds;
    void init() {
        if(initialized) return;
        if(ma_engine_init(nullptr,&engine)!=MA_SUCCESS) throw std::runtime_error("Audio device initialization failed");
        initialized=true;
    }
    ~Impl() { for(auto& s:sounds) ma_sound_uninit(s.get()); if(initialized) ma_engine_uninit(&engine); }
};
Audio::Audio():impl(std::make_unique<Impl>()) {} Audio::~Audio()=default;
void Audio::play(const fs::path& path,bool loop,float volume) {
    if(!fs::is_regular_file(path)) throw std::runtime_error("Audio file missing: "+path.u8string());
    impl->init();
    auto& sounds=impl->sounds;
    for(auto it=sounds.begin();it!=sounds.end();) { if(ma_sound_at_end(it->get())) { ma_sound_uninit(it->get()); it=sounds.erase(it); } else ++it; }
    auto sound=std::make_unique<ma_sound>();
    auto filename=path.u8string();
#ifdef _WIN32
    auto result=ma_sound_init_from_file_w(&impl->engine,path.wstring().c_str(),MA_SOUND_FLAG_STREAM,nullptr,nullptr,sound.get());
#else
    auto result=ma_sound_init_from_file(&impl->engine,filename.c_str(),MA_SOUND_FLAG_STREAM,nullptr,nullptr,sound.get());
#endif
    if(result!=MA_SUCCESS) throw std::runtime_error("Cannot decode audio: "+filename);
    ma_sound_set_looping(sound.get(),loop); ma_sound_set_volume(sound.get(),glm::clamp(volume,0.0f,1.0f));
    if(ma_sound_start(sound.get())!=MA_SUCCESS) { ma_sound_uninit(sound.get()); throw std::runtime_error("Cannot start audio"); }
    sounds.push_back(std::move(sound));
}
void Audio::stop() { for(auto& s:impl->sounds) ma_sound_uninit(s.get()); impl->sounds.clear(); }
}
