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
#include <cstdint>
#include <limits>
#include <vector>
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
    Rect duplicate() const { return {352*s,36*s,72*s,26*s}; }
    Rect erase() const { return {432*s,36*s,72*s,26*s}; }
    Rect undo() const { return {512*s,36*s,72*s,26*s}; }
    Rect redo() const { return {592*s,36*s,72*s,26*s}; }
    Rect numeric(unsigned rowIndex) const {
        return {inspectorX(),(174+30*static_cast<float>(rowIndex))*s,
            w-78*s-inspectorX(),24*s};
    }
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

std::string value(float v);

struct Edit {
    std::size_t index{};
    std::optional<WorldObject> before, after;
    std::string beforeSelection, afterSelection;
    std::uint64_t beforeRevision{}, afterRevision{};
};
struct AxisDrag {
    WorldObject original;
    unsigned axis{};
    float mouseX{},mouseY{},dx{},dy{},metres{};
};

struct Editor {
    WorldDocument doc = example();
    std::string selected;
    std::string status = "READY - GREYBOX CANDIDATE";
    std::filesystem::path source, output;
    std::size_t firstRow = 0;
    unsigned nextId = 1;
    bool dirty = false;
    static constexpr std::size_t historyLimit=128;
    std::vector<Edit> history;
    std::size_t cursor=0;
    std::uint64_t revision=0,savedRevision=0,nextRevision=1;
    std::optional<unsigned> numericRow;
    std::string numericText;
    bool replaceNumeric=false;
    std::optional<AxisDrag> drag;


    WorldObject* object() {
        const auto it = std::find_if(doc.objects.begin(), doc.objects.end(),
            [&](const WorldObject& o) { return o.id == selected; });
        return it == doc.objects.end() ? nullptr : &*it;
    }
    void select(std::string id) {
        numericRow.reset(); selected=std::move(id);
    }
    void refreshDirty() { dirty=revision!=savedRevision; }
    void apply(const Edit& edit,bool forward) {
        const auto& from=forward?edit.before:edit.after;
        const auto& to=forward?edit.after:edit.before;
        auto it=doc.objects.begin()+static_cast<std::ptrdiff_t>(edit.index);
        if (from && to) *it=*to;
        else if (from) doc.objects.erase(it);
        else if (to) doc.objects.insert(it,*to);
        select(forward?edit.afterSelection:edit.beforeSelection);
        revision=forward?edit.afterRevision:edit.beforeRevision;
        if (firstRow>=doc.objects.size()) firstRow=0;
        refreshDirty();
    }
    void commit(std::size_t index,std::optional<WorldObject> before,
                std::optional<WorldObject> after,std::string selection,std::string message) {
        history.erase(history.begin()+static_cast<std::ptrdiff_t>(cursor),history.end());
        Edit edit{index,std::move(before),std::move(after),selected,std::move(selection),revision,nextRevision++};
        apply(edit,true);
        history.push_back(std::move(edit));
        if (history.size()>historyLimit) history.erase(history.begin());
        cursor=history.size();
        status=std::move(message);
    }
    void undo() {
        if (!cursor) { status="NOTHING TO UNDO"; return; }
        apply(history[--cursor],false); status="UNDONE";
    }
    void redo() {
        if (cursor>=history.size()) { status="NOTHING TO REDO"; return; }
        apply(history[cursor++],true); status="REDONE";
    }
    static float* component(WorldObject& object,unsigned row) {
        Vec3* v=&object.position;
        if (row>=3) {
            auto* box=std::get_if<WorldBox>(&object.data);
            if (!box) return nullptr;
            v=&box->halfExtents; row-=3;
        }
        if (row==0) return &v->x;
        if (row==1) return &v->y;
        if (row==2) return &v->z;
        return nullptr;
    }
    bool valid(const WorldObject& changed) {
        if (auto error=validateWorldDocument({kWorldSchemaVersion,doc.id,{changed}})) {
            status=*error; return false;
        }
        return true;
    }
    void setValue(unsigned row,float amount) {
        auto* current=object();
        if (!current) return;
        auto changed=*current;
        auto* v=component(changed,row);
        if (!v || *v==amount) return;
        *v=amount;
        if (!valid(changed)) return;
        commit(static_cast<std::size_t>(current-doc.objects.data()),*current,changed,selected,"TRANSFORM CHANGED");
    }
    void change(unsigned row,float amount) {
        if (auto* current=object()) if (auto* v=component(*current,row)) setValue(row,*v+amount);
    }
    void beginNumeric(unsigned row) {
        if (auto* current=object()) if (auto* v=component(*current,row)) {
            numericRow=row; numericText=value(*v); replaceNumeric=true; status="TYPE VALUE / ENTER APPLY / ESC CANCEL";
        }
    }
    void numericInput(const platform::InputSnapshot& input) {
        if (!numericRow) return;
        auto pressed=[&](std::uint16_t key){return input.wasPressed({platform::InputControlKind::KeyboardKey,key});};
        if (pressed(27)) { numericRow.reset(); status="VALUE EDIT CANCELLED"; return; }
        if (pressed(13)) {
            float parsed=0;
            const auto result=std::from_chars(numericText.data(),numericText.data()+numericText.size(),parsed);
            if (result.ec!=std::errc{} || result.ptr!=numericText.data()+numericText.size() || !std::isfinite(parsed)) {
                status="INVALID NUMBER"; return;
            }
            const auto row=*numericRow;
            numericRow.reset(); setValue(row,parsed); return;
        }
        if (pressed(8)) {
            if (replaceNumeric) numericText.clear();
            else if (!numericText.empty()) numericText.pop_back();
            replaceNumeric=false;
        }
        for (char c:std::string_view("0123456789.-E")) if (pressed(static_cast<std::uint16_t>(c))) {
            if (replaceNumeric) numericText.clear();
            replaceNumeric=false;
            if (numericText.size()<24) numericText.push_back(c);
        }
    }
    void toggleCollision() {
        if (auto* current=object()) {
            auto changed=*current;
            if (auto* box=std::get_if<WorldBox>(&changed.data)) {
                box->blocksMovement=!box->blocksMovement;
                commit(static_cast<std::size_t>(current-doc.objects.data()),*current,changed,selected,"COLLISION FLAG CHANGED");
            }
        }
    }
    std::string newId() {
        std::string id;
        do { id="box-"+std::to_string(nextId++); } while (findWorldObject(doc,id));
        return id;
    }
    void addBox() {
        if (doc.objects.size()>=kWorldObjectLimit) { status="OBJECT LIMIT"; return; }
        const auto id=newId();
        commit(doc.objects.size(),{},WorldObject{id,"New box",{0,0.5F,2},WorldBox{}},id,"BOX ADDED");
    }
    void duplicateObject() {
        const auto* current=object();
        if (!current) { status="SELECT AN OBJECT"; return; }
        if (doc.objects.size()>=kWorldObjectLimit) { status="OBJECT LIMIT"; return; }
        auto copy=*current;
        copy.id=newId();
        copy.position.x+=snap;
        if (!valid(copy)) return;
        const auto id=copy.id;
        commit(doc.objects.size(),{},std::move(copy),id,"OBJECT DUPLICATED");
    }
    void deleteObject() {
        if (auto* current=object())
            commit(static_cast<std::size_t>(current-doc.objects.data()),*current,{},{},"OBJECT DELETED");
    }
    void beginDrag(unsigned axis,float mx,float my,float dx,float dy,float metres) {
        if (auto* current=object()) {
            numericRow.reset(); drag=AxisDrag{*current,axis,mx,my,dx,dy,metres};
        }
    }
    void updateDrag(float mx,float my) {
        if (!drag) return;
        auto* current=object();
        if (!current) return;
        const auto& d=*drag;
        const float length2=d.dx*d.dx+d.dy*d.dy;
        if (length2<1) return;
        const float delta=std::round(((mx-d.mouseX)*d.dx+(my-d.mouseY)*d.dy)/length2*d.metres/snap)*snap;
        auto changed=d.original;
        *component(changed,d.axis)+=delta;
        if (valid(changed)) { *current=std::move(changed); status="TRANSLATE / RELEASE TO APPLY"; }
    }
    void finishDrag(bool cancel) {
        if (!drag) return;
        auto original=std::move(drag->original); drag.reset();
        auto* current=object();
        if (!current) return;
        auto changed=*current;
        *current=original;
        if (cancel) { status="TRANSLATION CANCELLED"; return; }
        if (math::length(changed.position-original.position)>0)
            commit(static_cast<std::size_t>(current-doc.objects.data()),std::move(original),std::move(changed),selected,"TRANSLATED");
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
        source=path; select({}); firstRow=0; history.clear(); cursor=0;
        revision=savedRevision=0; nextRevision=1; refreshDirty(); status="MAP LOADED"; return true;
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
        source=output; savedRevision=revision; refreshDirty(); status="MAP SAVED";
    }
    void reload() {
        if (dirty) { status = "SAVE CHANGES BEFORE RELOAD"; return; }
        if (source.empty()) { status = "NO SAVED MAP TO RELOAD"; return; }
        const auto path = source;
        (void)load(path);
    }
    bool click(const Layout& l, float x, float y) {
        numericRow.reset();
        if (l.add().contains(x,y)) { addBox(); return true; }
        if (l.duplicate().contains(x,y)) { duplicateObject(); return true; }
        if (l.erase().contains(x,y)) { deleteObject(); return true; }
        if (l.undo().contains(x,y)) { undo(); return true; }
        if (l.redo().contains(x,y)) { redo(); return true; }
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
            if (l.numeric(i).contains(x,y)) { beginNumeric(i); return true; }
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
    const auto r = std::to_chars(b.data(),b.data()+b.size(),v,std::chars_format::general,std::numeric_limits<float>::max_digits10);
    return r.ec==std::errc{} ? std::string(b.data(),r.ptr) : "?";
}

struct AxisHandle {
    unsigned axis{};
    float x{},y{},dx{},dy{},metres{};
    Color color{};
};
std::vector<AxisHandle> axisHandles(const Editor& e,const render::RenderCamera3D& camera,const Layout& l) {
    std::vector<AxisHandle> result;
    const auto* object=findWorldObject(e.doc,e.selected);
    if (!object || !l.w || !l.h) return result;
    const auto frame=render::detail::cameraFrame(camera);
    const auto proj=render::detail::cameraProjection(camera,static_cast<unsigned>(l.w),static_cast<unsigned>(l.h));
    auto dot=[](Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;};
    auto project=[&](Vec3 p)->std::optional<math::Vec2>{
        const auto d=p-camera.position;
        const auto z=dot(d,frame.forward);
        if (z<=proj.nearPlane || z>=proj.farPlane) return {};
        return math::Vec2{l.w*(0.5F+0.5F*dot(d,frame.right)/(z*proj.horizontalRayScale)),
                          l.h*(0.5F-0.5F*dot(d,frame.up)/(z*proj.verticalRayScale))};
    };
    const auto start=project(object->position);
    if (!start || !l.viewport().contains(start->x,start->y)) return result;
    const float metres=std::clamp(dot(object->position-camera.position,frame.forward)*proj.verticalRayScale*150*l.s/l.h,0.2F,50.0F);
    constexpr std::array<Vec3,3> axes{{{1,0,0},{0,1,0},{0,0,1}}};
    constexpr std::array<Color,3> colors{{{0.95F,0.28F,0.28F,1},{0.28F,0.95F,0.42F,1},{0.28F,0.54F,0.95F,1}}};
    for (unsigned i=0;i<3;++i) if (const auto end=project(object->position+axes[i]*metres)) {
        const float dx=end->x-start->x,dy=end->y-start->y;
        if (l.viewport().contains(end->x,end->y) && dx*dx+dy*dy>144*l.s*l.s)
            result.push_back({i,start->x,start->y,dx,dy,metres,colors[i]});
    }
    return result;
}
bool hitAxis(Editor& e,const render::RenderCamera3D& c,const Layout& l,float x,float y) {
    for (const auto& a:axisHandles(e,c,l)) {
        const float dx=x-a.x-a.dx,dy=y-a.y-a.dy;
        if (dx*dx+dy*dy<=100*l.s*l.s) {
            e.beginDrag(a.axis,x,y,a.dx,a.dy,a.metres); return true;
        }
    }
    return false;
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
    // Screen-space handles use the same camera projection as world picking.
    for (const auto& a:axisHandles(e,camera,l)) {
        f.debugLines.push_back({a.x,a.y,a.x+a.dx,a.y+a.dy,a.color});
        rect(f,{a.x+a.dx-4*l.s,a.y+a.dy-4*l.s,8*l.s,8*l.s},a.color);
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
    button(f,l.duplicate(),s,"DUP");
    button(f,l.erase(),s,"DEL");
    button(f,l.undo(),s,"UNDO",e.cursor?buttonColor:panel);
    button(f,l.redo(),s,"REDO",e.cursor<e.history.size()?buttonColor:panel);
    text(f,l.w-245*s,12*s,1.0F*s,"GRID 1 M / SNAP 0.25 M",muted);
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
            const bool editing=e.numericRow && *e.numericRow==i;
            button(f,l.numeric(i),s,std::string(names[i])+" "+(editing?e.numericText:value(v[i])),
                editing?accent:buttonColor);
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
    text(f,8*s,l.h-l.bottom+20*s,1.0F*s,"RMB FLY | LMB SELECT / DRAG AXIS TIP | CLICK VALUE TO TYPE | SAVE-AS VIA CLI",muted);
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
    const auto projection=render::detail::cameraProjection(c,1280,720);
    const float rightPixel=640.0F*(1.0F+0.6F/projection.horizontalRayScale);
    hit=pick(d,c,1280,720,rightPixel,360);
    if (!hit || hit->objectId!="right") return 10;
    if (pick(d,c,1280,720,1280-rightPixel,360)) return 11;
    const auto f=frameFor(e,c,l);
    if (f.worldBoxes.size()!=6 || f.worldLines.size()<82 || f.debugRects.empty() || f.debugTexts.empty()) return 8;
    if (l.viewport().contains(l.add().x,l.add().y) || !l.viewport().contains(640,360)) return 9;
    // Delta history retains IDs, ordering and the saved revision marker.
    const auto original=e.doc.objects.size();
    e.savedRevision=e.revision; e.refreshDirty();
    e.duplicateObject();
    if (e.doc.objects.size()!=original+1 || e.selected=="box-1" || !e.dirty) return 12;
    const auto copied=e.selected;
    e.deleteObject();
    if (findWorldObject(e.doc,copied)) return 13;
    e.undo();
    if (e.selected!=copied || !findWorldObject(e.doc,copied)) return 14;
    e.undo();
    if (e.dirty || e.selected!="box-1") return 15;
    e.redo(); e.redo();
    if (e.doc.objects.size()!=original || !e.dirty) return 16;
    e.undo(); e.select("box-1"); e.setValue(0,1.125F);
    if (!e.object() || e.object()->position.x!=1.125F || e.cursor!=e.history.size()) return 17;
    e.beginDrag(0,10,10,100,0,2);
    e.updateDrag(60,10); e.finishDrag(false);
    if (e.object()->position.x!=2.125F) return 18;
    e.undo();
    if (e.object()->position.x!=1.125F) return 19;
    e.beginDrag(0,10,10,100,0,2); e.updateDrag(60,10); e.finishDrag(true);
    if (e.object()->position.x!=1.125F) return 20;
    e.beginNumeric(0);
    platform::InputSnapshot keys;
    keys.setButton({platform::InputControlKind::KeyboardKey,'2'},true,platform::InputDeviceKind::KeyboardMouse);
    e.numericInput(keys); keys.beginFrame();
    keys.setButton({platform::InputControlKind::KeyboardKey,13},true,platform::InputDeviceKind::KeyboardMouse);
    e.numericInput(keys);
    if (e.object()->position.x!=2 || e.numericRow) return 21;
    std::cout<<"World editor CPU scene/UI/history contract passed (no Vulkan execution)\n";
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
        if (!n.focused || n.right) { editor.finishDrag(true); editor.numericRow.reset(); }
        if (n.minimized || !n.width || !n.height || !n.pixelWidth || !n.pixelHeight) {
            editor.finishDrag(true);
            focused=false; left=n.left; right=n.right;
            std::this_thread::sleep_for(std::chrono::milliseconds(20)); continue;
        }
        const Layout l(n.pixelWidth,n.pixelHeight);
        const float mx=n.mouseX*static_cast<float>(n.pixelWidth)/static_cast<float>(n.width);
        const float my=n.mouseY*static_cast<float>(n.pixelHeight)/static_cast<float>(n.height);
        const bool matching=shownW==n.pixelWidth && shownH==n.pixelHeight;
        if (editor.drag) {
            if (!matching || n.right || !n.focused || window.inputSnapshot().wasPressed({platform::InputControlKind::KeyboardKey,27}))
                editor.finishDrag(true);
            else if (n.left) editor.updateDrag(mx,my);
            else editor.finishDrag(false);
        }
        if (n.focused && !n.right && !navigating && !editor.drag)
            editor.numericInput(window.inputSnapshot());
        if (focused && n.focused && matching && n.right && !right && l.viewport().contains(mx,my))
            navigating=window.setRelativeMouseMode(true);
        if (focused && n.focused && matching && n.left && !left && !n.right && !navigating) {
            if (!editor.click(l,mx,my) && !hitAxis(editor,shownCamera,l,mx,my)) {
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
    editor.finishDrag(true);
    if (editor.dirty) std::cerr<<"Unsaved document changes; no automatic overwrite performed.\n";
    if (navigating) (void)window.setRelativeMouseMode(false);
    return 0;
}
} // namespace

int main(int argc,char** argv) {
    if (argc==2 && std::string_view(argv[1])=="--self-test") return selfTest();
    if (argc==2 && std::string_view(argv[1])=="--help") {
        std::cout<<"novacore_world_editor [--map FILE] [--save-as FILE]\n"
            <<"SDL3/Vulkan required. RMB look/fly, LMB select/drag axis tip, click numeric value to edit.\n"
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
