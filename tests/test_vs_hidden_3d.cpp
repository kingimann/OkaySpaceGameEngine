// The Add Node menu lists the 3D script-function nodes, and no node title says "Fourth" (#888 finding 10).
// Before the fix the builtin title dropped digits, so box_cast3 was "Box Cast", the same title as box_cast. The
// palette shows one row per title, so every 3D twin was hidden. "Physics2D" split into "Physics", "D", and the
// single letter "d" was read as the pin word "fourth" ("Physics Fourth Raycast"). This test checks:
//   * every Fn.<builtin> node whose builtin name holds a 3 (box_cast3, ray3_x, create_object_3d...) is found by
//     the Add Node search, using the same score the menu uses (VsPaletteSearchScore);
//   * no title made from an identifier holds a pin-ordinal word (First ... Thirteenth) unless the identifier itself
//     has that word (the builtin `first` is "First"; "Frames Per Second" is fps);
//   * every row left on the hidden-until-tested list has a written reason;
//   * a few titles, so the wording stays readable.
#include "test_framework.hpp"
#include "VisualScriptEditor.hpp"
#include "VisualScriptPalette.hpp"
#include "okay/VisualScript/VsBuiltins.hpp"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace okay::editor::vsed;

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

// The words of a title or identifier, lower-cased: split at spaces, '_', '.', digits and lower-to-upper steps.
std::vector<std::string> Words(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = (unsigned char)s[i];
        if (!std::isalpha(c)) { if (!cur.empty()) out.push_back(Lower(cur)); cur.clear(); continue; }
        if (std::isupper(c) && !cur.empty() && std::islower((unsigned char)cur.back())) { out.push_back(Lower(cur)); cur.clear(); }
        cur += (char)c;
    }
    if (!cur.empty()) out.push_back(Lower(cur));
    return out;
}

const VsDef* FindRow(const std::string& type) {
    for (const VsDef& d : Registry()) if (type == d.type) return &d;
    return nullptr;
}

const char* TitleOf(const char* type) {
    const VsDef* d = FindRow(type);
    return d && d->title ? d->title : "";
}

} // namespace

int main() {
    RUN_SUITE("vs_hidden_3d");

    // ---- 1. Every 3D builtin node is listed and found by the menu's search --------------------------------------
    int threeD = 0, found = 0, aliasRows = 0;
    for (const okay::vs::BuiltinNodeInfo& b : okay::vs::BuiltinNodes()) {
        if (b.builtin.find('3') == std::string::npos) continue;
        const VsDef* d = FindRow(b.type);
        CHECK(d != nullptr);
        if (!d) { std::printf("  %s: no registry row\n", b.type.c_str()); continue; }
        ++threeD;
        if (std::getenv("OKAY_PRINT_TITLES")) std::printf("  %-26s \"%s\"\n", b.type.c_str(), d->title ? d->title : "");
        const VsBuiltinAliasRow* alias = nullptr;
        for (const VsBuiltinAliasRow& a : kVsBuiltinAliasRows) if (b.type == a.hidden) alias = &a;
        if (alias) {
            // The same function under another name: hidden, with a reason, and its name finds the listed row.
            const VsDef* twin = FindRow(alias->listed);
            CHECK(alias->reason && std::strlen(alias->reason) >= 10);
            CHECK(twin && !VsPaletteHidden(*twin));
            CHECK(twin && VsPaletteSearchScore(*twin, Lower(b.builtin)) > 0);
            ++aliasRows;
            std::printf("  alias %-20s listed as %s: %s\n", b.type.c_str(), alias->listed, alias->reason);
            continue;
        }
        const bool hidden = VsPaletteHidden(*d);
        const int byName = VsPaletteSearchScore(*d, Lower(b.builtin));
        const int byTitle = d->title ? VsPaletteSearchScore(*d, Lower(d->title)) : 0;
        CHECK(!hidden);
        CHECK(byName > 0);
        CHECK(byTitle >= 1000);   // its own title is an exact match, so it comes first
        if (!hidden && byName > 0 && byTitle >= 1000) ++found;
        else std::printf("  HIDDEN %-24s \"%s\"\n", b.type.c_str(), d->title ? d->title : "");
    }
    CHECK(threeD >= 40);
    CHECK(found + aliasRows == threeD);
    std::printf("  3D builtin nodes: %d, listed and found by search: %d, same function as a listed row: %d\n",
                threeD, found, aliasRows);

    // ---- 2. No title has a pin-ordinal word that its identifier does not have -----------------------------------
    static const char* kOrdinals[] = {"first", "second", "third", "fourth", "fifth", "sixth", "seventh",
                                      "eighth", "ninth", "tenth", "eleventh", "twelfth", "thirteenth"};
    int ordinalTitles = 0;
    for (const VsDef& d : Registry()) {
        // Only titles made from an identifier (Fn.* builtins, engine-API bindings with a '.'); hand-written titles
        // such as "Replace First Match" are written by a person and may use these words.
        if (!d.title || !std::strchr(d.type, '.')) continue;
        const std::vector<std::string> tw = Words(d.title), iw = Words(d.type);
        for (std::size_t k = 0; k < tw.size(); ++k) {
            const std::string& w = tw[k];
            if (w == "second" && k > 0 && tw[k - 1] == "per") continue;   // "Frames Per Second" (fps)
            bool ordinal = false, inIdent = false;
            for (const char* o : kOrdinals) if (w == o) ordinal = true;
            for (const std::string& x : iw) if (x == w) inIdent = true;
            if (ordinal && !inIdent) {
                ++ordinalTitles;
                std::printf("  ORDINAL %-34s \"%s\"\n", d.type, d.title);
                break;
            }
        }
    }
    CHECK(ordinalTitles == 0);
    std::printf("  titles with a leaked pin word: %d\n", ordinalTitles);

    // ---- 3. Every row still hidden until tested says why ---------------------------------------------------------
    int hiddenRows = 0;
    for (const VsHiddenUntilTested& h : kHiddenUntilTested) {
        ++hiddenRows;
        CHECK(h.type && h.type[0]);
        CHECK(h.reason && std::strlen(h.reason) >= 10);
        if (!h.reason || std::strlen(h.reason) < 10) std::printf("  %s has no reason\n", h.type);
    }
    std::printf("  hidden-until-tested rows, each with a reason: %d\n", hiddenRows);

    // ---- 4. Titles read as words, and the 2D and 3D twins differ -------------------------------------------------
    CHECK(std::string(TitleOf("Fn.box_cast3")) == "Box Cast in Three Dimensions");
    CHECK(std::string(TitleOf("Fn.raycast3")) == "Raycast in Three Dimensions");
    CHECK(std::string(TitleOf("Fn.Physics2D.Raycast")) == "Physics Raycast in Two Dimensions");
    CHECK(std::string(TitleOf("Fn.create_object_3d")) == "Create Object in Three Dimensions");
    CHECK(std::string(TitleOf("Fn.ForceMode2D.Impulse")) == "Force Mode Impulse in Two Dimensions");
    CHECK(std::string(TitleOf("Fn.max3")) == "Largest of Three");
    CHECK(std::string(TitleOf("Fn.min3")) == "Smallest of Three");
    CHECK(std::string(TitleOf("Fn.clamp01")) == "Keep Between Zero and One");
    CHECK(std::string(TitleOf("Fn.box_cast")) != TitleOf("Fn.box_cast3"));
    for (const char* t : {"Fn.box_cast3", "Fn.Physics2D.Raycast", "Fn.ray3_x", "Fn.max3", "Fn.deg2rad", "Fn.Mathf.Log10"}) {
        const std::string why = LintVsTitle(TitleOf(t), VsNameKind::Calculation);
        CHECK(why.empty());
        if (!why.empty()) std::printf("  %s \"%s\": %s\n", t, TitleOf(t), why.c_str());
    }
    // Pins keep their own words: the inputs of a four-input builtin are still First ... Fourth.
    CHECK(VsFriendlyPin("d", false) == "Fourth");
    // The search finds the 3D raycast by its text-script name, as docs/try_it.md says.
    if (const VsDef* r = FindRow("Fn.raycast3")) CHECK(VsPaletteSearchScore(*r, "raycast3") > 0);
    else CHECK(false);

    TEST_MAIN_RESULT();
}
