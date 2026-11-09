#include "okay/Testing/Regression.hpp"

#include "okay/Scene/Scene.hpp"
#include "okay/Testing/ObjectPath.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>

namespace okay::testing {

bool GoldenUpdateRequested() {
    const char* v = std::getenv("OKAY_UPDATE_GOLDEN");
    return v && *v && std::string(v) != "0";
}

namespace {

bool EnsureParentDir(const std::string& path) {
    std::error_code ec;
    const std::filesystem::path parent = std::filesystem::path(path).parent_path();
    if (parent.empty()) return true;
    std::filesystem::create_directories(parent, ec);
    return !ec;
}

bool WriteBytes(const std::string& path, const void* data, std::size_t size) {
    if (!EnsureParentDir(path)) return false;
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(static_cast<const char*>(data), (std::streamsize)size);
    return (bool)f;
}

bool ReadBytes(const std::string& path, std::vector<std::uint8_t>& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

GoldenResult Updated(const std::string& path) {
    GoldenResult r;
    r.updated = true;
    r.message = "golden updated: " + path;
    return r;
}

GoldenResult Missing(const std::string& path) {
    GoldenResult r;
    r.ok = false;
    r.missing = true;
    r.message = "golden file not found: " + path + " (run with OKAY_UPDATE_GOLDEN=1 to create it)";
    return r;
}

// ---- Text ----

std::vector<std::string> SplitLines(const std::string& text, const TextGoldenOptions& o) {
    std::vector<std::string> lines;
    std::string cur;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '\n' || (c == '\r' && o.normalizeLineEndings)) {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') ++i;
            lines.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    lines.push_back(cur);     // the text after the last newline (empty when it ended with one)
    if (o.trimTrailingWhitespace)
        for (auto& l : lines) {
            std::size_t end = l.find_last_not_of(" \t\r");
            l.erase(end == std::string::npos ? 0 : end + 1);
        }
    if (o.ignoreTrailingNewlines)
        while (!lines.empty() && lines.back().empty()) lines.pop_back();
    return lines;
}

struct Piece {
    bool number = false;
    std::string text;
    double value = 0.0;
};

bool IsWordChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

// Split a line into runs of plain text and numbers. A number is not picked out of the
// middle of a word ("Cube2" stays one piece of text).
std::vector<Piece> Pieces(const std::string& s) {
    std::vector<Piece> out;
    std::string text;
    auto flush = [&] { if (!text.empty()) { out.push_back({false, text, 0.0}); text.clear(); } };
    std::size_t i = 0;
    while (i < s.size()) {
        const bool startsNumber =
            (i == 0 || !IsWordChar(s[i - 1])) &&
            (std::isdigit((unsigned char)s[i]) ||
             ((s[i] == '-' || s[i] == '+' || s[i] == '.') && i + 1 < s.size() &&
              (std::isdigit((unsigned char)s[i + 1]) || (s[i + 1] == '.' && i + 2 < s.size() && std::isdigit((unsigned char)s[i + 2])))));
        if (startsNumber) {
            const char* begin = s.c_str() + i;
            char* end = nullptr;
            const double v = std::strtod(begin, &end);
            if (end && end > begin && !(end < s.c_str() + s.size() && IsWordChar(*end) && *end != 'e' && *end != 'E')) {
                flush();
                out.push_back({true, std::string(begin, (const char*)end), v});
                i += (std::size_t)(end - begin);
                continue;
            }
        }
        text += s[i++];
    }
    flush();
    return out;
}

bool NumbersClose(double a, double b, const TextGoldenOptions& o) {
    if (a == b) return true;
    if (std::isnan(a) && std::isnan(b)) return true;
    const double d = std::fabs(a - b);
    if (d <= o.numberTolerance) return true;
    return o.relativeTolerance > 0.0 && d <= o.relativeTolerance * std::max(std::fabs(a), std::fabs(b));
}

bool LinesMatch(const std::string& a, const std::string& e, const TextGoldenOptions& o) {
    if (a == e) return true;
    if (o.numberTolerance <= 0.0 && o.relativeTolerance <= 0.0) return false;
    const auto pa = Pieces(a), pe = Pieces(e);
    if (pa.size() != pe.size()) return false;
    for (std::size_t i = 0; i < pa.size(); ++i) {
        if (pa[i].number != pe[i].number) return false;
        if (pa[i].number ? !NumbersClose(pa[i].value, pe[i].value, o) : pa[i].text != pe[i].text) return false;
    }
    return true;
}

std::string Shorten(const std::string& s, std::size_t n = 200) {
    return s.size() <= n ? s : s.substr(0, n) + "...";
}

} // namespace

GoldenResult CompareText(const std::string& actual, const std::string& expected, const TextGoldenOptions& o) {
    const auto a = SplitLines(actual, o), e = SplitLines(expected, o);
    GoldenResult r;
    std::ostringstream msg;
    const std::size_t n = std::max(a.size(), e.size());
    for (std::size_t i = 0; i < n; ++i) {
        const bool hasA = i < a.size(), hasE = i < e.size();
        if (hasA && hasE && LinesMatch(a[i], e[i], o)) continue;
        if (r.differences < o.maxReportedDiffs) {
            msg << "  line " << (i + 1) << ":\n";
            if (hasE) msg << "    expected: " << Shorten(e[i]) << "\n"; else msg << "    expected: (no such line)\n";
            if (hasA) msg << "    actual:   " << Shorten(a[i]) << "\n"; else msg << "    actual:   (no such line)\n";
        }
        ++r.differences;
    }
    if (r.differences) {
        r.ok = false;
        std::string head = std::to_string(r.differences) + " line" + (r.differences == 1 ? "" : "s") + " differ";
        if (a.size() != e.size()) head += " (" + std::to_string(a.size()) + " lines, golden has " + std::to_string(e.size()) + ")";
        r.message = head + ":\n" + msg.str();
        if (r.differences > o.maxReportedDiffs) r.message += "  ... and " + std::to_string(r.differences - o.maxReportedDiffs) + " more\n";
    }
    return r;
}

GoldenResult CompareTextToGolden(const std::string& actual, const std::string& goldenPath, const TextGoldenOptions& o) {
    if (o.update || GoldenUpdateRequested()) {
        if (!WriteBytes(goldenPath, actual.data(), actual.size())) {
            GoldenResult r;
            r.ok = false;
            r.message = "cannot write golden file " + goldenPath;
            return r;
        }
        return Updated(goldenPath);
    }
    std::vector<std::uint8_t> bytes;
    if (!ReadBytes(goldenPath, bytes)) return Missing(goldenPath);
    GoldenResult r = CompareText(actual, std::string(bytes.begin(), bytes.end()), o);
    if (!r.ok) r.message = "text differs from " + goldenPath + ": " + r.message;
    return r;
}

// ---- Binary ----

GoldenResult CompareBinary(const std::vector<std::uint8_t>& actual, const std::vector<std::uint8_t>& expected,
                           const BinaryGoldenOptions& o) {
    GoldenResult r;
    if (actual.size() != expected.size()) {
        r.ok = false;
        r.differences = 1;
        r.message = "size differs: " + std::to_string(actual.size()) + " bytes, golden has " + std::to_string(expected.size());
        return r;
    }
    std::ostringstream msg;
    std::size_t over = 0;
    auto note = [&](const std::string& what) {
        if (over < o.maxReportedDiffs) msg << "  " << what << "\n";
        ++over;
    };
    std::size_t i = 0;
    if (o.asFloat32) {
        for (; i + 4 <= actual.size(); i += 4) {
            float a, e;
            std::memcpy(&a, &actual[i], 4);
            std::memcpy(&e, &expected[i], 4);
            const bool bothNan = std::isnan(a) && std::isnan(e);
            if (bothNan || a == e || std::fabs((double)a - (double)e) <= (double)o.floatTolerance) continue;
            note("float " + std::to_string(i / 4) + ": " + std::to_string(a) + " vs golden " + std::to_string(e));
        }
    }
    for (; i < actual.size(); ++i) {
        const int d = std::abs((int)actual[i] - (int)expected[i]);
        if (d <= (o.asFloat32 ? 0 : o.byteTolerance)) continue;
        note("byte " + std::to_string(i) + ": " + std::to_string((int)actual[i]) + " vs golden " + std::to_string((int)expected[i]));
    }
    r.differences = over;
    if (over > o.maxDifferingBytes) {
        r.ok = false;
        r.message = std::to_string(over) + " " + (o.asFloat32 ? "value" : "byte") + (over == 1 ? "" : "s") + " differ:\n" + msg.str();
        if (over > o.maxReportedDiffs) r.message += "  ... and " + std::to_string(over - o.maxReportedDiffs) + " more\n";
    }
    return r;
}

GoldenResult CompareBinaryToGolden(const std::vector<std::uint8_t>& actual, const std::string& goldenPath,
                                   const BinaryGoldenOptions& o) {
    if (o.update || GoldenUpdateRequested()) {
        if (!WriteBytes(goldenPath, actual.data(), actual.size())) {
            GoldenResult r;
            r.ok = false;
            r.message = "cannot write golden file " + goldenPath;
            return r;
        }
        return Updated(goldenPath);
    }
    std::vector<std::uint8_t> golden;
    if (!ReadBytes(goldenPath, golden)) return Missing(goldenPath);
    GoldenResult r = CompareBinary(actual, golden, o);
    if (!r.ok) r.message = "binary differs from " + goldenPath + ": " + r.message;
    return r;
}

// ---- Scene digest ----

namespace {

// Fixed-point text of a rounded value without printf, so it reads the same everywhere:
// 1.5 at 3 decimals is "1.500", and a value that rounds to zero never prints as "-0.000".
std::string Fixed(double v, int decimals) {
    if (std::isnan(v)) return "nan";
    if (std::isinf(v)) return v < 0 ? "-inf" : "inf";
    if (decimals < 0) decimals = 0;
    if (decimals > 9) decimals = 9;
    long long p10 = 1;
    for (int i = 0; i < decimals; ++i) p10 *= 10;
    const double scaled = std::round(v * (double)p10);
    if (std::fabs(scaled) > 9.0e15) return v < 0 ? "-huge" : "huge";
    long long n = (long long)scaled;
    const bool neg = n < 0;
    if (neg) n = -n;
    std::string s = std::string(neg ? "-" : "") + std::to_string(n / p10);
    if (decimals > 0) {
        std::string frac = std::to_string(n % p10);
        s += "." + std::string((std::size_t)decimals - frac.size(), '0') + frac;
    }
    return s;
}

std::uint64_t Fnv1a(const std::vector<std::string>& lines) {
    std::uint64_t h = 1469598103934665603ull;
    for (const auto& l : lines) {
        for (unsigned char c : l) { h ^= c; h *= 1099511628211ull; }
        h ^= (unsigned char)'\n';
        h *= 1099511628211ull;
    }
    return h;
}

} // namespace

std::string SceneDigest::HashHex() const {
    char buf[20];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)hash);
    return buf;
}

std::string SceneDigest::ToText() const {
    std::string t = "scenehash " + HashHex() + "\n";
    for (const auto& l : lines) t += l + "\n";
    return t;
}

SceneDigest DigestScene(const Scene& scene, const SceneHashOptions& o) {
    SceneDigest d;
    std::map<std::string, int> seen;     // duplicate paths get "#2", "#3" in scene order
    for (const auto& up : scene.Objects()) {
        const GameObject& go = *up;
        if (go.IsDestroyed() || !go.transform) continue;
        if (go.transient && !o.includeTransient) continue;
        if (!go.active && !o.includeInactive) continue;

        std::string path = ObjectPath(&go);
        const int n = ++seen[path];
        if (n > 1) path += "#" + std::to_string(n);

        const Vec3 p = go.transform->Position();
        Quat q = go.transform->Rotation();
        // q and -q are the same rotation: pick the one with w >= 0 (or the first nonzero part positive).
        const float comps[4] = {q.w, q.x, q.y, q.z};
        for (float c : comps) {
            if (std::fabs(c) < 1e-6f) continue;
            if (c < 0.0f) { q.x = -q.x; q.y = -q.y; q.z = -q.z; q.w = -q.w; }
            break;
        }
        const Vec3 s = go.transform->LossyScale();

        std::string line = path;
        line += "|active=" + std::string(go.active ? "1" : "0");
        if (o.includeLayerAndTag) line += "|layer=" + std::to_string(go.layer) + "|tag=" + go.tag;
        line += "|pos=" + Fixed(p.x, o.decimals) + "," + Fixed(p.y, o.decimals) + "," + Fixed(p.z, o.decimals);
        line += "|rot=" + Fixed(q.x, o.decimals) + "," + Fixed(q.y, o.decimals) + "," + Fixed(q.z, o.decimals) + "," + Fixed(q.w, o.decimals);
        line += "|scale=" + Fixed(s.x, o.decimals) + "," + Fixed(s.y, o.decimals) + "," + Fixed(s.z, o.decimals);
        if (o.includeComponents) {
            line += "|components=";
            bool first = true;
            for (const auto& c : go.Components()) {
                if (c.get() == static_cast<const Component*>(go.transform)) continue;
                if (!first) line += ",";
                line += ComponentTypeName(*c);
                first = false;
            }
        }
        d.lines.push_back(std::move(line));
    }
    std::sort(d.lines.begin(), d.lines.end());
    d.hash = Fnv1a(d.lines);
    return d;
}

std::string DiffDigests(const SceneDigest& expected, const SceneDigest& actual, std::size_t maxLines) {
    // Both line lists are sorted: walk them side by side.
    std::string out;
    std::size_t shown = 0, total = 0;
    auto emit = [&](const char* sign, const std::string& line) {
        ++total;
        if (shown < maxLines) { out += std::string(sign) + " " + line + "\n"; ++shown; }
    };
    std::size_t i = 0, j = 0;
    while (i < expected.lines.size() || j < actual.lines.size()) {
        if (j >= actual.lines.size() || (i < expected.lines.size() && expected.lines[i] < actual.lines[j])) emit("-", expected.lines[i++]);
        else if (i >= expected.lines.size() || actual.lines[j] < expected.lines[i]) emit("+", actual.lines[j++]);
        else { ++i; ++j; }
    }
    if (total > shown) out += "... and " + std::to_string(total - shown) + " more\n";
    return out;
}

GoldenResult CheckSceneGolden(const Scene& scene, const std::string& goldenPath, const SceneHashOptions& options,
                              bool update) {
    const SceneDigest now = DigestScene(scene, options);
    TextGoldenOptions t;
    t.update = update;
    GoldenResult r = CompareTextToGolden(now.ToText(), goldenPath, t);
    if (!r.ok && !r.missing) r.message = "the scene no longer matches " + goldenPath + " (hash " + now.HashHex() + "):\n" + r.message;
    return r;
}

// ---- Images ----

ImageCompareResult CompareImages(const Image& actual, const Image& expected, const ImageCompareOptions& o) {
    ImageCompareResult r;
    const int aw = actual.Width(), ah = actual.Height(), ew = expected.Width(), eh = expected.Height();
    if (aw != ew || ah != eh) {
        r.sizeMismatch = true;
        r.message = "image is " + std::to_string(aw) + "x" + std::to_string(ah) + ", golden is " + std::to_string(ew) + "x" + std::to_string(eh);
        const int w = std::max(aw, ew), h = std::max(ah, eh);
        r.diff = Image(w, h);
        r.diff.Fill(Color(1.0f, 0.0f, 0.0f, 1.0f));
        return r;
    }
    const int w = aw, h = ah;
    r.diff = Image(w, h);
    const std::size_t total = (std::size_t)w * (std::size_t)h;
    const int channels = o.compareAlpha ? 4 : 3;
    double sum = 0.0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::size_t at = ((std::size_t)y * w + x) * 4;
            const std::uint8_t* a = actual.Data() + at;
            const std::uint8_t* e = expected.Data() + at;
            int worst = 0;
            for (int c = 0; c < channels; ++c) {
                const int d = std::abs((int)a[c] - (int)e[c]);
                sum += d;
                worst = std::max(worst, d);
            }
            r.maxChannelDiff = std::max(r.maxChannelDiff, worst);
            std::uint8_t* dp = r.diff.Data() + at;
            if (worst > o.channelTolerance) {
                ++r.differingPixels;
                dp[0] = (std::uint8_t)std::min(255, 128 + worst); dp[1] = 0; dp[2] = 0; dp[3] = 255;
            } else {
                const int dim = ((int)a[0] + a[1] + a[2]) / 3 / 4 + 24;
                dp[0] = dp[1] = dp[2] = (std::uint8_t)dim; dp[3] = 255;
            }
        }
    r.meanChannelDiff = total ? sum / (double)(total * (std::size_t)channels) : 0.0;
    const double fraction = o.maxDifferingFraction < 0.0 ? 0.0 : (o.maxDifferingFraction > 1.0 ? 1.0 : o.maxDifferingFraction);
    const std::size_t byFraction = (std::size_t)std::floor(fraction * (double)total);
    const std::size_t allowed = std::max(o.maxDifferingPixels, byFraction);
    r.match = r.differingPixels <= allowed;
    if (!r.match)
        r.message = std::to_string(r.differingPixels) + " of " + std::to_string(total) + " pixels differ by more than " +
                    std::to_string(o.channelTolerance) + " (largest channel difference " + std::to_string(r.maxChannelDiff) +
                    ", allowed " + std::to_string(allowed) + " pixels)";
    return r;
}

namespace {

bool HasExtension(const std::string& path, const char* ext) {
    const std::size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string e = path.substr(dot);
    for (char& c : e) c = (char)std::tolower((unsigned char)c);
    return e == ext;
}

// PPM header tokens are separated by whitespace; `#` starts a comment to the end of the line.
bool NextToken(const std::vector<std::uint8_t>& d, std::size_t& pos, std::string& tok) {
    tok.clear();
    while (pos < d.size()) {
        if (d[pos] == '#') { while (pos < d.size() && d[pos] != '\n') ++pos; }
        else if (std::isspace(d[pos])) ++pos;
        else break;
    }
    while (pos < d.size() && !std::isspace(d[pos]) && d[pos] != '#') tok += (char)d[pos++];
    return !tok.empty();
}

bool LoadPpm(const std::string& path, Image& out, std::string* error) {
    auto fail = [&](const std::string& why) { if (error) *error = path + ": " + why; return false; };
    std::vector<std::uint8_t> d;
    if (!ReadBytes(path, d)) return fail("cannot open");
    std::size_t pos = 0;
    std::string magic, ws, hs, ms;
    if (!NextToken(d, pos, magic) || (magic != "P6" && magic != "P3")) return fail("not a P6/P3 PPM");
    if (!NextToken(d, pos, ws) || !NextToken(d, pos, hs) || !NextToken(d, pos, ms)) return fail("truncated header");
    const int w = std::atoi(ws.c_str()), h = std::atoi(hs.c_str()), maxv = std::atoi(ms.c_str());
    if (w <= 0 || h <= 0 || w > 16384 || h > 16384 || maxv <= 0 || maxv > 255) return fail("unsupported size or max value");
    std::vector<std::uint8_t> rgba((std::size_t)w * h * 4, 255);
    const std::size_t pixels = (std::size_t)w * h;
    if (magic == "P6") {
        ++pos;    // exactly one whitespace byte separates the header from the data
        if (d.size() < pos + pixels * 3) return fail("truncated pixel data");
        for (std::size_t i = 0; i < pixels; ++i)
            for (int c = 0; c < 3; ++c)
                rgba[i * 4 + (std::size_t)c] = (std::uint8_t)(d[pos + i * 3 + (std::size_t)c] * 255 / maxv);
    } else {
        std::string tok;
        for (std::size_t i = 0; i < pixels; ++i)
            for (int c = 0; c < 3; ++c) {
                if (!NextToken(d, pos, tok)) return fail("truncated pixel data");
                rgba[i * 4 + (std::size_t)c] = (std::uint8_t)(std::atoi(tok.c_str()) * 255 / maxv);
            }
    }
    out.Set(w, h, std::move(rgba));
    return true;
}

bool SavePpm(const Image& image, const std::string& path) {
    if (!image.Valid()) return false;
    const int w = image.Width(), h = image.Height();
    std::string head = "P6\n" + std::to_string(w) + " " + std::to_string(h) + "\n255\n";
    std::vector<std::uint8_t> bytes(head.begin(), head.end());
    bytes.reserve(head.size() + (std::size_t)w * h * 3);
    const std::uint8_t* px = image.Data();
    for (std::size_t i = 0; i < (std::size_t)w * h; ++i) { bytes.push_back(px[i * 4]); bytes.push_back(px[i * 4 + 1]); bytes.push_back(px[i * 4 + 2]); }
    return WriteBytes(path, bytes.data(), bytes.size());
}

} // namespace

bool LoadImageFile(const std::string& path, Image& out, std::string* error) {
    if (HasExtension(path, ".ppm")) return LoadPpm(path, out, error);
    return out.Load(path, error);
}

bool SaveImageFile(const Image& image, const std::string& path) {
    if (!EnsureParentDir(path)) return false;
    if (HasExtension(path, ".ppm")) return SavePpm(image, path);
    return image.SavePNG(path);
}

GoldenResult CheckImageGolden(const Image& actual, const std::string& goldenPath, const ImageCompareOptions& options,
                              const std::string& diffPath, bool update) {
    if (update || GoldenUpdateRequested()) {
        if (!SaveImageFile(actual, goldenPath)) {
            GoldenResult r;
            r.ok = false;
            r.message = "cannot write golden image " + goldenPath;
            return r;
        }
        return Updated(goldenPath);
    }
    Image golden;
    std::string err;
    if (!LoadImageFile(goldenPath, golden, &err)) {
        GoldenResult r = Missing(goldenPath);
        if (!err.empty()) r.message += " (" + err + ")";
        return r;
    }
    ImageCompareResult cmp = CompareImages(actual, golden, options);
    GoldenResult r;
    r.ok = cmp.match;
    r.differences = cmp.sizeMismatch ? 1 : cmp.differingPixels;
    if (!cmp.match) {
        r.message = "image differs from " + goldenPath + ": " + cmp.message;
        if (!diffPath.empty() && cmp.diff.Valid() && SaveImageFile(cmp.diff, diffPath)) r.message += " (diff image: " + diffPath + ")";
    }
    return r;
}

} // namespace okay::testing
