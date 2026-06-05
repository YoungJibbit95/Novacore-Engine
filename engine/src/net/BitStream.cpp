#include "novacore/net/BitStream.hpp"

#include <cstring>

namespace novacore::net {

void PacketWriter::writeU8(std::uint8_t value) {
    bytes_.push_back(value);
}

void PacketWriter::writeU16(std::uint16_t value) {
    bytes_.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    bytes_.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void PacketWriter::writeU32(std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        bytes_.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void PacketWriter::writeU64(std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
        bytes_.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void PacketWriter::writeFloat(float value) {
    static_assert(sizeof(float) == sizeof(std::uint32_t));
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(float));
    writeU32(bits);
}

void PacketWriter::writeBytes(std::string_view bytes) {
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
}

std::vector<std::uint8_t> PacketWriter::finish() const {
    return bytes_;
}

std::size_t PacketWriter::size() const {
    return bytes_.size();
}

PacketReader::PacketReader(const std::vector<std::uint8_t>& bytes)
    : bytes_(bytes) {
}

bool PacketReader::readU8(std::uint8_t& value) {
    if (!canRead(1)) {
        return false;
    }
    value = bytes_[offset_++];
    return true;
}

bool PacketReader::readU16(std::uint16_t& value) {
    if (!canRead(2)) {
        return false;
    }
    value = static_cast<std::uint16_t>(bytes_[offset_]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes_[offset_ + 1]) << 8U);
    offset_ += 2;
    return true;
}

bool PacketReader::readU32(std::uint32_t& value) {
    if (!canRead(4)) {
        return false;
    }

    value = 0;
    for (int shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(bytes_[offset_++]) << shift;
    }
    return true;
}

bool PacketReader::readU64(std::uint64_t& value) {
    if (!canRead(8)) {
        return false;
    }

    value = 0;
    for (int shift = 0; shift < 64; shift += 8) {
        value |= static_cast<std::uint64_t>(bytes_[offset_++]) << shift;
    }
    return true;
}

bool PacketReader::readFloat(float& value) {
    std::uint32_t bits = 0;
    if (!readU32(bits)) {
        return false;
    }
    std::memcpy(&value, &bits, sizeof(float));
    return true;
}

std::optional<std::string_view> PacketReader::readBytes(std::size_t count) {
    if (!canRead(count)) {
        return std::nullopt;
    }

    const auto* begin = reinterpret_cast<const char*>(bytes_.data() + offset_);
    offset_ += count;
    return std::string_view(begin, count);
}

bool PacketReader::canRead(std::size_t count) const {
    return count <= remaining();
}

bool PacketReader::consumed() const {
    return offset_ == bytes_.size();
}

std::size_t PacketReader::remaining() const {
    return bytes_.size() - offset_;
}

std::size_t PacketReader::offset() const {
    return offset_;
}

} // namespace novacore::net
