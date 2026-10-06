#pragma once
#include <forge/network.hpp>
namespace forge {
void validateSteamSettings(const NetworkJson &);
bool steamCompiled();
bool steamAvailable();
// SDK implementation is optional. Disabled projects never initialize Steam.
class SteamClient {
  public:
    explicit SteamClient(const NetworkJson &);
    ~SteamClient();
    SteamClient(const SteamClient &) = delete;
    SteamClient &operator=(const SteamClient &) = delete;
    void pump();
    NetworkJson poll(unsigned limit);
    NetworkJson call(const std::string &, const NetworkJson &);
    NetworkJson info() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace forge
