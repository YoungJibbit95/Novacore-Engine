#include "novacore/net/LocalDatagram.hpp"

#include <array>
#include <charconv>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace novacore::net {
namespace {
#ifdef _WIN32
using Handle = SOCKET;
using AddressLength = int;
constexpr Handle invalid = INVALID_SOCKET;

bool transientSocketError() {
    const auto error = WSAGetLastError();
    return error == WSAEWOULDBLOCK || error == WSAEINTR || error == WSAENOBUFS
        || error == WSAECONNRESET || error == WSAECONNREFUSED || error == WSAEMSGSIZE
        || error == WSAENETRESET || error == WSAENETUNREACH || error == WSAEHOSTUNREACH
        || error == WSAEHOSTDOWN || error == WSAENETDOWN || error == WSAETIMEDOUT;
}
#else
using Handle = int;
using AddressLength = socklen_t;
constexpr Handle invalid = -1;

bool transientSocketError() {
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR || errno == ENOBUFS
        || errno == ECONNRESET || errno == ECONNREFUSED || errno == EMSGSIZE
        || errno == ENETRESET || errno == ENETUNREACH || errno == EHOSTUNREACH
        || errno == EHOSTDOWN || errno == ENETDOWN || errno == ETIMEDOUT;
}
#endif

constexpr std::size_t maxDatagramBytes = 4096;

sockaddr_in socketAddress(DatagramEndpoint endpoint) {
    sockaddr_in result{};
    result.sin_family = AF_INET;
    result.sin_addr.s_addr = htonl(endpoint.address);
    result.sin_port = htons(endpoint.port);
    return result;
}
} // namespace

DatagramEndpoint DatagramEndpoint::loopback(std::uint16_t port) { return {INADDR_LOOPBACK, port}; }
DatagramEndpoint DatagramEndpoint::any(std::uint16_t port) { return {INADDR_ANY, port}; }

std::optional<DatagramEndpoint> DatagramEndpoint::parse(std::string_view numericAddress, std::uint16_t port) {
    std::uint32_t address = 0;
    for (int octetIndex = 0; octetIndex < 4; ++octetIndex) {
        const auto separator = numericAddress.find('.');
        if ((octetIndex == 3) != (separator == std::string_view::npos)) return {};
        const auto octet = numericAddress.substr(0, separator);
        if (octet.empty() || octet.size() > 3 || (octet.size() > 1 && octet.front() == '0')) return {};
        unsigned value = 0;
        const auto* end = octet.data() + octet.size();
        const auto [next, error] = std::from_chars(octet.data(), end, value);
        if (error != std::errc{} || next != end || value > 255) return {};
        address = (address << 8) | value;
        if (separator != std::string_view::npos) numericAddress.remove_prefix(separator + 1);
    }
    return DatagramEndpoint{address, port};
}

struct DatagramSocket::Impl {
    Handle handle = invalid;
    std::uint16_t port = 0;
#ifdef _WIN32
    bool winsock = false;
#endif
    ~Impl() {
#ifdef _WIN32
        if (handle != invalid) closesocket(handle);
        if (winsock) WSACleanup();
#else
        if (handle != invalid) close(handle);
#endif
    }
};

DatagramSocket::DatagramSocket(DatagramEndpoint endpoint) : impl_(std::make_unique<Impl>()) {
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data)) throw std::runtime_error("Winsock initialization failed");
    impl_->winsock = true;
#endif
    auto& handle = impl_->handle;
    handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle == invalid) throw std::runtime_error("Cannot open IPv4 datagram socket");
    auto local = socketAddress(endpoint);
    if (bind(handle, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) < 0)
        throw std::runtime_error("Cannot bind IPv4 datagram socket");
    AddressLength length = sizeof(local);
    if (getsockname(handle, reinterpret_cast<sockaddr*>(&local), &length) < 0)
        throw std::runtime_error("Cannot inspect IPv4 datagram socket");
    impl_->port = ntohs(local.sin_port);
#ifdef _WIN32
    u_long mode = 1;
    if (ioctlsocket(handle, FIONBIO, &mode) != 0)
#else
    const auto flags = fcntl(handle, F_GETFL);
    if (flags < 0 || fcntl(handle, F_SETFL, flags | O_NONBLOCK) < 0)
#endif
        throw std::runtime_error("Cannot configure nonblocking IPv4 datagram socket");
}

DatagramSocket::~DatagramSocket() = default;
std::uint16_t DatagramSocket::port() const { return impl_->port; }

void DatagramSocket::send(DatagramEndpoint destination, std::span<const std::uint8_t> bytes) const {
    if (bytes.size() > maxDatagramBytes) throw std::length_error("Datagram exceeds protocol budget");

    const auto remote = socketAddress(destination);
    // Winsock expects a valid buffer even when sending an empty datagram.
    const char* data = bytes.empty() ? "" : reinterpret_cast<const char*>(bytes.data());
    if (sendto(impl_->handle, data, static_cast<int>(bytes.size()), 0,
        reinterpret_cast<const sockaddr*>(&remote), sizeof(remote)) < 0 && !transientSocketError())
        throw std::runtime_error("IPv4 datagram send failed");
}

std::optional<Datagram> DatagramSocket::receive() const {
    std::array<std::uint8_t, maxDatagramBytes + 1> buffer;
    sockaddr_in source{};
    AddressLength length = sizeof(source);
    const auto size = recvfrom(impl_->handle, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0,
        reinterpret_cast<sockaddr*>(&source), &length);
    if (size < 0) {
        if (transientSocketError()) return {};
        throw std::runtime_error("IPv4 datagram receive failed");
    }
    if (static_cast<std::size_t>(size) > maxDatagramBytes) return {};
    return Datagram{{ntohl(source.sin_addr.s_addr), ntohs(source.sin_port)}, {buffer.data(), buffer.data() + size}};
}

LocalDatagramSocket::LocalDatagramSocket(std::uint16_t port) : socket_(DatagramEndpoint::loopback(port)) {}
LocalDatagramSocket::~LocalDatagramSocket() = default;
std::uint16_t LocalDatagramSocket::port() const { return socket_.port(); }

void LocalDatagramSocket::send(std::uint16_t destination, std::span<const std::uint8_t> bytes) const {
    socket_.send(DatagramEndpoint::loopback(destination), bytes);
}

std::optional<LocalDatagram> LocalDatagramSocket::receive() const {
    auto datagram = socket_.receive();
    if (!datagram || datagram->source != DatagramEndpoint::loopback(datagram->source.port)) return {};
    return LocalDatagram{datagram->source.port, std::move(datagram->bytes)};
}

} // namespace novacore::net
