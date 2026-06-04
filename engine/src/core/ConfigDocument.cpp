#include "novacore/core/ConfigDocument.hpp"

#include <charconv>
#include <cctype>
#include <cstdlib>
#include <string>
#include <system_error>
#include <utility>

namespace novacore::core {

namespace {

class JsonConfigParser final {
public:
    explicit JsonConfigParser(std::string_view text)
        : text_(text) {
    }

    ConfigParseResult parse(ConfigDocument& document) {
        document.clear();
        skipWhitespace();
        parseValue("$", document);
        skipWhitespace();

        if (!failed() && offset_ != text_.size()) {
            error("Unexpected trailing characters");
        }

        return ConfigParseResult{std::move(errors_)};
    }

private:
    void parseValue(const std::string& path, ConfigDocument& document) {
        skipWhitespace();
        if (failed()) {
            return;
        }

        if (peek() == '{') {
            parseObject(path == "$" ? std::string{} : path, document);
            return;
        }

        if (peek() == '[') {
            parseArray(path, document);
            return;
        }

        if (peek() == '"') {
            document.set(path, parseString());
            return;
        }

        if (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '-') {
            document.set(path, parseNumber());
            return;
        }

        if (consumeLiteral("true")) {
            document.set(path, "true");
            return;
        }

        if (consumeLiteral("false")) {
            document.set(path, "false");
            return;
        }

        if (consumeLiteral("null")) {
            document.set(path, "null");
            return;
        }

        error("Expected JSON value");
    }

    void parseObject(const std::string& path, ConfigDocument& document) {
        consume('{');
        skipWhitespace();

        if (tryConsume('}')) {
            return;
        }

        while (!failed()) {
            if (peek() != '"') {
                error("Expected object key");
                return;
            }

            const auto key = parseString();
            skipWhitespace();
            consume(':');

            const auto childPath = path.empty() ? key : path + "." + key;
            parseValue(childPath, document);
            skipWhitespace();

            if (tryConsume('}')) {
                return;
            }

            consume(',');
            skipWhitespace();
        }
    }

    void parseArray(const std::string& path, ConfigDocument& document) {
        consume('[');
        skipWhitespace();

        if (tryConsume(']')) {
            return;
        }

        std::size_t index = 0;
        while (!failed()) {
            parseValue(path + "." + std::to_string(index), document);
            ++index;
            skipWhitespace();

            if (tryConsume(']')) {
                return;
            }

            consume(',');
            skipWhitespace();
        }
    }

    std::string parseString() {
        consume('"');
        std::string value;

        while (!failed() && offset_ < text_.size()) {
            const char current = text_[offset_++];
            if (current == '"') {
                return value;
            }

            if (current == '\\') {
                if (offset_ >= text_.size()) {
                    error("Unterminated string escape");
                    return value;
                }

                const char escaped = text_[offset_++];
                switch (escaped) {
                case '"':
                case '\\':
                case '/':
                    value.push_back(escaped);
                    break;
                case 'b':
                    value.push_back('\b');
                    break;
                case 'f':
                    value.push_back('\f');
                    break;
                case 'n':
                    value.push_back('\n');
                    break;
                case 'r':
                    value.push_back('\r');
                    break;
                case 't':
                    value.push_back('\t');
                    break;
                default:
                    error("Unsupported string escape");
                    return value;
                }
            } else {
                value.push_back(current);
            }
        }

        error("Unterminated string");
        return value;
    }

    std::string parseNumber() {
        const auto start = offset_;

        if (peek() == '-') {
            ++offset_;
        }

        while (offset_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[offset_]))) {
            ++offset_;
        }

        if (offset_ < text_.size() && text_[offset_] == '.') {
            ++offset_;
            while (offset_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[offset_]))) {
                ++offset_;
            }
        }

        if (offset_ < text_.size() && (text_[offset_] == 'e' || text_[offset_] == 'E')) {
            ++offset_;
            if (offset_ < text_.size() && (text_[offset_] == '+' || text_[offset_] == '-')) {
                ++offset_;
            }
            while (offset_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[offset_]))) {
                ++offset_;
            }
        }

        return std::string(text_.substr(start, offset_ - start));
    }

    bool consumeLiteral(std::string_view literal) {
        if (text_.substr(offset_, literal.size()) != literal) {
            return false;
        }

        offset_ += literal.size();
        return true;
    }

    void skipWhitespace() {
        while (offset_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[offset_]))) {
            ++offset_;
        }
    }

    char peek() const {
        if (offset_ >= text_.size()) {
            return '\0';
        }
        return text_[offset_];
    }

    bool tryConsume(char expected) {
        if (peek() != expected) {
            return false;
        }
        ++offset_;
        return true;
    }

    void consume(char expected) {
        if (!tryConsume(expected)) {
            error(std::string("Expected '") + expected + "'");
        }
    }

    void error(std::string message) {
        errors_.push_back(ConfigParseError{std::move(message), offset_});
    }

    bool failed() const {
        return !errors_.empty();
    }

    std::string_view text_;
    std::size_t offset_ = 0;
    std::vector<ConfigParseError> errors_;
};

} // namespace

void ConfigDocument::set(std::string key, std::string value) {
    values_.insert_or_assign(std::move(key), std::move(value));
}

void ConfigDocument::clear() {
    values_.clear();
}

bool ConfigDocument::contains(std::string_view key) const {
    return values_.contains(std::string(key));
}

std::optional<std::string> ConfigDocument::stringValue(std::string_view key) const {
    auto it = values_.find(std::string(key));
    if (it == values_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::string ConfigDocument::stringOr(std::string_view key, std::string fallback) const {
    auto value = stringValue(key);
    if (!value.has_value()) {
        return fallback;
    }
    return *value;
}

std::optional<double> ConfigDocument::numberValue(std::string_view key) const {
    const auto value = stringValue(key);
    if (!value.has_value()) {
        return std::nullopt;
    }

    char* end = nullptr;
    const double parsed = std::strtod(value->c_str(), &end);
    if (end == value->c_str()) {
        return std::nullopt;
    }
    return parsed;
}

double ConfigDocument::numberOr(std::string_view key, double fallback) const {
    auto value = numberValue(key);
    if (!value.has_value()) {
        return fallback;
    }
    return *value;
}

std::optional<int> ConfigDocument::intValue(std::string_view key) const {
    const auto value = stringValue(key);
    if (!value.has_value()) {
        return std::nullopt;
    }

    int parsed = 0;
    const auto* begin = value->data();
    const auto* end = begin + value->size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return parsed;
}

int ConfigDocument::intOr(std::string_view key, int fallback) const {
    auto value = intValue(key);
    if (!value.has_value()) {
        return fallback;
    }
    return *value;
}

std::optional<bool> ConfigDocument::boolValue(std::string_view key) const {
    const auto value = stringValue(key);
    if (!value.has_value()) {
        return std::nullopt;
    }

    if (*value == "true") {
        return true;
    }
    if (*value == "false") {
        return false;
    }
    return std::nullopt;
}

bool ConfigDocument::boolOr(std::string_view key, bool fallback) const {
    auto value = boolValue(key);
    if (!value.has_value()) {
        return fallback;
    }
    return *value;
}

std::size_t ConfigDocument::valueCount() const {
    return values_.size();
}

const std::unordered_map<std::string, std::string>& ConfigDocument::values() const {
    return values_;
}

ConfigParseResult parseJsonConfig(std::string_view text, ConfigDocument& outDocument) {
    JsonConfigParser parser(text);
    return parser.parse(outDocument);
}

} // namespace novacore::core
