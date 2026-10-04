#include <forge/engine.hpp>
#include <forge/animation.hpp>
#include <forge/editor.hpp>
#include <imgui.h>
#include <algorithm>
namespace forge {
bool animationEditor(Entity& entity,Runtime& runtime,Editor& editor){
    if(ImGui::Button("Open animation editor"))editor.animationOpen=true;
    if(!editor.animationOpen)return false;
    ImGui::SetNextWindowPos(ImVec2(310,40),ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(600,700),ImGuiCond_FirstUseEver);
    if(!ImGui::Begin("Forge Animation Editor",&editor.animationOpen)){ImGui::End();return false;}
    bool changed=false;
    try{
        auto model=runtime.assets.model(runtime.config.asset("models",entity.model));
        if(editor.animationEntity!=entity.id){
            editor.animationEntity=entity.id;editor.animationPlaying=false;
            editor.animationDraft=entity.animatorSettings;
            if(editor.animationDraft.empty()){
                editor.animationDraft={{"layers",Json::array()}};
                if(!model->clips.empty())editor.animationDraft["layers"].push_back({{"clip",model->clips[0].name}});
            }
            std::snprintf(editor.animationJson.data(),editor.animationJson.size(),"%s",editor.animationDraft.dump(2).c_str());
            editor.animationStatus.clear();
        }
        auto& draft=editor.animationDraft;
        ImGui::SliderFloat("Transition seconds",&editor.animationFade,0,5);
        if(ImGui::Button("Apply / transition")){
            transitionAnimator(runtime,entity,draft,editor.animationFade);changed=true;
        }
        ImGui::SameLine();
        ImGui::Checkbox("Animation preview",&editor.animationPlaying);
        // Preview may run independently from scene physics/scripts.
        if(editor.animationPlaying && entity.animator && runtime.gamePaused){
            entity.animator->playing=true;updateAnimation(entity,runtime.dt);
            entity.animator->events=Json::array();
        }
        if(entity.animator){
            if(ImGui::Button(entity.animator->playing?"Pause clip":"Resume clip"))entity.animator->playing=!entity.animator->playing;
            float length=float(entity.animator->target->clips.empty()?0:entity.animator->target->clips[0].duration/entity.animator->target->clips[0].ticks);
            auto& first=entity.animator->layers[0];
            for(auto& clip:first.source->clips)if(clip.name==first.settings.at("clip"))length=float(clip.duration/clip.ticks);
            length=float(first.length);
            float time=length>0?float(std::fmod(first.time,length)):0;
            if(ImGui::SliderFloat("Timeline (seconds)",&time,0,std::max(.001f,length))){
                auto settings=entity.animatorSettings;
                for(auto& layer:settings["layers"])layer["time"]=time;
                settings["playing"]=false;configureAnimator(runtime,entity,settings);
                editor.animationPlaying=false;
            }
        }
        auto& layers=draft["layers"];
        for(size_t i=0;i<layers.size();++i){
            ImGui::PushID(int(i));auto& layer=layers[i];
            ImGui::Separator();ImGui::Text("Layer %zu",i);
            auto source=layer.contains("model")?runtime.assets.model(runtime.config.asset("models",layer["model"])):model;
            std::string current=layer.value("clip","");
            if(ImGui::BeginCombo("Clip",current.c_str())){
                for(auto& clip:source->clips)if(ImGui::Selectable(clip.name.c_str(),clip.name==current)){layer["clip"]=clip.name;layer.erase("duration");layer.erase("tracks");}
                ImGui::EndCombo();
            }
            float weight=layer.value("weight",1.f),speed=layer.value("speed",1.f);
            bool loop=layer.value("loop",true);
            if(ImGui::SliderFloat("Weight",&weight,0,1))layer["weight"]=weight;
            if(ImGui::DragFloat("Speed",&speed,.05f,-100,100))layer["speed"]=speed;
            if(ImGui::Checkbox("Loop",&loop))layer["loop"]=loop;
            double duration=layer.value("duration",0.0);for(auto& clip:source->clips)if(!layer.contains("duration") && clip.name==layer.value("clip",""))duration=clip.duration/clip.ticks;
            auto markers=layer.value("events",Json::array());
            auto pos=ImGui::GetCursorScreenPos();float width=ImGui::GetContentRegionAvail().x;
            ImGui::InvisibleButton("Marker timeline",ImVec2(width,24));
            auto draw=ImGui::GetWindowDrawList();draw->AddLine(ImVec2(pos.x,pos.y+12),ImVec2(pos.x+width,pos.y+12),IM_COL32(100,160,230,255),2);
            for(auto& marker:markers){float x=pos.x+float(marker.value("time",0.0)/std::max(.001,duration))*width;draw->AddCircleFilled(ImVec2(x,pos.y+12),4,IM_COL32(255,180,30,255));}
            if(ImGui::Button("Add marker") && markers.size()<1024)markers.push_back({{"name","event"},{"time",duration/2}});
            for(size_t j=0;j<markers.size();++j){
                ImGui::PushID(int(j));char name[256]{};std::snprintf(name,sizeof(name),"%s",markers[j].value("name","").c_str());
                if(ImGui::InputText("Event",name,sizeof(name)))markers[j]["name"]=name;
                float time=markers[j].value("time",0.f);if(ImGui::SliderFloat("At",&time,0,float(duration)))markers[j]["time"]=time;
                if(ImGui::Button("Remove marker")){markers.erase(j);--j;}ImGui::PopID();
            }
            layer["events"]=markers;
            if(layer.contains("duration")){
                float seconds=layer["duration"];
                if(ImGui::DragFloat("Clip duration",&seconds,.05f,.001f,10000)){layer["duration"]=seconds;duration=seconds;}
                if(ImGui::BeginCombo("Bone",editor.boneNode.c_str())){
                    for(auto& n:source->nodes)if(ImGui::Selectable(n.name.c_str(),editor.boneNode==n.name))editor.boneNode=n.name;
                    ImGui::EndCombo();
                }
                ImGui::SliderFloat("Key time",&editor.boneTime,0,float(duration));
                ImGui::DragFloat3("Key position",&editor.bonePosition.x,.01f);
                ImGui::DragFloat3("Key Euler degrees",&editor.boneRotation.x,1);
                ImGui::DragFloat3("Key scale",&editor.boneScale.x,.01f);
                if(ImGui::Button("Insert / replace TRS key") && !editor.boneNode.empty()){
                    auto tracks=layer.value("tracks",Json::array());
                    auto found=std::find_if(tracks.begin(),tracks.end(),[&](auto& track){return track.value("node","")==editor.boneNode;});
                    if(found==tracks.end()){tracks.push_back({{"node",editor.boneNode}});found=tracks.end()-1;}
                    for(auto item:{std::pair<const char*,glm::vec3>{"position",editor.bonePosition},{"rotation",editor.boneRotation},{"scale",editor.boneScale}}){
                        auto keys=found->value(item.first,Json::array());
                        keys.erase(std::remove_if(keys.begin(),keys.end(),[&](auto& key){return std::abs(key.at("time").template get<float>()-editor.boneTime)<1e-5f;}),keys.end());
                        keys.push_back({{"time",editor.boneTime},{"value",{item.second.x,item.second.y,item.second.z}}});
                        std::sort(keys.begin(),keys.end(),[](auto& a,auto& b){return a.at("time").template get<double>()<b.at("time").template get<double>();});
                        (*found)[item.first]=keys;
                    }
                    layer["tracks"]=tracks;
                }
                auto tracks=layer.value("tracks",Json::array());
                for(auto& track:tracks)if(track.value("node","")==editor.boneNode){
                    auto keys=track.value("position",Json::array());
                    for(auto& key:keys){float time=key.at("time");ImGui::PushID(int(time*1000));
                        if(ImGui::SmallButton(("Load key "+std::to_string(time)).c_str())){
                            editor.boneTime=time;
                            auto read=[&](const char* field,glm::vec3& value){for(auto& k:track.at(field))if(std::abs(k.at("time").template get<float>()-time)<1e-5f){auto v=k.at("value");value={v[0],v[1],v[2]};}};
                            read("position",editor.bonePosition);read("rotation",editor.boneRotation);read("scale",editor.boneScale);
                        }
                        ImGui::SameLine();if(ImGui::SmallButton("Delete key")){
                            for(auto field:{"position","rotation","scale"}){
                                auto& list=track[field];list.erase(std::remove_if(list.begin(),list.end(),[&](auto& k){return std::abs(k.at("time").template get<float>()-time)<1e-5f;}),list.end());
                                if(list.empty())track.erase(field);
                            }
                        }ImGui::PopID();
                    }
                }
                layer["tracks"]=tracks;
            }
            bool remove=ImGui::Button("Remove layer");ImGui::PopID();
            if(remove){layers.erase(i);--i;}
        }
        if(ImGui::Button("Add layer") && layers.size()<16 && !model->clips.empty())layers.push_back({{"clip",model->clips[0].name}});
        if(ImGui::Button("New authored bone clip") && layers.size()<16)layers.push_back({{"clip","Authored"},{"duration",2},{"tracks",Json::array()}});
        ImGui::TextWrapped("Retarget: set model, mapping (source -> target), translation_scale and mask in the JSON below. Apply validates before changing playback.");
        if(ImGui::Button("Draft -> JSON")){
            auto text=draft.dump(2);if(text.size()>=editor.animationJson.size())throw std::runtime_error("Settings exceed editor JSON buffer");
            std::snprintf(editor.animationJson.data(),editor.animationJson.size(),"%s",text.c_str());
        }
        ImGui::TextUnformatted("Layers / retarget JSON");
        ImGui::InputTextMultiline("##retarget-json",editor.animationJson.data(),editor.animationJson.size(),ImVec2(-1,140));
        if(ImGui::Button("Validate JSON -> draft")){
            auto next=Json::parse(editor.animationJson.data());validateAnimator(runtime,entity,next);draft=std::move(next);editor.animationStatus="Settings valid";
        }
        std::set<std::string> morphs;for(auto& part:model->parts)for(auto& target:part.morphs)morphs.insert(target.name);
        for(auto& name:morphs){
            float value=entity.morphWeights.value(name,0.f);
            if(ImGui::SliderFloat(("Morph: "+name).c_str(),&value,-1,1)){auto next=entity.morphWeights;next[name]=value;validateMorphWeights(*model,next);entity.morphWeights=std::move(next);changed=true;}
        }
        if(!morphs.empty() && ImGui::Button("Use animated morph weights")){entity.morphWeights=Json::object();changed=true;}
        ImGui::InputText("Prefab file",editor.animationPrefab,sizeof(editor.animationPrefab));
        if(ImGui::Button("Export animation prefab")){
            validateAnimator(runtime,entity,draft);
            auto path=runtime.config.asset("objects",editor.animationPrefab);
            if(path.extension()!=".json")throw std::runtime_error("Animation prefab needs .json extension");
            if(fs::exists(path))throw std::runtime_error("Export destination exists; choose a new filename");
            Json data={{"kind","mesh"},{"model",entity.model},{"animator",draft},{"morph_weights",entity.morphWeights}};
            std::ofstream out(path,std::ios::binary);out<<data.dump(2)<<'\n';out.close();if(!out)throw std::runtime_error("Cannot export animation prefab");
            editor.animationStatus="Exported "+path.u8string();
        }
    }catch(const std::exception& error){editor.animationStatus=error.what();}
    ImGui::TextWrapped("%s",editor.animationStatus.c_str());
    ImGui::End();
    return changed;
}
}
