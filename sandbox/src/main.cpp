#include "novacore/sandbox/EngineSandbox.hpp"

#include <iostream>

int main() {
    const auto result = novacore::sandbox::runEngineSandbox();
    std::cout << result.summary << '\n';
    std::cout << "preview boxes=" << result.previewFrame.worldBoxes.size()
              << " lines=" << result.previewFrame.worldLines.size() << '\n';
    return result.stable ? 0 : 1;
}
