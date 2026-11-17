// SPDX-FileCopyrightText: Copyright 2026 P1R4N351
// SPDX-License-Identifier: GPL-3.0-or-later

// The Legend of Zelda: Tears of the Kingdom 1.4.3 (0100F2C0115B6000), read-only.
//
// Game state comes from TotK Native's companion reader (totk_reader/, MIT, compiled unchanged):
// GameData flag tables and the player actor, read through host->read_memory with code guards
// that fail closed on any other build. The module turns its JSON snapshot into page values
// (totk.*); nothing in the game is written or called. Item actions are not offered: TotK Native
// performs them with guest calls, which Eden Duo does not provide on Android.
//
// Art and text are decoded from the player's romfs on request (module:totk:<key>, see
// LoadImage): item icons, layout textures from the Common layout archive, the map tiles, and the
// game's Rodin font (Super Mario Bros. Wonder's font code, wonder_font.cpp). Every TotK file is a
// zstd frame with a ZsDic dictionary (totk_romfs.cpp). Nothing is shipped or written to disk.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/mods/dsmod_module_abi.h"
#include "core/mods/dsmod_module_extensions.h"
#include "core/mods/modules/dsmod_module_sdk.h"
extern "C" {
#include "totk_reader/companion.h" // TotK Native's C header, kept as it is there
}
#include "totk_reader/totk_reader_ro.h"
#include "totk_romfs.h"
#include "wonder_assets.h"
#include "wonder_font.h"

namespace {

using namespace dsmod_sdk::int_types;
using json = nlohmann::json;

constexpr u64 TitleId = 0x0100F2C0115B6000ull;
constexpr std::string_view BuildId = "277178B7DBA1B6D4949A84778D4ABC58"; // 1.4.3 main
constexpr std::string_view Prefix = "module:totk:";
constexpr int LiveEvery = 6;   // ticks between live snapshots (10 Hz)
constexpr int FullEvery = 30;  // ticks between full snapshots (inventory, sages, map marks: 2 Hz)
constexpr int MaxHearts = 60;
constexpr int MaxRows = 300;
constexpr int Cols = 12, Rows = 10;

// companion_snapshot reads guest memory through a plain function pointer: the host table of the
// callback that is running it.
thread_local const EdenDsmodHostApi* t_host = nullptr;
int GuestRead(uint64_t address, void* out, size_t size) {
    return t_host && t_host->read_memory &&
           t_host->read_memory(t_host->userdata, address, out, size) != EDEN_DSMOD_FALSE;
}

std::string BuildHex(const EdenDsmodHostApi& h) {
    static constexpr char Hex[] = "0123456789ABCDEF";
    std::string s;
    for (const u8 b : h.build_id) {
        s += Hex[b >> 4];
        s += Hex[b & 15];
    }
    return s;
}

WonderAssets::RomfsReader RawRomfs(const EdenDsmodHostApi& h) {
    return [read = h.read_romfs, userdata = h.userdata](const std::string& path)
               -> std::optional<std::vector<u8>> {
        if (!read)
            return std::nullopt;
        const size_t size = read(userdata, path.c_str(), 0, nullptr, 0);
        if (size == 0 || size > 64u * 1024 * 1024)
            return std::nullopt;
        std::vector<u8> out(size);
        if (read(userdata, path.c_str(), 0, out.data(), size) != size)
            return std::nullopt;
        return out;
    };
}

// ---------- snapshot model ----------

enum Cat { Weapons, Bows, Shields, Arrows, Armor, Materials, Food, Zonai, KeyItems, CatCount };
constexpr std::array<const char*, CatCount> CatKeys{"weapons", "bows",  "shields", "arrows",  "armor",
                                                    "materials", "food", "zonai",   "keyItems"};
// The inventory page's eight tabs (bows and arrows share one, as in the game).
constexpr int TabCount = 8;
constexpr std::array<std::array<int, 2>, TabCount> Tabs{{{Weapons, -1}, {Bows, Arrows}, {Shields, -1},
                                                         {Armor, -1}, {Materials, -1}, {Food, -1},
                                                         {Zonai, -1}, {KeyItems, -1}}};
constexpr std::array<const char*, 5> SageIds{"Wind", "Fire", "Water", "Electric", "Soul"};
constexpr std::array<const char*, 5> SageActors{"Rito", "Goron", "Zora", "Gerudo", "Zonau"};

struct Item {
    std::string actor, fuse, effect;
    int life = INT32_MIN, count = INT32_MIN, fuse_life = INT32_MIN, heal = INT32_MIN;
    int level = INT32_MIN, time = INT32_MIN;
    bool equipped = false;
};
struct Sage {
    bool joined = false, summoned = false;
    double cooldown = 0;
};
struct Model {
    std::string status = "starting";
    std::string reason;
    bool pause_menu = false;
    json player = json::object();
    std::array<std::vector<Item>, CatCount> pouch;
    bool have_pouch = false;
    std::array<Sage, 5> sages;
    bool have_sages = false;
};

int Int(const json& o, const char* key, int fallback = INT32_MIN) {
    const auto it = o.find(key);
    return it != o.end() && it->is_number() ? static_cast<int>(std::lround(it->get<double>())) : fallback;
}
double Num(const json& o, const char* key, double fallback = NAN) {
    const auto it = o.find(key);
    return it != o.end() && it->is_number() ? it->get<double>() : fallback;
}
std::string Str(const json& o, const char* key) {
    const auto it = o.find(key);
    return it != o.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

// ---------- text helpers (the same wording as TotK Native's page) ----------

std::string Hearts(int quarters) {
    static constexpr std::array<const char*, 4> Parts{"", "\xC2\xBC", "\xC2\xBD", "\xC2\xBE"}; // ¼ ½ ¾
    const int q = std::max(0, quarters);
    if (q / 4 == 0 && q % 4 != 0)
        return Parts[q % 4];
    return std::to_string(q / 4) + Parts[q % 4];
}
std::string Wheels(double units) {
    const double v = std::max(0.0, units) / 1000;
    char b[32];
    if (v == std::rint(v))
        std::snprintf(b, sizeof b, "%lld", static_cast<long long>(v));
    else
        std::snprintf(b, sizeof b, "%.1f", v);
    return b;
}
std::string Duration(int seconds) {
    if (seconds < 0)
        return "";
    char b[32];
    std::snprintf(b, sizeof b, "%d:%02d", seconds / 60, seconds % 60);
    return b;
}
std::string Humanize(std::string_view actor) {
    std::string s{actor};
    std::replace(s.begin(), s.end(), '_', ' ');
    return s;
}
bool LowHearts(int life, int max) { // the stick-LED rule of TotK Native (BifrostLooks.lowOn)
    return max > 0 && life >= 0 && (life <= 4 || life * 4 <= max);
}

// ---------- the module ----------

class Module {
public:
    explicit Module(const EdenDsmodHostApi& api)
        : host{api}, supported{BuildHex(api).starts_with(BuildId)},
          romfs{std::make_shared<TotkRomfs::Reader>(RawRomfs(api))} {
        text_thread = std::thread([this] { LoadText(); });
    }
    ~Module() {
        if (text_thread.joinable())
            text_thread.join();
    }
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;

    void Configure(const EdenDsmodHostExtensions* ext) {
        if (ext && ext->version == EDEN_DSMOD_EXT_VERSION && ext->abi_hash == EDEN_DSMOD_EXT_HASH &&
            ext->struct_size >= sizeof(*ext)) {
            std::scoped_lock lock{image_mutex};
            astc = {ext->userdata, ext->decode_astc};
        }
    }

    void Sample(const EdenDsmodHostApi& api) {
        host = api;
        t_host = &host;
        if (supported && tick % LiveEvery == 0)
            Read(tick % FullEvery == 0 || !model.have_pouch);
        ++tick;
        Publish();
        t_host = nullptr;
    }

    EdenDsmodBool DecodeFont(const u8* bytes, size_t size, void* receiver, EdenDsmodFontSink sink) {
        const auto spec = WonderFont::ParseFontSpec({bytes, size});
        const auto font = spec ? WonderFont::BuildFontAtlas(romfs->AsWonderReader(), *spec) : nullptr;
        if (!font || font->glyphs.empty())
            return EDEN_DSMOD_FALSE;
        sink(receiver, font->line_height, font->first_codepoint, font->glyphs.data(),
             static_cast<u32>(font->glyphs.size()));
        return EDEN_DSMOD_TRUE;
    }

    EdenDsmodBool LoadImage(std::string_view key, void* receiver, EdenDsmodImageSink sink);

private:
    void Read(bool full);
    void Publish();
    void LoadText();
    std::string ItemName(const std::string& actor) const;
    std::string ItemCaption(const std::string& actor) const;
    void I64(const std::string& name, s64 v) const { host.publish_i64(host.userdata, name.c_str(), v); }
    void F64(const std::string& name, double v) const { host.publish_f64(host.userdata, name.c_str(), v); }
    void Text(const std::string& name, const std::string& v) const {
        host.publish_text(host.userdata, name.c_str(), v.c_str());
    }
    s64 Get(const char* name, s64 fallback) const {
        return host.get_i64 ? host.get_i64(host.userdata, name, fallback) : fallback;
    }
    void PublishReady();
    void PublishGear(const char* slot, Cat cat);
    void PublishHearts();
    void PublishMap();
    void PublishInventory();

    std::optional<WonderAssets::Image> Layout(std::string_view texture);
    std::optional<WonderAssets::Image> Icon(std::string_view actor);
    std::optional<WonderAssets::Image> MapTile(std::string_view key);
    std::optional<WonderAssets::Image> Heart(std::string_view code);
    std::optional<WonderAssets::Image> Generated(std::string_view key);

    EdenDsmodHostApi host;
    bool supported;
    std::shared_ptr<TotkRomfs::Reader> romfs;
    u64 tick = 0;
    TotkReaderState state{};
    Model model;
    CompanionBuf buf{};
    int sel_tab = -1;
    s64 sel_seen = -1;

    // text: PouchContent names and captions, loaded once on a thread
    std::thread text_thread;
    mutable std::mutex text_mutex;
    std::unordered_map<std::string, std::string> names;
    std::atomic<bool> text_ready{false};

    // images (asset worker)
    std::mutex image_mutex;
    WonderAssets::AstcDecoder astc{};
    std::optional<std::vector<u8>> combined; // Common layout archive's timg/__Combined.bntx
    bool combined_tried = false;
    std::optional<std::map<std::string, std::string>> tiles; // "<L>/<lod>/<c>_<r>" -> romfs path
};

void Module::LoadText() {
    // The English text archive; its name carries a data version (Mals/USen.Product.142.sarc.zs on
    // 1.4.3), so the newest one present is used.
    for (int v = 199; v >= 100; --v) {
        const std::string path = "Mals/USen.Product." + std::to_string(v) + ".sarc.zs";
        const auto sarc = romfs->Read(path);
        if (!sarc)
            continue;
        std::unordered_map<std::string, std::string> all;
        if (const auto msbt = WonderAssets::SarcMember(*sarc, "ActorMsg/PouchContent.msbt"))
            all = TotkRomfs::ParseMsbt(*msbt);
        std::scoped_lock lock{text_mutex};
        names = std::move(all);
        break;
    }
    text_ready = true;
}

std::string Module::ItemName(const std::string& actor) const {
    if (actor.empty())
        return {};
    std::scoped_lock lock{text_mutex};
    const auto it = names.find(actor + "_Name");
    return it != names.end() && !it->second.empty() ? it->second : Humanize(actor);
}
std::string Module::ItemCaption(const std::string& actor) const {
    std::scoped_lock lock{text_mutex};
    const auto it = names.find(actor + "_Caption");
    return it != names.end() ? it->second : std::string{};
}

void Module::Read(bool full) {
    const uintptr_t main = static_cast<uintptr_t>(host.main_base);
    totk_reader_refresh(main, GuestRead, &state);
    if (companion_snapshot(main, full ? COMPANION_FULL : COMPANION_LIVE, GuestRead, &buf) == 0)
        return;
    const json root = json::parse(std::string_view{buf.data, buf.len}, nullptr, false);
    if (!root.is_object())
        return;
    model.status = Str(root, "status");
    if (model.status != "ready") {
        model.player = json::object();
        if (model.status != "starting") { // keep the pouch across a moment of "starting" only
            model.have_pouch = model.have_sages = false;
            for (auto& c : model.pouch)
                c.clear();
        }
        model.reason = model.status;
        model.pause_menu = false;
        return;
    }
    model.player = root.value("player", json::object());
    const json action = root.value("actionState", json::object());
    model.reason = Str(action, "reason");
    model.pause_menu = action.value("pauseMenu", false);
    if (!full)
        return;
    if (const auto pouch = root.find("pouch"); pouch != root.end() && pouch->is_object()) {
        for (int c = 0; c < CatCount; ++c) {
            auto& list = model.pouch[c];
            list.clear();
            const auto items = pouch->find(CatKeys[c]);
            if (items == pouch->end() || !items->is_array())
                continue;
            for (const auto& o : *items) {
                Item it;
                it.actor = Str(o, "name");
                it.fuse = Str(o, "fuse");
                it.life = Int(o, "life");
                it.count = Int(o, "count");
                it.fuse_life = Int(o, "fuseLife");
                it.heal = Int(o, "heal");
                it.level = Int(o, "level");
                it.time = Int(o, "time");
                it.effect = Str(o, "effect");
                if (it.effect.empty())
                    if (const auto e = o.find("effect"); e != o.end() && e->is_object())
                        it.effect = Str(*e, "type");
                it.equipped = o.value("equipped", false);
                list.push_back(std::move(it));
            }
        }
        model.have_pouch = true;
    }
    if (const auto sages = root.find("sages"); sages != root.end() && sages->is_array()) {
        model.sages = {};
        for (const auto& o : *sages)
            for (size_t i = 0; i < SageIds.size(); ++i)
                if (Str(o, "id") == SageIds[i])
                    model.sages[i] = {o.value("joined", false), o.value("summoned", false),
                                      Num(o, "cooldown", 0)};
        model.have_sages = true;
    }
}

int Equipped(const std::vector<Item>& list) {
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].equipped)
            return static_cast<int>(i);
    return -1;
}

void Module::Publish() {
    // status: TotK Native's common state numbers (companion_state.h)
    const std::string& s = model.status;
    const int status = !supported ? 2
                       : s == "ready"        ? 5
                       : s == "title"        ? 3
                       : s == "unsupported"  ? 2
                                             : 1;
    I64("totk.status", status);
    I64("totk.ready", status == 5);
    I64("totk.not_ready", status != 5);
    Text("totk.status_text", status == 3   ? "Title screen"
                             : status == 2 ? "This companion needs Tears of the Kingdom 1.4.3"
                                           : "Waiting for the game");
    const bool event = status == 5 && model.reason == "event";
    const bool menu = status == 5 && !event && model.pause_menu;
    I64("totk.event", event);
    I64("totk.menu", menu);
    I64("totk.dim", event || menu);
    I64("totk.text_ready", text_ready.load());
    if (status != 5)
        return;
    PublishReady();
    PublishHearts();
    PublishMap();
    PublishInventory();
}

void Module::PublishGear(const char* slot, Cat cat) {
    const auto& list = model.pouch[cat];
    const int i = Equipped(list);
    const std::string p = std::string{"totk.gear."} + slot;
    I64(p + ".has", i >= 0);
    I64(p + ".none", i < 0);
    if (i < 0)
        return;
    const Item& it = list[i];
    int scale = 1;
    for (const auto& o : list)
        scale = std::max(scale, o.life == INT32_MIN ? 0 : o.life);
    Text(p + ".icon", std::string{Prefix} + "icon/" + it.actor);
    Text(p + ".name", ItemName(it.actor));
    const bool known = it.life != INT32_MIN;
    I64(p + ".life_known", known);
    I64(p + ".life", known ? std::max(0, it.life) : 0);
    I64(p + ".life_max", scale);
    I64(p + ".breaking", known && it.life < 5);
    I64(p + ".ok", !(known && it.life < 5));
}

void Module::PublishReady() {
    const json& pl = model.player;
    const auto has = [&](const char* k) { return pl.contains(k); };
    I64("totk.rupees", Int(pl, "rupees", 0));
    I64("totk.poes", Int(pl, "poes", 0));
    Text("totk.poes_text", has("poes") ? std::to_string(Int(pl, "poes", 0)) : "\xE2\x80\x94");
    Text("totk.stamina", has("maxStamina") ? Wheels(Num(pl, "maxStamina")) : "\xE2\x80\x94");
    Text("totk.stamina_extra", Num(pl, "extraStamina", 0) > 0 ? "+" + Wheels(Num(pl, "extraStamina")) : "");
    Text("totk.energy", has("maxEnergy") ? Wheels(Num(pl, "maxEnergy")) : "\xE2\x80\x94");
    Text("totk.energy_extra", Num(pl, "extraEnergy", 0) > 0 ? "+" + Wheels(Num(pl, "extraEnergy")) : "");

    // ability (bottom-left card)
    static const std::map<std::string, std::string> AbilityActor{
        {"UltraHand", "Obj_UltraHand"},       {"OneTouchBond", "Obj_OneTouchBond"},
        {"CeilingClipper", "Obj_Tooreroof"},  {"ReverseRecorder", "Obj_ReverseRecorder"},
        {"AutoBuilder", "Obj_AutoBuilder"}};
    const std::string ability = Str(pl, "ability");
    const auto ab = AbilityActor.find(ability);
    I64("totk.ability.has", ab != AbilityActor.end());
    if (ab != AbilityActor.end()) {
        Text("totk.ability.icon", std::string{Prefix} + "icon/" + ab->second);
        Text("totk.ability.name", ItemName(ab->second));
    }

    PublishGear("weapon", Weapons);
    PublishGear("bow", Bows);
    PublishGear("shield", Shields);

    // arrows under the bow
    const auto& arrows = model.pouch[Arrows];
    int a = Equipped(arrows);
    if (a < 0 && !arrows.empty())
        a = 0;
    I64("totk.arrow.has", a >= 0);
    I64("totk.arrow.none", a < 0);
    if (a >= 0) {
        Text("totk.arrow.icon", std::string{Prefix} + "icon/" + arrows[a].actor);
        Text("totk.arrow.count", "\xC3\x97" + std::to_string(std::max(0, arrows[a].count))); // ×
    }
    // the weapon's fused material above the weapon
    const auto& weapons = model.pouch[Weapons];
    const int w = Equipped(weapons);
    const bool fused = w >= 0 && !weapons[w].fuse.empty();
    I64("totk.fuse.has", fused);
    I64("totk.fuse.none", !fused);
    if (fused) {
        Text("totk.fuse.icon", std::string{Prefix} + "icon/" + weapons[w].fuse);
        Text("totk.fuse.life", weapons[w].fuse_life == INT32_MIN ? "" : std::to_string(weapons[w].fuse_life));
    }

    // armor pane: the three worn pieces
    int shown = 0;
    for (const auto& it : model.pouch[Armor]) {
        if (!it.equipped || shown >= 3)
            continue;
        Text("totk.armor." + std::to_string(shown) + ".icon", std::string{Prefix} + "icon/" + it.actor);
        I64("totk.armor." + std::to_string(shown) + ".on", 1);
        ++shown;
    }
    I64("totk.armor.none", shown == 0);

    // meal pane
    const json buff = pl.value("cookBuff", json::object());
    const bool meal = buff.contains("id") || buff.contains("type");
    I64("totk.meal.has", meal);
    I64("totk.meal.none", !meal);
    if (meal)
        Text("totk.meal.text", "Lv " + std::to_string(Int(buff, "level", 0)) + " \xC2\xB7 " +
                                   Duration(Int(buff, "time", 0)));

    // sages pane
    for (size_t i = 0; i < SageIds.size(); ++i) {
        const Sage& sg = model.sages[i];
        const std::string p = "totk.sage." + std::to_string(i);
        I64(p + ".joined", sg.joined);
        I64(p + ".away", !sg.joined);
        const int cd = static_cast<int>(std::lround(sg.cooldown));
        Text(p + ".status", !sg.joined ? "\xE2\x80\x94" : cd > 0 ? std::to_string(cd) + "s"
                                      : sg.summoned ? "With you" : "Away");
        I64(p + ".tone", !sg.joined ? 0 : cd > 0 ? 2 : sg.summoned ? 3 : 1);
    }

    // where Link is
    const auto pos = pl.find("pos");
    if (pos != pl.end() && pos->is_array() && pos->size() == 3) {
        char b[64];
        std::snprintf(b, sizeof b, "%+.0f  %+.0f  %+.0f", (*pos)[0].get<double>(), (*pos)[1].get<double>(),
                      (*pos)[2].get<double>());
        Text("totk.coords", b);
    } else {
        Text("totk.coords", "Position unavailable");
    }
    const std::string layer = Str(pl, "layer");
    const int li = layer == "Sky" ? 1 : layer == "UnderGround" ? 2 : 0;
    I64("totk.layer", li);
    Text("totk.layer_text", li == 1 ? "Sky" : li == 2 ? "Depths" : "Surface");
    I64("totk.depths", li == 2);
    I64("totk.surface_or_sky", li != 2);
}

void Module::PublishHearts() {
    const json& pl = model.player;
    const bool known = pl.contains("maxLife");
    I64("totk.hearts.known", known);
    const int max = std::max(0, Int(pl, "maxLife", 0)), life = std::max(0, Int(pl, "life", 0));
    const int extra = std::max(0, Int(pl, "extraLife", 0)), broken = std::max(0, Int(pl, "breakLife", 0));
    const bool low = known && LowHearts(life, max);
    I64("totk.hearts.low", low);
    I64("totk.hearts.ok", !low);
    Text("totk.hearts.text", known ? Hearts(life) + " / " + Hearts(max) : "\xE2\x80\x94");
    const int total = known ? std::min(MaxHearts, (max + extra + 3) / 4) : 0;
    I64("totk.hearts.count", total);
    I64("totk.hearts.dense", total > 32);
    I64("totk.hearts.sparse", total <= 32);
    for (int i = 0; i < MaxHearts; ++i) {
        const std::string p = "totk.heart." + std::to_string(i);
        I64(p + ".on", i < total);
        I64(p + ".s", i < total && total <= 32); // the 4-column grid
        I64(p + ".d", i < total && total > 32);  // the 6-column grid
        if (i >= total)
            continue;
        const bool yellow = i * 4 >= max;
        const int filled = std::clamp((yellow ? max + extra : life) - i * 4, 0, 4);
        const bool gloom = !yellow && i * 4 + 4 > max - broken;
        Text(p + ".src", std::string{Prefix} + "heart/" + (gloom ? "g" : yellow ? "y" : "r") +
                             std::to_string(filled));
    }
}

void Module::PublishMap() {
    const json& pl = model.player;
    const auto pos = pl.find("pos");
    const bool have = pos != pl.end() && pos->is_array() && pos->size() == 3;
    I64("totk.map.valid", have);
    if (have) {
        F64("totk.map.x", (*pos)[0].get<double>());
        F64("totk.map.y", -(*pos)[2].get<double>()); // map y is north-up: -Z
    }
    F64("totk.map.heading", Num(pl, "heading", 0));
    static constexpr std::array<char, 3> Layers{'G', 'S', 'U'};
    const std::string layer = Str(pl, "layer");
    const int follow = layer == "Sky" ? 1 : layer == "UnderGround" ? 2 : 0;
    // The Map page's layer: the one the player picked (flag map_layer 1..3), else Link's.
    const s64 picked = Get("@flag:map_layer", 0);
    const int page_layer = picked >= 1 && picked <= 3 ? static_cast<int>(picked - 1) : follow;
    I64("totk.map.page_layer", page_layer);
    for (int k = 0; k < 3; ++k)
        I64("totk.map.page_layer_is." + std::to_string(k), page_layer == k);
    // Finer tiles once the Map page is zoomed past the coarse tiles' size.
    const double ppw = host.get_f64 ? host.get_f64(host.userdata, "@map_view_ppw:TotkFull", 0) : 0;
    const int page_lod = ppw > 0.45 ? 1 : 2;
    for (int r = 0; r < Rows; ++r)
        for (int c = 0; c < Cols; ++c) {
            const int k = r * Cols + c;
            char tail[32];
            std::snprintf(tail, sizeof tail, "/%d_%d", c, r);
            Text("totk.mini.tile." + std::to_string(k),
                 std::string{Prefix} + "map/" + Layers[follow] + "/2" + tail);
            Text("totk.map.tile." + std::to_string(k), std::string{Prefix} + "map/" + Layers[page_layer] +
                                                           "/" + std::to_string(page_lod) + tail);
        }
}

void Module::PublishInventory() {
    const int tab = static_cast<int>(std::clamp<s64>(Get("@flag:inv_tab", 0), 0, TabCount - 1));
    for (int t = 0; t < TabCount; ++t)
        I64("totk.inv.tab." + std::to_string(t), t == tab);
    std::vector<std::pair<int, const Item*>> rows;
    for (const int cat : Tabs[tab])
        if (cat >= 0)
            for (const auto& it : model.pouch[cat])
                if (!it.actor.empty() && rows.size() < MaxRows)
                    rows.emplace_back(cat, &it);
    I64("totk.inv.count", static_cast<s64>(rows.size()));
    I64("totk.inv.empty", rows.empty());
    std::array<int, CatCount> scale{};
    for (int c = 0; c < CatCount; ++c)
        for (const auto& it : model.pouch[c])
            scale[c] = std::max(scale[c], it.life == INT32_MIN ? 0 : it.life);
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& [cat, it] = rows[i];
        const std::string p = "totk.inv." + std::to_string(i);
        I64(p + ".on", 1);
        Text(p + ".icon", std::string{Prefix} + "icon/" + it->actor);
        Text(p + ".name", ItemName(it->actor));
        I64(p + ".equipped", it->equipped);
        const bool life = it->life != INT32_MIN && it->count == INT32_MIN;
        I64(p + ".has_life", life);
        I64(p + ".life", life ? std::max(0, it->life) : 0);
        I64(p + ".life_max", std::max(1, scale[cat]));
        I64(p + ".life_low", life && it->life < 5);
        Text(p + ".qty", it->count != INT32_MIN && it->count > 1 ? "\xC3\x97" + std::to_string(it->count) : "");
        I64(p + ".fused", !it->fuse.empty());
        if (!it->fuse.empty())
            Text(p + ".fuse_icon", std::string{Prefix} + "icon/" + it->fuse);
    }
    // the selected item's detail strip (tap selects: @sel:inv carries the row index)
    const s64 sel_flag = Get("@flag:inv_sel", -1);
    if (tab != sel_tab) { // a new tab starts with nothing selected, until the next tap
        sel_tab = tab;
        sel_seen = sel_flag;
    }
    const s64 sel = sel_flag != sel_seen ? sel_flag : -1;
    if (sel >= 0)
        sel_seen = INT64_MIN; // a tap on this tab: keep it until the tab changes
    for (size_t i = 0; i < rows.size(); ++i)
        I64("totk.inv." + std::to_string(i) + ".selected", static_cast<s64>(i) == sel);
    const bool has_sel = sel >= 0 && sel < static_cast<s64>(rows.size());
    I64("totk.inv.sel.has", has_sel);
    I64("totk.inv.sel.none", !has_sel);
    if (has_sel) {
        const Item& it = *rows[static_cast<size_t>(sel)].second;
        Text("totk.inv.sel.name", ItemName(it.actor));
        Text("totk.inv.sel.caption", ItemCaption(it.actor));
        std::string facts;
        if (it.life != INT32_MIN && it.count == INT32_MIN)
            facts += "Durability " + std::to_string(it.life);
        if (!it.fuse.empty())
            facts += (facts.empty() ? "" : "   ") + std::string{"Fused: "} + ItemName(it.fuse) +
                     (it.fuse_life != INT32_MIN ? " (" + std::to_string(it.fuse_life) + ")" : "");
        if (it.count != INT32_MIN)
            facts += (facts.empty() ? "" : "   ") + std::string{"\xC3\x97"} + std::to_string(it.count);
        if (it.heal != INT32_MIN && it.heal > 0)
            facts += (facts.empty() ? "" : "   ") + std::string{"Hearts "} + Hearts(it.heal);
        if (it.time != INT32_MIN && it.time > 0)
            facts += (facts.empty() ? "" : "   ") + std::string{"Lv "} + std::to_string(std::max(0, it.level)) +
                     " \xC2\xB7 " + Duration(it.time);
        if (it.equipped)
            facts += (facts.empty() ? "" : "   ") + std::string{"Equipped"};
        Text("totk.inv.sel.facts", facts);
    }
}

// ---------- images ----------

using Image = WonderAssets::Image;

std::optional<Image> Module::Layout(std::string_view texture) {
    std::scoped_lock lock{image_mutex};
    if (!combined_tried) {
        combined_tried = true;
        if (const auto blarc = romfs->Read("UI/LayoutArchive/Common.Product.140.Nin_NX_NVN.blarc.zs"))
            combined = WonderAssets::SarcMember(*blarc, "timg/__Combined.bntx");
    }
    if (!combined)
        return std::nullopt;
    return WonderAssets::DecodeBntx(*combined, texture, astc);
}

std::optional<Image> Module::Icon(std::string_view actor) {
    if (actor.empty() || actor.find('/') != std::string_view::npos || actor.find("..") != std::string_view::npos)
        return std::nullopt;
    const auto bntx = romfs->Read("UI/Tex/Icon/" + std::string{actor} + ".bntx.zs");
    if (!bntx)
        return std::nullopt;
    WonderAssets::AstcDecoder a;
    {
        std::scoped_lock lock{image_mutex};
        a = astc;
    }
    return WonderAssets::DecodeBntx(*bntx, "", a);
}

// map/<G|S|U>/<1|2>/<col>_<row>: the tile TotK Native's art stage picks (most revealed variant),
// named in the package's map_tiles.txt (generated from a romfs listing; names only).
std::optional<Image> Module::MapTile(std::string_view key) {
    std::string path;
    {
        std::scoped_lock lock{image_mutex};
        if (!tiles) {
            tiles.emplace();
            const auto list = romfs->Read("file:map_tiles.txt");
            std::string_view all = list ? std::string_view{reinterpret_cast<const char*>(list->data()), list->size()}
                                        : std::string_view{};
            while (!all.empty()) {
                const size_t nl = all.find('\n');
                std::string_view line = all.substr(0, nl);
                all = nl == std::string_view::npos ? std::string_view{} : all.substr(nl + 1);
                // UI/Map/MainField/G_05-03_01_TT.bntx.zs
                const size_t at = line.rfind('/');
                if (at == std::string_view::npos || line.size() < at + 11)
                    continue;
                const std::string_view n = line.substr(at + 1);
                const int col = std::atoi(std::string{n.substr(2, 2)}.c_str());
                const int row = std::atoi(std::string{n.substr(5, 2)}.c_str());
                const int lod = n[9] - '0';
                char k[32];
                std::snprintf(k, sizeof k, "%c/%d/%d_%d", n[0], lod, col, row);
                (*tiles)[k] = std::string{line};
            }
        }
        const auto it = tiles->find(std::string{key});
        if (it == tiles->end())
            return std::nullopt;
        path = it->second;
    }
    const auto bntx = romfs->Read(path);
    if (!bntx)
        return std::nullopt;
    WonderAssets::AstcDecoder a;
    {
        std::scoped_lock lock{image_mutex};
        a = astc;
    }
    return WonderAssets::DecodeBntx(*bntx, "", a);
}

// The game's heart shapes tinted like TotK Native's page: heart/<r|y|g><quarters 0-4>.
// r: red quarters over an empty frame; y: the same in gold (extra hearts); g: gloom-damaged.
void Tint(Image& im, u32 argb) {
    const u8 r = argb >> 16 & 255, g = argb >> 8 & 255, b = argb & 255;
    for (size_t i = 0; i + 3 < im.rgba.size(); i += 4) {
        im.rgba[i] = r;
        im.rgba[i + 1] = g;
        im.rgba[i + 2] = b;
    }
}
void Over(Image& dst, const Image& src, u32 max_x) { // src over dst, columns < max_x only
    if (src.width != dst.width || src.height != dst.height)
        return;
    for (u32 y = 0; y < dst.height; ++y)
        for (u32 x = 0; x < std::min(max_x, dst.width); ++x) {
            const size_t i = (static_cast<size_t>(y) * dst.width + x) * 4;
            const u32 sa = src.rgba[i + 3], da = dst.rgba[i + 3];
            const u32 oa = sa + da * (255 - sa) / 255;
            if (oa == 0)
                continue;
            for (int c = 0; c < 3; ++c)
                dst.rgba[i + c] = static_cast<u8>((src.rgba[i + c] * sa + dst.rgba[i + c] * da * (255 - sa) / 255) / oa);
            dst.rgba[i + 3] = static_cast<u8>(oa);
        }
}
Image Resized(const Image& in, u32 w, u32 h) { // nearest neighbour onto the target frame
    Image out{w, h, std::vector<u8>(static_cast<size_t>(w) * h * 4)};
    for (u32 y = 0; y < h; ++y)
        for (u32 x = 0; x < w; ++x) {
            const u32 sx = x * in.width / w, sy = y * in.height / h;
            std::memcpy(&out.rgba[(static_cast<size_t>(y) * w + x) * 4],
                        &in.rgba[(static_cast<size_t>(sy) * in.width + sx) * 4], 4);
        }
    return out;
}

std::optional<Image> Module::Heart(std::string_view code) {
    if (code.size() != 2 || (code[0] != 'r' && code[0] != 'y' && code[0] != 'g') || code[1] < '0' || code[1] > '4')
        return std::nullopt;
    auto full = Layout("HeartW_00^s");
    auto frame = Layout("HeartFrame_00^s");
    if (!full)
        return std::nullopt;
    Image out = frame ? Resized(*frame, full->width, full->height) : *full;
    Tint(out, 0xff657369);
    if (!frame) // no frame texture: the empty heart is the full shape, dimmed
        for (size_t i = 3; i < out.rgba.size(); i += 4)
            out.rgba[i] = static_cast<u8>(out.rgba[i] * 3 / 4);
    const int quarters = code[1] - '0';
    if (quarters > 0) {
        Image fill = *full;
        Tint(fill, code[0] == 'y' ? 0xffead375 : 0xffe7796b);
        Over(out, fill, full->width * static_cast<u32>(quarters) / 4);
    }
    if (code[0] == 'g') {
        if (auto broken = Layout("HeartBreakW_00^s")) {
            Image b = Resized(*broken, full->width, full->height);
            Tint(b, 0xffa45660);
            Over(out, b, b.width);
        }
    }
    return out;
}

// gen/clear: a transparent pixel (the map areas' base picture: the tiles are overlays).
// gen/disc/<size>/<AARRGGBB>: the colour outside a centred disc, clear inside, with a thin ring:
// rounds the square minimap.
std::optional<Image> Module::Generated(std::string_view key) {
    if (key == "clear")
        return Image{1, 1, std::vector<u8>(4, 0)};
    if (key.starts_with("circle/")) { // circle/<size>/<AARRGGBB>: a filled disc, antialiased
        const auto rest = key.substr(7);
        const size_t slash = rest.find('/');
        if (slash == std::string_view::npos)
            return std::nullopt;
        const int size = std::atoi(std::string{rest.substr(0, slash)}.c_str());
        const u32 argb = static_cast<u32>(std::strtoul(std::string{rest.substr(slash + 1)}.c_str(), nullptr, 16));
        if (size < 2 || size > 2048)
            return std::nullopt;
        Image im{static_cast<u32>(size), static_cast<u32>(size), std::vector<u8>(static_cast<size_t>(size) * size * 4)};
        const double c = size / 2.0;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const double cover = std::clamp(c - std::hypot(x + 0.5 - c, y + 0.5 - c) + 0.5, 0.0, 1.0);
                u8* p = &im.rgba[(static_cast<size_t>(y) * size + x) * 4];
                p[0] = argb >> 16 & 255;
                p[1] = argb >> 8 & 255;
                p[2] = argb & 255;
                p[3] = static_cast<u8>(std::lround(cover * (argb >> 24 & 255)));
            }
        return im;
    }
    if (key.starts_with("disc/")) {
        const auto rest = key.substr(5);
        const size_t slash = rest.find('/');
        if (slash == std::string_view::npos)
            return std::nullopt;
        const int size = std::atoi(std::string{rest.substr(0, slash)}.c_str());
        const u32 argb = static_cast<u32>(std::strtoul(std::string{rest.substr(slash + 1)}.c_str(), nullptr, 16));
        if (size < 16 || size > 2048)
            return std::nullopt;
        Image im{static_cast<u32>(size), static_cast<u32>(size), std::vector<u8>(static_cast<size_t>(size) * size * 4)};
        const double c = size / 2.0, r = size / 2.0 - 4;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const double d = std::hypot(x + 0.5 - c, y + 0.5 - r - 4);
                u8* p = &im.rgba[(static_cast<size_t>(y) * size + x) * 4];
                // outside: the page colour (antialiased over one pixel)
                const double out = std::clamp(d - r + 0.5, 0.0, 1.0);
                // the ring: cream, 3 px, just inside the edge (TotK Native draws INK at 0xc0)
                const double ring = std::clamp(1.5 - std::abs(d - (r - 3)), 0.0, 1.0) * (0xc0 / 255.0);
                const double a_out = out * (argb >> 24 & 255) / 255.0;
                const double a = a_out + ring * (1 - a_out);
                if (a <= 0)
                    continue;
                const auto mix = [&](int shift, double ink) {
                    const double page = (argb >> shift & 255);
                    return static_cast<u8>(std::lround((page * a_out + ink * ring * (1 - a_out)) / a));
                };
                p[0] = mix(16, 0xe9);
                p[1] = mix(8, 0xe5);
                p[2] = mix(0, 0xd5);
                p[3] = static_cast<u8>(std::lround(a * 255));
            }
        return im;
    }
    return std::nullopt;
}

EdenDsmodBool Module::LoadImage(std::string_view key, void* receiver, EdenDsmodImageSink sink) {
    if (!key.starts_with(Prefix))
        return EDEN_DSMOD_FALSE;
    const auto k = key.substr(Prefix.size());
    std::optional<Image> im;
    std::shared_ptr<const WonderFont::FontAtlas> font;
    if (k.starts_with("icon/"))
        im = Icon(k.substr(5));
    else if (k.starts_with("lyt/"))
        im = Layout(k.substr(4));
    else if (k.starts_with("map/"))
        im = MapTile(k.substr(4));
    else if (k.starts_with("heart/"))
        im = Heart(k.substr(6));
    else if (k.starts_with("gen/"))
        im = Generated(k.substr(4));
    else if (k.starts_with("font/")) {
        font = WonderFont::BuildFontAtlas(romfs->AsWonderReader(), k.substr(5));
        if (!font || font->atlas.rgba.empty())
            return EDEN_DSMOD_FALSE;
        sink(receiver, font->atlas.width, font->atlas.height, font->atlas.rgba.data(), font->atlas.rgba.size());
        return EDEN_DSMOD_TRUE;
    }
    if (!im || im->rgba.empty())
        return EDEN_DSMOD_FALSE;
    sink(receiver, im->width, im->height, im->rgba.data(), im->rgba.size());
    return EDEN_DSMOD_TRUE;
}

// ---------- ABI ----------

EdenDsmodBool SupportsBuild(const char* build_id) {
    // Other builds load too: the page then says which version it needs (no memory is read).
    return build_id ? EDEN_DSMOD_TRUE : EDEN_DSMOD_FALSE;
}
void* Create(const EdenDsmodHostApi* host, const char*) {
    try {
        if (!dsmod_sdk::HostAbiMatches(host) || host->title_id != TitleId || !host->read_memory)
            return nullptr;
        return new Module{*host};
    } catch (...) {
        return nullptr;
    }
}
void Destroy(void* p) {
    try {
        delete static_cast<Module*>(p);
    } catch (...) {
    }
}
void SampleCallback(void* p, const EdenDsmodHostApi* host) {
    static std::atomic_flag reported = ATOMIC_FLAG_INIT;
    try {
        if (p && host)
            static_cast<Module*>(p)->Sample(*host);
    } catch (...) {
        t_host = nullptr;
        if (!reported.test_and_set() && host && host->log)
            host->log(host->userdata, EDEN_DSMOD_LOG_ERROR, "TotK DSMod sample failed");
    }
}
void TickCallback(void*, const EdenDsmodHostApi*) {}
void ConfigureCallback(void* p, const EdenDsmodHostExtensions* ext) {
    try {
        if (p)
            static_cast<Module*>(p)->Configure(ext);
    } catch (...) {
    }
}
EdenDsmodBool ActionCallback(void*, const char*, std::int64_t) {
    return EDEN_DSMOD_FALSE; // read-only: no module actions
}
EdenDsmodBool LoadImageCallback(void* p, const EdenDsmodHostApi*, const char* key, void* receiver,
                                EdenDsmodImageSink sink) {
    try {
        if (p && key && sink)
            return static_cast<Module*>(p)->LoadImage(key, receiver, sink);
    } catch (...) {
    }
    return EDEN_DSMOD_FALSE;
}
EdenDsmodBool DecodeFontCallback(void* p, const std::uint8_t* bytes, size_t size, void* receiver,
                                 EdenDsmodFontSink sink) {
    try {
        if (p && bytes && sink)
            return static_cast<Module*>(p)->DecodeFont(bytes, size, receiver, sink);
    } catch (...) {
    }
    return EDEN_DSMOD_FALSE;
}

const EdenDsmodModuleApi ModuleApi{EDEN_DSMOD_MODULE_ABI_VERSION,
                                   sizeof(EdenDsmodModuleApi),
                                   0,
                                   EDEN_DSMOD_MODULE_ABI_HASH,
                                   TitleId,
                                   "Tears of the Kingdom DSMod",
                                   EDEN_DSMOD_CAP_EXTENSIONS | EDEN_DSMOD_CAP_ROMFS_READ,
                                   &SupportsBuild,
                                   &Create,
                                   &Destroy,
                                   &SampleCallback,
                                   &TickCallback};
const EdenDsmodModuleExtensions ModuleExtensions{EDEN_DSMOD_EXT_VERSION, sizeof(EdenDsmodModuleExtensions),
                                                 EDEN_DSMOD_EXT_HASH,    &ConfigureCallback,
                                                 &ActionCallback,        &LoadImageCallback};
const EdenDsmodFontExtensions FontExtensions{EDEN_DSMOD_FONT_EXT_VERSION, sizeof(EdenDsmodFontExtensions),
                                             EDEN_DSMOD_FONT_EXT_HASH, &DecodeFontCallback};
} // namespace

DSMOD_SDK_EXPORT_MODULE(ModuleApi)
DSMOD_SDK_EXPORT_EXTENSIONS(ModuleExtensions)
DSMOD_SDK_EXPORT_FONT_EXTENSIONS(FontExtensions)
