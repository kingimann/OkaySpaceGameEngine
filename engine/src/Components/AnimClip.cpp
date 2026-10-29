#include "okay/Components/AnimClip.hpp"
#include "okay/Math/Quat.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace okay {

// Slerp the rotations so the bone takes the short arc, rather than lerping each
// angle (which swings -150 -> 150 the long way round and flips an arm through its
// body). Each angle of the result is moved by whole turns toward the plain lerp, so
// the numbers stay close to what was authored.
Vec3 AnimClip::BlendEuler(const Vec3& a, const Vec3& b, float u) {
    if (u <= 0.0f || (a.x == b.x && a.y == b.y && a.z == b.z)) return a;
    if (u >= 1.0f) return b;
    Vec3 e = Quat::Slerp(Quat::Euler(a), Quat::Euler(b), u).ToEuler();
    const Vec3 ref = a + (b - a) * u;
    auto near = [](float v, float r) { return v + 360.0f * std::round((r - v) / 360.0f); };
    return Vec3{near(e.x, ref.x), near(e.y, ref.y), near(e.z, ref.z)};
}

namespace {
std::vector<Vec3> BlendPose(const AnimKey& a, const AnimKey& b, float u) {
    std::size_t n = a.pose.size() > b.pose.size() ? a.pose.size() : b.pose.size();
    std::vector<Vec3> out(n, Vec3{0, 0, 0});
    for (std::size_t k = 0; k < n; ++k) {
        Vec3 pa = k < a.pose.size() ? a.pose[k] : Vec3{0, 0, 0};
        Vec3 pb = k < b.pose.size() ? b.pose[k] : Vec3{0, 0, 0};
        out[k] = AnimClip::BlendEuler(pa, pb, u);
    }
    return out;
}
} // namespace

namespace {
// Cubic: a Catmull-Rom curve through the keys around `t` (the clip's own key
// times, so unevenly spaced keys keep their timing). A looping clip with room
// past its last key wraps its neighbours around, so the loop seam is smooth too.
struct CubicSpan {
    const AnimKey* k[4];
    float t[4];
    float u;
};
bool FindCubicSpan(const AnimClip& c, float t, CubicSpan& s) {
    const auto& keys = c.keys;
    const int n = (int)keys.size();
    if (n < 3) return false;
    const float first = keys.front().time, last = keys.back().time;
    const bool cyclic = c.loop && c.length > last;
    int i = -1;
    if (t >= last) { if (!cyclic) return false; i = n - 1; }
    else for (int j = 0; j + 1 < n; ++j) if (t >= keys[j].time && t <= keys[j + 1].time) { i = j; break; }
    if (i < 0) return false;
    auto at = [&](int j, const AnimKey*& key, float& time) {
        if (cyclic) {
            int w = ((j % n) + n) % n;
            key = &keys[w];
            time = keys[w].time + c.length * (float)((j - w) / n);
        } else {
            int w = j < 0 ? 0 : (j >= n ? n - 1 : j);
            key = &keys[w];
            time = keys[w].time;
        }
    };
    for (int q = 0; q < 4; ++q) at(i - 1 + q, s.k[q], s.t[q]);
    (void)first;
    const float span = s.t[2] - s.t[1];
    s.u = span > 1e-6f ? (t - s.t[1]) / span : 0.0f;
    return true;
}
// Hermite through p1 -> p2, with Catmull-Rom tangents from the outer keys (an
// end key's tangent is one-sided).
float Cubic(float p0, float p1, float p2, float p3, const CubicSpan& s) {
    const float span = s.t[2] - s.t[1];
    auto tangent = [&](float a, float b, float ta, float tb, float fa, float fb) {
        return tb - ta > 1e-6f ? (b - a) * (span / (tb - ta)) : (fb - fa);
    };
    const float m1 = tangent(p0, p2, s.t[0], s.t[2], p1, p2);
    const float m2 = tangent(p1, p3, s.t[1], s.t[3], p1, p2);
    const float u = s.u, u2 = u * u, u3 = u2 * u;
    return (2 * u3 - 3 * u2 + 1) * p1 + (u3 - 2 * u2 + u) * m1 + (-2 * u3 + 3 * u2) * p2 + (u3 - u2) * m2;
}
} // namespace

float AnimClip::EaseFrom(const AnimKey& from, float u) const {
    if (from.ease == AnimKeyEase::Clip) return Ease(u);
    if (u < 0.0f) u = 0.0f; else if (u > 1.0f) u = 1.0f;
    switch (from.ease) {
        case AnimKeyEase::Smooth:  return u * u * (3.0f - 2.0f * u);
        case AnimKeyEase::Step:    return u >= 1.0f ? 1.0f : 0.0f;
        case AnimKeyEase::EaseIn:  return u * u;                              // starts slow
        case AnimKeyEase::EaseOut: return 1.0f - (1.0f - u) * (1.0f - u);     // ends slow
        case AnimKeyEase::Linear: default: return u;
    }
}

std::vector<Vec3> AnimClip::Sample(float t) const {
    if (keys.empty()) return {};
    if (interp == AnimInterp::Cubic) {
        CubicSpan s;
        // A key with its own easing takes its span out of the curve.
        if (t > keys.front().time && FindCubicSpan(*this, t, s) && s.k[1]->ease == AnimKeyEase::Clip) {
            std::size_t n = 0;
            for (const AnimKey* k : s.k) n = k->pose.size() > n ? k->pose.size() : n;
            std::vector<Vec3> out(n, Vec3{0, 0, 0});
            // Angles are unwrapped toward the segment's start, so a bone near
            // +-180 doesn't swing the long way round.
            auto near = [](float v, float r) { return v + 360.0f * std::round((r - v) / 360.0f); };
            for (std::size_t b = 0; b < n; ++b) {
                Vec3 p[4];
                for (int q = 0; q < 4; ++q) p[q] = b < s.k[q]->pose.size() ? s.k[q]->pose[b] : Vec3{0, 0, 0};
                for (int q : {0, 2, 3}) p[q] = Vec3{near(p[q].x, p[1].x), near(p[q].y, p[1].y), near(p[q].z, p[1].z)};
                out[b] = Vec3{Cubic(p[0].x, p[1].x, p[2].x, p[3].x, s), Cubic(p[0].y, p[1].y, p[2].y, p[3].y, s),
                              Cubic(p[0].z, p[1].z, p[2].z, p[3].z, s)};
            }
            return out;
        }
    }
    if (t <= keys.front().time) return keys.front().pose;
    if (t >= keys.back().time) {
        // A loop with room past its last key eases back into the first key.
        const float last = keys.back().time;
        if (!loop || length <= last || keys.size() < 2) return keys.back().pose;
        float u = (t - last) / (length - last);
        return BlendPose(keys.back(), keys.front(), EaseFrom(keys.back(), u));
    }
    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        const AnimKey& a = keys[i];
        const AnimKey& b = keys[i + 1];
        if (t < a.time || t > b.time) continue;
        float span = b.time - a.time;
        float u = span > 1e-6f ? (t - a.time) / span : 0.0f;
        return BlendPose(a, b, EaseFrom(a, u));   // the key's own easing, else the clip's mode
    }
    return keys.back().pose;
}

Vec3 AnimClip::SampleRoot(float t) const {
    if (!hasRoot || keys.empty()) return Vec3{0, 0, 0};
    if (interp == AnimInterp::Cubic) {
        CubicSpan s;
        if (t > keys.front().time && FindCubicSpan(*this, t, s) && s.k[1]->ease == AnimKeyEase::Clip) {
            const Vec3 &a = s.k[0]->root, &b = s.k[1]->root, &c = s.k[2]->root, &d = s.k[3]->root;
            return Vec3{Cubic(a.x, b.x, c.x, d.x, s), Cubic(a.y, b.y, c.y, d.y, s), Cubic(a.z, b.z, c.z, d.z, s)};
        }
    }
    if (t <= keys.front().time) return keys.front().root;
    const AnimKey* a = &keys.back();
    const AnimKey* b = &keys.back();
    float u = 0.0f;
    if (t >= keys.back().time) {
        const float last = keys.back().time;
        if (loop && length > last && keys.size() >= 2) { b = &keys.front(); u = (t - last) / (length - last); }
    } else {
        for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
            if (t < keys[i].time || t > keys[i + 1].time) continue;
            a = &keys[i]; b = &keys[i + 1];
            float span = b->time - a->time;
            u = span > 1e-6f ? (t - a->time) / span : 0.0f;
            break;
        }
    }
    return a->root + (b->root - a->root) * EaseFrom(*a, u);
}

// ---- Editing -------------------------------------------------------------------
namespace {
// Give the keys at `indices` the times `newTime` says and merge them back in
// order. A moved key replaces any unmoved key it lands on (two moved keys that
// meet keep the earlier one). Returns the moved keys' new indices, ascending.
std::vector<int> RetimeKeys(std::vector<AnimKey>& keys, const std::vector<int>& indices,
                            const std::function<float(float)>& newTime) {
    struct Slot { AnimKey key; bool moved; };
    std::vector<Slot> slots;
    slots.reserve(keys.size());
    for (int i = 0; i < (int)keys.size(); ++i) slots.push_back({keys[i], false});
    for (int i : indices)
        if (i >= 0 && i < (int)slots.size()) {
            slots[i].moved = true;
            slots[i].key.time = std::max(0.0f, newTime(keys[i].time));
        }
    std::stable_sort(slots.begin(), slots.end(), [](const Slot& a, const Slot& b) { return a.key.time < b.key.time; });
    keys.clear();
    std::vector<bool> moved;
    for (Slot& s : slots) {
        if (!keys.empty() && std::fabs(keys.back().time - s.key.time) < 1e-3f) {
            if (s.moved && !moved.back()) { keys.back() = std::move(s.key); moved.back() = true; }
            continue;
        }
        keys.push_back(std::move(s.key));
        moved.push_back(s.moved);
    }
    std::vector<int> out;
    for (int i = 0; i < (int)keys.size(); ++i) if (moved[i]) out.push_back(i);
    return out;
}
// A loop length the keys have reached or passed means nothing: loop at the last key.
void DropPassedLength(AnimClip& c) {
    const float last = c.keys.empty() ? 0.0f : c.keys.back().time;
    if (c.length > 0.0f && c.length <= last + 1e-4f) c.length = 0.0f;
}
} // namespace

std::vector<int> AnimClip::MoveKeys(const std::vector<int>& indices, float dt) {
    float first = 1e30f;
    for (int i : indices) if (i >= 0 && i < (int)keys.size()) first = std::min(first, keys[i].time);
    if (first == 1e30f) return {};
    if (first + dt < 0.0f) dt = -first;   // the earliest key stops at 0; the rest keep their spacing
    std::vector<int> out = RetimeKeys(keys, indices, [dt](float t) { return t + dt; });
    DropPassedLength(*this);
    return out;
}

std::vector<int> AnimClip::ScaleKeyTimes(const std::vector<int>& indices, float pivot, float factor) {
    if (factor <= 0.0f) factor = 1.0f;   // nothing sensible to do; keep the keys where they are
    std::vector<int> out = RetimeKeys(keys, indices, [=](float t) { return pivot + (t - pivot) * factor; });
    DropPassedLength(*this);
    return out;
}

void AnimClip::InsertTime(float at, float dt) {
    if (dt <= 0.0f) return;
    for (AnimKey& k : keys) if (k.time >= at - 1e-4f) k.time += dt;
    for (AnimEvent& e : events) if (e.time >= at - 1e-4f) e.time += dt;
    if (length > 0.0f && length >= at) length += dt;
}

void AnimClip::RemoveTime(float at, float dt) {
    if (dt <= 0.0f) return;
    const float end = at + dt;
    auto inside = [&](float t) { return t >= at - 1e-4f && t < end - 1e-4f; };
    keys.erase(std::remove_if(keys.begin(), keys.end(), [&](const AnimKey& k) { return inside(k.time); }), keys.end());
    events.erase(std::remove_if(events.begin(), events.end(), [&](const AnimEvent& e) { return inside(e.time); }), events.end());
    for (AnimKey& k : keys) if (k.time >= end - 1e-4f) k.time -= dt;
    for (AnimEvent& e : events) if (e.time >= end - 1e-4f) e.time -= dt;
    if (length > 0.0f) {
        if (length >= end) length -= dt;
        else if (length > at) length = at;
    }
    DropPassedLength(*this);
}

void AnimClip::Mirror(const std::function<void(std::vector<Vec3>&)>& mirrorPose) {
    for (AnimKey& k : keys) {
        mirrorPose(k.pose);
        k.root.x = -k.root.x;
    }
}

void AnimClip::OffsetPose(const std::vector<int>& indices, int bone, const Vec3& delta) {
    for (int i : indices) {
        if (i < 0 || i >= (int)keys.size()) continue;
        std::vector<Vec3>& p = keys[i].pose;
        if (bone < 0) { for (Vec3& v : p) v = v + delta; continue; }
        if (bone >= (int)p.size()) p.resize(bone + 1, Vec3{0, 0, 0});
        p[bone] = p[bone] + delta;
    }
}

void AnimClip::ScalePose(const std::vector<int>& indices, int bone, float factor) {
    if (keys.empty()) return;
    // The pivot is the clip's mean pose, so a swing grows about its centre
    // instead of the whole limb drifting away from rest.
    std::size_t nb = 0;
    for (const AnimKey& k : keys) nb = std::max(nb, k.pose.size());
    std::vector<Vec3> mean(nb, Vec3{0, 0, 0});
    for (const AnimKey& k : keys) for (std::size_t b = 0; b < k.pose.size(); ++b) mean[b] = mean[b] + k.pose[b];
    for (Vec3& m : mean) m = m * (1.0f / (float)keys.size());
    for (int i : indices) {
        if (i < 0 || i >= (int)keys.size()) continue;
        std::vector<Vec3>& p = keys[i].pose;
        for (std::size_t b = 0; b < p.size(); ++b)
            if (bone < 0 || (int)b == bone) p[b] = mean[b] + (p[b] - mean[b]) * factor;
    }
}

namespace {
// The easing words a `key` line may end with (none = the clip's mode).
const struct { AnimKeyEase ease; const char* word; } kEaseWords[] = {
    {AnimKeyEase::Linear, "linear"}, {AnimKeyEase::Smooth, "smooth"},   {AnimKeyEase::Step, "step"},
    {AnimKeyEase::EaseIn, "easein"}, {AnimKeyEase::EaseOut, "easeout"},
};
const char* EaseWord(AnimKeyEase e) {
    for (const auto& w : kEaseWords) if (w.ease == e) return w.word;
    return nullptr;
}
bool EaseFromWord(const std::string& s, AnimKeyEase& out) {
    for (const auto& w : kEaseWords) if (s == w.word) { out = w.ease; return true; }
    return false;
}
} // namespace

std::vector<AnimClip> AnimClip::ParseAll(const std::string& text,
                                         const std::function<int(const std::string&)>& resolveBone,
                                         int boneCount,
                                         std::string* err) {
    std::vector<AnimClip> clips;
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    auto fail = [&](const std::string& m) {
        if (err) *err = "line " + std::to_string(lineNo) + ": " + m;
        return clips;
    };

    while (std::getline(in, line)) {
        ++lineNo;
        if (auto h = line.find('#'); h != std::string::npos) line.erase(h);
        std::istringstream ls(line);
        std::string tok;
        if (!(ls >> tok)) continue;   // blank line

        if (tok == "clip") {
            AnimClip c;
            if (!(ls >> c.name)) return fail("clip needs a name");
            // Optional trailing words in any order: loop|once, linear|smooth|step.
            std::string w;
            while (ls >> w) {
                if (w == "once") c.loop = false;
                else if (w == "loop") c.loop = true;
                else if (w == "smooth") c.interp = AnimInterp::Smooth;
                else if (w == "step")   c.interp = AnimInterp::Step;
                else if (w == "linear") c.interp = AnimInterp::Linear;
                else if (w == "cubic")  c.interp = AnimInterp::Cubic;
                else if (w == "override") c.overrideMovement = true;
            }
            clips.push_back(std::move(c));
        } else if (tok == "speed") {
            if (clips.empty()) return fail("speed before any clip");
            float s = 1.0f;
            if (!(ls >> s)) return fail("speed needs a value");
            clips.back().speed = (s > 1e-4f) ? s : 1.0f;
        } else if (tok == "length") {
            if (clips.empty()) return fail("length before any clip");
            float l = 0.0f;
            if (!(ls >> l)) return fail("length needs a value");
            clips.back().length = l > 0.0f ? l : 0.0f;
        } else if (tok == "event") {
            if (clips.empty()) return fail("event before any clip");
            float et = 0.0f; std::string en;
            if (!(ls >> et >> en)) return fail("event needs a time and a name");
            clips.back().AddEvent(et, en);   // footstep / hit / spawn marker (fires as the clip plays)
        } else if (tok == "key") {
            if (clips.empty()) return fail("key before any clip");
            AnimKey k;
            if (!(ls >> k.time)) return fail("key needs a time");
            // An optional easing word for the span out of this key (older files
            // have none, so their keys keep the clip's mode).
            std::string w;
            if (ls >> w && !EaseFromWord(w, k.ease)) return fail("unknown key easing '" + w + "'");
            k.pose.assign(boneCount, Vec3{0, 0, 0});
            clips.back().keys.push_back(std::move(k));
        } else if (tok == "root") {
            if (clips.empty() || clips.back().keys.empty()) return fail("root outside a key");
            Vec3 r{0, 0, 0};
            if (!(ls >> r.x >> r.y >> r.z)) return fail("root needs x y z");
            clips.back().keys.back().root = r;
            clips.back().hasRoot = true;
        } else {
            // A bone line: "<bone> x y z" within the current keyframe.
            if (clips.empty() || clips.back().keys.empty())
                return fail("bone pose '" + tok + "' outside a key");
            int bi = resolveBone(tok);
            if (bi < 0 || bi >= boneCount) return fail("unknown bone '" + tok + "'");
            Vec3 e{0, 0, 0};
            if (!(ls >> e.x >> e.y >> e.z)) return fail("bone '" + tok + "' needs x y z");
            clips.back().keys.back().pose[bi] = e;
        }
    }
    return clips;
}

std::string AnimClip::WriteAll(const std::vector<AnimClip>& clips,
                               const std::function<std::string(int)>& boneName) {
    std::ostringstream out;
    out << "# OkaySpace animation clips (.okayanim) — generated by the editor\n";
    for (const AnimClip& c : clips) {
        out << "\nclip " << (c.name.empty() ? "clip" : c.name)
            << (c.loop ? " loop" : " once");
        if (c.interp == AnimInterp::Smooth)    out << " smooth";
        else if (c.interp == AnimInterp::Step) out << " step";
        else if (c.interp == AnimInterp::Cubic) out << " cubic";
        else                                   out << " linear";
        if (c.overrideMovement) out << " override";
        out << "\n";
        if (c.speed > 0.0f && c.speed != 1.0f) out << "speed " << c.speed << "\n";
        if (c.length > 0.0f) out << "length " << c.length << "\n";
        for (const AnimEvent& e : c.events) out << "event " << e.time << " " << e.name << "\n";
        for (const AnimKey& k : c.keys) {
            out << "key " << k.time;
            if (const char* w = EaseWord(k.ease)) out << " " << w;   // only keys with their own easing
            out << "\n";
            for (std::size_t bi = 0; bi < k.pose.size(); ++bi) {
                const Vec3& p = k.pose[bi];
                if (p.x == 0.0f && p.y == 0.0f && p.z == 0.0f) continue;   // omit zero poses
                std::string nm = boneName((int)bi);
                if (nm.empty()) continue;
                out << nm << " " << p.x << " " << p.y << " " << p.z << "\n";
            }
            if (c.hasRoot) out << "root " << k.root.x << " " << k.root.y << " " << k.root.z << "\n";
        }
    }
    return out.str();
}

} // namespace okay
