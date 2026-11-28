#include "novacore/world/WorldJson.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace novacore::world {
namespace {

bool validUtf8(std::string_view s) {
    for (std::size_t i = 0; i < s.size();) {
        const auto first = static_cast<unsigned char>(s[i++]);
        if (first < 0x80) continue;
        unsigned count = 0, code = 0, minimum = 0;
        if (first >= 0xC2 && first <= 0xDF) { count = 1; code = first & 31U; minimum = 0x80; }
        else if (first >= 0xE0 && first <= 0xEF) { count = 2; code = first & 15U; minimum = 0x800; }
        else if (first >= 0xF0 && first <= 0xF4) { count = 3; code = first & 7U; minimum = 0x10000; }
        else return false;
        if (s.size() - i < count) return false;
        for (unsigned j = 0; j < count; ++j) {
            const auto c = static_cast<unsigned char>(s[i++]);
            if ((c & 0xC0U) != 0x80U) return false;
            code = (code << 6U) | (c & 63U);
        }
        if (code < minimum || code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF)) return false;
    }
    return true;
}

void appendUtf8(std::string& s, unsigned c) {
    if (c <= 0x7F) s.push_back(static_cast<char>(c));
    else if (c <= 0x7FF) {
        s.push_back(static_cast<char>(0xC0U | (c >> 6U)));
        s.push_back(static_cast<char>(0x80U | (c & 63U)));
    } else if (c <= 0xFFFF) {
        s.push_back(static_cast<char>(0xE0U | (c >> 12U)));
        s.push_back(static_cast<char>(0x80U | ((c >> 6U) & 63U)));
        s.push_back(static_cast<char>(0x80U | (c & 63U)));
    } else {
        s.push_back(static_cast<char>(0xF0U | (c >> 18U)));
        s.push_back(static_cast<char>(0x80U | ((c >> 12U) & 63U)));
        s.push_back(static_cast<char>(0x80U | ((c >> 6U) & 63U)));
        s.push_back(static_cast<char>(0x80U | (c & 63U)));
    }
}

// Deliberately schema-specific reader: at most three object levels, no generic
// recursive DOM or unbounded skipped values. Structural failures carry offsets.
class Reader final {
public:
    explicit Reader(std::string_view input) : input_(input) {}
    WorldDocument document() {
        WorldDocument doc;
        const unsigned fields = object([&](std::string_view key) -> unsigned {
            if (key == "schemaVersion") {
                if (numberToken() != "1") fail("Unsupported schema version");
                return 1;
            }
            if (key == "id") { doc.id = string(); return 2; }
            if (key == "objects") {
                expect('[');
                if (!take(']')) {
                    do {
                        if (doc.objects.size() >= kWorldObjectLimit) fail("Object limit exceeded");
                        doc.objects.push_back(worldObject());
                    } while (take(','));
                    expect(']');
                }
                return 4;
            }
            fail("Unknown document field");
        });
        if (fields != 7) fail("Missing document field");
        whitespace();
        if (pos_ != input_.size()) fail("Trailing data");
        return doc;
    }

private:
    [[noreturn]] void fail(std::string_view reason) const {
        throw std::runtime_error(std::string(reason) + " at byte " + std::to_string(pos_));
    }
    char peek() const { return pos_ < input_.size() ? input_[pos_] : '\0'; }
    void whitespace() { while (peek() == ' ' || peek() == '\t' || peek() == '\r' || peek() == '\n') ++pos_; }
    bool take(char c) { whitespace(); if (peek() != c) return false; ++pos_; return true; }
    void expect(char c) { if (!take(c)) fail("Unexpected JSON token"); }
    bool literal(std::string_view s) {
        whitespace();
        if (input_.substr(pos_, s.size()) != s) return false;
        pos_ += s.size(); return true;
    }
    bool boolean() { if (literal("true")) return true; if (literal("false")) return false; fail("Expected boolean"); }
    static bool digit(char c) { return c >= '0' && c <= '9'; }
    std::string_view numberToken() {
        whitespace(); const auto start = pos_;
        if (peek() == '-') ++pos_;
        if (peek() == '0') ++pos_;
        else { if (!digit(peek())) fail("Expected number"); while (digit(peek())) ++pos_; }
        if (peek() == '.') { ++pos_; if (!digit(peek())) fail("Missing fraction"); while (digit(peek())) ++pos_; }
        if (peek() == 'e' || peek() == 'E') {
            ++pos_; if (peek() == '+' || peek() == '-') ++pos_;
            if (!digit(peek())) fail("Missing exponent");
            while (digit(peek())) ++pos_;
        }
        if (pos_ - start > 64) fail("Number token too long");
        return input_.substr(start, pos_ - start);
    }
    float number() {
        const auto s = numberToken(); float v = 0;
        const auto result = std::from_chars(s.data(), s.data() + s.size(), v, std::chars_format::general);
        if (result.ec != std::errc{} || result.ptr != s.data() + s.size() || !std::isfinite(v)) fail("Invalid finite float");
        return v;
    }
    unsigned hex4() {
        unsigned code = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = peek(); unsigned digitValue;
            if (c >= '0' && c <= '9') digitValue = static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') digitValue = static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') digitValue = static_cast<unsigned>(c - 'A' + 10);
            else fail("Invalid Unicode escape");
            ++pos_; code = (code << 4U) | digitValue;
        }
        return code;
    }
    std::string string() {
        expect('"'); std::string result;
        while (pos_ < input_.size()) {
            const auto c = static_cast<unsigned char>(input_[pos_++]);
            if (c == '"') {
                if (!validUtf8(result)) fail("Invalid UTF-8");
                return result;
            }
            if (c < 32) fail("Unescaped control character");
            if (c != '\\') result.push_back(static_cast<char>(c));
            else {
                if (pos_ == input_.size()) fail("Truncated escape");
                switch (input_[pos_++]) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'u': {
                    unsigned code = hex4();
                    if (code >= 0xD800 && code <= 0xDBFF) {
                        if (input_.substr(pos_, 2) != "\\u") fail("Missing low surrogate");
                        pos_ += 2; const unsigned low = hex4();
                        if (low < 0xDC00 || low > 0xDFFF) fail("Invalid low surrogate");
                        code = 0x10000U + ((code - 0xD800U) << 10U) + low - 0xDC00U;
                    } else if (code >= 0xDC00 && code <= 0xDFFF) fail("Unpaired low surrogate");
                    appendUtf8(result, code); break;
                }
                default: fail("Invalid escape");
                }
            }
            if (result.size() > 256) fail("String limit exceeded");
        }
        fail("Unterminated string");
    }
    template<class F> unsigned object(F field) {
        expect('{'); unsigned fields = 0;
        if (take('}')) return fields;
        do {
            const auto key = string(); expect(':'); const unsigned bit = field(key);
            if ((fields & bit) != 0) fail("Duplicate field");
            fields |= bit;
        } while (take(','));
        expect('}'); return fields;
    }
    math::Vec3 vector() {
        expect('['); const auto x = number(); expect(','); const auto y = number(); expect(','); const auto z = number(); expect(']');
        return {x, y, z};
    }
    WorldObject worldObject() {
        WorldObject obj; WorldBox box; std::string kind, asset, marker;
        const unsigned fields = object([&](std::string_view key) -> unsigned {
            if (key == "id") { obj.id = string(); return 1; }
            if (key == "name") { obj.name = string(); return 2; }
            if (key == "position") { obj.position = vector(); return 4; }
            if (key == "kind") { kind = string(); return 8; }
            if (key == "halfExtents") { box.halfExtents = vector(); return 16; }
            if (key == "blocksMovement") { box.blocksMovement = boolean(); return 32; }
            if (key == "assetId") { asset = string(); return 64; }
            if (key == "markerType") { marker = string(); return 128; }
            fail("Unknown object field");
        });
        if (kind == "box" && fields == 63) obj.data = box;
        else if (kind == "asset" && fields == 79) obj.data = WorldAssetReference{std::move(asset)};
        else if (kind == "marker" && fields == 143) obj.data = WorldMarker{std::move(marker)};
        else fail("Unknown object kind or incorrect fields");
        return obj;
    }
    std::string_view input_;
    std::size_t pos_ = 0;
};

void quoted(std::string& out, std::string_view s) {
    out += '"';
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += '"';
}
void numeric(std::string& out, float v) {
    if (v == 0) { out += '0'; return; }
    std::array<char, 64> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), v,
        std::chars_format::general, std::numeric_limits<float>::max_digits10);
    if (result.ec != std::errc{}) throw std::runtime_error("Float encoding failed");
    out.append(buffer.data(), result.ptr);
}
void vector(std::string& out, math::Vec3 v) {
    out += '['; numeric(out, v.x); out += ','; numeric(out, v.y); out += ','; numeric(out, v.z); out += ']';
}
} // namespace

std::optional<std::string> serializeWorldJson(const WorldDocument& doc, std::string& out) {
    if (auto error = validateWorldDocument(doc)) return error;
    try {
        std::string json = "{\"schemaVersion\":1,\"id\":"; quoted(json, doc.id); json += ",\"objects\":[";
        bool first = true;
        for (const auto& obj : doc.objects) {
            if (!validUtf8(obj.name)) return "Invalid UTF-8 name: " + obj.id;
            if (!first) json += ',';
            first = false;
            json += "\n{\"id\":"; quoted(json, obj.id); json += ",\"name\":"; quoted(json, obj.name);
            json += ",\"position\":"; vector(json, obj.position);
            if (const auto* box = std::get_if<WorldBox>(&obj.data)) {
                json += ",\"kind\":\"box\",\"halfExtents\":"; vector(json, box->halfExtents);
                json += ",\"blocksMovement\":"; json += box->blocksMovement ? "true" : "false";
            } else if (const auto* asset = std::get_if<WorldAssetReference>(&obj.data)) {
                json += ",\"kind\":\"asset\",\"assetId\":"; quoted(json, asset->assetId);
            } else {
                json += ",\"kind\":\"marker\",\"markerType\":"; quoted(json, std::get<WorldMarker>(obj.data).type);
            }
            json += '}';
            if (json.size() > kWorldJsonByteLimit - 4) return "World JSON byte limit exceeded";
        }
        json += "\n]}\n";
        out = std::move(json); return std::nullopt;
    } catch (const std::runtime_error& error) { return error.what(); }
}

std::optional<std::string> deserializeWorldJson(std::string_view json, WorldDocument& out) {
    if (json.size() > kWorldJsonByteLimit) return "World JSON byte limit exceeded";
    try {
        auto doc = Reader(json).document();
        if (auto error = validateWorldDocument(doc)) return error;
        out = std::move(doc); return std::nullopt;
    } catch (const std::runtime_error& error) { return error.what(); }
}
} // namespace novacore::world
