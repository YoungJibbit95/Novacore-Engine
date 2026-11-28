#include "novacore/world/WorldJson.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>

using namespace novacore::world;
namespace {
int checks = 0;
void require(bool value, const char* message) {
    ++checks;
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
const std::string empty = R"({"schemaVersion":1,"id":"map","objects":[]})";
std::string box(std::string_view pos = "[0,1,2]") {
    return R"({"id":"box","name":"Wall","kind":"box","position":)" + std::string(pos) +
        R"(,"halfExtents":[1,2,3],"blocksMovement":true})";
}
std::string map(std::string_view objects) {
    return R"({"schemaVersion":1,"id":"map","objects":[)" + std::string(objects) + "]}";
}
}
int main() {
    WorldDocument doc{kWorldSchemaVersion, "room", {
        {"floor", "Floor \\\"A\"", {0, -0.25F, 0}, WorldBox{{3, 0.25F, 4}, true}},
        {"asset", "B\xC3\xBCro", {1.2345678F, 0, -2}, WorldAssetReference{"prop_table"}},
        {"spawn", "Spawn", {0, 0, 1}, WorldMarker{"spawn"}}
    }};
    std::string json, again;
    require(!serializeWorldJson(doc, json), "encode all variants");
    require(!serializeWorldJson(doc, again) && json == again, "deterministic encode");
    WorldDocument decoded;
    require(!deserializeWorldJson(json, decoded), "decode");
    require(decoded.id == doc.id && decoded.objects.size() == 3, "world identity");
    require(decoded.objects[0].id == "floor" && decoded.objects[0].name == doc.objects[0].name, "escaped name and ID");
    require(decoded.objects[1].position.x == doc.objects[1].position.x && decoded.objects[1].name == doc.objects[1].name, "float and UTF-8 fidelity");
    require(std::get<WorldAssetReference>(decoded.objects[1].data).assetId == "prop_table", "asset ID");
    require(std::get<WorldMarker>(decoded.objects[2].data).type == "spawn", "marker type");
    require(std::get<WorldBox>(decoded.objects[0].data).halfExtents.z == 4 && std::get<WorldBox>(decoded.objects[0].data).blocksMovement, "box fields");
    require(!serializeWorldJson(decoded, again) && again == json, "byte-identical round trip");
    require(!deserializeWorldJson(R"( {"objects":[],"id":"map","schemaVersion":1} )", decoded), "reordered fields");
    require(!deserializeWorldJson(map(R"({"id":"m","name":"\u00fc\ud83d\ude80","position":[0,0,0],"kind":"marker","markerType":"spawn"})"), decoded), "Unicode escapes");
    require(decoded.objects[0].name == "\xC3\xBC\xF0\x9F\x9A\x80", "surrogate pair decoded");
    require(!deserializeWorldJson(map(box()), decoded), "box read");
    const auto checkBad = [&](std::string_view text) {
        require(deserializeWorldJson(text, decoded).has_value(), "reject malformed document");
        require(decoded.id == "map" && decoded.objects.size() == 1 && decoded.objects[0].id == "box", "transactional load failure");
    };
    for (const auto* bad : {"", "[]", "{}", "null", R"({"schemaVersion":2,"id":"map","objects":[]})",
        R"({"schemaVersion":"1","id":"map","objects":[]})", R"({"schemaVersion":1,"id":"map","objects":[],"id":"other"})",
        R"({"schemaVersion":1,"id":"map","objects":[],"extra":{}})", R"({"schemaVersion":1,"id":"map","objects":{},})",
        R"({"schemaVersion":1,"id":"map","objects":[]}) trailing", R"({"schemaVersion":1,"id":"map","objects":[] ,})",
        R"({"schemaVersion":1,"id":"map","objects":[null]})", R"({"schemaVersion":1,"id":"map","objects":[[[[]]]]})"}) checkBad(bad);
    for (const auto* pos : {"[01,0,0]", "[1.,0,0]", "[1e,0,0]", "[1e99,0,0]", "[NaN,0,0]", "[\"1\",0,0]", "[1,2]", "[1,2,3,4]", "[10001,0,0]"}) checkBad(map(box(pos)));
    checkBad(map(box() + "," + box()));
    for (const auto* object : {
        R"({"id":"m","name":"\ud800","position":[0,0,0],"kind":"marker","markerType":"spawn"})",
        R"({"id":"m","name":"\udc00","position":[0,0,0],"kind":"marker","markerType":"spawn"})",
        R"({"id":"m","name":"\u0000","position":[0,0,0],"kind":"marker","markerType":"spawn"})",
        R"({"id":"m","name":"\x","position":[0,0,0],"kind":"marker","markerType":"spawn"})",
        R"({"id":"m","name":"x","position":[0,0,0],"kind":"marker","markerType":"spawn","assetId":"x"})",
        R"({"id":"m","name":"x","position":[0,0,0],"kind":"marker","markerType":"spawn","\u0069d":"m"})",
        R"({"id":"b","name":"x","position":[0,0,0],"kind":"box","halfExtents":[1,1,1],"blocksMovement":"true"})"}) checkBad(map(object));
    checkBad(map("{\"id\":\"m\",\"name\":\"" + std::string(257, 'x') + "\",\"position\":[0,0,0],\"kind\":\"marker\",\"markerType\":\"spawn\"}"));
    checkBad(map("{\"id\":\"m\",\"name\":\"\xC0\x80\",\"position\":[0,0,0],\"kind\":\"marker\",\"markerType\":\"spawn\"}"));
    checkBad(std::string(kWorldJsonByteLimit + 1, ' '));
    std::string tooMany = R"({"schemaVersion":1,"id":"map","objects":[)";
    for (std::size_t i = 0; i <= kWorldObjectLimit; ++i) {
        if (i) tooMany += ',';
        tooMany += "{\"id\":\"b" + std::to_string(i) + R"(","name":"B","position":[0,0,0],"kind":"marker","markerType":"spawn"})";
    }
    tooMany += "]}";
    checkBad(tooMany);
    auto invalid = doc; invalid.objects[0].name = "\xFF";
    again = "unchanged";
    require(serializeWorldJson(invalid, again).has_value() && again == "unchanged", "reject invalid UTF-8 on save");
    invalid = doc; invalid.objects[0].position.x = std::numeric_limits<float>::infinity();
    require(serializeWorldJson(invalid, again).has_value() && again == "unchanged", "reject invalid geometry on save");
    require(!deserializeWorldJson(empty, decoded) && decoded.objects.empty(), "empty document");
    std::cout << "World JSON: " << checks << " checks passed\n";
}
