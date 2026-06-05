#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace novacore::net {

class PacketWriter final {
public:
    void writeU8(std::uint8_t value);
    void writeU16(std::uint16_t value);
    void writeU32(std::uint32_t value);
    void writeU64(std::uint64_t value);
    void writeFloat(float value);
    void writeBytes(std::string_view bytes);

    [[nodiscard]] std::vector<std::uint8_t> finish() const;
    [[nodiscard]] std::size_t size() const;

private:
    std::vector<std::uint8_t> bytes_;
};

class PacketReader final {
public:
    explicit PacketReader(const std::vector<std::uint8_t>& bytes);

    [[nodiscard]] bool readU8(std::uint8_t& value);
    [[nodiscard]] bool readU16(std::uint16_t& value);
    [[nodiscard]] bool readU32(std::uint32_t& value);
    [[nodiscard]] bool readU64(std::uint64_t& value);
    [[nodiscard]] bool readFloat(float& value);
    [[nodiscard]] std::optional<std::string_view> readBytes(std::size_t count);
    [[nodiscard]] bool canRead(std::size_t count) const;
    [[nodiscard]] bool consumed() const;
    [[nodiscard]] std::size_t remaining() const;
    [[nodiscard]] std::size_t offset() const;

private:
    const std::vector<std::uint8_t>& bytes_;
    std::size_t offset_ = 0;
};

} // namespace novacore::net
