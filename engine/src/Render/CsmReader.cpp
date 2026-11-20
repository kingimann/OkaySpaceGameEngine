// Our own CSM (Character Studio motion capture, text) reader: the markers as empty nodes under one root and the
// motion as one clip with a position key per frame and marker (no meshes, like BVH). Written from the field-by-field
// analysis of the one real file of the format that was available (a Vicon capture exported as CSM) and the layout
// of the published Character Studio text format: "$"-keyword header lines ($Filename, $Date, $Time, $Actor, $Comments,
// $FirstFrame, $LastFrame, $Rate, $Order with the marker names, $Points with one line per frame: the frame number and
// x y z for every marker, in $Order). No other project's importer source was read.
// Bounds-checked, per-file budgets (markers, keys, file size), no recursion, numbers parsed locale independently.
// See okay/Render/ModelData.hpp and docs/content/model_readers.md.
#include "ReaderCommon.hpp"
#include "TextReaderUtil.hpp"
#include "okay/Render/ModelData.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace okay {

namespace {
namespace csm {

constexpr std::size_t kMaxFileBytes = 128u * 1024u * 1024u;
constexpr std::size_t kMaxMarkers = 1024, kMaxNameLen = 64;
constexpr std::uint64_t kMaxKeys = 2000000;   // markers x frames over the file
constexpr double kMaxRate = 100000.0;

bool Fail(std::string* err, const std::string& m) { if (err) *err = "CSM: " + m; return false; }

// The next token of [p, end): sets [b, e) and moves p past it. False at the end.
bool NextToken(const char*& p, const char* end, const char*& b, const char*& e) {
    while (p < end && detail::rc::Ws(*p)) ++p;
    if (p >= end) return false;
    b = p;
    while (p < end && !detail::rc::Ws(*p)) ++p;
    e = p;
    return true;
}

} // namespace csm
} // namespace

static bool ReadCsmImpl(const std::string& text, ImportedModel& out, std::string* err) {
    using namespace csm;
    out = ImportedModel{};
    detail::txt::Lines lines(text);
    std::string_view line;   // a view into the text
    enum Section { kHeader, kComments, kOrder, kPoints } section = kHeader;
    std::vector<std::string> names;
    bool haveOrder = false, havePoints = false, haveRate = false;
    double rate = 30.0, firstFrameHdr = 0, lastFrameHdr = 0;
    bool haveFirst = false, haveLast = false;
    std::string actor;
    int unknownKeywords = 0;

    std::vector<ImportedChannel> chans;
    std::vector<double> vals;
    std::uint64_t frames = 0;
    double firstFrame = 0, lastFrame = 0;
    std::size_t badLines = 0, backwardLines = 0;
    std::vector<Vec3> firstPos;

    while (lines.Next(line)) {
        const char* p = line.data();
        const char* end = p + line.size();
        const char *tb, *te;
        const char* q = p;
        if (!NextToken(q, end, tb, te)) continue;   // an empty line
        if (*tb == '$') {
            const std::string kw = detail::rc::Lower(std::string(tb, te));
            section = kHeader;
            auto numberArg = [&](double& v) -> bool {
                const char *ab, *ae;
                return NextToken(q, end, ab, ae) && detail::rc::ParseNumber(ab, ae, v);
            };
            if (kw == "$comments") section = kComments;
            else if (kw == "$order") {
                if (haveOrder) return Fail(err, "a second $Order section");
                haveOrder = true;
                section = kOrder;
                // names may also follow the keyword on the same line
                const char *nb, *ne;
                while (NextToken(q, end, nb, ne)) {
                    if (names.size() >= kMaxMarkers) return Fail(err, "over the marker budget (" + std::to_string(kMaxMarkers) + " markers per file)");
                    if ((std::size_t)(ne - nb) > kMaxNameLen) return Fail(err, "a marker name is longer than " + std::to_string(kMaxNameLen) + " characters");
                    names.emplace_back(nb, ne);
                }
            } else if (kw == "$points") {
                if (!haveOrder || names.empty()) return Fail(err, "$Points comes before any $Order marker names");
                if (havePoints) return Fail(err, "a second $Points section");
                havePoints = true;
                section = kPoints;
                chans.resize(names.size());
                for (std::size_t i = 0; i < names.size(); ++i) chans[i].node = names[i];
                vals.assign(names.size() * 3 + 1, 0.0);
                // room for the frames $FirstFrame / $LastFrame announce, at most what the rest of the text can hold (a frame line
                // has at least two characters per value) and the key budget: no regrowth, no doubled capacity
                if (haveFirst && haveLast && lastFrameHdr >= firstFrameHdr && lastFrameHdr - firstFrameHdr < 1e9) {
                    const std::size_t announced = (std::size_t)(lastFrameHdr - firstFrameHdr) + 1;
                    const std::size_t room = std::min<std::size_t>({announced, (text.size() - std::min(text.size(), lines.pos)) / (2 * (names.size() * 3 + 1)) + 1, (std::size_t)(kMaxKeys / names.size())});
                    for (ImportedChannel& c : chans) c.position.reserve(room);
                }
            } else if (kw == "$rate") {
                double v;
                if (!numberArg(v) || !(v > 0) || v > kMaxRate) return Fail(err, "$Rate is not a number between 0 and " + std::to_string((int)kMaxRate));
                rate = v; haveRate = true;
            } else if (kw == "$firstframe") { double v; if (numberArg(v)) { firstFrameHdr = v; haveFirst = true; } }
            else if (kw == "$lastframe") { double v; if (numberArg(v)) { lastFrameHdr = v; haveLast = true; } }
            else if (kw == "$actor") { const char *ab, *ae; if (NextToken(q, end, ab, ae)) actor.assign(ab, (std::size_t)std::min<std::ptrdiff_t>(ae - ab, 256)); }
            else if (kw == "$filename" || kw == "$date" || kw == "$time") {}
            else ++unknownKeywords;
            continue;
        }
        if (section == kOrder) {
            const char *nb = tb, *ne = te;
            q = p;
            while (NextToken(q, end, nb, ne)) {
                if (names.size() >= kMaxMarkers) return Fail(err, "over the marker budget (" + std::to_string(kMaxMarkers) + " markers per file)");
                if ((std::size_t)(ne - nb) > kMaxNameLen) return Fail(err, "a marker name is longer than " + std::to_string(kMaxNameLen) + " characters");
                names.emplace_back(nb, ne);
            }
            continue;
        }
        if (section != kPoints) continue;   // comments and unknown lines before $Points
        // a frame line: the frame number, then x y z for every marker; in place, nothing is stored until the whole line is good
        const std::size_t want = names.size() * 3 + 1;
        std::size_t got = 0;
        bool good = true;
        q = p;
        for (;;) {   // the tokens NextToken gives, each read as ParseNumber reads it (ScanNumber: in one pass)
            while (q < end && detail::rc::Ws(*q)) ++q;
            if (q >= end) break;
            if (got >= want) { good = false; break; }
            double v;
            if (!detail::rc::ScanNumber(q, end, v)) { good = false; break; }
            vals[got++] = v;
        }
        if (!good || got != want) { ++badLines; continue; }
        if (frames > 0 && !(vals[0] > lastFrame)) { ++backwardLines; continue; }
        if ((frames + 1) * (std::uint64_t)names.size() > kMaxKeys) return Fail(err, "over the key budget (" + std::to_string(kMaxKeys) + " marker positions per file)");
        if (frames == 0) firstFrame = vals[0];
        lastFrame = vals[0];
        const float t = (float)((vals[0] - firstFrame) / rate);
        for (std::size_t m = 0; m < names.size(); ++m) {
            const Vec3 pos{(float)vals[1 + m * 3], (float)vals[2 + m * 3], (float)vals[3 + m * 3]};
            chans[m].position.push_back(ImportedKeyV3{t, pos});
            if (frames == 0) firstPos.push_back(pos);
        }
        ++frames;
    }
    if (!haveOrder || names.empty()) return Fail(err, "no $Order section with marker names");
    if (!havePoints) return Fail(err, "no $Points section");
    if (frames == 0) return Fail(err, "no frame line with the right number of values");
    {   // marker names must be unique: the clip names its nodes
        std::unordered_set<std::string> seen;
        for (const std::string& n : names) if (!seen.insert(n).second) return Fail(err, "the marker name \"" + n + "\" appears twice");
    }

    ImportedNode root;
    root.name = "CSM";
    while (std::find(names.begin(), names.end(), root.name) != names.end()) root.name += "_root";
    out.nodes.push_back(root);
    for (std::size_t m = 0; m < names.size(); ++m) {
        ImportedNode nd;
        nd.name = names[m];
        nd.parent = 0;
        nd.position = firstPos[m];
        out.nodes.push_back(std::move(nd));
    }
    ImportedClip clip;
    clip.name = "csm";
    clip.channels = std::move(chans);
    out.clips.push_back(std::move(clip));
    detail::rc::ApplyUpAxis(out, true, false);   // optical motion capture (Vicon, Motion Analysis) is Z-up
    out.backend = "own";

    auto note = [&](const std::string& s) { out.notes.push_back("CSM: " + s); };
    note("markers only: " + std::to_string(names.size()) + " marker(s) as empty nodes with position keys, no mesh; units are kept as written (the sample file is in millimetres), Z-up turned into the engine's Y-up by the root node");
    if (!haveRate) note("no $Rate: 30 frames per second assumed");
    if (haveFirst && haveLast && (double)frames != lastFrameHdr - firstFrameHdr + 1)
        note("$FirstFrame / $LastFrame do not match the number of frame lines read");
    if (badLines) note(std::to_string(badLines) + " frame line(s) skipped: not a frame number followed by 3 numbers per marker (missing markers are not interpolated)");
    if (backwardLines) note(std::to_string(backwardLines) + " frame line(s) skipped: frame numbers must increase");
    if (unknownKeywords) note(std::to_string(unknownKeywords) + " unknown $ keyword line(s) ignored");
    if (!actor.empty()) note("actor: " + actor);
    return true;
}

bool ReadCsmBuffer(const std::string& text, ImportedModel& out, std::string* err) {
    const unsigned char a = text.size() > 0 ? (unsigned char)text[0] : 0, b = text.size() > 1 ? (unsigned char)text[1] : 0;
    const bool marked = (a == 0xEF && b == 0xBB) || (a == 0xFF && b == 0xFE) || (a == 0xFE && b == 0xFF);   // a byte order mark: UTF-8 or UTF-16 text
    bool ok;
    if (marked) {
        std::string t = text;
        bool converted = false;
        std::string why;
        if (!detail::txt::NormalizeEncoding(t, converted, why, csm::kMaxFileBytes)) { out = ImportedModel{}; return csm::Fail(err, why); }
        ok = ReadCsmImpl(t, out, err);
    } else ok = ReadCsmImpl(text, out, err);
    if (!ok) out = ImportedModel{};   // a failed read leaves nothing behind
    return ok;
}

bool ReadCsmFile(const std::string& path, ImportedModel& out, std::string* err) {
    out = ImportedModel{};
    std::string bytes;
    if (!detail::txt::ReadFileCapped(path, csm::kMaxFileBytes, bytes, err)) return false;
    return ReadCsmBuffer(bytes, out, err);
}

} // namespace okay
