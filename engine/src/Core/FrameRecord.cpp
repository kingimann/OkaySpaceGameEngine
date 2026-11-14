#include "okay/Core/FrameRecord.hpp"
#include "okay/Core/SaveData.hpp"      // savefmt::AtomicWrite, ReadFileCapped, Fnv1a
#include "okay/Debug/FrameHistory.hpp" // Distribution::Of: min / average / percentiles

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace okay {

// ---- numbers ----------------------------------------------------------------------

std::uint32_t FrameRecording::ToMicros(double seconds) {
    if (!(seconds > 0.0) || !std::isfinite(seconds)) return 0;
    const double us = seconds * 1e6;
    if (us >= (double)kMaxFrameMicros) return kMaxFrameMicros;
    return (std::uint32_t)std::llround(us);
}

// ---- the data -----------------------------------------------------------------------

bool FrameRecording::Add(double seconds, std::uint8_t flags, std::int32_t marker) {
    FrameSample s;
    s.micros = ToMicros(seconds);
    s.flags = (std::uint8_t)(flags & (kFramePaused | kFrameHitch | kFrameStepped | kFrameSynthetic));
    s.marker = marker;
    return AddRun(s, 1);
}

bool FrameRecording::AddRun(const FrameSample& sample, std::uint64_t count) {
    if (count == 0) return true;
    if (count > kMaxFrames || m_frames + count > kMaxFrames) return false;
    FrameSample s = sample;
    if (s.micros > kMaxFrameMicros) s.micros = kMaxFrameMicros;
    s.flags = (std::uint8_t)(s.flags & (kFramePaused | kFrameHitch | kFrameStepped | kFrameSynthetic));
    if (!m_runs.empty()) {
        Run& last = m_runs.back();
        if (last.micros == s.micros && last.marker == s.marker && last.flags == s.flags &&
            (std::uint64_t)last.count + count <= 0xFFFFFFFFull) {
            last.count += (std::uint32_t)count;
            m_frames += count;
            m_micros += (std::uint64_t)s.micros * count;
            return true;
        }
    }
    Run r;
    r.first = (std::uint32_t)m_frames;
    r.count = (std::uint32_t)count;
    r.micros = s.micros;
    r.marker = s.marker;
    r.flags = s.flags;
    m_runs.push_back(r);
    m_frames += count;
    m_micros += (std::uint64_t)s.micros * count;
    return true;
}

void FrameRecording::Clear() { m_runs.clear(); m_frames = m_micros = 0; ClearRandom(); }

bool FrameRecording::RunAt(std::size_t i, FrameSample& sample, std::uint64_t& count) const {
    if (i >= m_runs.size()) return false;
    const Run& r = m_runs[i];
    sample.micros = r.micros; sample.marker = r.marker; sample.flags = r.flags;
    count = r.count;
    return true;
}

bool FrameRecording::At(std::uint64_t index, FrameSample& out) const {
    if (index >= m_frames) return false;
    // The last run that starts at or before `index`.
    std::size_t lo = 0, hi = m_runs.size();
    while (lo + 1 < hi) {
        const std::size_t mid = (lo + hi) / 2;
        if ((std::uint64_t)m_runs[mid].first <= index) lo = mid; else hi = mid;
    }
    const Run& r = m_runs[lo];
    out.micros = r.micros; out.marker = r.marker; out.flags = r.flags;
    return true;
}

std::vector<FrameSample> FrameRecording::Expand() const {
    std::vector<FrameSample> v;
    v.reserve((std::size_t)m_frames);
    for (const Run& r : m_runs) {
        FrameSample s; s.micros = r.micros; s.marker = r.marker; s.flags = r.flags;
        for (std::uint32_t k = 0; k < r.count; ++k) v.push_back(s);
    }
    return v;
}

bool FrameRecording::operator==(const FrameRecording& o) const {
    if (m_frames != o.m_frames || m_micros != o.m_micros || m_runs.size() != o.m_runs.size()) return false;
    if (m_hasRandom != o.m_hasRandom || (m_hasRandom && (m_random.s0 != o.m_random.s0 || m_random.s1 != o.m_random.s1))) return false;
    for (std::size_t i = 0; i < m_runs.size(); ++i) {   // runs are always maximal, so equal frames mean equal runs
        const Run& a = m_runs[i]; const Run& b = o.m_runs[i];
        if (a.count != b.count || a.micros != b.micros || a.marker != b.marker || a.flags != b.flags) return false;
    }
    return true;
}

// ---- summary ------------------------------------------------------------------------

FrameSummary FrameRecording::Summary(double hitchMs) const {
    FrameSummary s;
    s.hitchMs = (hitchMs > 0.0 && std::isfinite(hitchMs)) ? hitchMs : 0.0;
    s.frames = m_frames;
    s.seconds = TotalSeconds();
    if (m_frames == 0) return s;
    std::vector<double> ms;
    ms.reserve((std::size_t)m_frames);
    for (const Run& r : m_runs) {
        const double v = (double)r.micros / 1000.0;
        for (std::uint32_t k = 0; k < r.count; ++k) ms.push_back(v);
        if (r.flags & kFramePaused) s.pausedFrames += r.count;
        if (r.flags & kFrameStepped) s.steppedFrames += r.count;
        if (r.flags & kFrameSynthetic) s.syntheticFrames += r.count;
        if (r.marker != 0) s.markedFrames += r.count;
        if (s.hitchMs > 0.0 && v > s.hitchMs) { s.hitches += r.count; s.longestHitchMs = std::max(s.longestHitchMs, v); }
    }
    const Distribution d = Distribution::Of(std::move(ms));
    s.avgMs = d.avg; s.minMs = d.min; s.maxMs = d.max;
    s.p50Ms = d.p50; s.p95Ms = d.p95; s.p99Ms = d.p99;
    s.avgFps = s.seconds > 0.0 ? (double)m_frames / s.seconds : 0.0;
    return s;
}

namespace {
std::string F(const char* fmt, double v) { char b[64]; std::snprintf(b, sizeof b, fmt, v); return b; }
}

std::string FrameSummary::ToText() const {
    if (frames == 0) return "no frames";
    std::string t;
    t += std::to_string(frames) + " frames over " + F("%.3f", seconds) + " s (" + F("%.1f", avgFps) + " fps average)\n";
    t += "frame time: avg " + F("%.2f", avgMs) + " ms, min " + F("%.2f", minMs) + ", median " + F("%.2f", p50Ms) +
         ", 95th " + F("%.2f", p95Ms) + ", 99th " + F("%.2f", p99Ms) + ", max " + F("%.2f", maxMs) + "\n";
    t += std::to_string(hitches) + (hitches == 1 ? " hitch" : " hitches") + " over " + F("%.0f", hitchMs) + " ms";
    if (hitches > 0) t += ", longest " + F("%.2f", longestHitchMs) + " ms";
    t += "\n";
    t += std::to_string(pausedFrames) + " paused, " + std::to_string(steppedFrames) + " stepped, " +
         std::to_string(syntheticFrames) + " capture, " + std::to_string(markedFrames) + " marked frames\n";
    return t;
}

std::string FrameSummary::ToJson() const {
    std::string j = "{";
    j += "\"frames\":" + std::to_string(frames) + ",\"seconds\":" + F("%.6f", seconds);
    j += ",\"avgMs\":" + F("%.4f", avgMs) + ",\"minMs\":" + F("%.4f", minMs) + ",\"maxMs\":" + F("%.4f", maxMs);
    j += ",\"p50Ms\":" + F("%.4f", p50Ms) + ",\"p95Ms\":" + F("%.4f", p95Ms) + ",\"p99Ms\":" + F("%.4f", p99Ms);
    j += ",\"avgFps\":" + F("%.4f", avgFps) + ",\"hitchMs\":" + F("%.4f", hitchMs);
    j += ",\"hitches\":" + std::to_string(hitches) + ",\"longestHitchMs\":" + F("%.4f", longestHitchMs);
    j += ",\"pausedFrames\":" + std::to_string(pausedFrames) + ",\"steppedFrames\":" + std::to_string(steppedFrames);
    j += ",\"syntheticFrames\":" + std::to_string(syntheticFrames) + ",\"markedFrames\":" + std::to_string(markedFrames) + "}";
    return j;
}

// ---- text form --------------------------------------------------------------------

namespace {

constexpr int kTokensPerLine = 12;

std::string FlagLetters(std::uint8_t f) {
    std::string s;
    if (f & kFramePaused)    s += 'p';
    if (f & kFrameHitch)     s += 'h';
    if (f & kFrameStepped)   s += 's';
    if (f & kFrameSynthetic) s += 'c';
    return s;
}

// Digits only, 1..maxDigits of them, into `v`; stops at the first non digit.
bool ReadUInt(const std::string& s, std::size_t& i, int maxDigits, std::uint64_t& v) {
    const std::size_t start = i;
    v = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
        if ((int)(i - start) >= maxDigits) return false;
        v = v * 10 + (std::uint64_t)(s[i] - '0');
        ++i;
    }
    return i > start;
}

bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\n'; }

// One token: micros[/flags][@marker][*count]. Fills the sample and the count.
bool ParseToken(const std::string& s, FrameSample& sample, std::uint64_t& count, std::string& why) {
    std::size_t i = 0;
    std::uint64_t v = 0;
    sample = FrameSample();
    count = 1;
    if (!ReadUInt(s, i, 10, v)) { why = "a frame time is not a number"; return false; }
    if (v > FrameRecording::kMaxFrameMicros) { why = "a frame time is over one hour"; return false; }
    sample.micros = (std::uint32_t)v;
    if (i < s.size() && s[i] == '/') {
        ++i;
        const std::size_t start = i;
        while (i < s.size() && (s[i] == 'p' || s[i] == 'h' || s[i] == 's' || s[i] == 'c')) {
            const std::uint8_t bit = s[i] == 'p' ? kFramePaused : s[i] == 'h' ? kFrameHitch : s[i] == 's' ? kFrameStepped : kFrameSynthetic;
            if (sample.flags & bit) { why = "a flag is repeated"; return false; }
            sample.flags |= bit;
            ++i;
        }
        if (i == start) { why = "empty or unknown flags"; return false; }
    }
    if (i < s.size() && s[i] == '@') {
        ++i;
        bool neg = false;
        if (i < s.size() && s[i] == '-') { neg = true; ++i; }
        if (!ReadUInt(s, i, 10, v)) { why = "a marker is not a number"; return false; }
        const long long m = neg ? -(long long)v : (long long)v;
        if (m < -2147483648LL || m > 2147483647LL) { why = "a marker is out of range"; return false; }
        sample.marker = (std::int32_t)m;
    }
    if (i < s.size() && s[i] == '*') {
        ++i;
        if (!ReadUInt(s, i, 10, v) || v == 0) { why = "a run length is not a positive number"; return false; }
        if (v > FrameRecording::kMaxFrames) { why = "a run is longer than the frame limit"; return false; }
        count = v;
    }
    if (i != s.size()) { why = "unexpected characters in a frame token"; return false; }
    return true;
}

}  // namespace

std::string FrameRecording::ToText() const {
    std::string out;
    out += "okayframes 1\n";
    out += "frames " + std::to_string(m_frames) + "\n";
    out += "micros " + std::to_string(m_micros) + "\n";
    if (m_hasRandom) out += "random " + savefmt::Hex16(m_random.s0) + " " + savefmt::Hex16(m_random.s1) + "\n";
    int onLine = 0;
    for (const Run& r : m_runs) {
        out += std::to_string(r.micros);
        const std::string fl = FlagLetters(r.flags);
        if (!fl.empty()) { out += '/'; out += fl; }
        if (r.marker != 0) { out += '@'; out += std::to_string(r.marker); }
        if (r.count > 1) { out += '*'; out += std::to_string(r.count); }
        if (++onLine == kTokensPerLine) { out += '\n'; onLine = 0; } else out += ' ';
    }
    if (onLine != 0) { out.back() = '\n'; }
    out += "checksum " + savefmt::Hex16(savefmt::Fnv1a(out.data(), out.size())) + "\n";
    return out;
}

bool FrameRecording::FromText(const std::string& textIn, FrameRecording& out, std::string* error) {
    auto fail = [&](const std::string& m) { if (error) *error = m; return false; };
    if (textIn.size() > kMaxFileBytes) return fail("the file is too large for a frame recording");
    std::string text = textIn;
    if (text.find('\r') != std::string::npos) text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());   // CRLF files

    // Lines are read one at a time; `bodyEnd` is where the checksummed part stops.
    std::size_t pos = 0;
    auto line = [&](std::string& l, std::size_t& lineStart) {
        if (pos >= text.size()) return false;
        lineStart = pos;
        const std::size_t e = text.find('\n', pos);
        if (e == std::string::npos) { l = text.substr(pos); pos = text.size(); }
        else { l = text.substr(pos, e - pos); pos = e + 1; }
        return true;
    };

    std::string l; std::size_t at = 0;
    if (!line(l, at)) return fail("empty file");
    if (l.compare(0, 11, "okayframes ") != 0) return fail("not a frame recording (no 'okayframes' header)");
    {
        std::size_t i = 11; std::uint64_t v = 0;
        if (!ReadUInt(l, i, 6, v) || i != l.size()) return fail("bad version in the header");
        if (v != 1) return fail("unsupported frame recording version " + std::to_string((unsigned long long)v));
    }
    std::uint64_t wantFrames = 0, wantMicros = 0;
    for (int which = 0; which < 2; ++which) {
        const char* key = which == 0 ? "frames " : "micros ";
        const std::size_t klen = 7;
        if (!line(l, at) || l.compare(0, klen, key) != 0) return fail(std::string("missing '") + (which == 0 ? "frames" : "micros") + "' line");
        std::size_t i = klen; std::uint64_t v = 0;
        if (!ReadUInt(l, i, 16, v) || i != l.size()) return fail(std::string("bad number on the '") + (which == 0 ? "frames" : "micros") + "' line");
        (which == 0 ? wantFrames : wantMicros) = v;
    }
    if (wantFrames > kMaxFrames) return fail("too many frames (the limit is " + std::to_string((unsigned long long)kMaxFrames) + ")");

    FrameRecording tmp;
    std::size_t bodyEnd = std::string::npos;
    std::string checksumLine;
    bool sawRandom = false;
    while (line(l, at)) {
        if (l.compare(0, 9, "checksum ") == 0) { bodyEnd = at; checksumLine = l.substr(9); break; }
        // The optional random state: only straight after the totals, once (a frame token starts with a digit).
        if (l.compare(0, 7, "random ") == 0) {
            if (sawRandom || tmp.m_frames != 0) return fail("the 'random' line is out of place");
            std::uint64_t w[2] = {0, 0};
            std::size_t i = 7;
            for (int k = 0; k < 2; ++k) {
                if (k == 1) { if (i >= l.size() || l[i] != ' ') return fail("bad 'random' line"); ++i; }
                if (i + 16 > l.size()) return fail("bad 'random' line");
                for (std::size_t d = 0; d < 16; ++d, ++i) {
                    const char c = l[i];
                    const int v = c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1));
                    if (v < 0) return fail("bad 'random' line");
                    w[k] = (w[k] << 4) | (std::uint64_t)v;
                }
            }
            if (i != l.size()) return fail("bad 'random' line");
            tmp.SetRandom(Random::State{w[0], w[1]});
            sawRandom = true;
            continue;
        }
        // tokens separated by blanks
        std::size_t i = 0;
        while (i < l.size()) {
            while (i < l.size() && IsSpace(l[i])) ++i;
            if (i >= l.size()) break;
            std::size_t j = i;
            while (j < l.size() && !IsSpace(l[j])) ++j;
            if (j - i > 40) return fail("a frame token is too long");
            FrameSample s; std::uint64_t count = 1; std::string why;
            if (!ParseToken(l.substr(i, j - i), s, count, why)) return fail(why);
            if (!tmp.AddRun(s, count)) return fail("too many frames (the limit is " + std::to_string((unsigned long long)kMaxFrames) + ")");
            i = j;
        }
    }
    if (bodyEnd == std::string::npos) return fail("truncated: no checksum line");
    // nothing but blanks after the checksum
    for (std::size_t k = pos; k < text.size(); ++k) if (!IsSpace(text[k])) return fail("unexpected data after the checksum line");
    while (!checksumLine.empty() && IsSpace(checksumLine.back())) checksumLine.pop_back();
    if (checksumLine.size() != 16) return fail("bad checksum line");
    const std::string have = savefmt::Hex16(savefmt::Fnv1a(text.data(), bodyEnd));
    std::string want = checksumLine;
    for (char& c : want) if (c >= 'A' && c <= 'F') c = (char)(c - 'A' + 'a');
    if (have != want) return fail("checksum mismatch: the recording is damaged or was edited");
    if (tmp.m_frames != wantFrames) return fail("the frame count in the header does not match the frames");
    if (tmp.m_micros != wantMicros) return fail("the total time in the header does not match the frames");
    out = std::move(tmp);
    return true;
}

bool FrameRecording::Save(const std::string& path, std::string* error) const {
    if (path.empty()) { if (error) *error = "no path"; return false; }
    if (!savefmt::AtomicWrite(path, ToText())) { if (error) *error = "could not write " + path; return false; }
    return true;
}

bool FrameRecording::Load(const std::string& path, FrameRecording& out, std::string* error) {
    std::string text, why;
    if (!savefmt::ReadFileCapped(path, kMaxFileBytes, text, &why)) { if (error) *error = why; return false; }
    return FromText(text, out, error);
}

// ---- recorder and player ----------------------------------------------------------

bool FrameRecorder::Add(double seconds, std::uint8_t flags, std::int32_t marker) {
    if (!m_recording) return false;
    if (Full()) { m_truncated = true; return false; }
    // The shared stream as the recorded game is about to draw from it (the frame is recorded before its updates run).
    if (m_recordRandom && m_rec.Empty() && !m_rec.HasRandom()) m_rec.SetRandom(Random::Shared().GetState());
    return m_rec.Add(seconds, flags, marker);
}

void FramePlayer::Load(FrameRecording recording) {
    m_rec = std::move(recording);
    Rewind();
    m_loops = 0;
}

bool FramePlayer::Seek(std::uint64_t index) {
    if (index >= m_rec.Frames()) return false;
    // Find the run holding `index`.
    std::size_t lo = 0, hi = m_rec.Runs();
    FrameSample s; std::uint64_t c = 0;
    std::uint64_t first = 0, acc = 0;
    for (std::size_t i = 0; i < hi; ++i) {
        m_rec.RunAt(i, s, c);
        if (index < acc + c) { lo = i; first = acc; break; }
        acc += c;
    }
    m_run = lo; m_inRun = index - first; m_pos = index;
    return true;
}

bool FramePlayer::Next(FrameSample& out) {
    if (m_rec.Frames() == 0) return false;
    if (m_pos >= m_rec.Frames()) {
        if (!m_looping) return false;
        Rewind();
        ++m_loops;
    }
    FrameSample s; std::uint64_t c = 0;
    if (!m_rec.RunAt(m_run, s, c)) return false;
    out = s;
    ++m_pos;
    if (++m_inRun >= c) { ++m_run; m_inRun = 0; }
    return true;
}

}  // namespace okay
