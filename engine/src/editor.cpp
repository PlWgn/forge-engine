#include <forge/engine.hpp>
#include <forge/editor.hpp>
#include <forge/animation.hpp>
#include <forge/graphics_device.hpp>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <functional>
#include <cstdio>
namespace forge {
void Editor::draw(World &world,Runtime &runtime,const std::array<bool,GLFW_KEY_LAST+1> &keys,const std::array<bool,8> &buttons,glm::vec2 mouseDelta,const Json &diagnostics) {
    GraphicsDevice::current().editorNewFrame();ImGui_ImplGlfw_NewFrame();ImGui::NewFrame();
    auto &io=ImGui::GetIO();auto &session=runtime.editorSession;auto &selected=session.selected;
    auto tr=[&](const char *key){return runtime.localization.translate(std::string("engine.editor.")+key);};
    auto label=[&](const char *key){return tr(key)+"###"+key;};
    auto button=[&](const char *key){return ImGui::Button(label(key).c_str());};
    auto run=[&](const Json &request){try {session.command(runtime,request);}catch(const std::exception &e){status=e.what();logger.write("WARN",status);}};
    if(!styled) {
        auto &style=ImGui::GetStyle();style.WindowRounding=0;style.ChildRounding=3;style.FrameRounding=3;
        style.WindowPadding={10,8};style.FramePadding={7,4};style.ItemSpacing={7,6};style.WindowBorderSize=1;style.WindowMinSize={1,1};
        style.Colors[ImGuiCol_WindowBg]={.115f,.125f,.145f,1};style.Colors[ImGuiCol_ChildBg]={.095f,.105f,.12f,1};
        style.Colors[ImGuiCol_FrameBg]={.17f,.19f,.22f,1};style.Colors[ImGuiCol_Button]={.22f,.25f,.29f,1};
        style.Colors[ImGuiCol_Header]={.19f,.32f,.43f,1};style.Colors[ImGuiCol_HeaderHovered]={.24f,.40f,.54f,1};
        style.Colors[ImGuiCol_CheckMark]={.35f,.74f,1,1};style.Colors[ImGuiCol_Text]={.86f,.89f,.94f,1};
        styled=true;
    }
    if(sceneIdentity!=runtime.currentScene) {
        sceneIdentity=runtime.currentScene;
        if(fs::u8path(sceneIdentity).extension()==".json")std::snprintf(sceneFile,sizeof(sceneFile),"%s",sceneIdentity.c_str());
    }
    bool saveRequested=false;auto save=[&](){saveRequested=true;};
    preview=!runtime.gamePaused;
    float width=io.DisplaySize.x,height=io.DisplaySize.y;
    auto area=layout(width,height);
    constexpr auto flags=ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings;
    auto panel=[&](const char *id,float x,float y,float w,float h,ImGuiWindowFlags extra=0) {
        ImGui::SetNextWindowPos({x,y});ImGui::SetNextWindowSize({std::max(1.f,w),std::max(1.f,h)});ImGui::Begin(id,nullptr,flags|extra);
    };
    panel("##Toolbar",0,0,width,42,ImGuiWindowFlags_NoScrollbar);
    ImGui::TextColored({.4f,.77f,1,1},"FORGE");ImGui::SameLine();
    if(button("file"))ImGui::OpenPopup("File menu");
    if(ImGui::BeginPopup("File menu")) {
        if(ImGui::MenuItem(tr("save").c_str(),"Ctrl/Cmd+S"))save();
        if(ImGui::MenuItem(tr("reload").c_str()))run({{"op","load"},{"file",runtime.currentScene}});
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(preview);if(button("create"))ImGui::OpenPopup("Create menu");ImGui::EndDisabled();
    if(ImGui::BeginPopup("Create menu")) {
        for(auto kind:{"empty","cube","plane","sprite","text"})if(ImGui::MenuItem(tr(kind).c_str())) {
            Json entity={{"kind",kind},{"name",tr(kind)}};
            if(std::string(kind)=="plane")entity.update({{"kind","cube"},{"scale",{10,.1,10}}});
            if(std::string(kind)=="sprite")entity.update({{"position",{world.width*.5f,world.height*.5f,0}},{"scale",{100,100,1}}});
            if(std::string(kind)=="text")entity.update({{"text",tr("new_text")},{"screen",true},{"position",{100,100,0}}});
            run({{"op","patch"},{"patch",Json::array({{{"op","add"},{"path","/entities/-"},{"value",entity}}})}});
            if(!world.entities.empty())selected=world.entities.back()->id;
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();ImGui::BeginDisabled(preview || session.undo.empty());if(button("undo"))run({{"op","undo"}});ImGui::EndDisabled();
    ImGui::SameLine();ImGui::BeginDisabled(preview || session.redo.empty());if(button("redo"))run({{"op","redo"}});ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button,preview?ImVec4(.20f,.44f,.37f,1):ImVec4(.19f,.35f,.48f,1));
    if(button(preview?"pause":"play")) {run({{"op","preview"},{"enabled",!preview}});preview=!runtime.gamePaused;}
    ImGui::PopStyleColor();ImGui::SameLine();
    if(button("world"))settings=!settings;
    ImGui::SameLine();
    if(button("workspace"))ImGui::OpenPopup("Workspace menu");
    if(ImGui::BeginPopup("Workspace menu")) {
        if(ImGui::MenuItem(tr("reset_layout").c_str())){leftFraction=.19f;rightFraction=.27f;bottomFraction=.30f;}
        for(auto &[code,catalog]:runtime.localization.catalogs)
            if(ImGui::MenuItem(catalog.name.c_str(),nullptr,runtime.localization.language==code))runtime.localization.select(code,false);
        ImGui::EndPopup();
    }
    ImGui::End();
    panel("##SceneToolbar",area.x,42,area.width,32,ImGuiWindowFlags_NoScrollbar);
    ImGui::TextUnformatted(tr(preview?"game":"scene").c_str());ImGui::SameLine();
    ImGui::TextDisabled("%s",world.is3d?"3D":"2D");ImGui::SameLine();
    if(ImGui::SmallButton(tr("focus").c_str()) && !preview)focus(world,selected);
    ImGui::SameLine();ImGui::Checkbox(tr("grid").c_str(),&grid);ImGui::End();
    // Drag splitters; fractions survive resizing for this editor session.
    auto splitter=[&](const char *id,float x,float y,float w,float h,float &fraction,float scale,ImGuiMouseCursor cursor) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
        panel(id,x,y,w,h,ImGuiWindowFlags_NoScrollbar);ImGui::SetCursorPos({0,0});
        ImGui::InvisibleButton("drag",{std::max(1.f,w),std::max(1.f,h)});
        if(ImGui::IsItemHovered() || ImGui::IsItemActive())ImGui::SetMouseCursor(cursor);
        if(ImGui::IsItemActive())fraction+=(cursor==ImGuiMouseCursor_ResizeEW?io.MouseDelta.x:io.MouseDelta.y)*scale;
        ImGui::End();ImGui::PopStyleVar();
    };
    splitter("##LeftSplit",area.left,42,4,height-66,leftFraction,1/std::max(1.f,width),ImGuiMouseCursor_ResizeEW);
    splitter("##RightSplit",width-area.right-4,42,4,height-66,rightFraction,-1/std::max(1.f,width),ImGuiMouseCursor_ResizeEW);
    splitter("##BottomSplit",0,area.y+area.height,width-area.right-4,4,bottomFraction,-1/std::max(1.f,height-74),ImGuiMouseCursor_ResizeNS);
    leftFraction=std::clamp(leftFraction,.12f,.28f);rightFraction=std::clamp(rightFraction,.20f,.36f);bottomFraction=std::clamp(bottomFraction,.18f,.48f);
    panel("##Hierarchy",0,42,area.left,area.height+32);
    ImGui::TextUnformatted(tr("hierarchy").c_str());
    ImGui::Separator();ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##findentity",tr("search").c_str(),hierarchySearch,sizeof(hierarchySearch));
    ImGui::TextDisabled("%s",runtime.currentScene.c_str());
    ImGuiTextFilter filter(hierarchySearch);
    std::map<std::string,std::vector<std::shared_ptr<Entity>>> children;
    for(auto &e:world.entities)if(e->alive)children[e->parent].push_back(e);
    std::function<void(const std::string &)> tree=[&](const std::string &parent) {
        auto found=children.find(parent);if(found==children.end())return;
        for(auto &e:found->second) {
            if(hierarchySearch[0] && !filter.PassFilter(e->name.c_str()) && !filter.PassFilter(e->id.c_str()))continue;
            ImGui::PushID(e->id.c_str());
            auto nodeFlags=ImGuiTreeNodeFlags_OpenOnArrow|ImGuiTreeNodeFlags_SpanAvailWidth|ImGuiTreeNodeFlags_DefaultOpen;
            if(selected==e->id)nodeFlags|=ImGuiTreeNodeFlags_Selected;
            if(!children.count(e->id) || hierarchySearch[0])nodeFlags|=ImGuiTreeNodeFlags_Leaf;
            bool open=ImGui::TreeNodeEx("##entity",nodeFlags,"%s%s",e->visible?"":tr("hidden").c_str(),e->name.c_str());
            if(ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())selected=e->id;
            if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && !preview)focus(world,e->id);
            if(open){if(!hierarchySearch[0])tree(e->id);ImGui::TreePop();}ImGui::PopID();
        }
    };
    if(hierarchySearch[0]) {
        for(auto &e:world.entities)if(e->alive && (filter.PassFilter(e->name.c_str()) || filter.PassFilter(e->id.c_str())))
            if(ImGui::Selectable((e->name+"##"+e->id).c_str(),selected==e->id))selected=e->id;
    }else tree("");
    if(world.entities.empty())ImGui::TextWrapped("%s",tr("empty_scene").c_str());
    ImGui::End();
    auto draft=world.serialize();bool changed=false;
    auto vector=[&](Json &data,const char *field,const char *key,int count,float speed) {
        std::array<float,4> values{};for(int i=0;i<count;++i)values[i]=data.at(field).at(i).get<float>();
        bool edit=count==3?ImGui::DragFloat3(label(key).c_str(),values.data(),speed):ImGui::ColorEdit4(label(key).c_str(),values.data());
        if(edit){for(int i=0;i<count;++i)data[field][i]=values[i];changed=true;}
    };
    auto check=[&](Json &data,const char *field,const char *key) {
        bool value=data.value(field,false);if(ImGui::Checkbox(label(key).c_str(),&value)){data[field]=value;changed=true;}
    };
    auto field=[&](Json &data,const char *name,const char *key) {
        std::array<char,4096> value{};std::snprintf(value.data(),value.size(),"%s",data.value(name,std::string()).c_str());
        if(ImGui::InputText(label(key).c_str(),value.data(),value.size())){data[name]=value.data();changed=true;}
    };
    panel("##Inspector",width-area.right,42,area.right,height-66);
    ImGui::TextUnformatted(tr(settings?"world":"inspector").c_str());ImGui::Separator();
    ImGui::BeginDisabled(preview);
    if(settings) {
        if(ImGui::CollapsingHeader(tr("scene_settings").c_str(),ImGuiTreeNodeFlags_DefaultOpen)) {
            bool threeD=world.is3d;if(ImGui::Checkbox("3D",&threeD)){draft["mode"]=threeD?"3d":"2d";changed=true;}
            vector(draft,"background","background",4,0);check(draft,"physics_enabled","physics");vector(draft,"gravity","gravity",3,.1f);
        }
        if(ImGui::CollapsingHeader(tr("camera").c_str(),ImGuiTreeNodeFlags_DefaultOpen)) {
            vector(draft["camera"],"position","position",3,.05f);vector(draft["camera"],"target","target",3,.05f);
            float fov=world.fov;if(ImGui::SliderFloat(tr("fov").c_str(),&fov,1,179)){draft["camera"]["fov"]=fov;changed=true;}
        }
        if(ImGui::CollapsingHeader(tr("lighting").c_str(),ImGuiTreeNodeFlags_DefaultOpen)) {
            if(button("add_sun")) {
                if(!draft["rendering"].contains("lights"))draft["rendering"]["lights"]=Json::array();
                draft["rendering"]["lights"].push_back({{"type","directional"},{"direction",{-.5,-1,-.5}},{"color",{1,1,1}},{"intensity",1},{"shadows",true}});changed=true;
            }
            if(draft["rendering"].contains("lights"))for(auto &light:draft["rendering"]["lights"]) {
                ImGui::PushID(&light);float intensity=light.value("intensity",1.f);
                if(ImGui::SliderFloat(tr("intensity").c_str(),&intensity,0,10)){light["intensity"]=intensity;changed=true;}ImGui::PopID();
            }
        }
    }else if(auto entity=world.find(selected)) {
        auto it=std::find_if(draft["entities"].begin(),draft["entities"].end(),[&](const Json &e){return e["id"]==selected;});
        auto &data=*it;
        ImGui::TextDisabled("%s / %s",entity->kind.c_str(),entity->id.c_str());field(data,"name","name");
        if(ImGui::CollapsingHeader(tr("transform").c_str(),ImGuiTreeNodeFlags_DefaultOpen)) {
            auto parent=world.find(entity->parent);
            if(ImGui::BeginCombo(tr("parent").c_str(),parent?parent->name.c_str():tr("root").c_str())) {
                if(ImGui::Selectable(tr("root").c_str(),!parent)){data["parent"]="";changed=true;}
                for(auto &p:world.entities)if(p->alive && p!=entity && p->screen==entity->screen)
                    if(ImGui::Selectable((p->name+"##"+p->id).c_str(),p==parent)){data["parent"]=p->id;changed=true;}
                ImGui::EndCombo();
            }
            vector(data,"position","position",3,world.is3d?.05f:1.f);vector(data,"rotation","rotation",3,.5f);vector(data,"scale","scale",3,world.is3d?.02f:1.f);
        }
        if(ImGui::CollapsingHeader(tr("appearance").c_str(),ImGuiTreeNodeFlags_DefaultOpen)) {
            vector(data,"color","color",4,0);check(data,"visible","visible");check(data,"screen","screen");check(data,"casts_shadow","shadow");
            field(data,"texture","texture");
            if(entity->kind=="mesh")field(data,"model","model");
            auto material=data.value("material_properties",Json::object());bool pbr=material.value("shading","legacy")=="pbr";
            if(ImGui::Checkbox("PBR",&pbr)){material["shading"]=pbr?"pbr":"legacy";changed=true;}
            if(pbr)for(auto prop:{"metallic","roughness"}) {
                float value=material.value(prop,std::string(prop)=="roughness"?1.f:0.f);
                if(ImGui::SliderFloat(tr(prop).c_str(),&value,0,1)){material[prop]=value;changed=true;}
            }
            if(!material.empty())data["material_properties"]=material;
        }
        if(ImGui::CollapsingHeader(tr("collision").c_str())) {
            vector(data,"collider","collider",3,.05f);check(data,"dynamic","dynamic");check(data,"trigger","trigger");
        }
        if(entity->kind=="text" && ImGui::CollapsingHeader(tr("text").c_str(),ImGuiTreeNodeFlags_DefaultOpen)) {
            field(data,"text","content");float size=entity->fontSize;
            if(ImGui::DragFloat(tr("font_size").c_str(),&size,.5f,1,1024)){data["font_size"]=size;changed=true;}
        }
        if(!selected.empty() && ImGui::CollapsingHeader(tr("advanced").c_str())) {
            ImGui::TextWrapped("%s",tr("json_hint").c_str());
            bool read=button("read_selection");
            if(jsonEntity!=selected || read) {
                jsonEntity=selected;auto serialized=data.dump(2);
                if(serialized.size()<entityJson.size())std::snprintf(entityJson.data(),entityJson.size(),"%s",serialized.c_str());
                else {entityJson[0]=0;status=tr("json_large");}
            }
            ImGui::InputTextMultiline("##entityjson",entityJson.data(),entityJson.size(),{-1,200});
            if(button("apply_json"))try {
                auto value=Json::parse(entityJson.data());if(value.at("id")!=selected)throw std::runtime_error("Keep the selected entity ID");
                data=std::move(value);changed=true;
            }catch(const std::exception &e){status=e.what();}
        }
        bool remove=button("delete");ImGui::SameLine();
        if(button("duplicate")) {auto copy=data;copy.erase("id");copy["name"]=entity->name+" "+tr("copy");draft["entities"].push_back(copy);changed=true;}
        if(remove){auto &entities=draft["entities"];entities.erase(std::remove_if(entities.begin(),entities.end(),[&](const Json &e){return e.value("id",std::string())==selected;}),entities.end());changed=true;}
    }else ImGui::TextWrapped("%s",tr("select_hint").c_str());
    ImGui::EndDisabled();ImGui::Separator();
    ImGui::TextUnformatted(tr("save_as").c_str());ImGui::SetNextItemWidth(-1);ImGui::InputText("##scenefile",sceneFile,sizeof(sceneFile));
    if(button("save"))save();ImGui::SameLine();if(button("reload"))run({{"op","load"},{"file",runtime.currentScene}});
    ImGui::End();
    if(changed && !preview) {run({{"op","apply"},{"scene",draft}});if(!selected.empty() && !world.find(selected))selected.clear();}
    if(!settings)if(auto entity=world.find(selected))if(!entity->model.empty() && entity->model.rfind("@",0)!=0) {
        // Animation has its own validated authoring API and history entry.
        auto before=world.serialize();
        panel("##AnimationEntry",width-area.right,height-63,area.right,39,ImGuiWindowFlags_NoScrollbar);
        ImGui::BeginDisabled(preview);bool animated=animationEditor(*entity,runtime,*this);ImGui::EndDisabled();ImGui::End();
        if(animated && !preview)session.record(before,world.serialize());
    }
    panel("##Resources",0,area.y+area.height+4,width-area.right-4,area.bottom);
    projectPanel(world,runtime);ImGui::End();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(10,3));
    panel("##Status",0,height-24,width,24,ImGuiWindowFlags_NoScrollbar);
    ImGui::TextDisabled("%s | %s | %u %s | %.1f MiB GPU",FORGE_VERSION,tr(preview?"playing":"paused").c_str(),diagnostics.value("draw_calls",0u),tr("draws").c_str(),diagnostics.value("gpu_bytes",0.0)/(1024*1024));
    ImGui::SameLine();ImGui::TextUnformatted(status.empty()?tr("navigation").c_str():status.c_str());ImGui::End();ImGui::PopStyleVar();
    auto viewport= this->viewport(width,height,world.is3d);
    sceneOverlay(world,runtime,viewport);
    bool pointerInScene=io.MousePos.x>=viewport.x && io.MousePos.x<viewport.x+viewport.width && io.MousePos.y>=viewport.y && io.MousePos.y<viewport.y+viewport.height;
    bool shortcut=io.KeyCtrl || io.KeySuper;
    if(!io.WantTextInput) {
        if(shortcut && ImGui::IsKeyPressed(ImGuiKey_S,false))save();
        if(!preview && shortcut && ImGui::IsKeyPressed(ImGuiKey_Z,false))run({{"op",io.KeyShift?"redo":"undo"}});
        if(!preview && shortcut && ImGui::IsKeyPressed(ImGuiKey_Y,false))run({{"op","redo"}});
        if(!preview && pointerInScene && !io.WantCaptureKeyboard && ImGui::IsKeyPressed(ImGuiKey_F,false))focus(world,selected);
    }
    auto shiftCamera=[&](glm::dvec3 move) {
        auto position=glm::vec3(glm::dvec3(world.cameraPosition)+move),target=glm::vec3(glm::dvec3(world.cameraTarget)+move);
        auto finite=[](glm::vec3 v){return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);};
        if(finite(position) && finite(target) && (!world.is3d || glm::length(glm::dvec3(target)-glm::dvec3(position))>1e-6)) {
            world.cameraPosition=position;world.cameraTarget=target;
        }
    };
    if(!preview && pointerInScene && !io.WantCaptureKeyboard && !shortcut) {
        auto forward=glm::dvec3(world.cameraTarget)-glm::dvec3(world.cameraPosition);
        if(glm::length(forward)<.001f)forward={0,0,-1};forward=glm::normalize(forward);
        auto right=glm::cross(forward,glm::dvec3(0,1,0));right=glm::length(right)>.001f?glm::normalize(right):glm::dvec3(1,0,0);
        auto move=glm::dvec3(0);if(keys[GLFW_KEY_W])move+=forward;if(keys[GLFW_KEY_S])move-=forward;if(keys[GLFW_KEY_D])move+=right;if(keys[GLFW_KEY_A])move-=right;
        move*=runtime.dt*5;shiftCamera(move);
    }
    if(!preview && pointerInScene && !io.WantCaptureMouse && buttons[GLFW_MOUSE_BUTTON_MIDDLE]) {
        auto move=glm::dvec3(-double(mouseDelta.x),double(mouseDelta.y),0)*(world.is3d?.01:1.0);shiftCamera(move);
    }
    if(saveRequested)try {session.save(runtime,sceneFile);status=tr("saved")+" "+sceneFile;}catch(const std::exception &e){status=e.what();logger.write("WARN",status);}
    ImGui::Render();GraphicsDevice::current().editorDraw();
}
} // namespace forge
