#pragma once
// Ranked search for the Add Node panel. Pure logic: no ImGui and no node
// registry in here, so tests/test_vs_search.cpp can drive it directly and the
// panel only has to draw what comes back.
//
// A query is split into words. Every word has to match, in any order. A word is
// scored by the best way it matches one node, strongest first:
//   exact name > name starts with it > curated synonym > a word of the name
//   starts with it > a synonym starts with it > inside the name > abbreviation
//   (ssc = Set Scale) > one typo away > letters in order > topic > pins > text.
// The tier is the high part of the score. What the node was used for lately only
// moves it inside its tier, so a favourite can never jump over a better match.
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace okay::editor::vsed {

// ---- Synonyms ---------------------------------------------------------------
// Words people type that are not in the node's name. Each row: every word in
// `words` finds the nodes in `types`; the first type is the one people most often
// mean and ranks first. tests/test_vs_search.cpp checks that every type exists.
struct VsAlias { const char* words; const char* types; };
inline const VsAlias kVsAliases[] = {
    // Writing to the console
    {"log console debug output trace echo say report diagnostics", "Print LogWarn LogError LogOnce AssertThis DrawDebugLine"},
    {"warn warning caution",                       "LogWarn Print"},
    {"error fail problem exception",               "LogError Print FailNow"},
    {"text label caption words string",            "SetText Format Concat ToString ShowFloatingText"},
    {"join combine append",                        "Concat Format"},
    // Maths
    {"plus sum addition total increase",           "Add AddVar ScoreAdd Vec3Add"},
    {"minus subtract subtraction difference decrease", "Sub Vec3Sub"},
    {"times multiply multiplication product",      "Mul Vec3Scale"},
    {"divide division quotient ratio over",        "Div"},
    {"remainder modulo modulus",                   "Mod"},
    {"power exponent square squared",              "Pow Exp"},
    {"root",                                       "Sqrt"},
    {"absolute magnitude",                         "Abs Vec3Length"},
    {"negative opposite invert",                   "Neg Not Vec3Negate"},
    {"smallest lowest minimum least",              "Min ListMin"},
    {"biggest highest maximum largest most",       "Max ListMax"},
    {"limit bound cap restrict",                   "Clamp Saturate Min Max"},
    {"average mean",                               "Average"},
    {"truncate rounding",                          "Round Floor Ceil"},
    {"wave sine cosine pi",                        "Sin Cos Tan"},
    {"interpolate blend mix smooth gradient",      "Lerp Vec3Lerp SmoothStep EaseToward BlendTime"},
    {"random rand dice luck gamble",               "RandomRange Random RandomInt Chance ChanceBranch RandomPick"},
    {"coin heads tails",                           "ChanceBranch Chance FlipFlop"},
    {"equal same match",                           "Equals Compare Approx NotEquals"},
    {"greater less bigger smaller above below larger compare", "Compare Between"},
    {"both",                                       "And AllTrue"},
    {"either",                                     "Or AnyTrue"},
    {"between range inside",                       "Between Clamp InsideArea"},
    // Time and starting up
    {"wait delay sleep pause later after hold",    "Delay Wait WaitUntil WaitWhile WaitLimit"},
    {"timer clock countdown stopwatch interval",   "Timer OnTimer Countdown Stopwatch Cooldown"},
    {"cooldown recharge",                          "Cooldown"},
    {"seconds time",                               "Time Delay DeltaTime Timer"},
    {"start begin awake init initialize setup launch first ready", "OnStart Once"},
    {"update tick frame every each",               "OnUpdate OnFixedUpdate OnLateUpdate"},
    {"physics fixed",                              "OnFixedUpdate"},
    {"once single oneshot",                        "Once DoN"},
    // Input
    {"key keyboard button input press pressed",    "OnKey Key KeyDown KeyUp OnKeyUp OnAction"},
    {"release released",                           "OnKeyUp KeyUp OnActionUp OnMouseUp"},
    {"hold held",                                  "Key OnActionHeld MouseButton"},
    {"mouse cursor pointer",                       "MouseX MouseY MouseButton OnClick MouseGround"},
    {"click tap",                                  "OnClick OnMouseDown MouseButtonDown"},
    {"axis stick joystick wasd arrows",            "GetMoveAxis AxisX AxisY"},
    {"scroll wheel",                               "MouseWheel"},
    // Physics and contact
    {"hit collide touch collision bump crash overlap contact", "OnCollision OnTrigger OnCollisionStay OnCollisionTag OnCollisionExit"},
    {"enter entered inside",                       "OnTrigger OnTagEnter OnMouseEnter InsideArea"},
    {"leave exit left",                            "OnTriggerExit OnTagExit OnCollisionExit OnMouseExit"},
    {"raycast laser ray beam sight",               "Raycast3D Raycast LineOfSight SphereCast3D"},
    {"force push shove",                           "AddForce AddImpulse Knockback"},
    {"jump leap hop",                              "AddImpulse SetVelocity"},
    {"speed velocity",                             "SetVelocity VelX VelY VelZ"},
    {"slow slowmo freeze timescale",               "SetTimeScale TimeScale PauseGame"},
    // Health and combat
    {"hurt damage harm attack",                    "Damage Shoot Knockback"},
    {"health hp life lives",                       "Heal IsDead OnHealthChanged HealthEmpty HealthFull"},
    {"die dead death kill",                        "OnDeath IsDead Die Destroy"},
    {"heal restore regen",                         "Heal Revive"},
    {"shoot fire bullet gun projectile",           "Shoot SpawnProjectile Ammo"},
    {"respawn revive",                             "Respawn Revive"},
    // Moving things
    {"move walk go travel step",                   "Translate MoveBy MoveTowards TweenMove NavMoveTo"},
    {"position location place coordinates",        "SetPosition GetPosition SetPositionVec ObjectPosition"},
    {"teleport warp",                              "Teleport SetPosition"},
    {"rotate turn spin face orbit",                "Rotate RotateTowards LookAt Spin SetRotation"},
    {"size grow shrink resize",                    "SetScale GetScale TweenScale"},
    {"follow chase pursue",                        "Chase SmoothFollow FollowPath CameraFollow"},
    {"ease animate animation tween slide",         "TweenPosition PlayClip TweenMove TweenRotation TweenScale TweenColor"},
    {"vector vec point direction",                 "MakeVec3 Vec3Add Vec3Sub Vec3Scale Vec3Length Vec3Normalize Vec3Dot Vec3Cross ForwardVector"},
    {"distance far near nearby gap closer",        "DistanceTo Vec3Distance Distance2D Proximity CountNearby"},
    // Objects and scenes
    {"spawn create instantiate clone summon new make", "Spawn SpawnProjectile SpawnPickup SpawnEvery NetSpawnOwned"},
    {"delete remove despawn erase",                "Destroy DestroyAfter RemoveItem"},
    {"show hide visible invisible",                "SetVisible SetActive ShowUI HideUI"},
    {"enable disable activate deactivate",         "SetActive OnEnable OnDisable"},
    {"scene level map reload restart",             "LoadScene RestartScene"},
    {"quit close leavegame",                       "QuitGame"},
    {"color colour tint paint",                    "SetColor TweenColor"},
    {"find search lookup get",                     "FindObject FindWithTag NearestObject GetComponent"},
    {"tag group layer",                            "FindWithTag OnTagEnter CountWithTag"},
    // Variables and logic
    {"variable var store remember",                "SetVar GetVar AddVar RememberValue"},
    {"save persist write",                         "SaveValue CheckpointSave SaveCreation"},
    {"load read restore",                          "LoadValue CheckpointLoad LoadCreation"},
    {"score points",                               "ScoreAdd ScoreSet ScoreGet"},
    {"if else condition decide choose test check", "Branch Select Compare Condition"},
    {"loop repeat iterate again for while",        "ForLoop Repeat While ForEach ForStep DoWhile"},
    {"switch case match",                          "SwitchInt SwitchString SwitchCases"},
    {"toggle flip",                                "Toggle FlipFlop Lever"},
    {"count counter tally",                        "Counter Countdown CountNearby CountWithTag"},
    // Messages, sound and the rest
    {"message event signal emit notify tell",      "SendEvent BroadcastEvent SendMessage Broadcast OnEvent"},
    {"sound audio sfx noise effect",               "PlaySound PlaySoundAt StopSound"},
    {"music song soundtrack",                      "MusicPlay MusicStop MusicPause MusicResume"},
    {"shake rumble",                               "CameraShake"},
    {"online multiplayer network server lobby",    "NetHost NetJoin NetConnected NetIsServer NetSend"},
    {"inventory bag backpack loot",                "AddItem RemoveItem HasItem ItemCount LootRoll"},
    {"win victory finish goal",                    "Win"},
    {"lose gameover defeat",                       "Lose"},
    {"door",                                       "DoorOpen DoorClose"},
    {"fade",                                       "FadeScreen TweenColor"},
    {"function method procedure",                  "FunctionStart CallFunction CallMethod"},
    {"list array",                                 "NewList ListAdd ListGet ListLength"},
};

inline int VsAliasRowCount() { return (int)(sizeof kVsAliases / sizeof kVsAliases[0]); }

struct VsSearchWord { std::string w; bool primary = false; };

/// The synonyms that point at one node type (empty for most nodes).
inline const std::vector<VsSearchWord>& VsAliasWordsFor(const std::string& type) {
    static const std::unordered_map<std::string, std::vector<VsSearchWord>> map = [] {
        std::unordered_map<std::string, std::vector<VsSearchWord>> m;
        auto split = [](const char* s) {
            std::vector<std::string> out; std::string cur;
            for (const char* p = s; ; ++p) {
                if (*p == ' ' || *p == 0) { if (!cur.empty()) out.push_back(cur); cur.clear(); if (!*p) break; }
                else cur += *p;
            }
            return out;
        };
        for (const VsAlias& a : kVsAliases) {
            const std::vector<std::string> words = split(a.words), types = split(a.types);
            for (std::size_t t = 0; t < types.size(); ++t)
                for (const std::string& w : words) m[types[t]].push_back({w, t == 0});
        }
        return m;
    }();
    static const std::vector<VsSearchWord> none;
    const auto it = map.find(type);
    return it == map.end() ? none : it->second;
}

// ---- Items and hits ---------------------------------------------------------
/// What one palette row offers to the search. Fill the text fields, then call
/// VsSearchPrepare once; after that only the four inputs below change.
struct VsSearchItem {
    std::string title, topic, names, pins, desc;
    std::vector<VsSearchWord> aliases;
    int level = 1;               ///< 0 basic, 1 intermediate, 2 advanced
    // Inputs that change between searches:
    int  recentRank = 10000;     ///< 0 = used last, large = never
    int  uses = 0;               ///< how often it was added
    bool fits = true;            ///< can take the wire being dragged
    // Filled by VsSearchPrepare:
    std::string lt, core, ltopic, lnames, lpins, ldesc;
    std::vector<std::pair<int, int>> words;   ///< start, length of each title word
    std::vector<std::string> nameWords;
};

enum class VsWhy { Title, Synonym, Name, Abbreviation, Typo, Letters, Topic, Pin, Text };

struct VsSearchHit {
    int index = 0;               ///< into the items the caller passed
    int score = 0;
    VsWhy why = VsWhy::Title;
    std::string via;             ///< the word of the node that matched, when it is not in the title
    std::vector<int> hits;       ///< title letters to draw highlighted
};

inline std::string VsSearchLower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

inline bool VsSearchWordChar(char c) { return std::isalnum((unsigned char)c) != 0; }

inline void VsSearchWordsOf(const std::string& lower, std::vector<std::string>& out) {
    std::string cur;
    for (char c : lower) {
        if (VsSearchWordChar(c)) cur += c;
        else { if (!cur.empty()) out.push_back(cur); cur.clear(); }
    }
    if (!cur.empty()) out.push_back(cur);
}

inline void VsSearchPrepare(VsSearchItem& it) {
    it.lt = VsSearchLower(it.title);
    it.core = it.lt;
    const std::size_t paren = it.core.find(" (");   // "Add (+)" is called Add
    if (paren != std::string::npos) it.core.resize(paren);
    it.ltopic = VsSearchLower(it.topic);
    it.lnames = VsSearchLower(it.names);
    it.lpins = VsSearchLower(it.pins);
    it.ldesc = VsSearchLower(it.desc);
    it.words.clear();
    for (std::size_t i = 0; i < it.lt.size();) {
        if (!VsSearchWordChar(it.lt[i])) { ++i; continue; }
        std::size_t j = i;
        while (j < it.lt.size() && VsSearchWordChar(it.lt[j])) ++j;
        it.words.push_back({(int)i, (int)(j - i)});
        i = j;
    }
    it.nameWords.clear();
    VsSearchWordsOf(it.lnames, it.nameWords);
    std::sort(it.nameWords.begin(), it.nameWords.end());
    it.nameWords.erase(std::unique(it.nameWords.begin(), it.nameWords.end()), it.nameWords.end());
}

/// Edits (insert, delete, change, swap two neighbours) between two short words,
/// or maxd + 1 as soon as it is clear there are more than maxd.
inline int VsTypoDistance(const char* a, int n, const char* b, int m, int maxd) {
    if (n > 30 || m > 30 || std::abs(n - m) > maxd) return maxd + 1;
    int pp[32], p[32], c[32];
    for (int j = 0; j <= m; ++j) p[j] = j;
    for (int i = 1; i <= n; ++i) {
        c[0] = i;
        int rowMin = c[0];
        for (int j = 1; j <= m; ++j) {
            const int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            c[j] = std::min(std::min(p[j] + 1, c[j - 1] + 1), p[j - 1] + cost);
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) c[j] = std::min(c[j], pp[j - 2] + 1);
            rowMin = std::min(rowMin, c[j]);
        }
        if (rowMin > maxd) return maxd + 1;
        std::memcpy(pp, p, sizeof(int) * (m + 1));
        std::memcpy(p, c, sizeof(int) * (m + 1));
    }
    return p[m];
}

// ---- Scoring ------------------------------------------------------------------
// score = tier * 128 + fine. Tiers are far enough apart that nothing added to
// `fine` (closeness, use) can carry a node into the next tier.
enum : int {
    kTierExact = 10, kTierPrefix = 9, kTierWordStart = 7, kTierSynonymPrefix = 6, kTierSubstring = 5,
    kTierAbbrev = 5, kTierTypo = 4, kTierLetters = 3, kTierName = 2, kTierTopic = 2, kTierPin = 1, kTierText = 1,
};
inline int VsTierOf(int score) { return score >> 7; }
inline int VsMakeScore(int tier, int fine) { return tier * 128 + std::clamp(fine, 0, 127); }

struct VsTermMatch { int score = 0; VsWhy why = VsWhy::Title; std::string via; std::vector<int> hits; };

inline bool VsWordStartAt(const std::string& s, std::size_t pos) {
    return pos == 0 || !VsSearchWordChar(s[pos - 1]);
}

// Abbreviation: each letter is the first letter of a word of the title or the
// letter right after the previous match ("ssc" = Set Scale, "gp" = Get Position).
inline bool VsAbbrevMatch(const std::string& t, const std::string& q, std::size_t ti, std::size_t qi,
                          int prev, int& budget, std::vector<int>& out) {
    if (qi == q.size()) return true;
    for (std::size_t j = ti; j < t.size(); ++j) {
        if (t[j] != q[qi]) continue;
        if (!(VsWordStartAt(t, j) || (int)j == prev + 1)) continue;
        if (--budget < 0) return false;
        out.push_back((int)j);
        if (VsAbbrevMatch(t, q, j + 1, qi + 1, (int)j, budget, out)) return true;
        out.pop_back();
    }
    return false;
}

inline void VsMarkRange(std::vector<int>& hits, int at, int len) {
    for (int i = 0; i < len; ++i) hits.push_back(at + i);
}

/// One query word against one node. `m.score` is 0 when it does not match.
inline void VsScoreTerm(const std::string& q, const VsSearchItem& it, VsTermMatch& m) {
    m = VsTermMatch();
    const int ql = (int)q.size();
    if (ql == 0) { m.score = 1; return; }
    const std::string& t = it.lt;

    // 1. The name.
    if (t == q || it.core == q) { m.score = VsMakeScore(kTierExact, 127); VsMarkRange(m.hits, 0, t == q ? (int)t.size() : (int)it.core.size()); return; }
    if (t.compare(0, q.size(), q) == 0) {
        m.score = VsMakeScore(kTierPrefix, 20 + 60 * ql / (int)t.size());
        VsMarkRange(m.hits, 0, ql);
        return;   // nothing outranks this except an exact name
    }
    int best = 0;
    auto take = [&](int score, VsWhy why, const std::string& via) {
        if (score > best) { best = score; m.why = why; m.via = via; }
    };
    // A curated synonym equal to the word is as good as a name that starts with it.
    for (const VsSearchWord& a : it.aliases)
        if (a.w == q) take(VsMakeScore(kTierPrefix, a.primary ? 90 : 45), VsWhy::Synonym, a.w);
    for (const std::string& w : it.nameWords)
        if (w == q) take(VsMakeScore(kTierPrefix, 40), VsWhy::Name, w);
    int ws = -1;   // a title word that starts with it
    for (const auto& w : it.words)
        if (w.second >= ql && t.compare(w.first, q.size(), q) == 0) { ws = w.first; break; }
    if (ql >= 2) {
        for (const VsSearchWord& a : it.aliases)
            if (a.w.compare(0, q.size(), q) == 0) take(VsMakeScore(kTierSynonymPrefix, a.primary ? 80 : 50), VsWhy::Synonym, a.w);
        for (const std::string& w : it.nameWords)
            if (w.compare(0, q.size(), q) == 0) take(VsMakeScore(kTierSynonymPrefix, 40), VsWhy::Name, w);
    }
    if (ws >= 0) {   // the name has the word too: highlight it and say nothing about synonyms
        const int fine = 100 - std::min(ws, 60);
        best = std::max(best, VsMakeScore(kTierWordStart, fine));
        m.why = VsWhy::Title; m.via.clear(); VsMarkRange(m.hits, ws, ql);
    }
    if (best >= VsMakeScore(kTierSynonymPrefix, 0) || ws >= 0) {   // a strong match is final; skip the slow ones
        m.score = best;
        return;
    }
    if (ql >= 2) {
        const std::size_t p = t.find(q);
        if (p != std::string::npos) {
            m.score = VsMakeScore(kTierSubstring, 70 - std::min((int)p, 40));
            m.why = VsWhy::Title; m.via.clear(); m.hits.clear(); VsMarkRange(m.hits, (int)p, ql);
            return;
        }
        if (ql >= 2 && ql <= 8) {
            std::vector<int> pos; int budget = 400;
            if (VsAbbrevMatch(t, q, 0, 0, -2, budget, pos)) {
                m.score = VsMakeScore(kTierAbbrev, 50 - std::min((int)pos.back() - (int)pos.front(), 40));
                m.why = VsWhy::Abbreviation; m.via.clear(); m.hits = pos;
                return;
            }
        }
    }
    if (ql < 2) return;

    // 2. A typo of a title word, a synonym, or an old name.
    if (ql >= 4) {
        const int maxd = ql >= 6 ? 2 : 1;
        auto close = [&](const std::string& w, int off, int len) {
            const char* s = w.data() + off;
            if (VsTypoDistance(q.data(), ql, s, len, maxd) <= maxd) return true;
            // half-typed: the start of a longer word is one typo away (five letters or more, so "plus" is not "pause")
            return ql >= 5 && len > ql && VsTypoDistance(q.data(), ql, s, ql, maxd) <= maxd;
        };
        for (const auto& w : it.words)
            if (close(t, w.first, w.second)) {
                m.score = VsMakeScore(kTierTypo, 90 - 10 * std::min(std::abs(w.second - ql), 5));
                m.why = VsWhy::Typo; m.via.clear(); m.hits.clear(); VsMarkRange(m.hits, w.first, w.second);
                return;
            }
        for (const VsSearchWord& a : it.aliases)
            if (close(a.w, 0, (int)a.w.size())) { m.score = VsMakeScore(kTierTypo, a.primary ? 60 : 40); m.why = VsWhy::Typo; m.via = a.w; return; }
        for (const std::string& w : it.nameWords)
            if (close(w, 0, (int)w.size())) { m.score = VsMakeScore(kTierTypo, 30); m.why = VsWhy::Typo; m.via = w; return; }
    }

    // 3. Letters in order inside the name, close together.
    if (ql >= 3) {
        std::vector<int> pos; std::size_t j = 0;
        for (std::size_t i = 0; i < t.size() && j < q.size(); ++i)
            if (t[i] == q[j]) { pos.push_back((int)i); ++j; }
        if (j == q.size() && pos.back() - pos.front() + 1 <= 2 * ql + 3) {
            m.score = VsMakeScore(kTierLetters, 80 - (pos.back() - pos.front() + 1 - ql) * 4);
            m.why = VsWhy::Letters; m.via.clear(); m.hits = pos;
            return;
        }
    }

    // 4. Inside the other names, then the topic, the pins and the text.
    auto wordPrefixIn = [&](const std::string& hay) {
        for (std::size_t p = hay.find(q); p != std::string::npos; p = hay.find(q, p + 1))
            if (VsWordStartAt(hay, p)) return true;
        return false;
    };
    if (ql >= 3 && it.lnames.find(q) != std::string::npos) {
        m.score = VsMakeScore(kTierName, 40); m.why = VsWhy::Name;
        for (const std::string& w : it.nameWords) if (w.find(q) != std::string::npos) { m.via = w; break; }
        return;
    }
    if (wordPrefixIn(it.ltopic)) { m.score = VsMakeScore(kTierTopic, 30); m.why = VsWhy::Topic; m.via = it.topic; return; }
    if (ql >= 3 && wordPrefixIn(it.lpins)) { m.score = VsMakeScore(kTierPin, 60); m.why = VsWhy::Pin; return; }
    if (ql >= 3 && wordPrefixIn(it.ldesc)) { m.score = VsMakeScore(kTierText, 20); m.why = VsWhy::Text; return; }
}

/// A query cut into words, lower case. Empty for a blank query.
inline std::vector<std::string> VsSearchTerms(const std::string& query) {
    std::vector<std::string> terms;
    std::string cur;
    for (char c : query) {
        if (c == ' ' || c == '\t' || c == ',') { if (!cur.empty()) terms.push_back(cur); cur.clear(); }
        else cur += (char)std::tolower((unsigned char)c);
    }
    if (!cur.empty()) terms.push_back(cur);
    return terms;
}

/// Score a whole query against one node: 0 when it does not match. Every word
/// must match. `why`/`via` describe the weakest word, the one that needed the most help.
inline int VsSearchScore(const std::vector<std::string>& terms, const VsSearchItem& it, VsSearchHit& out) {
    out.hits.clear(); out.via.clear(); out.why = VsWhy::Title; out.score = 0;
    if (terms.empty()) { out.score = 1; return 1; }
    if (terms.size() > 1) {   // the words as typed, in order: "set scale", "on click"
        std::string phrase = terms[0];
        for (std::size_t i = 1; i < terms.size(); ++i) phrase += ' ' + terms[i];
        VsTermMatch pm;
        if (it.lt.find(phrase) != std::string::npos || it.core == phrase) {
            VsScoreTerm(phrase, it, pm);
            if (pm.score && VsTierOf(pm.score) >= kTierSubstring && pm.why == VsWhy::Title) {
                out.score = pm.score; out.hits = pm.hits;
                return out.score;
            }
        }
    }
    int minScore = 1 << 20, sum = 0;
    VsTermMatch weakest;
    VsTermMatch m;
    std::vector<int> hits;
    for (const std::string& q : terms) {
        VsScoreTerm(q, it, m);
        if (!m.score) { out.hits.clear(); return 0; }
        sum += m.score;
        hits.insert(hits.end(), m.hits.begin(), m.hits.end());
        if (m.score < minScore) { minScore = m.score; weakest = m; }
    }
    std::sort(hits.begin(), hits.end());
    hits.erase(std::unique(hits.begin(), hits.end()), hits.end());
    out.hits = std::move(hits);
    out.why = weakest.why; out.via = weakest.via;
    // The weakest word counts double, so "key zzz" cannot hide behind a strong "key".
    out.score = terms.size() == 1 ? minScore : (minScore * 2 + (sum - minScore) / (int)(terms.size() - 1)) / 3;
    return out.score;
}

/// A small push for what you use: the last dozen added, and how often. At most
/// 40, and kept inside the tier by VsApplyUse.
inline int VsUseBoost(int recentRank, int uses) {
    int b = 0;
    if (recentRank >= 0 && recentRank < 12) b += 12 - recentRank;
    b += std::min(std::max(uses, 0), 14) * 2;
    return b;
}

/// Adds the use boost without leaving the tier.
inline int VsApplyUse(int score, int boost) {
    const int tier = VsTierOf(score);
    return VsMakeScore(tier, (score & 127) * 3 / 4 + boost);   // 0.75 * fine + up to 40: stays below 128
}

struct VsSearchOptions {
    bool fitFirst = false;   ///< nodes with `fits` come before the rest (a wire is being dragged)
    int  level = -1;         ///< -1 any, else only that level
};

/// Every matching node, best first. Ties break by the easier level, then the
/// more recently used, then the shorter name, then the order given, so the
/// same query always gives the same list.
inline std::vector<VsSearchHit> VsSearchRun(const std::vector<const VsSearchItem*>& items, const std::string& query,
                                           const VsSearchOptions& opt = VsSearchOptions()) {
    const std::vector<std::string> terms = VsSearchTerms(query);
    std::vector<VsSearchHit> out;
    out.reserve(items.size() < 64 ? items.size() : 64);
    VsSearchHit h;
    for (int i = 0; i < (int)items.size(); ++i) {
        const VsSearchItem& it = *items[i];
        if (opt.level >= 0 && it.level != opt.level) continue;
        if (!VsSearchScore(terms, it, h)) continue;
        h.index = i;
        if (!terms.empty()) h.score = VsApplyUse(h.score, VsUseBoost(it.recentRank, it.uses));
        out.push_back(h);
    }
    std::stable_sort(out.begin(), out.end(), [&](const VsSearchHit& a, const VsSearchHit& b) {
        const VsSearchItem& x = *items[a.index];
        const VsSearchItem& y = *items[b.index];
        if (opt.fitFirst && x.fits != y.fits) return x.fits;
        if (a.score != b.score) return a.score > b.score;
        if (x.level != y.level) return x.level < y.level;
        if (x.recentRank != y.recentRank) return x.recentRank < y.recentRank;
        if (x.title.size() != y.title.size()) return x.title.size() < y.title.size();
        return a.index < b.index;
    });
    return out;
}

/// One short line saying why a result matched, empty when the name matched.
inline std::string VsSearchWhyText(const VsSearchHit& h) {
    switch (h.why) {
        case VsWhy::Synonym:      return "also called \"" + h.via + "\"";
        case VsWhy::Name:         return "also known as \"" + h.via + "\"";
        case VsWhy::Typo:         return h.via.empty() ? "close to what you typed" : "looks like \"" + h.via + "\"";
        case VsWhy::Abbreviation: return "abbreviation";
        case VsWhy::Letters:      return "letters match";
        case VsWhy::Topic:        return "in the topic " + h.via;
        case VsWhy::Pin:          return "has a matching pin";
        case VsWhy::Text:         return "mentioned in its description";
        default:                  return std::string();
    }
}

}  // namespace okay::editor::vsed
