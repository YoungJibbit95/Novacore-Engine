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

} // namespace

int main() {
    testWorldLifetime();
    testFixedStepAccumulator();
    testLoopbackChannel();
    testInputActions();
    testFileChangeTracker();

    if (failures > 0) {
        std::cerr << failures << " NovaCore smoke test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "NovaCore smoke tests passed\n";
    return EXIT_SUCCESS;
}
