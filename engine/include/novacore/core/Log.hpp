#pragma once

#include <mutex>
#include <string_view>

namespace novacore::core {

enum class LogLevel {
    Trace,
    Info,
    Warning,
    Error
};

class Logger final {
public:
    static Logger& instance();

    void write(LogLevel level, std::string_view category, std::string_view message);

private:
    std::mutex mutex_;
};

void logTrace(std::string_view category, std::string_view message);
void logInfo(std::string_view category, std::string_view message);
void logWarning(std::string_view category, std::string_view message);
void logError(std::string_view category, std::string_view message);

} // namespace novacore::core







