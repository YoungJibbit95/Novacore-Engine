#include "novacore/core/Log.hpp"

#include <chrono>
#include <iostream>

namespace novacore::core {

namespace {

std::string_view levelName(LogLevel level) {
    switch (level) {
    case LogLevel::Trace:
        return "trace";
    case LogLevel::Info:
        return "info";
    case LogLevel::Warning:
        return "warn";
    case LogLevel::Error:
        return "error";
    }
    return "unknown";
}

} // namespace

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::write(LogLevel level, std::string_view category, std::string_view message) {
    std::lock_guard lock(mutex_);
    std::cout << "[" << levelName(level) << "][" << category << "] " << message << '\n';
}

void logTrace(std::string_view category, std::string_view message) {
    Logger::instance().write(LogLevel::Trace, category, message);
}

void logInfo(std::string_view category, std::string_view message) {
    Logger::instance().write(LogLevel::Info, category, message);
}

void logWarning(std::string_view category, std::string_view message) {
    Logger::instance().write(LogLevel::Warning, category, message);
}

void logError(std::string_view category, std::string_view message) {
    Logger::instance().write(LogLevel::Error, category, message);
}

} // namespace novacore::core







