#include "novacore/render/Renderer.hpp"
#include "novacore/world/WorldJson.hpp"
#include "../../engine/src/render/CameraMath.hpp"

#if NOVACORE_HAS_SDL3
#include <SDL3/SDL.h>
#endif

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

namespace {
using namespace novacore;
using namespace novacore::world;
using math::Vec3;
using Color = std::array<float, 4>;
constexpr Color panel{0.075F, 0.085F, 0.11F, 1};
constexpr Color buttonColor{0.16F, 0.18F, 0.23F, 1};
constexpr Color accent{0.43F, 0.29F, 0.70F, 1};
constexpr Color textColor{0.91F, 0.93F, 0.98F, 1};
constexpr Color muted{0.65F, 0.69F, 0.76F, 1};
constexpr Color gridColor{0.24F, 0.28F, 0.33F, 1};
constexpr float snap = 0.25F;

struct Rect {
    float x, y, w, h;
    bool contains(float px, float py) const {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
};
struct Layout {
    float w, h, s, left, right, top, bottom;
    explicit Layout(unsigned width, unsigned height)
        : w(static_cast<float>(width)), h(static_cast<float>(height)),
          s(std::clamp(std::min(w / 1280, h / 720), 1.0F, 2.0F)),
          left(std::min(200 * s, w * 0.23F)), right(std::min(244 * s, w * 0.28F)),
          top(70 * s), bottom(36 * s) {}
    Rect viewport() const { return {left, top, w - left - right, h - top - bottom}; }
    Rect add() const { return {8*s, 36*s, 112*s, 26*s}; }
    Rect save() const { return {128*s, 36*s, 96*s, 26*s}; }
    Rect reload() const { return {232*s, 36*s, 112*s, 26*s}; }
    Rect row(std::size_t i) const { return {6*s, (108 + 24*static_cast<float>(i))*s, left - 12*s, 22*s}; }
    std::size_t capacity() const {
        return static_cast<std::size_t>(std::max(0.0F, std::floor((h - bottom - 142*s) / (24*s))));
    }
    Rect prev() const { return {6*s, h-bottom-26*s, (left-18*s)/2, 22*s}; }
    Rect next() const { auto r=prev(); r.x += r.w+6*s; return r; }
    float inspectorX() const { return w - right + 10*s; }
    Rect minus(unsigned rowIndex) const { return {w-72*s, (174+30*static_cast<float>(rowIndex))*s, 26*s, 24*s}; }
    Rect plus(unsigned rowIndex) const { auto r=minus(rowIndex); r.x+=32*s; return r; }
    Rect collision() const { return {inspectorX(), 366*s, right-20*s, 26*s}; }
};

WorldDocument example() {
    return {kWorldSchemaVersion, "editor-room", {
        {"floor", "Floor", {0,-0.15F,1}, WorldBox{{5,0.15F,5},true}},
        {"wall-a", "Left wall", {4.9F,1.4F,1}, WorldBox{{0.1F,1.4F,5},true}},
        {"wall-b", "Back wall", {0,1.4F,5.9F}, WorldBox{{5,1.4F,0.1F},true}},
        {"partition", "Partition", {-1.8F,1.2F,2}, WorldBox{{0.12F,1.2F,2},true}},
        {"crate", "Greybox crate", {1,0.5F,0.5F}, WorldBox{{0.5F,0.5F,0.5F},true}},
        {"spawn", "Spawn", {0,0,-1}, WorldMarker{"spawn"}}
    }};
}

struct Editor {
    WorldDocument doc = example();
    std::string selected;
    std::string status = "READY - GREYBOX CANDIDATE";
    std::filesystem::path source, output;
    std::size_t firstRow = 0;
    unsigned nextId = 1;
    bool dirty = false;

    WorldObject* object() {
        const auto it = std::find_if(doc.objects.begin(), doc.objects.end(),
            [&](const WorldObject& o) { return o.id == selected; });
        return it == doc.objects.end() ? nullptr : &*it;
    }
    void select(std::string id) { selected = std::move(id); }
    void change(unsigned rowIndex, float amount) {
        auto* current = object();
        if (!current) return;
        auto changed = *current;
        Vec3* v = &changed.position;
        if (rowIndex >= 3) {
            auto* box = std::get_if<WorldBox>(&changed.data);
            if (!box) return;
            v = &box->halfExtents; rowIndex -= 3;
        }
        if (rowIndex == 0) v->x += amount;
        else if (rowIndex == 1) v->y += amount;
        else if (rowIndex == 2) v->z += amount;
        else return;
        if (auto error = validateWorldDocument({kWorldSchemaVersion, doc.id, {changed}})) {
            status = *error; return;
        }
        *current = std::move(changed); dirty = true; status = "TRANSFORM CHANGED";
    }
    void toggleCollision() {
        if (auto* current = object()) if (auto* box = std::get_if<WorldBox>(&current->data)) {
            box->blocksMovement = !box->blocksMovement; dirty = true; status = "COLLISION FLAG CHANGED";
        }
    }
    void addBox() {
        if (doc.objects.size() >= kWorldObjectLimit) { status = "OBJECT LIMIT"; return; }
        std::string id;
        do { id = "box-" + std::to_string(nextId++); } while (findWorldObject(doc, id));
        doc.objects.push_back({id, "New box", {0,0.5F,2}, WorldBox{}});
        select(id); dirty = true; status = "BOX ADDED";
    }
    bool load(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) { status = "CANNOT OPEN MAP"; return false; }
        std::string data; std::array<char, 16384> buffer{};
        while (file) {
            file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto n = static_cast<std::size_t>(file.gcount());
            if (n > kWorldJsonByteLimit - data.size()) { status = "MAP TOO LARGE"; return false; }
            data.append(buffer.data(), n);
        }
        if (!file.eof() || file.bad()) { status = "MAP READ FAILED"; return false; }
        if (auto error = deserializeWorldJson(data, doc)) { status = *error; return false; }
        source = path; selected.clear(); firstRow = 0; dirty = false; status = "MAP LOADED"; return true;
    }
    void save() {
        if (output.empty()) { status = "SET --SAVE-AS PATH TO ENABLE SAVE"; return; }
        std::string data;
        if (auto error = serializeWorldJson(doc, data)) { status = *error; return; }
        // Create a private sibling staging directory without clobbering any
        // existing temporary file. Rename only a fully written, closed file.
        std::error_code ec;
        const auto parent = output.has_parent_path() ? output.parent_path() : std::filesystem::path(".");
        std::filesystem::path staging;
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        for (unsigned i = 0; i < 32; ++i) {
            auto candidate = parent / (".novacore-save-" + std::to_string(stamp) + "-" + std::to_string(i));
            if (std::filesystem::create_directory(candidate, ec)) { staging = std::move(candidate); break; }
            if (ec && ec != std::errc::file_exists) break;
        }
        if (staging.empty()) { status = "CANNOT CREATE SAVE STAGING DIRECTORY"; return; }
        const auto temp = staging / "world.json";
        bool written = false;
        {
            std::ofstream file(temp, std::ios::binary | std::ios::out);
            file.write(data.data(), static_cast<std::streamsize>(data.size()));
            file.close(); written = static_cast<bool>(file);
        }
        if (written) std::filesystem::rename(temp, output, ec);
        const bool success = written && !ec;
        std::error_code cleanup;
        std::filesystem::remove(temp, cleanup);
        std::filesystem::remove(staging, cleanup);
        if (!success) { status = "SAVE FAILED - PREVIOUS MAP NOT REMOVED"; return; }
        source = output; dirty = false; status = "MAP SAVED";
    }
    void reload() {
        if (dirty) { status = "SAVE CHANGES BEFORE RELOAD"; return; }
        if (source.empty()) { status = "NO SAVED MAP TO RELOAD"; return; }
        const auto path = source;
        (void)load(path);
    }
    bool click(const Layout& l, float x, float y) {
        if (l.add().contains(x,y)) { addBox(); return true; }
        if (l.save().contains(x,y)) { save(); return true; }
        if (l.reload().contains(x,y)) { reload(); return true; }
        if (l.prev().contains(x,y)) { firstRow = firstRow > l.capacity() ? firstRow-l.capacity() : 0; return true; }
        if (l.next().contains(x,y)) {
            if (l.capacity() && firstRow+l.capacity()<doc.objects.size()) firstRow+=l.capacity();
            return true;
        }
        for (std::size_t i=0; i<l.capacity() && firstRow+i<doc.objects.size(); ++i) {
            if (l.row(i).contains(x,y)) { select(doc.objects[firstRow+i].id); return true; }
        }
        for (unsigned i=0;i<6;++i) {
            if (l.minus(i).contains(x,y)) { change(i,-snap); return true; }
            if (l.plus(i).contains(x,y)) { change(i,snap); return true; }
        }
        if (l.collision().contains(x,y)) { toggleCollision(); return true; }
        return !l.viewport().contains(x,y);
    }
};

void rect(render::RenderFrameInfo& f, Rect r, Color c) {
    if (r.w>0 && r.h>0) f.debugRects.push_back({r.x,r.y,r.w,r.h,c});
}
std::string display(std::string_view input, std::size_t max) {
    std::string s;
    for (unsigned char c : input) {
        if (s.size() >= max) break;
        if (c >= 'a' && c <= 'z') c = static_cast<unsigned char>(c-'a'+'A');
        s.push_back(c>=32 && c<127 ? static_cast<char>(c) : '?');
    }
    return s;
}
void text(render::RenderFrameInfo& f, float x, float y, float scale, std::string_view s,
          Color color=textColor, std::size_t limit=100) {
    f.debugTexts.push_back({x,y,scale,color,display(s,limit)});
}
void button(render::RenderFrameInfo& f, Rect r, float s, std::string_view label, Color c=buttonColor) {
    rect(f,r,c);
    text(f,r.x+6*s,r.y+6*s,1.5F*s,label,textColor,
        static_cast<std::size_t>(std::max(0.0F,std::floor((r.w-10*s)/(9*s)))));
}
std::string value(float v) {
    std::array<char,32> b{};
    const auto r = std::to_chars(b.data(),b.data()+b.size(),v,std::chars_format::fixed,2);
    return r.ec==std::errc{} ? std::string(b.data(),r.ptr) : "?";
}

render::RenderFrameInfo frameFor(const Editor& e, const render::RenderCamera3D& camera, const Layout& l) {
    render::RenderFrameInfo f;
    f.clearColor={0.12F,0.15F,0.20F,1}; f.camera3D=camera;
    f.lighting.ambientIntensity=0.52F;
    for (int i=-20;i<=20;++i) {
        const float p=static_cast<float>(i);
        f.worldLines.push_back({{-20,0.012F,p},{20,0.012F,p},i==0?Color{0.52F,0.24F,0.24F,1}:gridColor});
        f.worldLines.push_back({{p,0.012F,-20},{p,0.012F,20},i==0?Color{0.24F,0.40F,0.70F,1}:gridColor});
    }
    for (const auto& o:e.doc.objects) {
        const bool selected=o.id==e.selected;
        if (const auto* b=std::get_if<WorldBox>(&o.data)) {
            f.worldBoxes.push_back({o.position,b->halfExtents,
                selected?Color{0.66F,0.45F,0.94F,1}:Color{0.56F,0.62F,0.68F,1}});
        } else if (std::holds_alternative<WorldMarker>(o.data)) {
            const Color c=selected?Color{0.66F,0.45F,0.94F,1}:Color{0.29F,0.85F,0.54F,1};
            f.worldLines.push_back({o.position+Vec3{-0.3F,0.03F,0},o.position+Vec3{0.3F,0.03F,0},c});
            f.worldLines.push_back({o.position+Vec3{0,0.03F,-0.3F},o.position+Vec3{0,0.03F,0.3F},c});
            f.worldLines.push_back({o.position,o.position+Vec3{0,0.8F,0},c});
        }
        // Assets remain unresolved references: never present a proxy as a loaded mesh.
    }
    if (const auto* o=findWorldObject(e.doc,e.selected)) {
        const auto p=o->position;
        f.worldLines.push_back({p,p+Vec3{1.4F,0,0},{0.95F,0.28F,0.28F,1}});
        f.worldLines.push_back({p,p+Vec3{0,1.4F,0},{0.28F,0.95F,0.42F,1}});
        f.worldLines.push_back({p,p+Vec3{0,0,1.4F},{0.28F,0.54F,0.95F,1}});
    }
    const float s=l.s;
    rect(f,{0,0,l.w,l.top},panel);
    rect(f,{0,l.top,l.left,l.h-l.top-l.bottom},panel);
    rect(f,{l.w-l.right,l.top,l.right,l.h-l.top-l.bottom},panel);
    rect(f,{0,l.h-l.bottom,l.w,l.bottom},panel);
    text(f,10*s,10*s,2*s,"NOVACORE / WORLD EDITOR",textColor);
    text(f,340*s,12*s,1.5F*s,e.doc.id+(e.dirty?" *":""),muted,32);
    button(f,l.add(),s,"ADD BOX",accent);
    button(f,l.save(),s,"SAVE");
    button(f,l.reload(),s,"RELOAD");
    text(f,360*s,43*s,1.5F*s,"PERSPECTIVE / GRID 0.25 M",muted);
    text(f,10*s,82*s,1.5F*s,"SCENE HIERARCHY");
    for (std::size_t i=0;i<l.capacity() && e.firstRow+i<e.doc.objects.size();++i) {
        const auto& o=e.doc.objects[e.firstRow+i];
        const auto rr=l.row(i);
        rect(f,rr,o.id==e.selected?accent:buttonColor);
        const char prefix=std::holds_alternative<WorldBox>(o.data)?'B':
            (std::holds_alternative<WorldMarker>(o.data)?'M':'A');
        text(f,rr.x+5*s,rr.y+5*s,1.5F*s,std::string(1,prefix)+" "+o.name,textColor,
            static_cast<std::size_t>(std::max(0.0F,std::floor((rr.w-10*s)/(9*s)))));
    }
    button(f,l.prev(),s,"PREV"); button(f,l.next(),s,"NEXT");
    const float ix=l.inspectorX();
    text(f,ix,82*s,1.5F*s,"INSPECTOR");
    if (const auto* o=findWorldObject(e.doc,e.selected)) {
        text(f,ix,108*s,1.5F*s,o->name,textColor,23);
        text(f,ix,130*s,1.0F*s,o->id,muted,32);
        text(f,ix,152*s,1.5F*s,"POSITION / HALF SIZE");
        std::array<float,6> v{o->position.x,o->position.y,o->position.z,0,0,0};
        const auto* box=std::get_if<WorldBox>(&o->data);
        if (box) { v[3]=box->halfExtents.x; v[4]=box->halfExtents.y; v[5]=box->halfExtents.z; }
        constexpr std::array<std::string_view,6> names{"X","Y","Z","HX","HY","HZ"};
        for (unsigned i=0;i<(box?6U:3U);++i) {
            text(f,ix,(180+30*static_cast<float>(i))*s,1.5F*s,
                std::string(names[i])+" "+value(v[i]),textColor,18);
            button(f,l.minus(i),s,"-"); button(f,l.plus(i),s,"+");
        }
        if (box) button(f,l.collision(),s,box->blocksMovement?"COLLISION ON":"COLLISION OFF");
        else if (const auto* a=std::get_if<WorldAssetReference>(&o->data)) {
            text(f,ix,300*s,1.0F*s,a->assetId,muted,32);
            text(f,ix,322*s,1.0F*s,"ASSET NOT RESOLVED",muted);
        } else text(f,ix,300*s,1.0F*s,"GENERIC MARKER",muted);
    } else text(f,ix,112*s,1.5F*s,"SELECT AN OBJECT",muted);
    text(f,8*s,l.h-l.bottom+5*s,1.0F*s,e.status,textColor,
        static_cast<std::size_t>(std::max(0.0F,std::floor((l.w-16*s)/(6*s)))));
    text(f,8*s,l.h-l.bottom+20*s,1.0F*s,"RMB LOOK + WASD QE FLY | LMB SELECT | SAVE-AS VIA CLI | NO GAME LOADER YET",muted);
    return f;
}

// Use exactly the renderer's Vulkan framebuffer ray basis, including its
// right-handed camera convention and downward framebuffer Y.
std::optional<WorldBoxHit> pick(const WorldDocument& doc,const render::RenderCamera3D& c,
                              unsigned w,unsigned h,float x,float y) {
    if (!w || !h) return std::nullopt;
    const auto basis=render::detail::skyRayBasis(c,w,h);
    const auto ray=basis.forward+basis.right*(2*x/static_cast<float>(w)-1)+
        basis.down*(2*y/static_cast<float>(h)-1);
    const auto projection=render::detail::cameraProjection(c,w,h);
    const double len=math::length(ray);
    return pickWorldBox(doc,c.position+ray*projection.nearPlane,ray,
        len*(projection.farPlane-projection.nearPlane));
}

struct NativeInput {
    bool focused=false, minimized=false, left=false, right=false;
    unsigned width=0,height=0,pixelWidth=0,pixelHeight=0;
    float mouseX=0,mouseY=0,forward=0,strafe=0,vertical=0;
};
NativeInput nativeInput(platform::Window& window) {
    NativeInput n;
#if NOVACORE_HAS_SDL3
    auto* handle=static_cast<SDL_Window*>(window.nativeHandle());
    if (!handle) return n;
    const auto flags=SDL_GetWindowFlags(handle);
    n.focused=(flags&SDL_WINDOW_INPUT_FOCUS)!=0;
    n.minimized=(flags&SDL_WINDOW_MINIMIZED)!=0;
    int w=0,h=0,pw=0,ph=0;
    if (!SDL_GetWindowSize(handle,&w,&h) || !SDL_GetWindowSizeInPixels(handle,&pw,&ph)) return n;
    n.width=static_cast<unsigned>(std::max(w,0)); n.height=static_cast<unsigned>(std::max(h,0));
    n.pixelWidth=static_cast<unsigned>(std::max(pw,0)); n.pixelHeight=static_cast<unsigned>(std::max(ph,0));
    const auto mouse=SDL_GetMouseState(&n.mouseX,&n.mouseY);
    n.left=(mouse&SDL_BUTTON_LMASK)!=0; n.right=(mouse&SDL_BUTTON_RMASK)!=0;
    const auto* keys=SDL_GetKeyboardState(nullptr);
    n.forward=static_cast<float>(keys[SDL_SCANCODE_W])-static_cast<float>(keys[SDL_SCANCODE_S]);
    n.strafe=static_cast<float>(keys[SDL_SCANCODE_D])-static_cast<float>(keys[SDL_SCANCODE_A]);
    n.vertical=static_cast<float>(keys[SDL_SCANCODE_E])-static_cast<float>(keys[SDL_SCANCODE_Q]);
#else
    (void)window;
#endif
    return n;
}

int selfTest() {
    Editor e;
    if (validateWorldDocument(e.doc)) return 1;
    const Layout l(1280,720);
    e.click(l,l.add().x+2,l.add().y+2);
    if (e.doc.objects.size()!=7 || e.selected!="box-1" || !e.dirty) return 2;
    e.change(0,snap);
    if (!e.object() || e.object()->position.x!=snap) return 3;
    e.change(3,-10);
    if (std::get<WorldBox>(e.object()->data).halfExtents.x!=0.5F) return 4;
    e.toggleCollision();
    if (std::get<WorldBox>(e.object()->data).blocksMovement) return 5;
    std::string json;
    WorldDocument decoded;
    if (serializeWorldJson(e.doc,json) || deserializeWorldJson(json,decoded) ||
        !findWorldObject(decoded,e.selected)) return 6;
    render::RenderCamera3D c;
    c.enabled=true; c.position={};
    WorldDocument d{kWorldSchemaVersion,"ray",{{"center","Center",{0,0,5},WorldBox{}},
        {"right","Right",{-3,0,5},WorldBox{}}}};
    auto hit=pick(d,c,1280,720,640,360);
    if (!hit || hit->objectId!="center") return 7;
    const auto f=frameFor(e,c,l);
    if (f.worldBoxes.size()!=6 || f.worldLines.size()<82 || f.debugRects.empty() || f.debugTexts.empty()) return 8;
    if (l.viewport().contains(l.add().x,l.add().y) || !l.viewport().contains(640,360)) return 9;
    std::cout<<"World editor CPU scene/UI contract passed (no Vulkan execution)\n";
    return 0;
}

int run(Editor& editor) {
    platform::Window window;
    render::Renderer renderer;
    if (!window.create({"NovaCore World Editor - Candidate",1280,800,true}) || window.isHeadless() ||
        !renderer.create(window,{.preferVulkan=true,.requireVulkan=true}) ||
        renderer.backendName()!="vulkan-world") {
        std::cerr<<"A real SDL3/Vulkan backend is required. No fallback editor.\n"; return 1;
    }
#if NOVACORE_HAS_SDL3
    (void)SDL_SetWindowMinimumSize(static_cast<SDL_Window*>(window.nativeHandle()),960,540);
#endif
    platform::InputSystem input;
    render::RenderCamera3D camera;
    camera.enabled=true; camera.position={8,7,-11}; camera.yawDegrees=-36; camera.pitchDegrees=-25;
    camera.verticalFovDegrees=65; camera.farPlane=250;
    auto shownCamera=camera;
    unsigned shownW=0,shownH=0;
    bool focused=false,left=false,right=false,navigating=false;
    auto previous=std::chrono::steady_clock::now();
    while (!window.shouldClose()) {
        window.pollEvents(input);
        if (window.shouldClose()) break;
        const auto now=std::chrono::steady_clock::now();
        const float dt=std::clamp(std::chrono::duration<float>(now-previous).count(),0.0F,0.05F);
        previous=now;
        const auto n=nativeInput(window);
        if (!n.focused || !n.right) {
            if (navigating) (void)window.setRelativeMouseMode(false);
            navigating=false;
        }
        if (n.minimized || !n.width || !n.height || !n.pixelWidth || !n.pixelHeight) {
            focused=false; left=n.left; right=n.right;
            std::this_thread::sleep_for(std::chrono::milliseconds(20)); continue;
        }
        const Layout l(n.pixelWidth,n.pixelHeight);
        const float mx=n.mouseX*static_cast<float>(n.pixelWidth)/static_cast<float>(n.width);
        const float my=n.mouseY*static_cast<float>(n.pixelHeight)/static_cast<float>(n.height);
        const bool matching=shownW==n.pixelWidth && shownH==n.pixelHeight;
        if (focused && n.focused && matching && n.right && !right && l.viewport().contains(mx,my))
            navigating=window.setRelativeMouseMode(true);
        if (focused && n.focused && matching && n.left && !left && !n.right && !navigating) {
            if (!editor.click(l,mx,my)) {
                auto hit=pick(editor.doc,shownCamera,shownW,shownH,mx,my);
                editor.select(hit?hit->objectId:std::string{});
            }
        }
        if (navigating && n.focused) {
            const auto& snapInput=window.inputSnapshot();
            if (right) {
                camera.yawDegrees=std::remainder(camera.yawDegrees-
                    snapInput.axisValue({platform::InputControlKind::MouseAxis,0})*0.13F,360.0F);
                camera.pitchDegrees=std::clamp(camera.pitchDegrees-
                    snapInput.axisValue({platform::InputControlKind::MouseAxis,1})*0.13F,-89.0F,89.0F);
            }
            const auto axes=render::detail::cameraFrame(camera);
            auto move=axes.forward*n.forward+axes.right*n.strafe+Vec3{0,n.vertical,0};
            const float len=math::length(move);
            if (len>0) camera.position=camera.position+move*(6*dt/std::max(1.0F,len));
        }
        left=n.left; right=n.right; focused=n.focused;
        const auto before=renderer.backendFrameStats().submittedFrames;
        renderer.beginFrame(frameFor(editor,camera,l)); renderer.endFrame();
        const auto stats=renderer.backendFrameStats();
        if (stats.submittedFrames>before) {
            shownCamera=camera; shownW=stats.swapchainWidth; shownH=stats.swapchainHeight;
        }
        if (renderer.backendName()!="vulkan-world") { std::cerr<<"Vulkan backend lost\n"; return 2; }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (editor.dirty) std::cerr<<"Unsaved document changes; no automatic overwrite performed.\n";
    if (navigating) (void)window.setRelativeMouseMode(false);
    return 0;
}
} // namespace

int main(int argc,char** argv) {
    if (argc==2 && std::string_view(argv[1])=="--self-test") return selfTest();
    if (argc==2 && std::string_view(argv[1])=="--help") {
        std::cout<<"novacore_world_editor [--map FILE] [--save-as FILE]\n"
            <<"SDL3/Vulkan required. RMB look/fly, LMB select, inspector +/- moves or resizes.\n"
            <<"--save-as explicitly authorizes replacing that file on Save. --self-test is CPU-only.\n";
        return 0;
    }
    Editor editor;
    std::filesystem::path map;
    for (int i=1;i<argc;++i) {
        const std::string_view arg=argv[i];
        if ((arg=="--map" || arg=="--save-as") && i+1<argc) {
            if (arg=="--map") map=argv[++i]; else editor.output=argv[++i];
        } else { std::cerr<<"Unknown or incomplete argument: "<<arg<<'\n'; return 2; }
    }
    if (!map.empty() && !editor.load(map)) { std::cerr<<editor.status<<'\n'; return 3; }
    return run(editor);
}
