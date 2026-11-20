#pragma once
// The Add Component browser as plain data (no ImGui, no scene): the list of components, their plain-word categories,
// one-line descriptions and search words, the search ranking, "Recent" and "Favorites", "Goes well with this object"
// suggestions, the reasons a greyed-out component gives, the keyboard state machine and the text shown after an add.
// editor/AddComponentMenu.cpp draws it; tests/test_add_component_model.cpp checks it without a window.
//
// Written from the owner's request (2026-11-19) and from general ideas (prefix / word / substring / subsequence matching,
// edit distance for "Did you mean"); no code from another project.
#include <string>
#include <vector>

namespace oked {
namespace addcomp {

/// One component the browser offers. `label` is exactly what the menu code passes (used as the stable key with the raw
/// category), `name` is the part to show big, `description` one plain line.
struct Entry {
    std::string key;            // raw category + "|" + label: unique, used to run the add
    std::string label;          // as passed by the menu code
    std::string name;           // shown big ("Joint 3D")
    std::string description;    // one plain line
    std::string category;       // plain-word category ("Physics"); "Uncategorised" when no rule knows it
    std::string group;          // sub-group inside the category ("Colliders 3D"), "" when the category has none
    std::string nkey;           // KeyOf + Canon: the one stable id of a component (rows and registry types agree on it)
    std::string reason;         // why it cannot be added (empty = can be added)
    std::vector<std::string> aliases;   // other words people use for it
    bool available = true;
    bool present = false;       // true when the reason is "already on this object"
};

/// What the menu code reports for each component: it can be added (`absent`) or not. The raw category the host passes is only
/// part of the unique `key`; the real place comes from Classify() (so a new component needs no per-type edit when a rule covers it).
Entry MakeEntry(const std::string& rawCategory, const std::string& label, bool absent);

// ---- the taxonomy ---------------------------------------------------------------------------------------------------------
/// "Box Collider 3D (a box)" / "BoxCollider3D" / "Stat: Health" -> "boxcollider3d" / "stathealth": lower case letters and digits,
/// without the bracket text. Menu labels and registry type names agree on it.
std::string KeyOf(const std::string& nameOrLabel);
/// The one key of a component when the menu row and the registry spell it differently ("light" -> "directionallight").
std::string Canon(const std::string& key);
struct Place { std::string category, group; };
extern const char* const kUncategorised;
/// Where a component (a Canon key) lives. {kUncategorised, ""} when no rule knows it: a test fails then, so it gets a rule.
Place Classify(const std::string& key);
const std::vector<std::string>& CategoryOrder();               // the real categories, in the order the left column shows them
std::vector<std::string> GroupOrder(const std::string& category);   // its sub-groups, in order
std::vector<std::string> RuleExactKeys();                      // every exact key the rules name (a test checks none is listed twice)
std::string PlainCategory(const std::string& rawCategory, const std::string& name);   // = Classify(Canon(KeyOf(name))).category
/// The component a row needs on the object first ("" = none): "buoyancy" -> "rigidbody3d". Used to grey it with a reason.
std::string PrereqOf(const std::string& key);
/// Greys the rows whose prerequisite is not on the object ("Needs a Rigidbody3D first.").
void ApplyPrereqs(std::vector<Entry>& entries);
std::string Pretty(const std::string& key);   // "rigidbody3d" -> "Rigidbody3D" for the reason text (the row's own name when it is listed)

// ---- the word lists -----------------------------------------------------------------------------------------------------
/// "Joint 3D (distance/spring/pin)" -> name "Joint 3D", hint "distance/spring/pin". Short brackets like "(3D)" stay in the name.
void SplitLabel(const std::string& label, std::string& name, std::string& hint);
std::string Describe(const std::string& name, const std::string& hint, const std::string& category);
std::vector<std::string> AliasesOf(const std::string& name);
/// Components this one adds next to itself when they are missing ("Mesh Renderer" for LOD Group), for the card text.
std::vector<std::string> AlsoAdds(const std::string& name);
/// Short badge letters: "Box Collider 3D" -> "BC".
std::string Badge(const std::string& name);
/// "okay::MeshRenderer" or "class okay::MeshRenderer" -> "Mesh Renderer" (the already-readable form of a type name).
std::string PrettyTypeName(const std::string& demangled);
/// The readable name of a component's C++ type (demangles on GCC / Clang).
std::string PrettyTypeNameFromTypeInfo(const char* rawName);

// ---- search ---------------------------------------------------------------------------------------------------------------
struct Match {
    int score = 0;              // 0 = no match
    std::vector<int> hits;      // letters of the name to highlight
};
/// Ranking: whole name > name start > word start > substring > letters in order > description words > other words.
/// Several words in the query must all match. Empty query matches everything with score 1.
Match MatchEntry(const Entry& e, const std::string& query);
/// For a search with no results: up to three names that are close to what was typed.
std::vector<std::string> DidYouMean(const std::vector<Entry>& entries, const std::string& query);
int EditDistance(const std::string& a, const std::string& b);

// ---- Recent and Favorites (kept between runs) -------------------------------------------------------------------------
struct Prefs {
    std::vector<std::string> recent;       // nkeys, newest first (at most 8)
    std::vector<std::string> favorites;    // nkeys
    void NoteUsed(const std::string& name);
    bool IsFavorite(const std::string& name) const;
    bool IsRecent(const std::string& name) const;
    void ToggleFavorite(const std::string& name);
    std::string Serialize() const;
    static Prefs Parse(const std::string& text);
};
bool LoadPrefs(const std::string& path, Prefs& out);
bool SavePrefs(const std::string& path, const Prefs& p);

// ---- suggestions ------------------------------------------------------------------------------------------------------
struct Suggestion { std::string name;   // a menu label
    std::string why; };
/// Components that go well with what is already on the object (`presentLabels` = menu labels of the components on it).
std::vector<Suggestion> Suggest(const std::vector<std::string>& presentLabels);

// ---- the view -----------------------------------------------------------------------------------------------------------
inline const char* kCatForObject = "Suggested for this object";
inline const char* kCatRecent    = "Recently used";
inline const char* kCatFavorites = "Favourites";

struct Row {
    int entry = -1;             // index in the entry list
    int score = 0;
    std::vector<int> hits;
    std::string note;           // e.g. the suggestion's "why"
    std::string group;          // the sub-group heading above this card ("" = none)
    bool groupStart = false;    // first card of its group: draw the heading
};
struct CatCount { std::string name; int count = 0; bool special = false; };

enum class Column { Categories, Cards };
enum class Key { Up, Down, PageUp, PageDown, Home, End, Enter, Escape, Tab };

struct State {
    std::string query;
    std::string category;       // a CatCount name
    int cursor = 0;             // index in the visible rows
    Column column = Column::Cards;
    std::string notice;         // a line under the list (why something cannot be added, ...)
    void SetQuery(const std::string& q) { if (q != query) { query = q; cursor = 0; column = Column::Cards; notice.clear(); } }
    void SelectCategory(const std::string& c) { category = c; cursor = 0; query.clear(); notice.clear(); }
};

struct View {
    std::vector<Row> rows;                 // the right column, in order
    std::vector<CatCount> cats;            // the left column, in order
    std::vector<std::string> didYouMean;   // when a search has no results
    bool searching = false;
};

View Build(const std::vector<Entry>& entries, const State& s, const Prefs& prefs);
/// The category to start on: "For this object" when there are suggestions, else Recent, else the first real one.
std::string DefaultCategory(const std::vector<Entry>& entries, const Prefs& prefs);
void ClampCursor(State& s, const View& v);

struct Outcome {
    int activate = -1;          // entry index to add now
    bool close = false;
    bool rebuild = false;       // the state changed: build the view again
};
Outcome HandleKey(State& s, const View& v, const std::vector<Entry>& entries, Key k);
/// A click or "+" on a row: runs it when it can be added, explains in `s.notice` when it cannot.
Outcome Choose(State& s, const std::vector<Entry>& entries, int entryIndex);

/// "Added Rigidbody3D." / "Added LOD Group. It needs a Mesh Renderer, so that was added too."
std::string AddedNotice(const std::string& name, const std::vector<std::string>& alsoAdded);

}  // namespace addcomp
}  // namespace oked
