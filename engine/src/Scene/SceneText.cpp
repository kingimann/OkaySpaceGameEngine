#include "okay/Scene/SceneText.hpp"
#include "okay/Scene/ComponentSerializer.hpp"
#include "okay/Scene/SceneSerializer.hpp"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace okay {

namespace {

bool IsBlank(char c) { return c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\r' || c == '\n'; }

std::string Trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && IsBlank(s[b])) ++b;
    while (e > b && IsBlank(s[e - 1])) --e;
    return s.substr(b, e - b);
}

// Reads one logical line: a line that goes on while a quoted string is open. The
// quote rules are the loader's (see SceneText::Tokenize): a quote opens at the start
// of a token or right after '='; inside a string a backslash escapes the next char.
// CRLF, LF and a lone CR end a line outside a string; inside a string every byte stays.
struct LineScanner {
    const std::string& s;
    std::size_t pos = 0;
    explicit LineScanner(const std::string& text) : s(text) {}
    bool Next(std::size_t& b, std::size_t& e, bool& open) {
        if (pos >= s.size()) return false;
        b = pos;
        open = false;
        bool inQuote = false;
        char prev = ' ';
        std::size_t i = pos;
        while (i < s.size()) {
            const char c = s[i];
            if (inQuote) {
                if (c == '\\' && i + 1 < s.size()) { i += 2; prev = 'x'; continue; }
                if (c == '"') inQuote = false;
                prev = c;
                ++i;
                continue;
            }
            if (c == '\n') { e = i; pos = i + 1; return true; }
            if (c == '\r') { e = i; pos = (i + 1 < s.size() && s[i + 1] == '\n') ? i + 2 : i + 1; return true; }
            if (c == '"' && (i == b || IsBlank(prev) || prev == '=')) inQuote = true;
            prev = c;
            ++i;
        }
        e = s.size();
        pos = s.size();
        open = inQuote;
        return true;
    }
};

std::size_t LineNumberAt(const std::string& s, std::size_t at) {
    std::size_t n = 1;
    for (std::size_t i = 0; i < at && i < s.size(); ++i) if (s[i] == '\n') ++n;
    return n;
}

bool ParseInt(const std::string& t, long long& v) {
    if (t.empty() || t.size() > 11) return false;
    std::size_t i = (t[0] == '-' || t[0] == '+') ? 1 : 0;
    if (i >= t.size()) return false;
    for (std::size_t k = i; k < t.size(); ++k) if (t[k] < '0' || t[k] > '9') return false;
    v = std::strtoll(t.c_str(), nullptr, 10);
    return true;
}

// The records the loader reads inside an object (and the one file-level list).
const std::unordered_set<std::string>& KnownObjectKeys() {
    static const std::unordered_set<std::string> keys = {
    "actions", "active", "aimik", "aimoffset", "animator", "animsm", "animsm2d",
    "animsmactions", "animsmany", "animsmanyint", "animsmanypos", "animsmev", "animsmfall", "animsmfb",
    "animsmikc", "animsmint", "animsmlayer", "animsmlayerx", "animsmlk", "animsmlname", "animsmmatch",
    "animsmmir", "animsmmute", "animsmnoint", "animsmparams", "animsmpdef", "animsmpos", "animsmstance",
    "animsmsync", "animsmx", "animsmxfade", "animx", "anticheat", "anticheathit", "anticheatsafety",
    "anticheatweapon", "aomap", "audio", "autosave", "blendshapes", "blockbuilder", "boxcollider2d",
    "boxcollider3d", "btrunner", "buildable", "buildermode", "buildmode", "buildpiece", "camera",
    "camerafollow", "canvas", "capsulecollider2d", "capsulecollider3d", "cc3dstance", "chainik", "chainx2",
    "character", "charbind", "charctrl2d", "charctrl3d", "charenabled", "charext", "chestinventory",
    "circlecollider2d", "cmbrain", "collectible", "compoff", "comporder", "consumables", "crafting", "craftingstation",
    "craftmenu", "crosshair", "ctmctrl", "ctmstance", "cylindercollider3d", "damagetouch", "daynight",
    "destructible", "dialoguerunner", "dollycart", "dollypath", "draggable", "dropzone", "edgecollider2d",
    "equipment", "eventsystem", "fbik", "fbikx", "flashlight", "follow2d", "footik",
    "fpctrl", "fpfeel", "fphand", "fphands", "fphands2", "fpstance", "frctrl",
    "gridinventory", "gridinventoryui", "groundshadow", "guid", "holdable", "holdprompt", "humretarget",
    "iktarget", "ikx", "inventory", "inventoryui", "itempickup", "joint2d", "joint3d",
    "jumppad", "layer", "lifetime", "light", "limbik", "linerenderer", "loadingscreen",
    "loc", "locked", "logicsource", "logictarget", "lookatik", "lootsource", "material",
    "melee", "mesh", "meshcollider3d", "meshcolors", "meshenabled", "meshgeo", "meshpoly",
    "mrmap", "emissivemap",
    "meshseams", "meshshade", "meshshadows", "meshuv", "meshvar", "minimap", "minimapblip",
    "mod", "modelanim", "modelanimair", "modelanimblend", "modelanimcubic", "modelanimdir", "modelanimdrive",
    "modelanimevents", "modelanimlegs", "modelanimroot", "modelanimsmooth", "modelanimstance", "modelanimstride", "modstack",
    "mover", "music", "navagent", "navlink", "navmod", "navobstacle", "navsurface", "netinventory", "netobject",
    "netprefab", "netsync", "nettransform", "netvalidator", "network", "normalmap", "npc",
    "npcstance", "npcwp", "npspawner", "objholding", "outline", "override", "p25ctrl",
    "parent", "particles", "particlesfx", "particleswind", "pausemenu", "pcfirst", "pcfirstfeel",
    "pcfirststance", "pcthird", "pcthirdfeel", "pcthirdstance", "playercontroller", "playerview", "polygoncollider2d",
    "posecap", "prefab", "proj3d", "questlog", "questlogdef", "questtrigger", "ragdoll",
    "reflect", "rigidbody2d", "rigidbody3d", "rim", "rootmotion", "saveable", "savesettings", "script", "scriptfields",
    "scriptpath", "seat", "sequenceplayer", "shader", "sibling", "skinmesh", "skinx",
    "spawner", "specmap", "spherecollider3d", "spinner", "spline", "splinefollower", "splineinstancer",
    "splinemesh", "splinept", "sprite", "spriteanim", "srcscene", "stat_bleed", "stat_carry",
    "stat_health", "stat_hunger", "stat_oxygen", "stat_poison", "stat_radiation", "stat_sanity", "stat_sleep",
    "stat_stamina", "stat_temp", "stat_thirst", "stat_wet", "static", "stats", "statuseffect",
    "storystate", "structurebuilder", "survival", "survivalsave", "survivalzone", "syncvar", "tag",
    "tdctrl", "tdctrl2d", "tdsctrl", "tdstance", "teleporter", "terrain", "terraindigger",
    "texopts", "text", "tilemap", "tilemapcollider", "tpctrl", "tpfeel", "tpsctrl",
    "tpsstance", "tpstance", "trailrenderer", "transform", "trigfilter2d", "trigfilter3d", "triggerzone",
    "turnmgr", "tweenanim", "uianim", "uibarbind", "uibind", "uibutton", "uicolorpicker",
    "uicontext", "uidocument", "uidraggable", "uidropdown", "uidroptarget", "uiflexitem", "uiframe",
    "uiimage", "uiinput", "uikeybind", "uilayout", "uilist", "uimask", "uimodal",
    "uiorder", "uipanel", "uiprogress", "uiradial", "uirating", "uirawimage", "uisafearea",
    "uiscroll", "uislider", "uislotgrid", "uistepper", "uitabs", "uitheme", "uitoast",
    "uitoggle", "uitooltip", "uvanim", "vcam", "vehicle", "vehicle2d", "vehiclesusp",
    "visualscript", "visualscriptpath", "voxeldigger", "voxelterrain", "water", "worldstreamer", "worldui",
    "worldui3d",
    };
    return keys;
}

const std::vector<std::string>& GlobalOrder() {
    static const std::vector<std::string> order = {
        "name", "uifont", "gravity", "layermatrix", "gravity3", "rendersettings", "fog", "vignette", "tonemap",
        "sky", "skystars", "ambientmode", "fogmode", "skyproc", "post", "weather"};
    return order;
}

int GlobalRank(const std::string& key) {
    const auto& o = GlobalOrder();
    for (std::size_t i = 0; i < o.size(); ++i) if (o[i] == key) return (int)i;
    return -1;
}

// The writer's order for an object's bookkeeping records; everything else ranks last.
int ObjectRank(const std::string& key) {
    static const char* order[] = {"guid", "active", "tag", "static", "locked", "layer", "uiorder",
                                  "srcscene", "prefab", "override", "parent", "sibling"};
    for (int i = 0; i < 12; ++i) if (key == order[i]) return i;
    return 100;
}

// Records whose every unquoted token is a number the engine wrote. Only these get
// their numbers tidied (a bare word that looks like a number could be a name in a
// record this list does not vouch for).
bool IsNumericRecord(const std::string& key) {
    static const std::unordered_set<std::string> keys = {
        "gravity", "gravity3", "layermatrix", "rendersettings", "fog", "vignette", "tonemap", "sky", "skystars",
        "ambientmode", "fogmode", "skyproc", "post", "weather",
        "active", "static", "locked", "layer", "uiorder", "parent", "sibling",
        "transform", "sprite", "camera", "light", "rigidbody2d", "rigidbody3d",
        "boxcollider2d", "circlecollider2d", "boxcollider3d", "spherecollider3d",
        "spinner", "lifetime", "mover", "audio", "text"};
    return keys.count(key) != 0;
}

bool IsIdentChar(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }

// Rewrite one record: tokens joined by single spaces, numbers tidied where allowed.
// `known` false leaves the text alone.
void NormalizeRecord(SceneText::Record& r, bool known, bool numbers) {
    if (!known) return;
    const std::vector<SceneText::Token> toks = SceneText::Tokenize(r.text);
    if (toks.empty()) return;
    const bool numeric = numbers && IsNumericRecord(r.key);
    std::string out;
    for (std::size_t i = 0; i < toks.size(); ++i) {
        std::string t = toks[i].text;
        if (numbers && i > 0 && !toks[i].quoted) {
            const std::size_t eq = t.find('=');
            const bool kv = eq != std::string::npos && eq > 0 &&
                            std::all_of(t.begin(), t.begin() + (std::ptrdiff_t)eq, IsIdentChar);
            if (kv) {
                if (eq + 1 < t.size() && t[eq + 1] != '"') t = t.substr(0, eq + 1) + SceneText::NormalizeNumber(t.substr(eq + 1));
            } else if (numeric) {
                t = SceneText::NormalizeNumber(t);
            }
        }
        if (i) out += ' ';
        out += t;
    }
    r.text = out;
}

bool AllKnownObject(const std::vector<SceneText::Record>& recs) {
    for (const auto& r : recs) if (!SceneText::IsKnownRecord(r.key)) return false;
    return true;
}

void SortObjectRecords(std::vector<SceneText::Record>& recs) {
    std::stable_sort(recs.begin(), recs.end(), [](const SceneText::Record& a, const SceneText::Record& b) {
        return ObjectRank(a.key) < ObjectRank(b.key);
    });
}

void SortGlobalRecords(std::vector<SceneText::Record>& recs) {
    for (const auto& r : recs) if (GlobalRank(r.key) < 0) return;   // an unknown record: keep file order
    std::stable_sort(recs.begin(), recs.end(), [](const SceneText::Record& a, const SceneText::Record& b) {
        return GlobalRank(a.key) < GlobalRank(b.key);
    });
}

} // namespace

// ---- helpers ----------------------------------------------------------------

int SceneText::CurrentVersion() { return SceneSerializer::kSceneFormatVersion; }

std::string SceneText::Quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

std::string SceneText::Unquote(const std::string& t) {
    if (t.empty() || t[0] != '"') return t;
    std::string out;
    for (std::size_t i = 1; i < t.size(); ++i) {
        const char c = t[i];
        if (c == '\\') { if (i + 1 < t.size()) out += t[++i]; }
        else if (c == '"') break;
        else out += c;
    }
    return out;
}

bool SceneText::IsValidGuid(const std::string& g) {
    if (g.empty() || g.size() > 64) return false;
    for (char c : g)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == '-')) return false;
    return true;
}

bool SceneText::IsKnownRecord(const std::string& key) {
    if (KnownObjectKeys().count(key)) return true;
    return ComponentSerializerRegistry::Find(key) != nullptr;
}

bool SceneText::IsKnownGlobalRecord(const std::string& key) { return GlobalRank(key) >= 0; }

std::string SceneText::NormalizeNumber(const std::string& t) {
    if (t.empty()) return t;
    std::size_t i = (t[0] == '-') ? 1 : 0;
    const std::size_t dot = t.find('.', i);
    if (dot == std::string::npos) return t;
    for (std::size_t k = i; k < dot; ++k) if (t[k] < '0' || t[k] > '9') return t;
    for (std::size_t k = dot + 1; k < t.size(); ++k) if (t[k] < '0' || t[k] > '9') return t;
    const std::string sign = i ? "-" : "";
    const std::string ip = t.substr(i, dot - i);
    std::string fp = t.substr(dot + 1);
    if (ip.empty() && fp.empty()) return t;
    while (!fp.empty() && fp.back() == '0') fp.pop_back();
    if (fp.empty()) return ip.empty() ? t : sign + ip;
    return sign + ip + "." + fp;
}

std::string SceneText::KeyOf(const std::string& rec) {
    std::size_t b = 0;
    while (b < rec.size() && IsBlank(rec[b])) ++b;
    std::size_t e = b;
    while (e < rec.size() && !IsBlank(rec[e])) ++e;
    return rec.substr(b, e - b);
}

std::vector<SceneText::Token> SceneText::Tokenize(const std::string& s) {
    std::vector<Token> toks;
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n) {
        while (i < n && IsBlank(s[i])) ++i;
        if (i >= n) break;
        const std::size_t start = i;
        bool inQuote = false;
        char prev = ' ';
        while (i < n) {
            const char c = s[i];
            if (inQuote) {
                if (c == '\\' && i + 1 < n) { i += 2; prev = 'x'; continue; }
                if (c == '"') inQuote = false;
                prev = c;
                ++i;
                continue;
            }
            if (IsBlank(c)) break;
            if (c == '"' && (i == start || prev == '=')) inQuote = true;
            prev = c;
            ++i;
        }
        Token t;
        t.text = s.substr(start, i - start);
        t.quoted = !t.text.empty() && t.text[0] == '"';
        toks.push_back(std::move(t));
    }
    return toks;
}

// ---- Document ---------------------------------------------------------------

bool SceneText::Parse(const std::string& text, Document& out, std::string* error) {
    out = Document();
    auto fail = [&](const std::string& what, std::size_t at) {
        if (error) *error = what + " (line " + std::to_string(LineNumberAt(text, at)) + ")";
        return false;
    };
    LineScanner sc(text);
    std::size_t b = 0, e = 0;
    bool open = false, haveHeader = false;
    int cur = -1;
    std::unordered_set<int> seen;
    while (sc.Next(b, e, open)) {
        if (open) return fail("unterminated quoted string", b);
        const std::string line = Trim(text.substr(b, e - b));
        if (line.empty()) continue;
        const std::string key = KeyOf(line);
        if (!haveHeader) {
            if (key != "okayscene") { if (error) *error = "missing 'okayscene' header"; return false; }
            const auto toks = Tokenize(line);
            long long v = 0;
            if (toks.size() != 2 || !ParseInt(toks[1].text, v) || v < 1 || v > 1000000) {
                if (error) *error = "bad 'okayscene' header";
                return false;
            }
            out.version = (int)v;
            haveHeader = true;
            continue;
        }
        if (cur >= 0) {
            if (key == "end") {
                if (Tokenize(line).size() != 1) return fail("text after 'end'", b);
                cur = -1;
                continue;
            }
            if (key == "gameobject")
                return fail("object " + std::to_string(out.objects[(std::size_t)cur].index) + " is cut short (no 'end')", b);
            out.objects[(std::size_t)cur].records.push_back({key, line});
            continue;
        }
        if (key == "end") return fail("'end' outside an object", b);
        if (key == "gameobject") {
            const auto toks = Tokenize(line);
            long long idx = 0;
            if (toks.size() < 3 || !ParseInt(toks[1].text, idx) || idx < -1000000000LL || idx > 1000000000LL)
                return fail("malformed gameobject header", b);
            if (!seen.insert((int)idx).second) return fail("duplicate gameobject index " + std::to_string(idx), b);
            DocObject o;
            o.index = (int)idx;
            o.name = Unquote(toks[2].text);
            for (std::size_t i = 3; i < toks.size(); ++i) { if (i > 3) o.headerTail += ' '; o.headerTail += toks[i].text; }
            out.objects.push_back(std::move(o));
            cur = (int)out.objects.size() - 1;
            continue;
        }
        out.globals.push_back({key, line});
    }
    if (!haveHeader) { if (error) *error = "missing 'okayscene' header"; return false; }
    if (cur >= 0) {
        if (error) *error = "object " + std::to_string(out.objects[(std::size_t)cur].index) + " is cut short (no 'end')";
        return false;
    }
    return true;
}

std::string SceneText::Write(const Document& d) {
    std::string out;
    out += "okayscene " + std::to_string(d.version) + "\n";
    for (const auto& g : d.globals) { out += g.text; out += '\n'; }
    for (const auto& o : d.objects) {
        out += "gameobject " + std::to_string(o.index) + " " + Quote(o.name);
        if (!o.headerTail.empty()) { out += ' '; out += o.headerTail; }
        out += '\n';
        for (const auto& r : o.records) { out += "  "; out += r.text; out += '\n'; }
        out += "end\n";
    }
    return out;
}

// ---- Canonical form ---------------------------------------------------------

std::string SceneText::Canonicalize(const std::string& text) {
    return Canonicalize(text, CanonicalOptions(), nullptr);
}

std::string SceneText::Canonicalize(const std::string& text, const CanonicalOptions& opt, CanonicalReport* report) {
    CanonicalReport local;
    CanonicalReport& rep = report ? *report : local;
    rep = CanonicalReport();
    Document d;
    std::string err;
    if (!Parse(text, d, &err)) {
        rep.skipped = true;
        rep.note = "left as is, it does not parse: " + err;
        return text;
    }
    if (d.version > CurrentVersion()) {
        rep.skipped = true;
        rep.note = "left as is, okayscene " + std::to_string(d.version) + " is newer than this build reads (" +
                   std::to_string(CurrentVersion()) + ")";
        return text;
    }
    rep.objects = (int)d.objects.size();

    for (auto& g : d.globals) NormalizeRecord(g, IsKnownGlobalRecord(g.key), opt.normalizeNumbers);
    if (opt.sortHeaderRecords) SortGlobalRecords(d.globals);

    for (auto& o : d.objects) {
        for (auto& r : o.records) NormalizeRecord(r, IsKnownRecord(r.key), opt.normalizeNumbers);
        if (opt.sortHeaderRecords && AllKnownObject(o.records)) SortObjectRecords(o.records);
    }

    if (opt.renumberObjects) {
        std::unordered_map<int, int> remap;
        for (std::size_t i = 0; i < d.objects.size(); ++i) remap[d.objects[i].index] = (int)i;
        for (std::size_t i = 0; i < d.objects.size(); ++i) {
            for (auto& r : d.objects[i].records) {
                if (r.key != "parent") continue;
                const auto toks = Tokenize(r.text);
                long long p = 0;
                if (toks.size() < 2 || !ParseInt(toks[1].text, p)) continue;   // malformed: leave it
                auto it = remap.find((int)p);
                // A parent number no object has means "no parent" to the loader: say -1,
                // so the new numbering cannot make it point somewhere by accident.
                const int np = (p >= 0 && p <= 1000000000LL && it != remap.end()) ? it->second : -1;
                std::string text2 = "parent " + std::to_string(np);
                for (std::size_t k = 2; k < toks.size(); ++k) text2 += " " + toks[k].text;
                r.text = text2;
            }
        }
        for (std::size_t i = 0; i < d.objects.size(); ++i) d.objects[i].index = (int)i;
    }

    const std::string out = Write(d);
    rep.changed = out != text;
    return out;
}

// ---- Model ------------------------------------------------------------------

bool SceneText::BuildModel(const std::string& text, Model& out, std::string* error) {
    Document d;
    if (!Parse(text, d, error)) return false;
    return BuildModel(d, out, error);
}

bool SceneText::BuildModel(const Document& d, Model& m, std::string* error) {
    m = Model();
    m.version = d.version;
    m.globals = d.globals;
    const std::size_t n = d.objects.size();
    m.objects.resize(n);
    std::unordered_map<int, int> posOfIndex;
    for (std::size_t i = 0; i < n; ++i) posOfIndex[d.objects[i].index] = (int)i;

    std::unordered_set<std::string> seenGuids;
    std::vector<int> hint(n, -1);
    for (std::size_t i = 0; i < n; ++i) {
        const DocObject& o = d.objects[i];
        ModelObject& mo = m.objects[i];
        mo.name = o.name;
        bool haveGuid = false;
        // A guid may also sit on the header line after the name (the loader takes it):
        // parse it here so the model, the validator and diff/merge all see the same
        // identity, instead of the object silently degrading to path matching.
        std::string tailRest, guidToken;
        {
            const auto toks = Tokenize("x " + o.headerTail);   // "x" stands in for the name
            bool wantValue = false;
            for (std::size_t t = 1; t < toks.size(); ++t) {
                if (wantValue) { guidToken = toks[t].text; wantValue = false; continue; }
                if (toks[t].text == "guid" && !haveGuid) { haveGuid = true; wantValue = true; continue; }
                if (!tailRest.empty()) tailRest += ' ';
                tailRest += toks[t].text;
            }
        }
        if (haveGuid) {
            const std::string g = Unquote(guidToken);
            if (!IsValidGuid(g)) m.warnings.push_back("ignored malformed guid on object " + std::to_string(o.index));
            else if (!seenGuids.insert(g).second) m.warnings.push_back("duplicate guid on object " + std::to_string(o.index));
            else mo.guid = g;
        }
        mo.headerTail = tailRest;
        mo.fileIndex = o.index;
        for (const Record& r : o.records) {
            if (r.key == "guid") {
                const auto toks = Tokenize(r.text);
                if (haveGuid || toks.size() < 2) { m.warnings.push_back("object " + std::to_string(o.index) + " has a stray guid record; ignored"); continue; }
                haveGuid = true;
                const std::string g = Unquote(toks[1].text);
                if (!IsValidGuid(g)) m.warnings.push_back("ignored malformed guid on object " + std::to_string(o.index));
                else if (!seenGuids.insert(g).second) m.warnings.push_back("duplicate guid on object " + std::to_string(o.index));
                else mo.guid = g;
                continue;
            }
            if (r.key == "parent") {
                const auto toks = Tokenize(r.text);
                long long p = 0;
                if (toks.size() < 2 || !ParseInt(toks[1].text, p)) {
                    if (error) *error = "malformed 'parent' record on object " + std::to_string(o.index);
                    return false;
                }
                if (p >= 0 && p <= 1000000000LL) {
                    auto it = posOfIndex.find((int)p);
                    if (it != posOfIndex.end()) mo.parent = it->second;   // the last usable one wins, as in the loader
                    else m.warnings.push_back("object " + std::to_string(o.index) + " names a parent that does not exist");
                }
                continue;
            }
            if (r.key == "sibling") {
                const auto toks = Tokenize(r.text);
                long long p = 0;
                if (toks.size() >= 2 && ParseInt(toks[1].text, p) && p >= 0 && p < 1000000000LL) hint[i] = (int)p;
                continue;
            }
            mo.records.push_back(r);
        }
    }

    // A parent loop is refused by the loader, so refuse it here.
    for (std::size_t i = 0; i < n; ++i) {
        std::size_t steps = 0;
        for (int p = m.objects[i].parent; p >= 0; p = m.objects[(std::size_t)p].parent)
            if (++steps > n) {
                if (error) *error = "object " + std::to_string(d.objects[i].index) + " has a parent cycle";
                return false;
            }
    }

    // Children in loader order: attached in file order, then the saved positions.
    m.children.assign(n, {});
    for (std::size_t i = 0; i < n; ++i) {
        const int p = m.objects[i].parent;
        if (p < 0) m.roots.push_back((int)i);
        else m.children[(std::size_t)p].push_back((int)i);
    }
    for (std::size_t p = 0; p < n; ++p) {
        std::vector<int>& cur = m.children[p];
        bool any = false;
        for (int c : cur) any = any || hint[(std::size_t)c] >= 0;
        if (!any) continue;
        const std::size_t cnt = cur.size();
        std::vector<int> target(cnt, -1), hinted;
        for (int c : cur) if (hint[(std::size_t)c] >= 0) hinted.push_back(c);
        std::stable_sort(hinted.begin(), hinted.end(), [&](int a, int b) { return hint[(std::size_t)a] < hint[(std::size_t)b]; });
        for (int c : hinted) {
            const std::size_t at = (std::size_t)hint[(std::size_t)c];
            if (at < cnt && target[at] < 0) target[at] = c;
        }
        std::unordered_set<int> placed;
        for (int t : target) if (t >= 0) placed.insert(t);
        std::size_t fill = 0;
        for (int c : cur) {
            if (placed.count(c)) continue;
            while (fill < cnt && target[fill] >= 0) ++fill;
            if (fill < cnt) target[fill++] = c;
        }
        cur = target;
    }

    // Paths: parent path + "/" + name, with #n when siblings share one.
    std::vector<std::string> path(n);
    std::vector<char> done(n, 0);
    std::unordered_map<std::string, int> uses;
    // Parents first: order objects by depth via a simple pass over roots.
    std::vector<int> stack;
    for (auto it = m.roots.rbegin(); it != m.roots.rend(); ++it) stack.push_back(*it);
    // Depth-first in file order within each family; uses[] counts per full path.
    while (!stack.empty()) {
        const int i = stack.back();
        stack.pop_back();
        if (done[(std::size_t)i]) continue;
        done[(std::size_t)i] = 1;
        const int p = m.objects[(std::size_t)i].parent;
        std::string base = (p >= 0 ? path[(std::size_t)p] + "/" : std::string()) + m.objects[(std::size_t)i].name;
        const int k = uses[base]++;
        path[(std::size_t)i] = k ? base + "#" + std::to_string(k) : base;
        const auto& ch = m.children[(std::size_t)i];
        for (auto it = ch.rbegin(); it != ch.rend(); ++it) stack.push_back(*it);
    }
    for (std::size_t i = 0; i < n; ++i) m.objects[i].path = path[i];
    return true;
}

void SceneText::SetFileChildOrder(Model& m) {
    const std::size_t n = m.objects.size();
    m.children.assign(n, {});
    m.roots.clear();
    for (std::size_t i = 0; i < n; ++i) {
        const int p = m.objects[i].parent;
        if (p < 0) m.roots.push_back((int)i);
        else m.children[(std::size_t)p].push_back((int)i);
    }
}

std::string SceneText::Emit(const Model& m) {
    const std::size_t n = m.objects.size();
    // Where file order and the wanted child order disagree, write `sibling` hints for
    // every child of that parent (what the scene writer does).
    std::vector<int> hint(n, -1);
    for (std::size_t p = 0; p < m.children.size() && p < n; ++p) {
        const std::vector<int>& kids = m.children[p];
        if (kids.size() < 2) continue;
        std::vector<int> sorted = kids;
        std::sort(sorted.begin(), sorted.end());
        if (sorted == kids) continue;
        for (std::size_t q = 0; q < kids.size(); ++q) hint[(std::size_t)kids[q]] = (int)q;
    }
    Document d;
    d.version = m.version;
    d.globals = m.globals;
    d.objects.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const ModelObject& mo = m.objects[i];
        DocObject& o = d.objects[i];
        o.index = (int)i;
        o.name = mo.name;
        o.headerTail = mo.headerTail;
        std::vector<Record> recs;
        if (!mo.guid.empty()) recs.push_back({"guid", "guid " + Quote(mo.guid)});
        std::vector<Record> tail;
        tail.push_back({"parent", "parent " + std::to_string(mo.parent)});
        if (hint[i] >= 0) tail.push_back({"sibling", "sibling " + std::to_string(hint[i])});
        if (AllKnownObject(mo.records)) {
            recs.insert(recs.end(), mo.records.begin(), mo.records.end());
            recs.insert(recs.end(), tail.begin(), tail.end());
            SortObjectRecords(recs);
        } else {
            // Unknown records are not reordered: parent and sibling go after the
            // leading bookkeeping records, where the writer puts them.
            std::size_t lead = 0;
            while (lead < mo.records.size() && ObjectRank(mo.records[lead].key) < 100) ++lead;
            recs.insert(recs.end(), mo.records.begin(), mo.records.begin() + (std::ptrdiff_t)lead);
            recs.insert(recs.end(), tail.begin(), tail.end());
            recs.insert(recs.end(), mo.records.begin() + (std::ptrdiff_t)lead, mo.records.end());
        }
        o.records = std::move(recs);
    }
    return Write(d);
}

// ---- Validation -------------------------------------------------------------

SceneText::Validation SceneText::Validate(const std::string& text) {
    Validation v;
    Model m;
    std::string err;
    if (!BuildModel(text, m, &err)) {
        v.ok = false;
        v.problems.push_back(err);
        return v;
    }
    for (const auto& w : m.warnings) { v.ok = false; v.problems.push_back(w); }
    return v;
}

} // namespace okay
