#pragma once
// UI library slice 2, game HUD parts. Each is a component you put on a UI object; the renderer draws it on one shared
// layer (uilayers::K_GameHud, see UIElement.hpp and UIGameHudDraw.cpp).
//
//   UIRadialMenu         a wheel of choices opened by a key or a call; the pointer picks a wedge by direction
//   UIKillFeed           the stack of "A > B" lines in a corner, newest on top, fading out
//   UIAmmoCounter        "12 / 90" with bullet pips, from a RangedWeapon, from variables or typed in
//   UIInteractionPrompt  a key cap and a word ("E  Open") that shows only when the player is near something
//   UIObjectiveTracker   the active quests and their objectives from the scene's QuestLog
//   UICompass            a heading strip with N E S W, degrees and markers for named objects
//   UIItemTooltipCard    the card for one item: name in its rarity colour, text, stats, compare with what is worn
//   UIAbilityIcon        an ability icon with a cooldown ring (the ring is the sibling UIRadialProgress), key and charges
//
// Written for this engine from the plain ideas of each part (a wheel split into equal wedges, a list that ages out, a
// distance check, a bearing); no other project's code, fonts or images were used. They reuse our own helpers only
// (QuestLog, ItemDatabase and DescribeItem, RangedWeapon, UIRadialProgress, the UI painter).
#include "okay/Components/UIWidgetUtil.hpp"
#include "okay/Components/UIRadialProgress.hpp"
#include "okay/Components/UIVars.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>
#include <string>
#include <vector>

namespace okay {

class Scene;

/// What every HUD part has: a rectangle placed by an anchor, and three colours.
class UIHudBase : public Behaviour {
public:
    Vec2 position{20.0f, 20.0f};
    Vec2 size{240.0f, 120.0f};
    UIAnchor anchor = UIAnchor::TopLeft;
    Color background = Color::FromBytes(24, 28, 38, 210);
    Color accent = Color::FromBytes(66, 150, 250);
    Color textColor = Color::White;
};

// ---- Radial menu ----------------------------------------------------------------------------------------------------

/// A wheel of up to 12 choices. The wheel is the largest circle in `size`. Open it with `openKey` (hold the key, point,
/// let go to choose, like most games) or with Open() from a script; in click mode a click on a wedge chooses it and a
/// click outside (or Escape) closes it. The wedge is picked by the direction of the pointer from the centre, so you never
/// have to aim at the exact slice. The object's script gets on_select(); Chosen() is the picked index.
class UIRadialMenu final : public UIHudBase {
public:
    std::string items = "Attack|Defend|Heal|Run";   ///< the labels, separated by |
    float innerRadius = 48.0f;                       ///< the dead zone in the middle (pixels)
    char openKey = 0;                                ///< 0 = only Open() opens it
    bool holdToOpen = true;                          ///< hold the key, point, release to choose (else click mode)
    bool closeOnSelect = true;
    bool alwaysShown = false;                        ///< draw it even while closed (a fixed wheel)
    std::string centerText;                          ///< shown in the middle (empty = nothing; the hovered wedge is lit anyway)
    /// A variable that receives the chosen wedge number; "<name>_text" receives its label (a UI Text Bind can show it).
    std::string chosenVar;

    static constexpr int kMaxItems = 12;

    std::vector<std::string> Labels() const {
        std::vector<std::string> out;
        std::string cur;
        for (char c : items) { if (c == '|') { out.push_back(cur); cur.clear(); } else cur += c; }
        if (!cur.empty() || !out.empty()) out.push_back(cur);
        if ((int)out.size() > kMaxItems) out.resize(kMaxItems);
        return out;
    }
    int Count() const { return (int)Labels().size(); }

    bool IsOpen() const { return m_open; }
    int Hovered() const { return m_hover; }
    int Chosen() const { return m_chosen; }

    /// The wedge the vector (dx, dy) from the centre points into: 0 is straight up and the numbers go clockwise.
    /// -1 inside the dead zone or when there are no wedges.
    static int SegmentAt(float dx, float dy, int count, float innerRadius) {
        if (count <= 0) return -1;
        if (std::sqrt(dx * dx + dy * dy) < innerRadius) return -1;
        float a = std::atan2(dx, -dy) * Mathf::Rad2Deg;          // 0 at the top, clockwise
        const float w = 360.0f / (float)count;
        a += w * 0.5f;                                           // wedge 0 is centred on the top
        a = std::fmod(a, 360.0f); if (a < 0.0f) a += 360.0f;
        int i = (int)(a / w);
        if (i >= count) i = count - 1;
        return i;
    }
    /// The angle (degrees clockwise from the top) at the middle of wedge i.
    static float SegmentCenter(int i, int count) { return count > 0 ? (float)i * 360.0f / (float)count : 0.0f; }

    void Open() { if (!m_open) { m_open = true; m_hover = -1; } }
    /// Close the wheel; with `choose` and a hovered wedge, choose it first. True when something was chosen.
    bool Close(bool choose) {
        const bool picked = choose && m_open && m_hover >= 0 && Choose(m_hover);
        m_open = false;
        return picked;
    }
    /// Choose wedge `i` (fires on_select). Closes the wheel when closeOnSelect.
    bool Choose(int i) {
        if (i < 0 || i >= Count()) return false;
        m_chosen = i;
        if (!chosenVar.empty()) {
            uivars::WriteNumber(gameObject, chosenVar, (float)i);
            uivars::WriteText(gameObject, chosenVar + "_text", Labels()[(std::size_t)i]);
        }
        if (closeOnSelect) m_open = false;
        FireUIEvent(gameObject, "on_select");
        return true;
    }
    /// Keyboard and gamepad: move the hover one wedge (dir -1 or +1).
    void Step(int dir) {
        const int n = Count();
        if (n <= 0) return;
        m_hover = m_hover < 0 ? (dir > 0 ? 0 : n - 1) : ((m_hover + dir) % n + n) % n;
    }
    /// Point at wedge from the pointer (tests call this; Update does it each frame while open).
    void PointAt(float dx, float dy) { m_hover = SegmentAt(dx, dy, Count(), innerRadius); }

    void Update(float) override {
        if (!enabled) return;
        if (openKey) {
            if (Input::GetKeyDown(openKey)) { if (holdToOpen) Open(); else if (m_open) Close(false); else Open(); }
            if (holdToOpen && m_open && Input::GetKeyUp(openKey)) { Close(true); return; }
        }
        if (!m_open && !alwaysShown) return;   // a fixed wheel (alwaysShown) is pointed at and clicked without opening it
        Vec2 o, sz;
        UIControlRect(anchor, position, size, o, sz);
        const Vec2 lm = UIToLocal(gameObject, o, sz, Input::MousePosition());
        PointAt(lm.x - (o.x + sz.x * 0.5f), lm.y - (o.y + sz.y * 0.5f));
        if (Input::GetKeyDown((char)27)) { Close(false); return; }
        if (Input::GetKeyDown('\r') && m_hover >= 0) { Choose(m_hover); return; }
        if ((!holdToOpen || alwaysShown) && Input::GetMouseButtonDown(0)) { if (m_hover >= 0) Choose(m_hover); else if (m_open) Close(false); }
    }

private:
    bool m_open = false;
    int m_hover = -1;
    int m_chosen = -1;
};

// ---- Kill feed ------------------------------------------------------------------------------------------------------

/// The lines "A > B" that stack in a corner when something dies, newest first, each gone after `lifetime` seconds.
/// Push() from a script (UIKillFeed.Push) or from C++ (Announce reaches every feed in the scene). A line that names
/// `playerName` is drawn with the accent colour so you spot your own kills.
class UIKillFeed final : public UIHudBase {
public:
    int maxEntries = 5;
    float lifetime = 6.0f;
    float fadeTime = 0.5f;
    float rowHeight = 26.0f;
    bool alignRight = true;
    std::string playerName = "Player";
    std::string format = "{killer}  >  {victim}";
    std::string weaponFormat = "{killer}  [{weapon}]  {victim}";   ///< used when a weapon is given

    struct Entry { std::string text; bool mine = false; float age = 0.0f; };
    const std::vector<Entry>& Entries() const { return m_entries; }

    /// Fraction of full opacity an entry has now (1 until it starts to fade, 0 at the end of its life).
    float Alpha(const Entry& e) const {
        if (fadeTime <= 0.0f) return e.age < lifetime ? 1.0f : 0.0f;
        const float left = lifetime - e.age;
        return left >= fadeTime ? 1.0f : (left <= 0.0f ? 0.0f : left / fadeTime);
    }
    void Push(const std::string& killer, const std::string& victim, const std::string& weapon = "");
    void PushText(const std::string& text, bool mine = false) {
        m_entries.insert(m_entries.begin(), Entry{text, mine, 0.0f});
        while ((int)m_entries.size() > (maxEntries < 1 ? 1 : maxEntries)) m_entries.pop_back();
    }
    void Clear() { m_entries.clear(); }
    /// Push to every kill feed in `scene`. Returns how many feeds got it.
    static int Announce(Scene& scene, const std::string& killer, const std::string& victim, const std::string& weapon = "");

    void Update(float dt) override {
        for (Entry& e : m_entries) e.age += dt;
        while (!m_entries.empty() && m_entries.back().age >= lifetime) m_entries.pop_back();
    }

private:
    std::vector<Entry> m_entries;
};

// ---- Ammo counter ---------------------------------------------------------------------------------------------------

/// "12 / 90". It reads the rounds from, in this order: the variables named in currentVar and reserveVar (when they exist),
/// a RangedWeapon (the object named `weapon`, else the object tagged Player, else the first one in the scene), or the
/// numbers typed in here. The count turns the warning colour at lowFraction of the magazine and red when empty; a reload in
/// progress shows RELOADING.
class UIAmmoCounter final : public UIHudBase {
public:
    std::string weapon;          ///< the object with the RangedWeapon (empty = the Player, then the first in the scene)
    std::string currentVar;      ///< a variable with the rounds in the gun (empty = not used)
    std::string reserveVar;      ///< a variable with the spare rounds
    std::string label = "AMMO";
    int current = 30, reserve = 90, magazine = 30;   ///< used when neither a variable nor a weapon is found
    bool reloading = false;
    float lowFraction = 0.25f;
    bool showPips = true;
    bool showReserve = true;
    Color warnColor = Color::FromBytes(250, 190, 60);
    Color emptyColor = Color::FromBytes(235, 80, 80);

    struct State { bool fromWeapon = false; int current = 0, reserve = 0, magazine = 0; bool reloading = false; };
    /// Where the numbers come from right now.
    State Read() const;
    /// 0 fine, 1 low, 2 empty.
    int Level() const {
        const State s = Read();
        if (s.current <= 0) return 2;
        return (s.magazine > 0 && (float)s.current <= lowFraction * (float)s.magazine) ? 1 : 0;
    }
    /// "12 / 90", "12" without a reserve, "12 / INF" when the spare rounds never run out.
    std::string Text() const {
        const State s = Read();
        std::string t = std::to_string(s.current < 0 ? 0 : s.current);
        if (showReserve) t += s.reserve < 0 ? std::string(" / INF") : " / " + std::to_string(s.reserve);
        return t;
    }
};

// ---- Interaction prompt ---------------------------------------------------------------------------------------------

/// A key cap and a word, like "[E] Open door". It fades in when the player is within `range` of the object named in
/// `target` (or when the variable in `showVar` is not 0, or always with alwaysShow) and fades out when they walk away. While
/// it shows, pressing the key makes the object's script run on_interact().
class UIInteractionPrompt final : public UIHudBase {
public:
    std::string key = "E";
    std::string action = "Interact";
    std::string target;              ///< the object to be near (empty = use showVar or alwaysShow)
    float range = 2.5f;              ///< world units
    std::string showVar;             ///< a variable; the prompt shows while it is not 0
    bool alwaysShow = false;
    std::string playerTag = "Player";
    float fadeSpeed = 8.0f;          ///< bigger fades faster

    /// True when the prompt should be up now.
    bool ShouldShow() const;
    float Alpha() const { return m_alpha; }
    bool Visible() const { return m_alpha > 0.01f; }
    /// Distance from the player to the target (a large number when either is missing).
    float Distance() const;

    void Update(float dt) override;
    /// Jump the fade to its end state (tests, and a prompt that must be right on the first frame).
    void Settle() { m_alpha = ShouldShow() ? 1.0f : 0.0f; }

private:
    float m_alpha = 0.0f;
};

// ---- Objective tracker ----------------------------------------------------------------------------------------------

/// The quests the player is on, from the scene's Quest Log: each active quest's title and its objectives with a tick or
/// "2/5" progress. Set `questId` to show one quest. This is the placeable, restylable version of the Quest Log's own
/// tracker: turn "Show tracker" off on the Quest Log so the two do not both draw.
class UIObjectiveTracker final : public UIHudBase {
public:
    std::string title = "Objectives";
    std::string questId;             ///< empty = the Quest Log's tracked quest, else every active quest
    int maxQuests = 3;
    bool showDescription = false;
    Color doneColor = Color::FromBytes(110, 210, 140);

    struct Line {
        enum Kind { Quest = 0, Objective = 1, Done = 2, Note = 3 };
        Kind kind = Objective;
        std::string text;
        int have = 0, need = 0;      ///< progress for an objective that counts (need > 1)
    };
    /// The lines to draw now (empty when there is no Quest Log or no active quest).
    std::vector<Line> Lines() const;
    /// The height the lines need (the draw never shows more than size.y).
    float ContentHeight() const { return 36.0f + 20.0f * (float)Lines().size(); }
};

// ---- Compass --------------------------------------------------------------------------------------------------------

/// A strip that slides as you turn: N, E, S, W and the angles between them, with a marker for each object named in
/// `markers` (separated by |). North is +Z in the world (the same as the Minimap). The heading comes from the object named in
/// `source`, else the main camera, else the number typed into `heading`.
class UICompass final : public UIHudBase {
public:
    std::string source;              ///< the object whose facing is the heading (empty = the main camera)
    float heading = 0.0f;            ///< degrees clockwise from north, used when there is no source or camera
    float fieldOfView = 120.0f;      ///< degrees shown across the strip
    std::string markers;             ///< names of objects to mark, separated by |
    std::string headingVar;          ///< a variable holding the heading in degrees (wins over everything else while it is set)
    bool showDegrees = true;
    bool showMarkerDistance = true;
    Color markerColor = Color::FromBytes(250, 200, 80);

    /// Degrees clockwise from north (+Z) of the direction (dx east, dz north).
    static float BearingOf(float dx, float dz) {
        float a = std::atan2(dx, dz) * Mathf::Rad2Deg;
        if (a < 0.0f) a += 360.0f;
        return a;
    }
    /// An angle difference folded to -180..180.
    static float Wrap180(float d) {
        d = std::fmod(d, 360.0f);
        if (d > 180.0f) d -= 360.0f;
        if (d < -180.0f) d += 360.0f;
        return d;
    }
    /// Where a bearing `rel` degrees off the heading lands on a strip `width` wide that shows `fov` degrees, measured from
    /// the strip's left edge; negative or past the width means off the strip.
    static float StripX(float rel, float fov, float width) { return width * 0.5f + rel / (fov > 1.0f ? fov : 1.0f) * width; }
    /// "N", "NE", "E" ... for a heading.
    static const char* Cardinal(float deg) {
        static const char* k[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
        float d = std::fmod(deg + 22.5f, 360.0f); if (d < 0) d += 360.0f;
        return k[(int)(d / 45.0f) & 7];
    }

    /// The heading now, and (when found) the world position it is measured from.
    float Heading(float* fromX = nullptr, float* fromZ = nullptr, bool* hasPos = nullptr) const;
    struct Marker { std::string name; float bearing = 0.0f; float distance = 0.0f; };
    /// The named objects that exist, with their bearing from the heading's origin.
    std::vector<Marker> Markers() const;
};

// ---- Item tooltip card ----------------------------------------------------------------------------------------------

/// The card for one item from the item database: the name in its rarity colour, category, description, stack, weight,
/// use, durability, game-specific fields, and a compare against what is worn when `equipment` names an object with
/// Equipment. The words are the same the inventory tooltips use (DescribeItem). Set `item` to an id, or `itemVar` to a
/// variable that holds one, or call Show(id) from a script; with no item the card is hidden.
class UIItemTooltipCard final : public UIHudBase {
public:
    std::string item;                ///< an item id or name from the item database
    std::string itemVar;             ///< a variable holding the id (wins over `item` while it is set)
    int count = 1;
    std::string equipment;           ///< an object with Equipment, to compare against (empty = no compare)
    /// An .okayitems file whose items are added to the item database the first time the card is read (the project's own items
    /// stay). For a scene that ships with its items, such as the UI library example. Empty = the project's items only.
    std::string itemsFile;
    bool showIcon = true;
    Color nameColor = Color::White;

    struct Line { std::string text; bool name = false; bool useTint = false; Color tint{}; };
    /// The item the card shows now ("" for none).
    std::string CurrentItem() const;
    /// The lines of the card (empty when there is no item).
    std::vector<Line> Lines() const;
    float ContentHeight() const { return Lines().empty() ? 0.0f : 20.0f + 18.0f * (float)Lines().size(); }
    /// Merge `itemsFile` into the item database now (it also happens the first time Lines() is called). False when unreadable.
    bool LoadItems() const;
    void Show(const std::string& id, int n = 1) { item = id; count = n; itemVar.clear(); }
    void Hide() { item.clear(); itemVar.clear(); }

private:
    mutable std::string m_loadedFile;
};

// ---- Ability icon ---------------------------------------------------------------------------------------------------

/// An ability slot: an icon, a cooldown ring, the key and the charges. It needs a UIRadialProgress on the same object (the
/// ring: its size is the slot's size); this component drives the ring's value from the time left and draws the icon, the
/// seconds left, the key and the charge count inside it. Use() starts the cooldown; `useKey` does the same from the
/// keyboard. on_ability_use() and on_ability_ready() run on the object's script.
class UIAbilityIcon final : public Behaviour {
public:
    std::string label = "Q";          ///< one or two letters drawn as the icon when there is no picture
    std::string iconPath;             ///< an image for the icon (empty = the letter)
    std::string keyText;              ///< the key shown in the corner (e.g. "Q")
    char useKey = 0;                  ///< pressing it calls Use()
    float cooldown = 5.0f;            ///< seconds
    int maxCharges = 1;
    Color iconColor = Color::FromBytes(60, 90, 160);
    Color readyColor = Color::FromBytes(120, 220, 150);

    int Charges() const { return m_charges < 0 ? maxCharges : m_charges; }
    float Remaining() const { return m_remaining; }
    /// Seconds-left over cooldown, 0 when ready.
    float Fraction() const { return cooldown > 0.0f ? Mathf::Clamp01(m_remaining / cooldown) : 0.0f; }
    bool Ready() const { return Charges() > 0; }
    /// Spend a charge and start (or keep) the cooldown. False when none is ready.
    bool Use() {
        if (!Ready()) return false;
        if (m_charges < 0) m_charges = maxCharges;
        --m_charges;
        if (m_remaining <= 0.0f) m_remaining = cooldown;
        FireUIEvent(gameObject, "on_ability_use");
        return true;
    }
    /// Make it ready again right now.
    void Reset() { m_charges = maxCharges; m_remaining = 0.0f; Drive(); }

    void Start() override { if (m_charges < 0) m_charges = maxCharges; Drive(); }
    void Update(float dt) override {
        if (useKey && Input::GetKeyDown(useKey)) Use();
        if (m_charges < 0) m_charges = maxCharges;
        if (m_remaining > 0.0f && cooldown > 0.0f) {
            m_remaining -= dt;
            if (m_remaining <= 0.0f) {
                ++m_charges;
                FireUIEvent(gameObject, "on_ability_ready");
                m_remaining = m_charges < maxCharges ? cooldown : 0.0f;   // the next charge starts recharging
            }
        }
        Drive();
    }

private:
    int m_charges = -1;     ///< -1 = full (until the first frame sets it)
    float m_remaining = 0.0f;
    void Drive() {
        if (!gameObject) return;
        if (auto* r = gameObject->GetComponent<UIRadialProgress>()) r->SetValue(Fraction());
    }
};

} // namespace okay
