#include <forge/engine.hpp>
#include <condition_variable>
#include <deque>
#include <forge/model.hpp>
#include <limits>
#include <mutex>
#include <stb_image.h>
#include <thread>
namespace forge {
struct Assets::Impl {
    struct Entry {
        fs::path path, root;
        std::string kind, status = "queued", error;
        unsigned pins = 0;
        size_t bytes = 0;
        uint64_t used = 0;
        std::shared_ptr<ImageData> image;
        std::shared_ptr<Model> model;
        std::vector<unsigned char> data;
    };
    Config &config;
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<std::thread> workers;
    std::deque<std::shared_ptr<Entry>> queue;
    std::map<std::string, std::shared_ptr<Entry>> cache;
    std::map<unsigned, std::shared_ptr<Entry>> handles;
    bool stopping = false;
    unsigned nextId = 0;
    size_t limit = 256 * 1024 * 1024, resident = 0;
    uint64_t clock = 0;
    uint64_t retiredModels = 0;
    explicit Impl(Config &c) : config(c) {
        try {
            for (int i = 0; i < 4; ++i)
                workers.emplace_back([this] { work(); });
        } catch (...) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                stopping = true;
            }
            changed.notify_all();
            for (auto &worker : workers)
                worker.join();
            throw;
        }
    }
    ~Impl() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
        }
        changed.notify_all();
        for (auto &worker : workers)
            worker.join();
    }
    bool evict(size_t incoming) {
        if (incoming > limit)
            return false;
        while (resident > limit - incoming) {
            auto candidate = cache.end();
            for (auto it = cache.begin(); it != cache.end(); ++it) {
                auto &e = *it->second;
                if (e.status != "ready" || e.pins || it->second.use_count() > 1 ||
                    (e.image && e.image.use_count() > 1) || (e.model && e.model.use_count() > 1))
                    continue;
                if (candidate == cache.end() || e.used < candidate->second->used)
                    candidate = it;
            }
            if (candidate == cache.end())
                return false;
            resident -= candidate->second->bytes;
            cache.erase(candidate);
        }
        return true;
    }
    void work() {
        for (;;) {
            std::shared_ptr<Entry> entry;
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock, [&] { return stopping || !queue.empty(); });
                if (stopping)
                    return;
                entry = queue.front();
                queue.pop_front();
                entry->status = "loading";
            }
            std::shared_ptr<ImageData> image;
            std::shared_ptr<Model> model;
            std::vector<unsigned char> data;
            size_t bytes = 0;
            std::string error;
            try {
                if (entry->kind == "textures") {
                    int w, h, n;
                    auto name = entry->path.u8string();
                    if (!stbi_info(name.c_str(), &w, &h, &n) || w < 1 || h < 1 || w > 16384 || h > 16384)
                        throw std::runtime_error("Invalid texture dimensions: " + name);
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        if (size_t(w) * h * 4 > limit)
                            throw std::runtime_error("Texture exceeds asset memory budget");
                    }
                    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
                        stbi_load(name.c_str(), &w, &h, &n, 4), stbi_image_free);
                    if (!pixels)
                        throw std::runtime_error("Cannot decode texture: " + name);
                    image = std::make_shared<ImageData>();
                    image->width = w;
                    image->height = h;
                    image->pixels.assign(pixels.get(), pixels.get() + size_t(w) * h * 4);
                    bytes = image->pixels.size();
                } else if (entry->kind == "models") {
                    model = loadModel(entry->path, entry->root);
                    bytes = model->memoryBytes;
                } else {
                    std::ifstream stream(entry->path, std::ios::binary);
                    if (!stream)
                        throw std::runtime_error("Asset missing: " + entry->path.u8string());
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        if (fs::file_size(entry->path) > limit)
                            throw std::runtime_error("File exceeds asset memory budget");
                    }
                    data.assign(std::istreambuf_iterator<char>(stream), {});
                    bytes = data.size();
                }
            } catch (const std::exception &failure) {
                error = failure.what();
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (error.empty() && !evict(bytes))
                    error = "Asset memory budget exceeded: release pinned handles or increase budget";
                if (error.empty()) {
                    entry->image = std::move(image);
                    entry->model = std::move(model);
                    entry->data = std::move(data);
                    entry->bytes = bytes;
                    entry->status = "ready";
                    resident += bytes;
                } else {
                    entry->status = "failed";
                    entry->error = error;
                }
            }
            changed.notify_all();
        }
    }
    std::shared_ptr<Entry> ensure(const fs::path &path, const std::string &kind) {
        auto stamp = fs::last_write_time(path).time_since_epoch().count();
        auto key = path.u8string() + "#" + kind + "#" + std::to_string(static_cast<long long>(stamp)) +
                   "#" + std::to_string(fs::file_size(path));
        std::lock_guard<std::mutex> lock(mutex);
        auto found = cache.find(key);
        if (found != cache.end() && kind == "models") {
            if (found->second->status == "failed") {
                // A repaired sidecar need not change the main model's metadata.
                cache.erase(found);
                found = cache.end();
            } else if (found->second->model && !found->second->model->dependenciesCurrent()) {
                // Retain old pinned generations and their budget accounting.
                auto retired = cache.extract(found);
                retired.key() += "#retired:" + std::to_string(++retiredModels);
                cache.insert(std::move(retired));
                found = cache.end();
            }
        }
        if (found != cache.end()) {
            found->second->used = ++clock;
            return found->second;
        }
        auto entry = std::make_shared<Entry>();
        entry->path = path;
        entry->root = config.root;
        entry->kind = kind;
        entry->used = ++clock;
        cache[key] = entry;
        queue.push_back(entry);
        changed.notify_one();
        return entry;
    }
    std::shared_ptr<Entry> wait(const fs::path &path, const std::string &kind) {
        auto entry = ensure(path, kind);
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait(lock, [&] { return entry->status == "ready" || entry->status == "failed"; });
        if (entry->status == "failed")
            throw std::runtime_error(entry->error);
        return entry;
    }
};
Assets::Assets(Config &c) : impl(std::make_unique<Impl>(c)) {}
Assets::~Assets() = default;
unsigned Assets::request(const std::string &group, const std::string &name) {
    auto path = impl->config.asset(group, name);
    auto entry = impl->ensure(path, group);
    std::lock_guard<std::mutex> lock(impl->mutex);
    auto id = ++impl->nextId;
    ++entry->pins;
    impl->handles[id] = entry;
    return id;
}
Json Assets::info(unsigned id) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    auto it = impl->handles.find(id);
    if (it == impl->handles.end())
        throw std::runtime_error("Unknown or released asset handle");
    auto &e = *it->second;
    Json result = {{"id", id},         {"status", e.status},        {"bytes", e.bytes},
                   {"error", e.error}, {"path", e.path.u8string()}, {"group", e.kind}};
    if (e.image) {
        result["width"] = e.image->width;
        result["height"] = e.image->height;
    }
    if (e.model)
        result["model"] = e.model->info();
    return result;
}
std::shared_ptr<ImageData> Assets::image(const fs::path &path) {
    return impl->wait(path, "textures")->image;
}
std::shared_ptr<Model> Assets::model(const fs::path &path) {
    return impl->wait(path, "models")->model;
}
std::vector<unsigned char> Assets::bytes(unsigned id) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    auto entry = impl->handles.at(id);
    if (entry->status != "ready")
        throw std::runtime_error("Asset not ready");
    if (entry->image)
        return entry->image->pixels;
    return entry->data;
}
void Assets::release(unsigned id) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    auto found = impl->handles.find(id);
    if (found == impl->handles.end())
        return;
    auto entry = found->second;
    --entry->pins;
    impl->handles.erase(found);
    if (entry->status == "failed") {
        for (auto it = impl->cache.begin(); it != impl->cache.end();) {
            if (it->second == entry)
                it = impl->cache.erase(it);
            else
                ++it;
        }
    }
    impl->evict(0);
}
void Assets::budget(size_t value) {
    if (!value)
        throw std::runtime_error("Asset budget must be positive");
    std::lock_guard<std::mutex> lock(impl->mutex);
    auto old = impl->limit;
    impl->limit = value;
    if (!impl->evict(0)) {
        impl->limit = old;
        throw std::runtime_error("Pinned assets exceed requested budget");
    }
}
Json Assets::stats() {
    std::lock_guard<std::mutex> lock(impl->mutex);
    unsigned loading = 0;
    for (auto &[key, e] : impl->cache)
        if (e->status == "loading" || e->status == "queued")
            ++loading;
    return {
        {"resident_bytes", impl->resident}, {"budget_bytes", impl->limit}, {"handles", impl->handles.size()},
        {"entries", impl->cache.size()},    {"loading", loading},          {"workers", 4}};
}
unsigned Assets::checkpoint() {
    std::lock_guard<std::mutex> lock(impl->mutex);
    return impl->nextId;
}
void Assets::rollback(unsigned checkpoint) {
    std::lock_guard<std::mutex> lock(impl->mutex);
    for (auto it = impl->handles.begin(); it != impl->handles.end();) {
        if (it->first > checkpoint) {
            --it->second->pins;
            it = impl->handles.erase(it);
        } else
            ++it;
    }
    impl->evict(0);
}
} // namespace forge
