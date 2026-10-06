#include <forge/network.hpp>
#include <stdexcept>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif
namespace forge {
// Valve IP listeners require a nonzero port. Ask the OS for one, then retry the
// listener if another process claims it between reservation and Valve binding.
uint16_t networkEphemeralPort(const std::string &bind) {
#ifdef _WIN32
    struct Wsa {
        Wsa() {
            WSADATA data;
            if (WSAStartup(MAKEWORD(2, 2), &data))
                throw std::runtime_error("Cannot initialize sockets");
        }
        ~Wsa() {
            WSACleanup();
        }
    } wsa;
#endif
    sockaddr_storage address{};
    int family = bind.find(':') == std::string::npos ? AF_INET : AF_INET6;
#ifdef _WIN32
    int size = family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
#else
    socklen_t size = family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6);
#endif
    address.ss_family = static_cast<decltype(address.ss_family)>(family);
    void *ip = family == AF_INET
                   ? static_cast<void *>(&reinterpret_cast<sockaddr_in *>(&address)->sin_addr)
                   : static_cast<void *>(&reinterpret_cast<sockaddr_in6 *>(&address)->sin6_addr);
    if (inet_pton(family, bind.c_str(), ip) != 1)
        throw std::invalid_argument("Socket bind needs a numeric IP address");
    auto fd = socket(family, SOCK_DGRAM, 0);
#ifdef _WIN32
    if (fd == INVALID_SOCKET)
        throw std::runtime_error("Cannot reserve socket port");
    struct Close {
        SOCKET fd;
        ~Close() {
            closesocket(fd);
        }
    } close{fd};
#else
    if (fd < 0)
        throw std::runtime_error("Cannot reserve socket port");
    struct Close {
        int fd;
        ~Close() {
            ::close(fd);
        }
    } close{fd};
#endif
    if (::bind(fd, reinterpret_cast<sockaddr *>(&address), size) ||
        getsockname(fd, reinterpret_cast<sockaddr *>(&address), &size))
        throw std::runtime_error("Cannot reserve socket bind port");
    return ntohs(family == AF_INET ? reinterpret_cast<sockaddr_in *>(&address)->sin_port
                                   : reinterpret_cast<sockaddr_in6 *>(&address)->sin6_port);
}
} // namespace forge
