#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace novacore::net {

// An IPv4 address and UDP port, both in host byte order.
struct DatagramEndpoint {
    std::uint32_t address;
    std::uint16_t port;

    bool operator==(const DatagramEndpoint&) const = default;

    [[nodiscard]] static DatagramEndpoint loopback(std::uint16_t port);
    [[nodiscard]] static DatagramEndpoint any(std::uint16_t port);
    [[nodiscard]] static std::optional<DatagramEndpoint> parse(std::string_view numericAddress, std::uint16_t port);
};

struct Datagram {
    DatagramEndpoint source;
    std::vector<std::uint8_t> bytes;
};

class DatagramSocket final {
public:
    explicit DatagramSocket(DatagramEndpoint local);
    ~DatagramSocket();
    [[nodiscard]] std::uint16_t port() const;
    // Transient network failures drop the outgoing datagram.
    void send(DatagramEndpoint destination, std::span<const std::uint8_t> bytes) const;
    // Attempts one read. Oversized packets and transient errors yield no datagram.
    [[nodiscard]] std::optional<Datagram> receive() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct LocalDatagram {
    std::uint16_t sourcePort;
    std::vector<std::uint8_t> bytes;
};

// A nonblocking IPv4 datagram socket bound exclusively to the loopback interface.
class LocalDatagramSocket final {
public:
    explicit LocalDatagramSocket(std::uint16_t port = 0);
    ~LocalDatagramSocket();
    [[nodiscard]] std::uint16_t port() const;
    void send(std::uint16_t destination, std::span<const std::uint8_t> bytes) const;
    [[nodiscard]] std::optional<LocalDatagram> receive() const;

private:
    DatagramSocket socket_;
};

} // namespace novacore::net
