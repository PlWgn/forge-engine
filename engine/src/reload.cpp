// Python paths and hot reload extracted from runtime.cpp (licensed core origin).
#include <forge/engine.hpp>
#include <pybind11/stl.h>
#include <algorithm>
#include <forge/file_watch.hpp>
namespace forge {
std::map<fs::path,fs::file_time_type> Runtime::snapshot() const {
    std::map<fs::path,fs::file_time_type> result;result[config.file]=fs::last_write_time(config.file);
    for(auto& [group,path]:config.paths)if(fs::exists(path))for(auto& entry:fs::recursive_directory_iterator(path))if(entry.is_regular_file() && entry.path().extension()!=".pyc" && entry.path().u8string().find("__pycache__")==std::string::npos)result[entry.path()]=entry.last_write_time();
    return result;
}
std::unique_ptr<FileWatch> Runtime::prepareWatcher(){
    if(!dev)return {};
    std::vector<fs::path> roots;
    for(auto& [group,path]:config.paths)roots.push_back(path);
    for(auto& path:config.data.value("python_paths",Json::array()))roots.push_back(config.resolve(path.get<std::string>()));
    std::sort(roots.begin(),roots.end());roots.erase(std::unique(roots.begin(),roots.end()),roots.end());
    auto options=config.data.value("development",Json::object());
    auto interval=std::chrono::milliseconds(int(options.value("watch_interval",.3)*1000));
    if(watcher && watcher->matches(config.file,roots,interval))return {};
    return std::make_unique<FileWatch>(config.file,std::move(roots),interval);
}
void Runtime::restartWatcher(){auto candidate=prepareWatcher();if(candidate)watcher=std::move(candidate);}
bool Runtime::changed(){
    if(!watcher)return false;
    auto result=watcher->poll();
    profile["watcher"]={{"scans",result.scans},{"files",result.files},{"scan_ms",result.scanMs},{"error",result.error}};
    if(!result.error.empty())logger.write("WARN","Development watcher: "+result.error);
    // Accumulate until a reload commits: a rejected candidate restores sys.modules,
    // so files changed before that failure must still be invalidated on the next attempt.
    if(result.changed){
        if(result.overflow || changedFilesOverflow || changedFiles.size()+result.filesChanged.size()>8192){changedFiles.clear();changedFilesOverflow=true;}
        else changedFiles.merge(result.filesChanged);
    }
    return result.changed;
}
bool Runtime::tryReload(){
    try {
        reload();
        logger.write("INFO","Hot reload complete");
        return true;
    }catch(const std::exception& error){
        logger.error(error.what());
        logger.write("WARN","Development paused. Edit a watched file to retry.");
        auto notify=[&](py::object instance){
            if(!py::hasattr(instance,"on_reload_failed"))return;
            try {instance.attr("on_reload_failed")(std::string(error.what()));}
            catch(const std::exception& failure){logger.error(failure.what());}
        };
        auto previousScripts=scripts;
        for(auto& script:previousScripts)notify(script.instance);
        auto previousStartup=startup;
        for(auto& module:previousStartup)notify(module);
        return false;
    }
}
static std::vector<std::string> searchPaths(const Config& config) {
    std::vector<std::string> paths;
    for(auto group:{"modules","scripts","scenes"})paths.push_back(config.paths.at(group).u8string());
    for(auto& path:config.data.value("python_paths",Json::array()))paths.push_back(config.resolve(path.get<std::string>()).u8string());
    std::vector<std::string> result;
    for(auto it=paths.rbegin();it!=paths.rend();++it)if(std::find(result.begin(),result.end(),*it)==result.end())result.push_back(*it);
    return result;
}
void Runtime::refreshPythonPaths() {
    auto next=searchPaths(config);auto sys=py::module_::import("sys");py::list replacement;
    for(auto& path:next)replacement.append(path);
    for(auto item:sys.attr("path")) {
        if(py::isinstance<py::str>(item)){auto path=item.cast<std::string>();
            if(std::find(pythonPaths.begin(),pythonPaths.end(),path)!=pythonPaths.end() || std::find(next.begin(),next.end(),path)!=next.end())continue;
        }
        replacement.append(item);
    }
    sys.attr("path")=replacement;pythonPaths=std::move(next);
}
void Runtime::reload(){
    reloading=true;auto previousPersistence=persistence;auto previousBudget=assets.stats()["budget_bytes"].get<size_t>();
    auto previousLocalization=localization;auto previousConfig=config;auto previousPythonPaths=pythonPaths;auto sys=py::module_::import("sys");auto modules=sys.attr("modules").cast<py::dict>();
    auto previousModules=modules.attr("copy")().cast<py::dict>();auto previousPath=sys.attr("path").attr("copy")();
    try{
        if(editing && !editorSession.runtimeBase.is_null() && world.serialize()!=editorSession.runtimeBase)
            throw std::runtime_error("Editor has unsaved changes; save, undo or explicitly reload the scene before automatic hot reload. The current world was preserved.");
        auto updated=Config::load(config.file);if(!entryOverride.empty())updated.data["entry_scene"]=entryOverride;updated.validate(false);if(updated.data.value("steam",Json::object())!=config.data.value("steam",Json::object()))throw std::runtime_error("Steam settings changes require restarting the runtime");auto nextPaths=searchPaths(updated);
        std::vector<fs::path> invalidatedRoots;
        for(auto group:{"modules","scripts","scenes"})invalidatedRoots.push_back(config.paths.at(group));
        // Preserve unchanged extension packages; remove retired paths and changed files.
        for(auto& path:pythonPaths)if(changedFilesOverflow || std::find(nextPaths.begin(),nextPaths.end(),path)==nextPaths.end())invalidatedRoots.push_back(fs::u8path(path));
        auto obsolete=[&](const std::string& input){auto file=fs::weakly_canonical(fs::u8path(input));if(changedFiles.count(file))return true;for(auto& root:invalidatedRoots){auto relative=file.lexically_relative(root);if(!relative.empty() && *relative.begin()!="..")return true;}return false;};
        py::module_::import("importlib").attr("invalidate_caches")();py::list remove;
        for(auto item:modules){bool discard=false;
            if(py::hasattr(item.second,"__file__") && !item.second.attr("__file__").is_none())discard=obsolete(item.second.attr("__file__").cast<std::string>());
            if(!discard && py::hasattr(item.second,"__path__"))for(auto path:item.second.attr("__path__"))if(obsolete(py::cast<std::string>(path))){discard=true;break;}
            if(discard)remove.append(item.first);
        }
        for(auto item:remove)modules.attr("pop")(item,py::none());
        auto previousEntry=config.entry();config=std::move(updated);world.config=&config;assets.budget(config.data.value("asset_budget_bytes",size_t(256*1024*1024)));
        auto nextWatcher=prepareWatcher(); // Baseline before reading scene scripts, never after commit.
        refreshPythonPaths();
        localization.load(config,previousLocalization.language);
        if(renderer)renderer->stage();
        loadScene(previousEntry==config.entry()?currentScene:config.entry(),&previousLocalization);
        if(nextWatcher)watcher=std::move(nextWatcher);
        if(renderer)renderer->commit();if(editing)gamePaused=renderer?!renderer->previewing():true;
    }catch(...){assets.budget(previousBudget);reloading=false;persistence=std::move(previousPersistence);if(renderer)renderer->discard();localization=std::move(previousLocalization);config=std::move(previousConfig);pythonPaths=std::move(previousPythonPaths);world.config=&config;sys.attr("path")=previousPath;modules.attr("clear")();modules.attr("update")(previousModules);throw;}
    reloading=false;changedFiles.clear();changedFilesOverflow=false;auto tasks=std::move(persistence);persistence.clear();for(auto& task:tasks)try{task();}catch(const std::exception& e){logger.error(e.what());}
}
}
