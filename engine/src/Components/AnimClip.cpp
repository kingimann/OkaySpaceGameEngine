#include "okay/Components/AnimClip.hpp"
#include "okay/Math/Quat.hpp"
#include <cctype>
#include <cmath>
#include <sstream>

namespace okay {

namespace {
// One bone between two Euler poses (degrees, the rig's Z-X-Y order): slerp the
// rotations so the bone takes the short arc, rather than lerping each angle (which
// swings -150 -> 150 the long way round and flips an arm through its body). Each
// angle of the result is moved by whole turns toward the plain lerp, so the numbers
// stay close to what was authored.
Vec3 BlendEuler(const Vec3& a, const Vec3& b, float u) {
    if (u <= 0.0f || (a.x == b.x && a.y == b.y && a.z == b.z)) return a;
    if (u >= 1.0f) return b;
    Vec3 e = Quat::Slerp(Quat::Euler(a), Quat::Euler(b), u).ToEuler();
    const Vec3 ref = a + (b - a) * u;
    auto near = [](float v, float r) { return v + 360.0f * std::round((r - v) / 360.0f); };
    return Vec3{near(e.x, ref.x), near(e.y, ref.y), near(e.z, ref.z)};
}

std::vector<Vec3> BlendPose(const AnimKey& a, const AnimKey& b, float u) {
    std::size_t n = a.pose.size() > b.pose.size() ? a.pose.size() : b.pose.size();
    std::vector<Vec3> out(n, Vec3{0, 0, 0});
    for (std::size_t k = 0; k < n; ++k) {
        Vec3 pa = k < a.pose.size() ? a.pose[k] : Vec3{0, 0, 0};
        Vec3 pb = k < b.pose.size() ? b.pose[k] : Vec3{0, 0, 0};
        out[k] = BlendEuler(pa, pb, u);
    }
    return out;
}
} // namespace

std::vector<Vec3> AnimClip::Sample(float t) const {
    if (keys.empty()) return {};
    if (t <= keys.front().time) return keys.front().pose;
    if (t >= keys.back().time) {
        // A loop with room past its last key eases back into the first key.
        const float last = keys.back().time;
        if (!loop || length <= last || keys.size() < 2) return keys.back().pose;
        float u = (t - last) / (length - last);
        return BlendPose(keys.back(), keys.front(), Ease(u));
    }
    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        const AnimKey& a = keys[i];
        const AnimKey& b = keys[i + 1];
        if (t < a.time || t > b.time) continue;
        float span = b.time - a.time;
        float u = span > 1e-6f ? (t - a.time) / span : 0.0f;
        return BlendPose(a, b, Ease(u));   // eased by the clip's mode (linear / smooth / step)
    }
    return keys.back().pose;
}

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
            k.pose.assign(boneCount, Vec3{0, 0, 0});
            clips.back().keys.push_back(std::move(k));
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
        else                                   out << " linear";
        if (c.overrideMovement) out << " override";
        out << "\n";
        if (c.speed > 0.0f && c.speed != 1.0f) out << "speed " << c.speed << "\n";
        if (c.length > 0.0f) out << "length " << c.length << "\n";
        for (const AnimEvent& e : c.events) out << "event " << e.time << " " << e.name << "\n";
        for (const AnimKey& k : c.keys) {
            out << "key " << k.time << "\n";
            for (std::size_t bi = 0; bi < k.pose.size(); ++bi) {
                const Vec3& p = k.pose[bi];
                if (p.x == 0.0f && p.y == 0.0f && p.z == 0.0f) continue;   // omit zero poses
                std::string nm = boneName((int)bi);
                if (nm.empty()) continue;
                out << nm << " " << p.x << " " << p.y << " " << p.z << "\n";
            }
        }
    }
    return out.str();
}

} // namespace okay
