#include "novacore/sandbox/EngineSandbox.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

namespace {

struct CliOptions final {
    novacore::sandbox::EngineSandboxOptions sandbox;
    bool showHelp = false;
    bool listScenarios = false;
    bool json = false;
    bool invalid = false;
    std::string error;
};

[[nodiscard]] bool parseUInt32(std::string_view value, std::uint32_t& outValue) {
    const std::string text(value);
    char* end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 10);
    if (errno != 0 || end != text.c_str() + text.size() || parsed > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    outValue = static_cast<std::uint32_t>(parsed);
    return outValue > 0U;
}

[[nodiscard]] bool parsePositiveDouble(std::string_view value, double& outValue) {
    const std::string text(value);
    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(text.c_str(), &end);
    if (errno != 0 || end != text.c_str() + text.size() || parsed <= 0.0) {
        return false;
    }

    outValue = parsed;
    return true;
}

[[nodiscard]] bool appendScenarioFilter(
    novacore::sandbox::EngineSandboxOptions& options,
    std::string_view value,
    std::string& error) {
    while (!value.empty()) {
        const auto comma = value.find(',');
        const auto token = value.substr(0U, comma);
        if (token.empty()) {
            error = "empty scenario id";
            return false;
        }

        if (!novacore::sandbox::isEngineSandboxScenarioAvailable(token)) {
            error = "unknown scenario id: " + std::string(token);
            return false;
        }

        options.scenarioIds.emplace_back(token);
        if (comma == std::string_view::npos) {
            break;
        }
        value.remove_prefix(comma + 1U);
    }

    return true;
}

[[nodiscard]] bool readFollowingValue(
    int argc,
    char** argv,
    int& index,
    std::string_view flag,
    std::string_view& outValue,
    std::string& error) {
    if (index + 1 >= argc) {
        error = "missing value for " + std::string(flag);
        return false;
    }

    ++index;
    outValue = argv[index];
    return true;
}

[[nodiscard]] CliOptions parseCommandLine(int argc, char** argv) {
    CliOptions options{};
    for (int index = 1; index < argc; ++index) {
        const std::string_view arg(argv[index]);

        if (arg == "--help" || arg == "-h") {
            options.showHelp = true;
            continue;
        }
        if (arg == "--list") {
            options.listScenarios = true;
            continue;
        }
        if (arg == "--json") {
            options.json = true;
            continue;
        }
        if (arg == "--fail-fast") {
            options.sandbox.failFast = true;
            continue;
        }
        if (arg == "--no-telemetry") {
            options.sandbox.includeTelemetry = false;
            continue;
        }
        if (arg == "--no-preview") {
            options.sandbox.emitPreviewFrame = false;
            continue;
        }
        if (arg == "--no-sprint") {
            options.sandbox.sprint = false;
            continue;
        }

        std::string_view value;
        if (arg == "--scenario" || arg == "-s") {
            if (!readFollowingValue(argc, argv, index, arg, value, options.error) ||
                !appendScenarioFilter(options.sandbox, value, options.error)) {
                options.invalid = true;
                return options;
            }
            continue;
        }
        if (arg.starts_with("--scenario=")) {
            value = arg.substr(std::string_view("--scenario=").size());
            if (!appendScenarioFilter(options.sandbox, value, options.error)) {
                options.invalid = true;
                return options;
            }
            continue;
        }

        if (arg == "--ticks") {
            if (!readFollowingValue(argc, argv, index, arg, value, options.error) ||
                !parseUInt32(value, options.sandbox.tickCount)) {
                options.invalid = true;
                if (options.error.empty()) {
                    options.error = "invalid --ticks value";
                }
                return options;
            }
            continue;
        }
        if (arg.starts_with("--ticks=")) {
            value = arg.substr(std::string_view("--ticks=").size());
            if (!parseUInt32(value, options.sandbox.tickCount)) {
                options.invalid = true;
                options.error = "invalid --ticks value";
                return options;
            }
            continue;
        }

        if (arg == "--delta") {
            double parsedDelta = 0.0;
            if (!readFollowingValue(argc, argv, index, arg, value, options.error) ||
                !parsePositiveDouble(value, parsedDelta)) {
                options.invalid = true;
                if (options.error.empty()) {
                    options.error = "invalid --delta value";
                }
                return options;
            }
            options.sandbox.fixedDeltaSeconds =
                static_cast<decltype(options.sandbox.fixedDeltaSeconds)>(parsedDelta);
            continue;
        }
        if (arg.starts_with("--delta=")) {
            double parsedDelta = 0.0;
            value = arg.substr(std::string_view("--delta=").size());
            if (!parsePositiveDouble(value, parsedDelta)) {
                options.invalid = true;
                options.error = "invalid --delta value";
                return options;
            }
            options.sandbox.fixedDeltaSeconds =
                static_cast<decltype(options.sandbox.fixedDeltaSeconds)>(parsedDelta);
            continue;
        }

        options.invalid = true;
        options.error = "unknown argument: " + std::string(arg);
        return options;
    }

    return options;
}

void printUsage(std::ostream& stream) {
    stream << "NovaCore Engine Sandbox\n"
           << "Usage: novacore_engine_sandbox [options]\n"
           << "Options:\n"
           << "  --list                         List scenario ids.\n"
           << "  --scenario, -s <id[,id]>       Run selected scenario ids.\n"
           << "  --ticks <count>                Override smoke tick count.\n"
           << "  --delta <seconds>              Override fixed delta seconds.\n"
           << "  --json                         Emit machine-readable JSON.\n"
           << "  --no-telemetry                 Omit per-scenario telemetry events.\n"
           << "  --no-preview                   Skip preview frame generation.\n"
           << "  --no-sprint                    Disable sprint input in movement replay.\n"
           << "  --fail-fast                    Stop after first failed scenario.\n"
           << "  --help, -h                     Show this help.\n"
           << "Exit codes: 0 success, 1 scenario failure, 2 invalid arguments, 3 no scenario selected.\n";
}

void printScenarioList(std::ostream& stream) {
    for (const auto id : novacore::sandbox::availableEngineSandboxScenarioIds()) {
        stream << id << '\n';
    }
}

} // namespace

int main(int argc, char** argv) {
    const auto cli = parseCommandLine(argc, argv);
    if (cli.invalid) {
        std::cerr << "error: " << cli.error << "\n\n";
        printUsage(std::cerr);
        return novacore::sandbox::engineSandboxExitCodeValue(
            novacore::sandbox::EngineSandboxExitCode::InvalidArguments);
    }

    if (cli.showHelp) {
        printUsage(std::cout);
        return 0;
    }

    if (cli.listScenarios) {
        printScenarioList(std::cout);
        return 0;
    }

    const auto result = novacore::sandbox::runEngineSandbox(cli.sandbox);
    if (cli.json) {
        std::cout << novacore::sandbox::formatEngineSandboxJson(result) << '\n';
    } else {
        std::cout << novacore::sandbox::formatEngineSandboxText(result);
    }

    return novacore::sandbox::engineSandboxExitCodeValue(result.exitCode);
}
