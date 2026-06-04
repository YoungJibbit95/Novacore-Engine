#include "novacore/Novacore.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message) {
    if (condition) {
        return;
    }

    ++failures;
    std::cerr << "[fail] " << message << '\n';
}

void testWorldLifetime() {
    novacore::ecs::World world;

    const auto entity = world.createEntity();
    expect(world.isAlive(entity), "created entity is alive");

    world.addComponent(entity, novacore::ecs::NameComponent{"smoke"});
    expect(world.getComponent<novacore::ecs::NameComponent>(entity) != nullptr, "component can be read");

    world.destroyEntity(entity);
    expect(!world.isAlive(entity), "destroyed entity is not alive");
    expect(world.aliveCount() == 0, "alive count returns to zero");
    expect(world.getComponent<novacore::ecs::NameComponent>(entity) == nullptr, "destroy removes components");
}

void testFixedStepAccumulator() {
    novacore::core::FixedStepAccumulator accumulator({60.0, 0.25});
    accumulator.advance(1.0 / 30.0);

    int steps = 0;
    while (accumulator.shouldStep()) {
        accumulator.consumeStep();
        ++steps;
    }

    expect(steps == 2, "1/30s advances exactly two 60Hz ticks");
    expect(accumulator.consumedSteps() == 2, "consumed step counter matches");
    expect(accumulator.alpha() < 0.001, "alpha is near zero after exact fixed steps");
}

void testLoopbackChannel() {
    novacore::net::LoopbackChannel loopback;
    novacore::net::Packet packet{};
    packet.sequence = novacore::net::PacketSequence{7};
    packet.payload = {1, 2, 3, 4};

    loopback.sendToServer(packet);

    novacore::net::Packet received{};
    expect(loopback.tryReceiveForServer(received), "server receives loopback packet");
    expect(received.sequence == novacore::net::PacketSequence{7}, "packet sequence survives loopback");
    expect(received.payload.size() == 4, "packet payload survives loopback");
}

void testInputActions() {
    novacore::platform::InputActionMap actionMap;
    novacore::platform::InputControl jumpKey{
        novacore::platform::InputControlKind::KeyboardKey,
        32,
    };

    actionMap.bind(novacore::platform::InputBinding{"jump", jumpKey, 1.0F, 0.5F});

    novacore::platform::InputSnapshot snapshot;
    snapshot.setButton(jumpKey, true, novacore::platform::InputDeviceKind::KeyboardMouse);
    actionMap.update(snapshot);

    const auto jump = actionMap.stateOrDefault("jump");
    expect(jump.down, "bound button drives action down state");
    expect(jump.pressed, "first down frame reports pressed");

    actionMap.update(snapshot);
    expect(!actionMap.stateOrDefault("jump").pressed, "held button does not repeat pressed");

    snapshot.setButton(jumpKey, false, novacore::platform::InputDeviceKind::KeyboardMouse);
    actionMap.update(snapshot);
    expect(actionMap.stateOrDefault("jump").released, "button release reports released");
}

void testFileChangeTracker() {
    const auto path = std::filesystem::temp_directory_path() / "novacore_smoke_file.txt";
    {
        std::ofstream file(path);
        file << "first";
    }

    novacore::io::FileChangeTracker tracker;
    tracker.track(path);
    expect(tracker.trackedCount() == 1, "file tracker records tracked file");
    expect(tracker.pollChanges().empty(), "unchanged file does not emit change");

    {
        std::ofstream file(path, std::ios::app);
        file << "second";
    }

    expect(!tracker.pollChanges().empty(), "modified file emits change");
    std::filesystem::remove(path);
}

void testConfigDocument() {
    constexpr std::string_view json = R"({
        "movement": {
            "sprint_speed": 7.4,
            "enabled": true
        },
        "weapons": [
            { "id": "ar_01", "magazine_size": 30 }
        ]
    })";

    novacore::core::ConfigDocument document;
    const auto result = novacore::core::parseJsonConfig(json, document);

    expect(result.ok(), "json config parses successfully");
    expect(document.numberOr("movement.sprint_speed", 0.0) > 7.3, "nested number can be read");
    expect(document.boolOr("movement.enabled", false), "nested bool can be read");
    expect(document.stringOr("weapons.0.id", "") == "ar_01", "array object string can be read");
    expect(document.intOr("weapons.0.magazine_size", 0) == 30, "array object int can be read");
}

void testConfigRegistry() {
    const auto path = std::filesystem::temp_directory_path() / "novacore_smoke_config.json";
    {
        std::ofstream file(path);
        file << R"({ "value": 1 })";
    }

    novacore::core::ConfigRegistry registry;
    const auto initial = registry.watchJson("smoke", path);
    expect(initial.loaded, "registry loads watched json file");
    expect(registry.watchedCount() == 1, "registry tracks watched config");

    const auto* document = registry.find("smoke");
    expect(document != nullptr, "watched config is available");
    expect(document != nullptr && document->intOr("value", 0) == 1, "watched config value can be read");

    {
        std::ofstream file(path);
        file << R"({ "value": 22 })";
    }

    const auto events = registry.pollReloads();
    expect(!events.empty(), "registry emits reload event after file change");
    document = registry.find("smoke");
    expect(document != nullptr && document->intOr("value", 0) == 22, "registry reload updates document");
    std::filesystem::remove(path);
}

} // namespace

int main() {
    testWorldLifetime();
    testFixedStepAccumulator();
    testLoopbackChannel();
    testInputActions();
    testFileChangeTracker();
    testConfigDocument();
    testConfigRegistry();

    if (failures > 0) {
        std::cerr << failures << " NovaCore smoke test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "NovaCore smoke tests passed\n";
    return EXIT_SUCCESS;
}
