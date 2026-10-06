#include <forge/engine.hpp>
#include <iostream>
#include <forge/documents.hpp>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif
namespace forge {
static fs::path executable([[maybe_unused]] const char* argv0) {
#ifdef _WIN32
    std::wstring buffer(32768,L'\0');auto size=GetModuleFileNameW(nullptr,buffer.data(),static_cast<DWORD>(buffer.size()));buffer.resize(size);return fs::path(buffer);
#elif defined(__APPLE__)
    uint32_t size=0;_NSGetExecutablePath(nullptr,&size);std::string buffer(size,'\0');_NSGetExecutablePath(buffer.data(),&size);return fs::weakly_canonical(buffer.c_str());
#else
    return fs::weakly_canonical(argv0);
#endif
}
static void syntax(const Config& c) {
    auto compile=py::module_::import("builtins").attr("compile");std::set<fs::path> seen;
    std::set<fs::path> roots;for(const auto &[group,folder]:c.paths)roots.insert(folder);
    for(const auto &path:c.data.value("python_paths",Json::array()))roots.insert(c.resolve(path.get<std::string>()));
    for(const auto &folder:roots)for(auto& item:fs::recursive_directory_iterator(folder))if(item.is_regular_file() && item.path().extension()==".py"){
        auto path=c.resolve(item.path().lexically_relative(c.root).generic_u8string());
        if(!seen.insert(path).second)continue;
        std::ifstream input(path,std::ios::binary);if(!input)throw std::runtime_error("Cannot read Python source: "+path.u8string());
        std::string code{std::istreambuf_iterator<char>(input),{}};compile(py::bytes(code),path.u8string(),"exec");
    }
}
}
int main(int argc,char** argv) {
    using namespace forge;
#ifdef _WIN32
    // The narrow CRT argv uses the active ANSI code page. Read Unicode arguments directly.
    SetConsoleOutputCP(CP_UTF8);SetConsoleCP(CP_UTF8);
    int wideCount=0;auto wideArgs=CommandLineToArgvW(GetCommandLineW(),&wideCount);
    if(!wideArgs)return 1;
    std::vector<std::string> utf8Args;std::vector<char*> pointers;
    for(int i=0;i<wideCount;++i){int size=WideCharToMultiByte(CP_UTF8,0,wideArgs[i],-1,nullptr,0,nullptr,nullptr);std::string arg(size,'\0');WideCharToMultiByte(CP_UTF8,0,wideArgs[i],-1,arg.data(),size,nullptr,nullptr);arg.resize(size-1);utf8Args.push_back(std::move(arg));}
    LocalFree(wideArgs);for(auto& arg:utf8Args)pointers.push_back(arg.data());argc=wideCount;argv=pointers.data();
#endif
    fs::path exe=executable(argv[0]);fs::path defaultSettings=fs::current_path()/"engine.json";
    fs::path dataRoot=exe.parent_path();if(!fs::is_regular_file(dataRoot/"game.json") && fs::is_regular_file(dataRoot.parent_path()/"Resources/game.json"))dataRoot=dataRoot.parent_path()/"Resources";
    bool packaged=fs::is_regular_file(dataRoot/"game.json");if(packaged)defaultSettings=dataRoot/"game.json";
    std::string command=packaged?"run":"help";int index=1;
    if(argc>1 && argv[1][0]!='-'){command=argv[1];index=2;}
    std::string sceneOverride;fs::path settings=defaultSettings,out,requestFile;bool serve=false;std::string shell="builtin";std::vector<std::string> extensions;bool headless=false,noOpen=false,silentAudio=false;int frames=-1;
    try {
        for(int i=index;i<argc;++i){std::string arg=argv[i];auto value=[&](){if(++i>=argc)throw std::runtime_error("Missing value after "+arg);return std::string(argv[i]);};
            if(arg=="--extension")extensions.push_back(value());else if(arg=="--request")requestFile=fs::u8path(value());else if(arg=="--serve")serve=true;else if(arg=="--shell")shell=value();else if(arg=="--scene")sceneOverride=value();else if(arg=="--project")settings=fs::absolute(fs::u8path(value()));else if(arg=="--output")out=fs::absolute(fs::u8path(value()));else if(arg=="--frames"){
                auto text=value();size_t consumed=0;
                auto error="--frames must be a positive integer in 1.."+std::to_string(std::numeric_limits<int>::max());
                try {frames=std::stoi(text,&consumed);}
                catch(const std::invalid_argument&){throw std::runtime_error(error);}
                catch(const std::out_of_range&){throw std::runtime_error(error);}
                if(consumed!=text.size() || frames<1)throw std::runtime_error(error);
            }else if(arg=="--help")command="help";else if(arg=="--headless")headless=true;else if(arg=="--silent-audio")silentAudio=true;else if(arg=="--no-open-log")noOpen=true;else throw std::runtime_error("Unknown argument: "+arg);
        }
        if(command=="help" || command=="--help") {std::cout<<"Forge " FORGE_VERSION " | C++ / Python modular game engine\n\nCommands: validate, dev, run, edit, build, project\nProject API: forge project --serve (JSON lines) or --request request.json\nEditor: edit --shell builtin|none|project-shell.py; external shells use the project API\nOptions: --project engine.json --headless --silent-audio --frames N --no-open-log\nBuild: forge build --output dist/MyGame\nScaffold/build engine: python tools/forge.py --help\n";return 0;}
        if(command!="validate" && command!="dev" && command!="run" && command!="build" && command!="edit" && command!="project")throw std::runtime_error("Unknown command: "+command);
        fs::path logRoot=fs::absolute(settings).parent_path();if(!fs::is_directory(logRoot))logRoot=fs::current_path();
        try{Config boot;boot.root=logRoot;boot.data=readJson(settings);if(boot.data.value("storage",Json::object()).value("mode","project")=="user")logRoot=userPath(boot,"logs");}catch(...){}
        logger.protocol=command=="project";
        logger.start(logRoot,false);
        if(command=="project")return projectProtocol(settings,requestFile,serve);
        if(serve || !requestFile.empty())throw std::runtime_error("--serve/--request require the project command");
        if(command=="edit" && shell=="builtin" && !FORGE_WITH_EDITOR && !headless)throw std::runtime_error("Builtin editor was not compiled; use --shell none or an external shell");
        auto config=Config::load(settings);logger.autoOpen=!noOpen && (command=="run" || command=="dev" || command=="edit") && config.data.value("logging",Json::object()).value("open_on_error",true);
        if(!sceneOverride.empty())config.data["entry_scene"]=sceneOverride;config.validate(command=="validate" || command=="build");
        // Packaged games use their private CPython distribution, without PYTHONPATH contamination.
        PyPreConfig pre;PyPreConfig_InitIsolatedConfig(&pre);pre.utf8_mode=1;
        auto preStatus=Py_PreInitialize(&pre);if(PyStatus_Exception(preStatus))throw std::runtime_error("Cannot initialize UTF-8 Python runtime");
        PyConfig pythonConfig;PyConfig_InitIsolatedConfig(&pythonConfig);pythonConfig.write_bytecode=0;pythonConfig.install_signal_handlers=0;
        auto home=dataRoot/"runtime";
        if(packaged){auto status=PyConfig_SetBytesString(&pythonConfig,&pythonConfig.home,home.u8string().c_str());if(PyStatus_Exception(status)){PyConfig_Clear(&pythonConfig);throw std::runtime_error("Cannot configure packaged Python");}}
        py::scoped_interpreter interpreter(&pythonConfig,0,nullptr,false);
        int exitCode=1;
        auto execute=[&]() -> int {
        syntax(config);
        if(command=="validate"){logger.write("INFO","Project assets and Python syntax validated");return 0;}
        if(command=="build"){
            if(packaged)throw std::runtime_error("Build must be invoked from the engine development installation");
            fs::path tools=fs::path(FORGE_SOURCE_DIR)/"tools";
            py::dict globals;globals["__file__"]=(tools/"packager.py").u8string();
            py::eval_file((tools/"packager.py").u8string(),globals);
            if(out.empty())out=config.root/"dist"/"game";
            globals["build_bundle"](config.file.u8string(),exe.u8string(),out.u8string());
            logger.write("INFO","Build complete: "+out.u8string());return 0;
        }
        if(command=="edit" && packaged)throw std::runtime_error("Editor requires the engine development installation");
        installCrashHandler(logger.path.parent_path()/"crash-reports");
        Runtime runtime(std::move(config),command=="dev" || command=="edit",headless);runtime.audio.silent=headless||silentAudio;runtime.entryOverride=sceneOverride;runtime.editing=command=="edit";runtime.executable=exe;runtime.extensionFiles=extensions;runtime.baseShell=shell=="builtin";if(runtime.editing && shell!="builtin" && shell!="none")runtime.shellFile=shell;return runtime.run(frames);
        };
        // Format and destroy Python exceptions while CPython is still alive.
        try {exitCode=execute();}catch(const std::exception& e){logger.error(e.what());}
        return exitCode;
    }catch(const std::exception& e){logger.error(e.what());return 1;}
}
