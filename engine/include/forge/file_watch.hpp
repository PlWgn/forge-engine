#pragma once
#include <forge/types.hpp>
#include <chrono>
namespace forge {
// Filesystem-only worker. No Python, OpenGL, Config references or user callbacks.
class FileWatch {
public:
    struct Stamp {fs::file_time_type time;uintmax_t size;bool operator==(const Stamp& other)const{return time==other.time && size==other.size;}};
    using Snapshot=std::map<fs::path,Stamp>;
    struct Poll {bool changed=false;std::string error;size_t scans=0,files=0;double scanMs=0;std::set<fs::path> filesChanged;bool overflow=false;};
    FileWatch(fs::path config,std::vector<fs::path> roots,std::chrono::milliseconds interval);
    ~FileWatch();
    Poll poll();
    bool matches(const fs::path&,const std::vector<fs::path>&,std::chrono::milliseconds) const;
    static Snapshot scan(const fs::path&,const std::vector<fs::path>&);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
