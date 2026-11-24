// Our own reader for Doom 3 / id Tech 4 camera paths (.md5camera, text): one camera node and one clip with a key per frame.
// Written from the public description of the MD5 family of text formats (the same header and token rules as .md5mesh /
// .md5anim): "MD5Version 10", an optional "commandline" string, "numFrames", "frameRate", "numCuts", a "cuts { ... }" block of
// frame numbers where the shot jumps, and a "camera { ... }" block of one line per frame: "( x y z ) ( qx qy qz ) fov". The
// quaternion's w is not stored: it is worked out so the quaternion has length one (w <= 0, the joint convention of the other
// MD5 files). No other project's code was read. Bounds-checked, capped, no recursion. See okay/Render/ModelData.hpp.
#include "TextReaderUtil.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace okay {

namespace {
namespace md5cam {

using namespace detail::txt;

constexpr std::size_t kMaxFileBytes = 64u * 1024u * 1024u;
constexpr long long   kMaxFrames    = 1000000;
constexpr float       kCutGap       = 1e-4f;   // seconds: the held key just before a cut

struct Reader {
    const std::string& s;
    std::size_t pos = 0, line = 1;
    std::string err;
    explicit Reader(const std::string& t) : s(t) {}

    bool Fail(const std::string& m) {
        if (err.empty()) err = "MD5 camera: " + m + " (line " + std::to_string(line) + ")";
        return false;
    }
    // the next token: a word, a "string" (quotes dropped), or one of ( ) { }; "//" comments run to the end of the line
    bool Next(std::string& t) {
        SkipSpaceAndLineComments(s, pos, line);
        t.clear();
        if (pos >= s.size()) return false;
        const char c = s[pos];
        if (c == '(' || c == ')' || c == '{' || c == '}') { t.assign(1, c); ++pos; return true; }
        if (c == '"') {
            const std::size_t a = ++pos;
            while (pos < s.size() && s[pos] != '"' && s[pos] != '\n') ++pos;
            t = "\"" + s.substr(a, pos - a);
            if (pos < s.size() && s[pos] == '"') ++pos;
            return true;
        }
        const std::size_t a = pos;
        while (pos < s.size() && !IsWs(s[pos]) && s[pos] != '(' && s[pos] != ')' && s[pos] != '{' && s[pos] != '}' && s[pos] != '"') ++pos;
        t = s.substr(a, pos - a);
        return true;
    }
    bool Expect(const char* want) {
        std::string t;
        if (!Next(t) || t != want) return Fail(std::string("expected '") + want + "'");
        return true;
    }
    bool Int(long long& v) {
        std::string t;
        if (!Next(t) || !ToInt(t, v)) return Fail("expected a whole number");
        return true;
    }
    bool Flt(float& v) {
        std::string t;
        if (!Next(t) || !ToFloat(t, v)) return Fail("bad number");
        return true;
    }
    bool Triple(float* v) {
        if (!Expect("(")) return false;
        for (int i = 0; i < 3; ++i) if (!Flt(v[i])) return false;
        return Expect(")");
    }

    struct Frame { Vec3 pos; Quat rot; float fov; };
    std::vector<Frame> frames;
    std::vector<long long> cuts;
    long long numFrames = -1, numCuts = -1;
    float fps = -1.0f;

    bool Parse() {
        std::string t;
        if (!Next(t) || t != "MD5Version") return Fail("not an MD5 file (no MD5Version)");
        long long ver = 0;
        if (!Int(ver)) return false;
        if (ver != 10) return Fail("unsupported MD5Version " + std::to_string(ver) + " (only 10)");
        bool haveCamera = false;
        while (Next(t)) {
            if (t == "commandline") { if (!Next(t) || t.empty() || t[0] != '"') return Fail("commandline without a string"); continue; }
            if (t == "numFrames") {
                if (!Int(numFrames)) return false;
                if (numFrames < 1 || numFrames > kMaxFrames) return Fail("bad or too many frames");
                // every frame line holds at least 17 characters ("( 0 0 0 ) ( 0 0 0 ) 0"): a count larger than the file allows is a lie
                if ((std::size_t)numFrames > s.size() / 17 + 1) return Fail("the frame count is larger than the file");
                continue;
            }
            if (t == "frameRate") {
                long long r = 0;
                if (!Int(r)) return false;
                if (r < 1 || r > 10000) return Fail("bad frameRate");
                fps = (float)r;
                continue;
            }
            if (t == "numCuts") {
                if (!Int(numCuts)) return false;
                if (numCuts < 0 || (numFrames >= 0 && numCuts > numFrames)) return Fail("bad numCuts");
                continue;
            }
            if (t == "cuts") {
                if (numCuts < 0 || numFrames < 0) return Fail("cuts before numFrames / numCuts");
                if (!Expect("{")) return false;
                for (long long i = 0; i < numCuts; ++i) {
                    long long c = 0;
                    if (!Int(c)) return false;
                    if (c < 0 || c >= numFrames) return Fail("a cut outside the frames");
                    cuts.push_back(c);
                }
                if (!Expect("}")) return false;
                continue;
            }
            if (t == "camera") {
                if (numFrames < 0 || fps <= 0.0f) return Fail("camera before numFrames / frameRate");
                if (haveCamera) return Fail("a second camera block");
                haveCamera = true;
                if (!Expect("{")) return false;
                frames.reserve((std::size_t)numFrames);
                for (long long f = 0; f < numFrames; ++f) {
                    float p[3], q[3], fov = 0;
                    if (!Triple(p) || !Triple(q) || !Flt(fov)) return false;
                    if (!(fov > 0.0f && fov < 180.0f)) return Fail("a field of view outside 0 to 180 degrees");
                    const float w2 = 1.0f - q[0] * q[0] - q[1] * q[1] - q[2] * q[2];
                    if (w2 < -1e-3f) return Fail("an orientation longer than one");
                    const float w = w2 > 0.0f ? -std::sqrt(w2) : 0.0f;
                    frames.push_back({Vec3{p[0], p[1], p[2]}, Quat{q[0], q[1], q[2], w}.Normalized(), fov});
                }
                if (!Expect("}")) return false;
                continue;
            }
            return Fail("unknown keyword '" + t.substr(0, 40) + "'");
        }
        if (!haveCamera) return Fail("no camera block");
        if (numCuts > 0 && (long long)cuts.size() != numCuts) return Fail("numCuts without a cuts block");
        return true;
    }

    void Build(ImportedModel& out) {
        ImportedNode root;
        root.name = "MD5";
        root.rotation = Quat::AngleAxis(-90.0f, Vec3{1, 0, 0});   // Z up to Y up: (x, y, z) -> (x, z, -y), as the other MD5 files
        out.nodes.push_back(root);
        ImportedNode cam;
        cam.name = "camera";
        cam.parent = 0;
        cam.position = frames[0].pos;
        cam.rotation = frames[0].rot;
        out.nodes.push_back(cam);

        std::vector<char> cutAt(frames.size(), 0);
        for (long long c : cuts) cutAt[(std::size_t)c] = 1;
        ImportedChannel ch;
        ch.node = "camera";
        int heldCuts = 0;
        for (std::size_t f = 0; f < frames.size(); ++f) {
            const float time = (float)((double)f / (double)fps);
            // a cut: the shot jumps, so the previous frame is held up to just before this one (no blend across the cut)
            if (cutAt[f] && f > 0) {
                const float hold = time - std::min(kCutGap, 0.5f / fps);
                ch.position.push_back({hold, frames[f - 1].pos});
                ch.rotation.push_back({hold, frames[f - 1].rot});
                ++heldCuts;
            }
            ch.position.push_back({time, frames[f].pos});
            ch.rotation.push_back({time, frames[f].rot});
        }
        ImportedClip clip;
        clip.name = "camera";
        clip.channels.push_back(std::move(ch));
        out.clips.push_back(std::move(clip));

        float lo = frames[0].fov, hi = lo;
        for (const Frame& f : frames) { lo = std::min(lo, f.fov); hi = std::max(hi, f.fov); }
        auto note = [&](const std::string& m) { out.notes.push_back("MD5 camera: " + m); };
        note("the root node turns Z-up into Y-up; the camera node keeps id Tech 4's orientation (it looks down its own +X axis)");
        note("the field of view (" + std::to_string((int)std::lround(lo)) + (hi != lo ? " to " + std::to_string((int)std::lround(hi)) : std::string()) +
             " degrees) is not carried: the engine's model data has no camera lens");
        note("the path plays at " + std::to_string((int)fps) + " frames per second (frameRate in the file)");
        if (heldCuts) note(std::to_string(heldCuts) + " cut(s): the frame before each cut is held, so the camera jumps instead of sliding");
    }
};

} // namespace md5cam
} // namespace

bool ReadMd5CameraBuffer(const std::string& text, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string t = text, why;
    bool conv = false;
    if (t.empty()) { if (err) *err = "MD5 camera: empty file"; return false; }
    if (!detail::txt::NormalizeEncoding(t, conv, why, md5cam::kMaxFileBytes)) { if (err) *err = "MD5 camera: " + why; return false; }
    md5cam::Reader r(t);
    if (!r.Parse()) { out = ImportedModel{}; if (err) *err = r.err; return false; }
    r.Build(out);
    out.backend = "own";
    return true;
}

bool ReadMd5CameraFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, md5cam::kMaxFileBytes, bytes, err)) return false;
    return ReadMd5CameraBuffer(bytes, out, err);
}

} // namespace okay
