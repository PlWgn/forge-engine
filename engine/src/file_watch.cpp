#include <forge/file_watch.hpp>
#include <condition_variable>
#include <mutex>
#include <thread>
namespace forge {
FileWatch::Snapshot FileWatch::scan(const fs::path& config,const std::vector<fs::path>& roots){
    Snapshot result;
    auto record=[&](const fs::path& path){result[path]={fs::last_write_time(path),fs::file_size(path)};};
    record(config);
    for(const auto& root:roots){
        if(!fs::exists(root))continue;
        for(auto entry=fs::recursive_directory_iterator(root);entry!=fs::recursive_directory_iterator();++entry){
            if(entry->is_directory() && (entry->path().filename()=="__pycache__" || entry->path().filename()==".git")){
                entry.disable_recursion_pending();continue;
            }
            if(entry->is_regular_file() && entry->path().extension()!=".pyc")record(entry->path());
        }
    }
    return result;
}
struct FileWatch::Impl {
    fs::path config;
    std::vector<fs::path> roots;
    std::chrono::milliseconds interval;
    Snapshot previous;
    std::mutex mutex;
    std::condition_variable wake;
    std::thread worker;
    bool stop=false;
    Poll pending;
    Impl(fs::path file,std::vector<fs::path> paths,std::chrono::milliseconds delay):config(std::move(file)),roots(std::move(paths)),interval(delay){
        previous=scan(config,roots);
        worker=std::thread([this]{
            std::unique_lock<std::mutex> lock(mutex);
            while(!wake.wait_for(lock,interval,[this]{return stop;})){
                lock.unlock();
                auto started=std::chrono::steady_clock::now();
                Snapshot next;std::string error;
                try{next=scan(config,roots);}catch(const std::exception& e){error=e.what();}
                double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
                lock.lock();
                ++pending.scans;pending.scanMs=ms;
                if(error.empty()){
                    bool changed=next!=previous;pending.changed=pending.changed || changed;
                    if(changed && !pending.overflow){
                        auto add=[&](const fs::path& path){if(pending.overflow)return;if(pending.filesChanged.size()<8192)pending.filesChanged.insert(path);else {pending.overflow=true;pending.filesChanged.clear();}};
                        for(auto& [file,stamp]:next){auto old=previous.find(file);if(old==previous.end() || !(old->second==stamp)){add(file);std::error_code error;auto canonical=fs::weakly_canonical(file,error);if(!error)add(canonical);}}
                        for(auto& [file,stamp]:previous)if(!next.count(file))add(file);
                    }
                    previous=std::move(next);pending.files=previous.size();pending.error.clear();
                }else pending.error=std::move(error);
            }
        });
    }
    ~Impl(){
        {std::lock_guard<std::mutex> lock(mutex);stop=true;}
        wake.notify_all();if(worker.joinable())worker.join();
    }
};
FileWatch::FileWatch(fs::path config,std::vector<fs::path> roots,std::chrono::milliseconds interval):impl(std::make_unique<Impl>(std::move(config),std::move(roots),interval)){}
FileWatch::~FileWatch()=default;
bool FileWatch::matches(const fs::path& file,const std::vector<fs::path>& paths,std::chrono::milliseconds delay)const{
    return impl->config==file && impl->roots==paths && impl->interval==delay;
}
FileWatch::Poll FileWatch::poll(){
    std::lock_guard<std::mutex> lock(impl->mutex);
    auto result=std::move(impl->pending);
    impl->pending=Poll{};impl->pending.scans=result.scans;impl->pending.files=result.files;impl->pending.scanMs=result.scanMs;
    return result;
}
}
