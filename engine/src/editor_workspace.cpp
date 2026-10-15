#include <forge/editor.hpp>
#include <forge/engine.hpp>
#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <fstream>
#include <functional>
#include <sstream>
namespace forge {
void Editor::refreshAssets(Runtime &runtime) {
    assetEntries.clear();assetError.clear();assetsReady=true;
    // An explicit refresh keeps filesystem walks out of the per-frame path.
    try {
        for(const auto &[group,root]:runtime.config.paths) {
            if(!fs::is_directory(root))continue;
            for(const auto &item:fs::recursive_directory_iterator(root,fs::directory_options::skip_permission_denied)) {
                if(assetEntries.size()>=10000){assetError=runtime.localization.translate("engine.editor.asset_limit");break;}
                if(!item.is_regular_file() || item.path().filename().u8string().front()=='.')continue;
                auto name=item.path().lexically_relative(root).generic_u8string();
                if(name.find("__pycache__/")!=std::string::npos || item.path().extension()==".pyc")continue;
                // Use the same confinement check as loading, including symlink targets.
                try {runtime.config.asset(group,name);}catch(const std::exception &){continue;}
                assetEntries.push_back({group,name});
            }
        }
        std::sort(assetEntries.begin(),assetEntries.end(),[](const Asset &a,const Asset &b){return std::tie(a.group,a.name)<std::tie(b.group,b.name);});
    }catch(const std::exception &e){assetError=e.what();}
}
void Editor::projectPanel(World &world,Runtime &runtime) {
    auto tr=[&](const char *key){return runtime.localization.translate(std::string("engine.editor.")+key);};
    if(!ImGui::BeginTabBar("Workspace resources"))return;
    if(ImGui::BeginTabItem(tr("project").c_str())) {
        std::string paths;for(const auto &[group,path]:runtime.config.paths)paths+=group+":"+path.generic_u8string()+"\n";
        if(!assetsReady || assetRoot!=paths) {
            assetRoot=paths;refreshAssets(runtime);
        }
        if(ImGui::Button(tr("refresh").c_str()))refreshAssets(runtime);
        ImGui::SameLine();ImGui::SetNextItemWidth(std::max(80.f,ImGui::GetContentRegionAvail().x));
        ImGui::InputTextWithHint("##assets",tr("search_assets").c_str(),assetSearch,sizeof(assetSearch));
        ImGui::BeginChild("Folders",ImVec2(std::min(170.f,ImGui::GetContentRegionAvail().x*.25f),0),true);
        for(const auto &[group,path]:runtime.config.paths)
            if(ImGui::Selectable(group.c_str(),assetGroup==group))assetGroup=group;
        ImGui::EndChild();ImGui::SameLine();
        ImGui::BeginChild("Files",ImVec2(0,0),true);
        if(!assetError.empty())ImGui::TextWrapped("%s",assetError.c_str());
        ImGuiTextFilter filter(assetSearch);
        if(ImGui::BeginTable("Assets",2,ImGuiTableFlags_RowBg|ImGuiTableFlags_Resizable|ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn(tr("file").c_str(),ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn(tr("type").c_str(),ImGuiTableColumnFlags_WidthFixed,85);
            ImGui::TableHeadersRow();
            for(const auto &asset:assetEntries)if(asset.group==assetGroup && filter.PassFilter(asset.name.c_str())) {
                ImGui::TableNextRow();ImGui::TableNextColumn();
                if(ImGui::Selectable(asset.name.c_str(),assetSelection==asset.group+":"+asset.name,ImGuiSelectableFlags_SpanAllColumns|ImGuiSelectableFlags_AllowDoubleClick)) {
                    assetSelection=asset.group+":"+asset.name;
                    if(ImGui::IsMouseDoubleClicked(0))try {
                        if(asset.group=="scenes") {
                            auto ext=fs::u8path(asset.name).extension();
                            if(ext==".json" || ext==".py")runtime.editorSession.command(runtime,{{"op","load"},{"file",asset.name}});
                        }else if(!preview && (asset.group=="textures" || asset.group=="models")) {
                            auto scene=world.serialize();bool found=false;
                            for(auto &e:scene["entities"])if(e["id"]==runtime.editorSession.selected) {
                                if(asset.group=="textures")e["texture"]=asset.name;
                                else {e["kind"]="mesh";e["model"]=asset.name;e.erase("animator");e.erase("animation");e.erase("morph_weights");}
                                found=true;break;
                            }
                            if(!found && asset.group=="models")scene["entities"].push_back({{"kind","mesh"},{"model",asset.name},{"name",fs::u8path(asset.name).stem().u8string()}});
                            if(found || asset.group=="models")runtime.editorSession.command(runtime,{{"op","apply"},{"scene",scene}});
                        }
                    }catch(const std::exception &e){status=e.what();}
                }
                if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",tr("asset_hint").c_str());
                ImGui::TableNextColumn();ImGui::TextUnformatted(fs::u8path(asset.name).extension().u8string().c_str());
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();ImGui::EndTabItem();
    }
    if(ImGui::BeginTabItem(tr("console").c_str())) {
        ImGui::Checkbox(tr("errors").c_str(),&consoleErrors);ImGui::SameLine();
        ImGui::Checkbox(tr("follow").c_str(),&consoleFollow);ImGui::SameLine();
        ImGui::SetNextItemWidth(std::max(80.f,ImGui::GetContentRegionAvail().x));
        ImGui::InputTextWithHint("##logfilter",tr("search").c_str(),consoleSearch,sizeof(consoleSearch));
        if(ImGui::GetTime()>=consoleRefresh) {
            consoleRefresh=ImGui::GetTime()+.5;
            std::ifstream file(logger.path,std::ios::binary|std::ios::ate);
            if(file) {
                auto size=std::streamoff(file.tellg()),start=std::max(std::streamoff(0),size-65536);
                file.seekg(start);std::string tail(size_t(size-start),'\0');file.read(tail.data(),std::streamsize(tail.size()));tail.resize(size_t(file.gcount()));
                if(start>0){auto newline=tail.find('\n');tail.erase(0,newline==std::string::npos?tail.size():newline+1);}
                consoleText=std::move(tail);
            }
        }
        ImGui::BeginChild("Log messages",ImVec2(0,0),true,ImGuiWindowFlags_HorizontalScrollbar);
        ImGuiTextFilter filter(consoleSearch);std::istringstream stream(consoleText);
        for(std::string line;std::getline(stream,line);) {
            bool error=line.find("[ERROR]")!=std::string::npos,warning=line.find("[WARN]")!=std::string::npos;
            if((consoleErrors && !error && !warning) || !filter.PassFilter(line.c_str()))continue;
            ImGui::PushStyleColor(ImGuiCol_Text,error?ImVec4(1,.4f,.4f,1):warning?ImVec4(1,.75f,.35f,1):ImVec4(.72f,.76f,.82f,1));
            ImGui::TextUnformatted(line.c_str());ImGui::PopStyleColor();
        }
        if(consoleFollow)ImGui::SetScrollHereY(1);
        ImGui::EndChild();ImGui::EndTabItem();
    }
    if(ImGui::BeginTabItem(tr("commands").c_str())) {
        if(runtime.editorClient) {
            for(auto item:runtime.editorClient.attr("commands")) {
                auto name=py::cast<std::string>(item);
                if(ImGui::Selectable(name.c_str()))std::snprintf(extensionName,sizeof(extensionName),"%s",name.c_str());
            }
            ImGui::InputText(tr("command").c_str(),extensionName,sizeof(extensionName));
            ImGui::InputText(tr("arguments").c_str(),extensionArguments,sizeof(extensionArguments));
            if(ImGui::Button(tr("execute").c_str()))try {
                status=runtime.editorSession.command(runtime,{{"op","command"},{"name",extensionName},{"arguments",Json::parse(extensionArguments)}}).dump();
            }catch(const std::exception &e){status=e.what();}
        }
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}
void Editor::focus(World &world,const std::string &id) {
    if(auto entity=world.find(id)) {
        auto position=world.worldPosition(*entity);
        if(!world.is3d || entity->screen) {
            world.cameraPosition.x=position.x-world.width*.5f;world.cameraPosition.y=position.y-world.height*.5f;
        }else {
            auto direction=glm::dvec3(world.cameraPosition)-glm::dvec3(world.cameraTarget);
            if(glm::length(direction)<.001)direction={1,1,1};
            double distance=std::clamp(glm::length(glm::dvec3(entity->scale))*3.0,2.0,1000.0);
            auto camera=glm::dvec3(position)+glm::normalize(direction)*distance;
            glm::vec3 next(camera);
            if(std::isfinite(next.x) && std::isfinite(next.y) && std::isfinite(next.z) && glm::length(glm::dvec3(next)-glm::dvec3(position))>1e-6) {
                world.cameraTarget=position;world.cameraPosition=next;
            }
        }
    }
}
void Editor::sceneOverlay(World &world,Runtime &runtime,const Layout &area) {
    auto draw=ImGui::GetBackgroundDrawList();
    draw->PushClipRect(ImVec2(area.x,area.y),ImVec2(area.x+area.width,area.y+area.height),true);
    auto direction=glm::dvec3(world.cameraTarget)-glm::dvec3(world.cameraPosition);
    if(glm::length(direction)<.0001)direction={0,0,-1};
    auto up=glm::length(glm::cross(glm::normalize(direction),glm::dvec3(0,1,0)))<1e-6?glm::dvec3(0,0,1):glm::dvec3(0,1,0);
    auto ortho=glm::ortho(0.0,double(world.width),double(world.height),0.0,-10000.0,10000.0);
    auto view=world.is3d?glm::perspective(glm::radians(double(world.fov)),double(area.width)/area.height,.05,10000.0)*glm::lookAt(glm::dvec3(world.cameraPosition),glm::dvec3(world.cameraTarget),up):
        ortho*glm::translate(glm::dmat4(1),glm::dvec3(-double(world.cameraPosition.x),-double(world.cameraPosition.y),0));
    auto project=[&](glm::vec3 point,ImVec2 &out,bool screen=false) {
        auto p=(screen?ortho:view)*glm::dvec4(point,1);
        if(!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.w) || p.w<=.001 || p.z < -p.w || p.z>p.w)return false;
        double x=area.x+(p.x/p.w+1)*area.width*.5,y=area.y+(1-p.y/p.w)*area.height*.5;
        // Draw-list clipping cannot safely accept unbounded coordinates.
        if(std::abs(x)>1e6 || std::abs(y)>1e6)return false;
        out=ImVec2(float(x),float(y));return true;
    };
    if(grid && !preview) {
        auto line=[&](glm::vec3 a,glm::vec3 b,ImU32 color){ImVec2 p,q;if(project(a,p)&&project(b,q))draw->AddLine(p,q,color);};
        for(int i=-20;i<=20;++i) {
            if(world.is3d) {
                line({float(i),0,-20},{float(i),0,20},i==0?IM_COL32(75,115,185,150):IM_COL32(155,170,190,35));
                line({-20,0,float(i)},{20,0,float(i)},i==0?IM_COL32(190,85,85,150):IM_COL32(155,170,190,35));
            }else {
                float step=100;
                line({i*step,-2000,0},{i*step,2000,0},IM_COL32(155,170,190,40));
                line({-2000,i*step,0},{2000,i*step,0},IM_COL32(155,170,190,40));
            }
        }
    }
    if(!preview)if(auto entity=world.find(runtime.editorSession.selected)) {
        ImVec2 point;if(project(world.worldPosition(*entity),point,entity->screen)) {
            draw->AddCircle(point,12,IM_COL32(100,190,255,255),24,2);
            draw->AddText(ImVec2(point.x+17,point.y-8),IM_COL32(210,230,255,255),entity->name.c_str());
        }
    }
    draw->PopClipRect();
    // Center picking is deliberately bounded in screen pixels; this is not mesh raycasting.
    auto &io=ImGui::GetIO();
    if(!preview && !io.WantCaptureMouse && ImGui::IsMouseClicked(0) && io.MousePos.x>=area.x && io.MousePos.x<area.x+area.width && io.MousePos.y>=area.y && io.MousePos.y<area.y+area.height) {
        float best=24*24;std::string id;
        for(auto &entity:world.entities)if(entity->alive && entity->visible) {
            ImVec2 p;if(!project(world.worldPosition(*entity),p,entity->screen))continue;
            float distance=(p.x-io.MousePos.x)*(p.x-io.MousePos.x)+(p.y-io.MousePos.y)*(p.y-io.MousePos.y);
            if(distance<best){best=distance;id=entity->id;}
        }
        runtime.editorSession.selected=id;
    }
}
}
