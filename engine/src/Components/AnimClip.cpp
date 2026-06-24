#include "okay/Components/AnimClip.hpp"
#include <cctype>
#include <sstream>

namespace okay {

std::vector<Vec3> AnimClip::Sample(float t) const {
    if (keys.empty()) return {};
    if (t <= keys.front().time) return keys.front().pose;
    if (t >= keys.back().time)  return keys.back().pose;
    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        const AnimKey& a = keys[i];
        const AnimKey& b = keys[i + 1];
        if (t < a.time || t > b.time) continue;
        float span = b.time - a.time;
        float u = span > 1e-6f ? (t - a.time) / span : 0.0f;
        std::size_t n = a.pose.size() > b.pose.size() ? a.pose.size() : b.pose.size();
        std::vector<Vec3> out(n, Vec3{0, 0, 0});
        for (std::size_t k = 0; k < n; ++k) {
            Vec3 pa = k < a.pose.size() ? a.pose[k] : Vec3{0, 0, 0};
            Vec3 pb = k < b.pose.size() ? b.pose[k] : Vec3{0, 0, 0};
            out[k] = pa + (pb - pa) * u;
        }
        return out;
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
            std::string mode;
            if (ls >> mode) c.loop = (mode != "once");
            clips.push_back(std::move(c));
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

} // namespace okay
