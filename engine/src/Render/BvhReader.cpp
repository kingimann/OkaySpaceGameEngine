// Our own BVH (Biovision hierarchy, motion capture) reader: the joint hierarchy as
// nodes and the motion as one animation clip. Written from the public description of
// the text format. Bounds-checked, capped, no recursion. See okay/Render/ModelData.hpp
// and docs/own_bvh_reader.md.
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {

namespace {
namespace bvh {

constexpr std::size_t kMaxFileBytes = 512u * 1024u * 1024u;
constexpr std::size_t kMaxJoints    = 10000;
constexpr int         kMaxDepth     = 256;
constexpr std::size_t kMaxFrames    = 1000000;
constexpr std::size_t kMaxSamples   = 60000000;   // frames x channels, so memory stays bounded
constexpr std::size_t kMaxKeys      = 30000000;   // output keys over all joints

inline bool IsWs(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }

enum Chan { XPOS, YPOS, ZPOS, XROT, YROT, ZROT };

struct Joint {
    std::string name;
    int parent = -1;
    Vec3 offset{0, 0, 0};
    std::vector<int> chans;   // Chan values in file order
    std::size_t first = 0;    // index of the first channel in a frame row
    int endSites = 0;
};

struct Reader {
    const std::string* text = nullptr;
    std::size_t pos = 0;
    std::string err;
    std::vector<Joint> joints;
    int endSites = 0;
    int extraRoots = 0;

    bool Fail(const std::string& m) { if (err.empty()) err = "BVH: " + m; return false; }

    // Next line (without the line break), advancing; '\n', '\r' and '\r\n' all end a line.
    bool NextLine(std::string& line) {
        const std::string& t = *text;
        if (pos >= t.size()) return false;
        std::size_t e = pos;
        while (e < t.size() && t[e] != '\n' && t[e] != '\r') ++e;
        line.assign(t, pos, e - pos);
        pos = e;
        if (pos < t.size() && t[pos] == '\r') { ++pos; if (pos < t.size() && t[pos] == '\n') ++pos; }
        else if (pos < t.size()) ++pos;
        return true;
    }

    static std::vector<std::string> Split(const std::string& s) {
        std::vector<std::string> r;
        std::size_t i = 0;
        while (i < s.size()) {
            while (i < s.size() && IsWs(s[i])) ++i;
            std::size_t a = i;
            while (i < s.size() && !IsWs(s[i])) ++i;
            if (i > a) r.push_back(s.substr(a, i - a));
        }
        return r;
    }

    static std::string Lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }

    static bool Num(const std::string& s, double& v) {
        if (s.empty()) return false;
        char* e = nullptr;
        v = std::strtod(s.c_str(), &e);
        return e && *e == '\0' && std::isfinite(v) && std::fabs(v) <= 1e15;
    }

    static bool ChanFromName(const std::string& raw, int& c) {
        const std::string n = Lower(raw);
        if (n == "xposition") c = XPOS; else if (n == "yposition") c = YPOS; else if (n == "zposition") c = ZPOS;
        else if (n == "xrotation") c = XROT; else if (n == "yrotation") c = YROT; else if (n == "zrotation") c = ZROT;
        else return false;
        return true;
    }

    bool Hierarchy(std::size_t& totalChannels) {
        std::string line;
        std::vector<int> stack;      // open joints; -1 = an End Site block
        bool pendingOpen = false;    // a name line is waiting for its "{"
        int pendingJoint = -1;       // joint (or -1 for End Site) that the next "{" opens
        bool pendingIsEnd = false;
        bool sawRoot = false, closedAll = false;
        totalChannels = 0;
        while (NextLine(line)) {
            std::vector<std::string> tk = Split(line);
            if (tk.empty()) continue;
            std::string kw = Lower(tk[0]);
            // "{" or "}" may sit at the start or end of a line with other text.
            auto openBrace = [&]() -> bool {
                if (!pendingOpen) return Fail("'{' without a joint");
                pendingOpen = false;
                if (stack.size() >= (std::size_t)kMaxDepth) return Fail("joints nested too deeply");
                stack.push_back(pendingIsEnd ? -1 : pendingJoint);
                return true;
            };
            if (kw == "motion") { if (!stack.empty() || pendingOpen) return Fail("MOTION inside an unclosed joint"); return sawRoot ? (closedAll ? true : Fail("hierarchy never closed")) : Fail("no ROOT before MOTION"); }
            if (kw == "hierarchy") continue;
            if (kw == "{") { if (!openBrace()) return false; continue; }
            if (kw == "}") {
                if (stack.empty()) return Fail("'}' without a joint");
                stack.pop_back();
                if (stack.empty()) closedAll = true;
                continue;
            }
            if (kw == "root" || kw == "joint") {
                if (pendingOpen) return Fail("joint without a body");
                if (tk.size() < 2) return Fail("joint without a name");
                if (joints.size() >= kMaxJoints) return Fail("too many joints");
                if (kw == "root") {
                    if (!stack.empty()) return Fail("ROOT inside a joint");
                    if (sawRoot) ++extraRoots;
                    sawRoot = true; closedAll = false;
                } else if (stack.empty() || stack.back() < 0) return Fail("JOINT outside a joint");
                Joint j;
                // the name is the rest of the line (names may hold spaces), minus a trailing "{"
                std::size_t a = line.find(tk[0]) + tk[0].size();
                std::string nm = line.substr(a);
                bool brace = false;
                while (!nm.empty() && (IsWs(nm.back()) || nm.back() == '{')) { if (nm.back() == '{') brace = true; nm.pop_back(); }
                std::size_t b = 0; while (b < nm.size() && IsWs(nm[b])) ++b;
                j.name = nm.substr(b);
                if (j.name.empty()) return Fail("joint without a name");
                j.parent = kw == "root" ? -1 : stack.back();
                joints.push_back(std::move(j));
                pendingOpen = true; pendingJoint = (int)joints.size() - 1; pendingIsEnd = false;
                if (brace && !openBrace()) return false;
                continue;
            }
            if (kw == "end") {
                if (pendingOpen) return Fail("joint without a body");
                if (stack.empty() || stack.back() < 0) return Fail("End Site outside a joint");
                ++endSites; ++joints[(std::size_t)stack.back()].endSites;
                pendingOpen = true; pendingIsEnd = true; pendingJoint = -1;
                if (line.find('{') != std::string::npos && !openBrace()) return false;
                continue;
            }
            if (kw == "offset") {
                if (stack.empty() || tk.size() < 4) return Fail("bad OFFSET");
                double v[3];
                for (int i = 0; i < 3; ++i) if (!Num(tk[(std::size_t)i + 1], v[i])) return Fail("bad OFFSET value");
                if (stack.back() >= 0) joints[(std::size_t)stack.back()].offset = Vec3{(float)v[0], (float)v[1], (float)v[2]};
                continue;
            }
            if (kw == "channels") {
                if (stack.empty() || stack.back() < 0 || tk.size() < 2) return Fail("bad CHANNELS");
                double cnt;
                if (!Num(tk[1], cnt) || cnt < 0 || cnt > 24 || cnt != std::floor(cnt)) return Fail("bad channel count");
                if (tk.size() != (std::size_t)cnt + 2) return Fail("channel count does not match the channel names");
                Joint& j = joints[(std::size_t)stack.back()];
                if (!j.chans.empty()) return Fail("a joint has two CHANNELS lines");
                for (std::size_t i = 0; i < (std::size_t)cnt; ++i) {
                    int c;
                    if (!ChanFromName(tk[2 + i], c)) return Fail("unknown channel '" + tk[2 + i] + "'");
                    j.chans.push_back(c);
                }
                j.first = totalChannels;
                totalChannels += j.chans.size();
                continue;
            }
            // "{" glued to something else, or an unknown keyword
            if (line.find('{') != std::string::npos && pendingOpen) { if (!openBrace()) return false; continue; }
            return Fail("unexpected '" + tk[0] + "' in the hierarchy");
        }
        return Fail("no MOTION section");
    }

    bool Run(const std::string& t, ImportedModel& out) {
        text = &t;
        if (t.size() > kMaxFileBytes) return Fail("file too large");
        std::size_t channels = 0;
        // first non-empty line must be HIERARCHY
        {
            std::string line;
            for (;;) {
                if (!NextLine(line)) return Fail("not a BVH file (no HIERARCHY)");
                const std::vector<std::string> tk = Split(line);
                if (tk.empty()) continue;
                if (Lower(tk[0]) != "hierarchy") return Fail("not a BVH file (no HIERARCHY)");
                break;
            }
        }
        if (!Hierarchy(channels)) return false;
        if (joints.empty()) return Fail("no joints");

        // MOTION header
        std::size_t frames = 0;
        double dt = 0;
        {
            std::string line;
            bool gotFrames = false, gotTime = false;
            while (!(gotFrames && gotTime)) {
                if (!NextLine(line)) return Fail("MOTION header incomplete");
                std::string l = Lower(line);
                std::vector<std::string> tk = Split(l);
                if (tk.empty()) continue;
                if (tk[0] == "frames:" || tk[0] == "frames") {
                    std::string v = tk[0] == "frames:" ? (tk.size() > 1 ? tk[1] : "") : (tk.size() > 2 ? tk[2] : (tk.size() > 1 ? tk[1] : ""));
                    if (tk[0] == "frames" && tk.size() > 1 && tk[1] == ":") v = tk.size() > 2 ? tk[2] : "";
                    double f;
                    if (!Num(v, f) || f < 0 || f != std::floor(f)) return Fail("bad frame count");
                    if (f > (double)kMaxFrames) return Fail("too many frames");
                    frames = (std::size_t)f; gotFrames = true;
                } else if (tk[0] == "frame") {
                    std::string v;
                    for (std::size_t i = 1; i < tk.size(); ++i) if (tk[i] != "time:" && tk[i] != "time" && tk[i] != ":") { v = tk[i]; break; }
                    if (!Num(v, dt) || !(dt > 0) || dt > 1e6) return Fail("bad frame time");
                    gotTime = true;
                } else return Fail("unexpected line in the MOTION header");
            }
        }
        if (channels > 0 && frames > kMaxSamples / channels) return Fail("too many motion samples");
        const std::size_t samples = frames * channels;
        // Every sample takes at least two bytes of text, so a claim larger than the rest of the file is rejected before allocating.
        if (samples > (t.size() - std::min(pos, t.size())) / 2 + 1) return Fail("the motion data is shorter than the frame count says");
        std::vector<float> data;
        data.reserve(samples);
        {
            const char* s = t.c_str() + pos;
            while (data.size() < samples) {
                while (*s && IsWs(*s)) ++s;
                if (!*s) return Fail("the motion data is shorter than the frame count says");
                char* e = nullptr;
                const double v = std::strtod(s, &e);
                if (e == s || !std::isfinite(v) || std::fabs(v) > 1e15) return Fail("bad motion value");
                if (*e && !IsWs(*e)) return Fail("bad motion value");
                data.push_back((float)v);
                s = e;
            }
            while (*s && IsWs(*s)) ++s;
            if (*s) trailing = true;
        }

        // nodes: parents come before children by construction; several ROOTs get a shared parent
        const bool multi = extraRoots > 0;
        std::unordered_map<std::string, int> use;
        std::vector<int> nodeOf(joints.size(), -1);
        if (multi) { ImportedNode r; r.name = "Skeleton"; use["Skeleton"] = 1; out.nodes.push_back(r); }
        for (std::size_t i = 0; i < joints.size(); ++i) {
            ImportedNode nd;
            std::string nm = joints[i].name;
            if (int c = use[nm]++) nm += "_" + std::to_string(c);
            nd.name = nm;
            nd.parent = joints[i].parent >= 0 ? nodeOf[(std::size_t)joints[i].parent] : (multi ? 0 : -1);
            nd.position = joints[i].offset;
            nodeOf[i] = (int)out.nodes.size();
            out.nodes.push_back(nd);
        }

        // the clip
        if (frames > 0) {
            ImportedClip clip;
            clip.name = "motion";
            std::size_t keys = 0;
            for (std::size_t i = 0; i < joints.size(); ++i) {
                const Joint& j = joints[i];
                bool hasPos = false, hasRot = false;
                for (int c : j.chans) { if (c <= ZPOS) hasPos = true; else hasRot = true; }
                if (!hasPos && !hasRot) continue;
                keys += frames * ((hasPos ? 1 : 0) + (hasRot ? 1 : 0));
                if (keys > kMaxKeys) return Fail("too many animation keys");
                ImportedChannel ch;
                ch.node = out.nodes[(std::size_t)nodeOf[i]].name;
                if (hasPos) ch.position.reserve(frames);
                if (hasRot) ch.rotation.reserve(frames);
                for (std::size_t f = 0; f < frames; ++f) {
                    const float time = (float)((double)f * dt);
                    const float* row = data.data() + f * channels + j.first;
                    Vec3 p = j.offset;
                    Quat q;
                    for (std::size_t c = 0; c < j.chans.size(); ++c) {
                        const float v = row[c];
                        switch (j.chans[c]) {
                        case XPOS: p.x += v; break;
                        case YPOS: p.y += v; break;
                        case ZPOS: p.z += v; break;
                        default: {
                            const float h = v * 3.14159265358979f / 360.0f, s = std::sin(h), co = std::cos(h);
                            const Quat r = j.chans[c] == XROT ? Quat{s, 0, 0, co} : j.chans[c] == YROT ? Quat{0, s, 0, co} : Quat{0, 0, s, co};
                            q = q * r;   // the first listed channel is the outermost rotation
                        }
                        }
                    }
                    if (hasPos) ch.position.push_back({time, p});
                    if (hasRot) ch.rotation.push_back({time, q.Normalized()});
                }
                clip.channels.push_back(std::move(ch));
            }
            if (!clip.channels.empty()) out.clips.push_back(std::move(clip));
        }
        out.backend = "own";
        if (endSites) out.notes.push_back("BVH: " + std::to_string(endSites) + " End Site(s) (bone tips) not imported as nodes");
        if (extraRoots) out.notes.push_back("BVH: " + std::to_string(extraRoots + 1) + " ROOT joints; they hang under a synthetic node 'Skeleton'");
        if (trailing) out.notes.push_back("BVH: text after the last frame ignored");
        return true;
    }
    bool trailing = false;
};

} // namespace bvh
} // namespace

bool ReadBvhBuffer(const std::string& text, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    bvh::Reader r;
    if (!r.Run(text, out)) {
        const std::string m = r.err.empty() ? "BVH: could not read file" : r.err;
        out = ImportedModel{};
        if (err) *err = m;
        return false;
    }
    return true;
}

bool ReadBvhFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) { if (err) *err = "cannot open file"; return false; }
    std::string bytes;
    char buf[65536];
    for (;;) {
        const std::size_t k = std::fread(buf, 1, sizeof(buf), fp);
        if (k == 0) break;
        if (bytes.size() + k > bvh::kMaxFileBytes) { std::fclose(fp); if (err) *err = "file too large"; return false; }
        bytes.append(buf, k);
    }
    std::fclose(fp);
    return ReadBvhBuffer(bytes, out, err);
}

} // namespace okay
