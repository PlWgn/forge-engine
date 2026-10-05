#pragma once
#include <forge/config.hpp>
#include <stdexcept>
#include <functional>
namespace forge {
// Editor-neutral document operations. No window, Python or shell dependency.
struct DocumentConflict : std::runtime_error {
    Json paths;
    explicit DocumentConflict(Json paths);
};
Json mergeDocuments(const Json &base, const Json &local, const Json &disk);
Json documentSnapshot(const fs::path &);
Json commitDocument(const fs::path &, const Json &base, const Json &local,
                    const std::function<void(const Json &)> &validate = {});
Json projectResponse(const fs::path &settings,const Json &request);
Json projectRequest(const fs::path &settings, const Json &request);
int projectProtocol(const fs::path &settings, const fs::path &requestFile, bool serve);
}
