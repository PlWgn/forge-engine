#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#include <forge/engine.hpp>
#include <cmath>
namespace forge {
namespace {
float level(float value){if(!std::isfinite(value) || value<0 || value>1)throw std::runtime_error("Volume must be in 0..1");return value;}
float duration(float value){if(!std::isfinite(value) || value<0)throw std::runtime_error("Fade duration must be finite and nonnegative");return value;}
}
struct Audio::Impl {
    ma_engine engine{};
    bool initialized=false, transaction=false;
    unsigned nextId=0;
    struct Voice {
        std::shared_ptr<ma_sound> sound;
        std::string channel;
        float envelope=1, start=1, target=1, elapsed=0, seconds=0;
        bool paused=false, stopAfter=false;
    };
    std::map<unsigned,Voice> voices, backup;
    std::map<std::string,float> volumes, backupVolumes;
    float volume(const std::string& channel)const{auto it=volumes.find(channel);return it==volumes.end()?1:it->second;}
    void apply(){for(auto& [id,v]:voices){ma_sound_set_volume(v.sound.get(),v.envelope*volume(v.channel)*volume("master"));if(v.paused)ma_sound_stop(v.sound.get());else if(!ma_sound_is_playing(v.sound.get()))ma_sound_start(v.sound.get());}}
    void init(bool silent) {
        if(initialized)return;
        auto config=ma_engine_config_init();config.noDevice=silent;config.channels=2;config.sampleRate=48000;
        if(ma_engine_init(&config,&engine)!=MA_SUCCESS)throw std::runtime_error("Audio device initialization failed");initialized=true;
    }
    ~Impl(){voices.clear();backup.clear();if(initialized)ma_engine_uninit(&engine);}
};
Audio::Audio():impl(std::make_unique<Impl>()){} Audio::~Audio()=default;
unsigned Audio::play(const fs::path& path,bool loop,float volume,const std::string& channel,float seconds){
    level(volume);duration(seconds);if(channel.empty() || channel=="master")throw std::runtime_error("Audio channel must be named and cannot be master");
    if(!fs::is_regular_file(path))throw std::runtime_error("Audio file missing: "+path.u8string());impl->init(silent);
    auto raw=std::make_unique<ma_sound>();
#ifdef _WIN32
    auto result=ma_sound_init_from_file_w(&impl->engine,path.wstring().c_str(),MA_SOUND_FLAG_STREAM,nullptr,nullptr,raw.get());
#else
    auto result=ma_sound_init_from_file(&impl->engine,path.u8string().c_str(),MA_SOUND_FLAG_STREAM,nullptr,nullptr,raw.get());
#endif
    if(result!=MA_SUCCESS)throw std::runtime_error("Cannot decode audio: "+path.u8string());
    auto sound=std::shared_ptr<ma_sound>(raw.release(),[](ma_sound* s){ma_sound_uninit(s);delete s;});
    ma_sound_set_looping(sound.get(),loop);
    Impl::Voice voice;voice.sound=sound;voice.channel=channel;voice.envelope=seconds>0?0:volume;voice.target=volume;voice.start=voice.envelope;voice.seconds=seconds;
    unsigned id=++impl->nextId;impl->voices.emplace(id,std::move(voice));if(!impl->transaction)impl->apply();return id;
}
void Audio::stop(unsigned id,const std::string& channel,float seconds){
    duration(seconds);std::vector<unsigned> remove;
    for(auto& [key,v]:impl->voices)if((!id || id==key) && (channel.empty() || v.channel==channel)){if(seconds>0)fade(key,0,seconds,true);else remove.push_back(key);}
    for(auto key:remove){if(!impl->transaction)ma_sound_stop(impl->voices.at(key).sound.get());impl->voices.erase(key);}
}
void Audio::volume(const std::string& channel,float value){impl->volumes[channel]=level(value);if(!impl->transaction)impl->apply();}
float Audio::volume(const std::string& channel)const{return impl->volume(channel);}
void Audio::fade(unsigned id,float target,float seconds,bool stopAfter){
    level(target);duration(seconds);auto it=impl->voices.find(id);if(it==impl->voices.end())return;
    auto& v=it->second;v.start=v.envelope;v.target=target;v.elapsed=0;v.seconds=seconds;v.stopAfter=stopAfter;
    if(seconds==0){v.envelope=target;if(stopAfter){stop(id);return;}}if(!impl->transaction)impl->apply();
}
float Audio::gain(unsigned id)const{auto it=impl->voices.find(id);return it==impl->voices.end()?0:it->second.envelope;}
bool Audio::playing(unsigned id)const{auto it=impl->voices.find(id);return it!=impl->voices.end() && !it->second.paused && !ma_sound_at_end(it->second.sound.get());}
std::vector<unsigned> Audio::ids(const std::string& channel)const{std::vector<unsigned> result;for(auto& [id,v]:impl->voices)if(channel.empty() || v.channel==channel)result.push_back(id);return result;}
void Audio::pause(unsigned id,bool paused){auto it=impl->voices.find(id);if(it!=impl->voices.end()){it->second.paused=paused;if(!impl->transaction)impl->apply();}}
void Audio::update(float dt){
    std::vector<unsigned> remove;
    for(auto& [id,v]:impl->voices){if(!v.paused && v.seconds>0){v.elapsed+=dt;v.envelope=v.start+(v.target-v.start)*std::min(1.0f,v.elapsed/v.seconds);if(v.elapsed>=v.seconds){v.seconds=0;if(v.stopAfter)remove.push_back(id);}}if(ma_sound_at_end(v.sound.get()))remove.push_back(id);}
    for(auto id:remove)stop(id);impl->apply();
    if(silent && impl->initialized){std::vector<float> output(size_t(std::ceil(dt*48000))*2);ma_engine_read_pcm_frames(&impl->engine,output.data(),output.size()/2,nullptr);}
}
void Audio::begin(){if(impl->transaction)throw std::runtime_error("Nested audio transaction");impl->backup=impl->voices;impl->backupVolumes=impl->volumes;impl->transaction=true;}
void Audio::commit(){for(auto& [id,v]:impl->backup)if(!impl->voices.count(id))ma_sound_stop(v.sound.get());impl->backup.clear();impl->backupVolumes.clear();impl->transaction=false;impl->apply();}
void Audio::rollback(){impl->voices=std::move(impl->backup);impl->volumes=std::move(impl->backupVolumes);impl->transaction=false;impl->apply();}
}
