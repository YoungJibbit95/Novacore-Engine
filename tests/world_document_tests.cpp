#include "novacore/world/WorldDocument.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace novacore::world;
namespace {
int checks = 0;
void require(bool condition, const char* message) {
    ++checks;
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
WorldDocument example() {
    return {kWorldSchemaVersion, "test-map", {
        {"far", "Far wall", {5, 0, 0}, WorldBox{}},
        {"near", "Near wall", {2, 0, 0}, WorldBox{}},
        {"prop", "Prop", {1, 0, 0}, WorldAssetReference{"props.crate"}},
        {"spawn", "Spawn", {0, 0, 0}, WorldMarker{"spawn"}}
    }};
}
}
int main() {
    const auto original = example();
    require(!validateWorldDocument(original), "valid document");
    require(findWorldObject(original, "near") != nullptr, "find stable ID");
    require(findWorldObject(original, "missing") == nullptr, "missing ID");
    auto doc = original;
    doc.objects[0].id = "near";
    require(validateWorldDocument(doc).has_value(), "duplicate ID");
    doc = original; doc.schemaVersion = 2;
    require(validateWorldDocument(doc).has_value(), "unknown version");
    doc = original; doc.id = "../bad";
    require(validateWorldDocument(doc).has_value(), "unsafe map ID");
    doc = original; doc.objects[0].position.x = std::numeric_limits<float>::infinity();
    require(validateWorldDocument(doc).has_value(), "infinite position");
    doc = original; doc.objects[0].position.x = kWorldCoordinateLimit;
    require(validateWorldDocument(doc).has_value(), "bounds extend outside world");
    doc = original; std::get<WorldBox>(doc.objects[0].data).halfExtents.y = 0;
    require(validateWorldDocument(doc).has_value(), "zero box dimension");
    doc = original; std::get<WorldBox>(doc.objects[0].data).halfExtents.z = std::numeric_limits<float>::quiet_NaN();
    require(validateWorldDocument(doc).has_value(), "nonfinite box dimension");
    doc = original; std::get<WorldAssetReference>(doc.objects[2].data).assetId = "../crate.glb";
    require(validateWorldDocument(doc).has_value(), "asset path not ID");
    doc = original; std::get<WorldMarker>(doc.objects[3].data).type.clear();
    require(validateWorldDocument(doc).has_value(), "empty marker type");
    doc = original; doc.objects[0].name = "bad\nname";
    require(validateWorldDocument(doc).has_value(), "control characters in name");
    doc = original; doc.objects.resize(kWorldObjectLimit + 1);
    require(validateWorldDocument(doc).has_value(), "object count limit");
    auto hit = pickWorldBox(original, {}, {10, 0, 0}, 10);
    require(hit && hit->objectId == "near" && hit->distance == 1.5, "nearest normalized hit ignores markers/assets");
    require(!pickWorldBox(original, {}, {1, 0, 0}, 1.49), "max distance");
    require(!pickWorldBox(original, {}, {-1, 0, 0}, 10), "behind ray");
    require(!pickWorldBox(original, {0, 1, 0}, {1, 0, 0}, 10), "parallel miss");
    require(!pickWorldBox(original, {}, {}, 10), "zero direction");
    require(!pickWorldBox(original, {}, {1, 0, 0}, -1), "negative range");
    require(!pickWorldBox(original, {}, {1, 0, 0}, std::numeric_limits<double>::infinity()), "infinite range");
    hit = pickWorldBox(original, {2, 0, 0}, {1, 0, 0}, 10);
    require(hit && hit->objectId == "near" && hit->distance == 0, "inside box");
    doc = original; doc.objects[0].position = doc.objects[1].position; doc.objects[0].id = "a";
    hit = pickWorldBox(doc, {}, {1, 0, 0}, 10);
    require(hit && hit->objectId == "a", "tie resolved by ID");
    std::reverse(doc.objects.begin(), doc.objects.end());
    hit = pickWorldBox(doc, {}, {1, 0, 0}, 10);
    require(hit && hit->objectId == "a", "tie independent of object order");
    doc = original; doc.objects[1].position.x = 9; doc.objects[1].name = "Renamed";
    hit = pickWorldBox(doc, {}, {1, 0, 0}, 10);
    require(hit && hit->objectId == "far", "transform affects picking");
    require(findWorldObject(doc, "near")->name == "Renamed", "rename retains ID");
    std::cout << "World document: " << checks << " checks passed\n";
}
