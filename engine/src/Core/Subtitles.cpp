#include "okay/Core/Subtitles.hpp"
#include "okay/Audio/AudioClip.hpp"
#include "okay/Core/Localization.hpp"
#include "okay/Core/Prefs.hpp"
#include "okay/Core/Utf8.hpp"
#include "okay/Graphics/ColorVision.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace okay {

namespace {

const std::size_t npos = std::string::npos;

float Clamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
int ClampI(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

std::string Trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}

int CellOf(std::uint32_t cp) {
    if ((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x0591 && cp <= 0x05C7) || (cp >= 0x064B && cp <= 0x065F) ||
        (cp >= 0x200B && cp <= 0x200F) || (cp >= 0xFE00 && cp <= 0xFE0F) || cp == 0xFEFF)
        return 0;
    if ((cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0xA4CF) || (cp >= 0xAC00 && cp <= 0xD7A3) ||
        (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFE30 && cp <= 0xFE6F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
        (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1FAFF) || (cp >= 0x20000 && cp <= 0x3FFFD))
        return 2;
    return 1;
}

// "[door slams]": brackets added unless the text already is wholly in [] or ().
std::string Bracketed(const std::string& text) {
    if (text.size() >= 2 && ((text.front() == '[' && text.back() == ']') || (text.front() == '(' && text.back() == ')'))) return text;
    return "[" + text + "]";
}

// ---- SubRip time -------------------------------------------------------------------------------
bool ParseTime(const std::string& in, float& seconds) {
    const std::string s = Trim(in);
    int h = 0, m = 0, sec = 0, ms = 0;
    char sep = 0;
    // Fields are read as 64-bit and range-checked: %d overflowing is undefined behaviour, and
    // a huge hour count would overflow the arithmetic below.
    constexpr long long kMaxField = 100000;   // ~11 years of hours; no real subtitle gets near it
    long long h64 = 0, m64 = 0, s64 = 0, ms64 = 0;
    if (std::sscanf(s.c_str(), "%lld:%lld:%lld%c%lld", &h64, &m64, &s64, &sep, &ms64) == 5 && (sep == ',' || sep == '.')) {
        if (h64 < 0 || m64 < 0 || s64 < 0 || ms64 < 0 || h64 > kMaxField || m64 > kMaxField || s64 > kMaxField) return false;
        h = (int)h64; m = (int)m64; sec = (int)s64;
        std::string msDigits;
        for (std::size_t i = s.find(sep) + 1; i < s.size() && std::isdigit((unsigned char)s[i]); ++i) msDigits += s[i];
        if (msDigits.size() > 3) msDigits.resize(3);
        while (msDigits.size() < 3) msDigits += '0';
        ms = std::atoi(msDigits.c_str());
        seconds = (float)h * 3600.0f + (float)m * 60.0f + (float)sec + (float)ms / 1000.0f;
        return true;
    }
    if (std::sscanf(s.c_str(), "%lld:%lld:%lld", &h64, &m64, &s64) == 3) {
        if (h64 < 0 || m64 < 0 || s64 < 0 || h64 > kMaxField || m64 > kMaxField || s64 > kMaxField) return false;
        seconds = (float)(h64 * 3600 + m64 * 60 + s64);
        return true;
    }
    return false;
}

std::string FormatTime(float seconds) {
    if (seconds < 0.0f) seconds = 0.0f;
    const long total = (long)(seconds * 1000.0f + 0.5f);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02ld:%02ld:%02ld,%03ld", total / 3600000, (total / 60000) % 60, (total / 1000) % 60, total % 1000);
    return buf;
}

// Drop <i>, <b>, <font ...> tags and {\an8} style overrides.
std::string StripTags(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '<') { const std::size_t e = s.find('>', i); if (e != npos) { i = e; continue; } }
        if (s[i] == '{' && i + 1 < s.size() && s[i + 1] == '\\') { const std::size_t e = s.find('}', i); if (e != npos) { i = e; continue; } }
        out += s[i];
    }
    return out;
}

// "Anna: Hello" -> speaker "Anna", text "Hello". The name is letters, spaces, apostrophes, dots and dashes.
bool SplitSpeaker(const std::string& line, std::string& speaker, std::string& text) {
    const std::size_t colon = line.find(": ");
    if (colon == npos || colon == 0 || colon > 24) return false;
    for (std::size_t i = 0; i < colon; ++i) {
        const unsigned char c = (unsigned char)line[i];
        if (!(std::isalpha(c) || c >= 0x80 || c == ' ' || c == '\'' || c == '.' || c == '-')) return false;
    }
    speaker = Trim(line.substr(0, colon));
    text = Trim(line.substr(colon + 2));
    return !speaker.empty() && !text.empty();
}

} // namespace

// =====================================================================================
// SubtitleSettings
// =====================================================================================
void SubtitleSettings::Clamp() {
    textScale = okay::Clamp(textScale, 0.5f, 3.0f);
    backgroundOpacity = okay::Clamp(backgroundOpacity, 0.0f, 1.0f);
    textOpacity = okay::Clamp(textOpacity, 0.0f, 1.0f);
    maxLines = ClampI(maxLines, 1, 6);
    maxCharsPerLine = ClampI(maxCharsPerLine, 16, 120);
}

void SubtitleSettings::Load() {
    const std::string p = "a11y.subtitles.";
    auto b = [&](const char* k, bool& v) { if (Prefs::Has(p + k)) v = Prefs::GetInt(p + k) != 0; };
    auto f = [&](const char* k, float& v) { if (Prefs::Has(p + k)) v = Prefs::GetFloat(p + k); };
    auto i = [&](const char* k, int& v) { if (Prefs::Has(p + k)) v = Prefs::GetInt(p + k); };
    b("enabled", enabled); b("captions", soundCaptions); b("names", speakerNames); b("colours", speakerColours);
    f("scale", textScale); b("background", background); f("bgOpacity", backgroundOpacity); f("textOpacity", textOpacity);
    i("lines", maxLines); i("chars", maxCharsPerLine); b("top", top);
    Clamp();
}

void SubtitleSettings::Save() const {
    const std::string p = "a11y.subtitles.";
    Prefs::SetInt(p + "enabled", enabled ? 1 : 0);
    Prefs::SetInt(p + "captions", soundCaptions ? 1 : 0);
    Prefs::SetInt(p + "names", speakerNames ? 1 : 0);
    Prefs::SetInt(p + "colours", speakerColours ? 1 : 0);
    Prefs::SetFloat(p + "scale", textScale);
    Prefs::SetInt(p + "background", background ? 1 : 0);
    Prefs::SetFloat(p + "bgOpacity", backgroundOpacity);
    Prefs::SetFloat(p + "textOpacity", textOpacity);
    Prefs::SetInt(p + "lines", maxLines);
    Prefs::SetInt(p + "chars", maxCharsPerLine);
    Prefs::SetInt(p + "top", top ? 1 : 0);
}

bool SubtitleSettings::operator==(const SubtitleSettings& o) const {
    return enabled == o.enabled && soundCaptions == o.soundCaptions && speakerNames == o.speakerNames &&
           speakerColours == o.speakerColours && textScale == o.textScale && background == o.background &&
           backgroundOpacity == o.backgroundOpacity && textOpacity == o.textOpacity && maxLines == o.maxLines &&
           maxCharsPerLine == o.maxCharsPerLine && top == o.top;
}

// =====================================================================================
// SubtitleTrack
// =====================================================================================
SubtitleCue& SubtitleTrack::Add(float start, const std::string& speaker, const std::string& text, float duration) {
    SubtitleCue cue;
    cue.start = start;
    cue.line.speaker = speaker;
    cue.line.text = text;
    cue.line.duration = duration;
    auto it = std::upper_bound(cues.begin(), cues.end(), start, [](float s, const SubtitleCue& c) { return s < c.start; });
    return *cues.insert(it, std::move(cue));
}

SubtitleCue& SubtitleTrack::AddSound(float start, const std::string& description, float duration) {
    SubtitleCue& c = Add(start, "", Bracketed(description), duration);
    c.line.kind = SubtitleKind::Sound;
    c.line.priority = -1;
    return c;
}

float SubtitleTrack::Length() const {
    float end = 0.0f;
    for (const SubtitleCue& c : cues) {
        const float d = c.line.duration > 0.0f ? c.line.duration : CaptionManager::DurationFor(c.line.text, c.line.audioSeconds);
        end = std::max(end, c.start + d);
    }
    return end;
}

bool SubtitleTrack::ParseSrt(const std::string& textIn, SubtitleTrack& out, std::string* err) {
    out.cues.clear();
    std::string text = textIn.compare(0, 3, "\xEF\xBB\xBF") == 0 ? textIn.substr(3) : textIn;
    std::string norm;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') { if (i + 1 < text.size() && text[i + 1] == '\n') continue; norm += '\n'; }
        else norm += text[i];
    }
    std::vector<std::string> lines;
    {
        std::size_t from = 0;
        for (;;) {
            const std::size_t nl = norm.find('\n', from);
            lines.push_back(norm.substr(from, nl == npos ? npos : nl - from));
            if (nl == npos) break;
            from = nl + 1;
        }
    }
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const std::size_t arrow = lines[i].find("-->");
        if (arrow == npos) continue;
        float a = 0, b = 0;
        {   // the end time may be followed by position settings: take the first token after the arrow
            std::istringstream ss(lines[i].substr(arrow + 3));
            std::string tok;
            ss >> tok;
            if (!ParseTime(lines[i].substr(0, arrow), a) || !ParseTime(tok, b)) continue;
        }
        std::string body;
        std::size_t j = i + 1;
        for (; j < lines.size() && !Trim(lines[j]).empty(); ++j) {
            if (!body.empty()) body += '\n';
            body += StripTags(lines[j]);
        }
        i = j;
        body = Trim(body);
        if (body.empty()) continue;
        std::string speaker, spoken;
        const std::size_t nl = body.find('\n');
        const std::string first = body.substr(0, nl);
        SubtitleKind kind = SubtitleKind::Speech;
        if (body.front() == '[' && body.back() == ']' && body.find('\n') == npos) kind = SubtitleKind::Sound;
        else if (SplitSpeaker(first, speaker, spoken)) body = spoken + (nl == npos ? std::string() : body.substr(nl));
        SubtitleCue& c = out.Add(a, speaker, body, b > a ? b - a : 0.0f);
        c.line.kind = kind;
        if (kind == SubtitleKind::Sound) c.line.priority = -1;
    }
    if (out.cues.empty()) { if (err) *err = "no subtitle cues found"; return false; }
    return true;
}

std::string SubtitleTrack::ToSrt() const {
    std::ostringstream o;
    int n = 0;
    for (const SubtitleCue& c : cues) {
        const float d = c.line.duration > 0.0f ? c.line.duration : CaptionManager::DurationFor(c.line.text, c.line.audioSeconds, 0.0f);
        o << ++n << "\n" << FormatTime(c.start) << " --> " << FormatTime(c.start + d) << "\n";
        if (!c.line.speaker.empty()) o << c.line.speaker << ": ";
        o << c.line.text << "\n\n";
    }
    return o.str();
}

// =====================================================================================
// CaptionManager
// =====================================================================================
CaptionManager& CaptionManager::Main() {
    static CaptionManager m;
    return m;
}

int CaptionManager::Cells(const std::string& text) {
    int w = 0;
    for (std::size_t i = 0; i < text.size();) w += CellOf(utf8::Next(text, i));
    return w;
}

float CaptionManager::DurationFor(const std::string& text, float audioSeconds, float linger) {
    if (audioSeconds > 0.0f) return audioSeconds + std::max(0.0f, linger);
    return Clamp((float)Cells(text) / 15.0f, 1.5f, 8.0f);
}

namespace {

// The longest start of `word` that fits `room` cells (at least one character unless even that
// doesn't fit), and what is left.
std::string TakeCells(const std::string& word, int room, std::string& rest) {
    std::size_t i = 0;
    int used = 0;
    while (i < word.size()) {
        std::size_t k = i;
        const int cw = CellOf(utf8::Next(word, k));
        if (used + cw > room) break;
        used += cw;
        i = k;
    }
    rest = word.substr(i);
    return word.substr(0, i);
}

} // namespace

std::vector<std::string> CaptionManager::Wrap(const std::string& text, int maxCells, int firstRowIndent) {
    if (maxCells < 4) maxCells = 4;
    std::vector<std::string> rows;
    std::size_t from = 0;
    bool firstParagraph = true;
    for (;;) {
        const std::size_t nl = text.find('\n', from);
        const std::string para = text.substr(from, nl == npos ? npos : nl - from);
        std::string cur;
        int curCells = firstParagraph ? firstRowIndent : 0;
        firstParagraph = false;
        std::size_t pos = 0;
        while (pos < para.size()) {
            std::size_t sp = para.find(' ', pos);
            if (sp == npos) sp = para.size();
            std::string word = para.substr(pos, sp - pos);
            pos = sp + 1;
            if (word.empty()) continue;
            for (;;) {
                const int wc = Cells(word);
                if (!cur.empty()) {
                    if (curCells + 1 + wc <= maxCells) { cur += ' '; cur += word; curCells += 1 + wc; break; }
                    rows.push_back(cur);   // no room: this word starts the next row
                    cur.clear();
                    curCells = 0;
                    continue;
                }
                if (curCells + wc <= maxCells) { cur = word; curCells += wc; break; }
                // An empty row (maybe holding a label) the word doesn't fit: break the word.
                std::string rest;
                const std::string piece = TakeCells(word, maxCells - curCells, rest);
                rows.push_back(piece);   // an empty piece when only the label fits: the label stands alone
                curCells = 0;
                if (rest.empty()) break;
                word = rest;
            }
        }
        rows.push_back(cur);
        if (nl == npos) break;
        from = nl + 1;
    }
    return rows;
}

// ---- Showing ---------------------------------------------------------------------------------------
CaptionManager::Entry* CaptionManager::FindVisible(int id) {
    for (Entry& e : m_visible) if (e.id == id) return &e;
    return nullptr;
}

void CaptionManager::Show(Entry e) {
    e.elapsed = 0.0f;
    if (!e.line.speaker.empty() && settings.speakerColours && !e.line.hasColour) SpeakerColour(e.line.speaker);
    m_visible.push_back(std::move(e));
}

int CaptionManager::Drop(Entry& e) {
    const int id = e.id;
    if (onDropped) onDropped(id);
    return id;
}

int CaptionManager::Say(const SubtitleLine& line) {
    const bool described = line.kind != SubtitleKind::Speech;
    if (described ? !settings.soundCaptions : !settings.enabled) return 0;
    std::string text = line.text;
    if (!line.textKey.empty()) {
        std::string t;
        if (Localization::TryGet(line.textKey, t)) text = t;
    }
    text = Trim(text);
    if (text.empty()) return 0;
    if (described) text = Bracketed(text);

    if (described) {   // the same caption again: restart it instead of stacking copies
        for (Entry& e : m_visible) if (e.line.kind == line.kind && e.text == text) { e.elapsed = 0.0f; return e.id; }
        for (Entry& e : m_queue) if (e.line.kind == line.kind && e.text == text) { e.elapsed = 0.0f; return e.id; }
    }

    Entry e;
    e.id = m_nextId++;
    e.line = line;
    e.text = text;
    e.order = m_order++;
    e.duration = line.duration > 0.0f ? line.duration : DurationFor(text, line.audioSeconds, linger);

    const int room = std::max(1, settings.maxLines);
    if ((int)m_visible.size() < room) { const int id = e.id; Show(std::move(e)); return id; }

    // No room: a higher priority line takes the place of the lowest one on screen (the oldest of equals).
    std::size_t lowest = 0;
    for (std::size_t i = 1; i < m_visible.size(); ++i)
        if (m_visible[i].line.priority < m_visible[lowest].line.priority) lowest = i;
    if (e.line.priority > m_visible[lowest].line.priority) {
        Drop(m_visible[lowest]);
        m_visible.erase(m_visible.begin() + (std::ptrdiff_t)lowest);
        const int id = e.id;
        Show(std::move(e));
        return id;
    }

    const int id = e.id;
    auto at = std::find_if(m_queue.begin(), m_queue.end(), [&](const Entry& q) { return q.line.priority < e.line.priority; });
    m_queue.insert(at, std::move(e));
    if ((int)m_queue.size() > std::max(1, maxQueue)) {   // past the limit: the last (lowest priority, latest) goes
        Entry gone = std::move(m_queue.back());
        m_queue.pop_back();
        Drop(gone);
        if (gone.id == id) return 0;
    }
    return id;
}

int CaptionManager::Say(const std::string& speaker, const std::string& text, float audioSeconds, int priority) {
    SubtitleLine l;
    l.speaker = speaker;
    l.text = text;
    l.audioSeconds = audioSeconds;
    l.priority = priority;
    return Say(l);
}

int CaptionManager::Say(const std::string& speaker, const std::string& text, const AudioClip& clip, int priority) {
    return Say(speaker, text, clip.Duration(), priority);
}

int CaptionManager::Sound(const std::string& description, float duration, int priority) {
    SubtitleLine l;
    l.text = description;
    l.kind = SubtitleKind::Sound;
    l.duration = duration;
    l.priority = priority;
    return Say(l);
}

int CaptionManager::Music(const std::string& description, float duration, int priority) {
    SubtitleLine l;
    l.text = description;
    l.kind = SubtitleKind::Music;
    l.duration = duration;
    l.priority = priority;
    return Say(l);
}

// ---- Tracks -------------------------------------------------------------------------------------------------
int CaptionManager::Play(const SubtitleTrack& track, float startAt) {
    if (track.cues.empty()) return 0;
    TrackRun p;
    p.handle = m_nextHandle++;
    p.track = track;
    p.time = startAt;
    const int handle = p.handle;
    m_playing.push_back(std::move(p));
    Update(0.0f);   // cues at or before the start show now
    return handle;
}

void CaptionManager::Stop(int handle) {
    m_playing.erase(std::remove_if(m_playing.begin(), m_playing.end(), [&](const TrackRun& p) { return p.handle == handle; }), m_playing.end());
}

bool CaptionManager::Playing(int handle) const {
    for (const auto& p : m_playing) if (p.handle == handle) return true;
    return false;
}

// ---- Control ---------------------------------------------------------------------------------------------------
bool CaptionManager::Cancel(int id) {
    for (auto it = m_visible.begin(); it != m_visible.end(); ++it)
        if (it->id == id) { m_visible.erase(it); Fill(); return true; }
    for (auto it = m_queue.begin(); it != m_queue.end(); ++it)
        if (it->id == id) { m_queue.erase(it); return true; }
    return false;
}

int CaptionManager::CancelTag(const std::string& tag) {
    if (tag.empty()) return 0;
    int n = 0;
    auto matches = [&](const Entry& e) { return e.line.tag == tag; };
    for (const Entry& e : m_visible) if (matches(e)) ++n;
    for (const Entry& e : m_queue) if (matches(e)) ++n;
    m_visible.erase(std::remove_if(m_visible.begin(), m_visible.end(), matches), m_visible.end());
    m_queue.erase(std::remove_if(m_queue.begin(), m_queue.end(), matches), m_queue.end());
    Fill();
    return n;
}

void CaptionManager::Clear() {
    m_visible.clear();
    m_queue.clear();
    m_playing.clear();
}

void CaptionManager::Fill() {
    const std::size_t room = (std::size_t)std::max(1, settings.maxLines);
    while (m_visible.size() < room && !m_queue.empty()) {
        Entry e = std::move(m_queue.front());
        m_queue.erase(m_queue.begin());
        Show(std::move(e));
    }
}

void CaptionManager::Update(float dt) {
    if (dt < 0.0f) dt = 0.0f;
    // Tracks hand over their cues as their clock passes them.
    for (std::size_t pi = 0; pi < m_playing.size(); ++pi) {
        m_playing[pi].time += dt;
        for (;;) {
            TrackRun& p = m_playing[pi];
            if (p.next >= p.track.cues.size() || p.track.cues[p.next].start > p.time) break;
            SubtitleLine line = p.track.cues[p.next].line;
            const float start = p.track.cues[p.next].start;
            ++p.next;
            if (line.duration > 0.0f) {   // late by up to a frame: end when the cue ends
                line.duration -= p.time - start;
                if (line.duration <= 0.0f) continue;
            }
            Say(line);
        }
    }
    m_playing.erase(std::remove_if(m_playing.begin(), m_playing.end(), [](const TrackRun& p) { return p.next >= p.track.cues.size(); }), m_playing.end());

    // Lines age and finish.
    for (Entry& e : m_visible) e.elapsed += dt;
    std::vector<int> finished;
    m_visible.erase(std::remove_if(m_visible.begin(), m_visible.end(), [&](const Entry& e) {
        if (e.elapsed < e.duration + std::max(0.0f, fadeOut)) return false;
        finished.push_back(e.id);
        return true;
    }), m_visible.end());
    for (int id : finished) if (onFinished) onFinished(id);

    Fill();

    // Lines that waited too long for room are stale.
    for (Entry& q : m_queue) q.elapsed += dt;
    std::vector<int> stale;
    m_queue.erase(std::remove_if(m_queue.begin(), m_queue.end(), [&](const Entry& q) {
        if (q.elapsed <= q.line.maxDelay) return false;
        stale.push_back(q.id);
        return true;
    }), m_queue.end());
    for (int id : stale) if (onDropped) onDropped(id);
}

// ---- What to draw -----------------------------------------------------------------------------------------------
bool CaptionManager::IsShowing(int id) const {
    for (const Entry& e : m_visible) if (e.id == id) return true;
    return false;
}
bool CaptionManager::IsQueued(int id) const {
    for (const Entry& e : m_queue) if (e.id == id) return true;
    return false;
}

void CaptionManager::SetSpeakerColour(const std::string& speaker, const Color& colour) { m_speakerColours[speaker] = colour; }

Color CaptionManager::SpeakerColour(const std::string& speaker) {
    auto it = m_speakerColours.find(speaker);
    if (it != m_speakerColours.end()) return it->second;
    const Color c = SafePaletteColor((int)m_nextColour++);
    m_speakerColours[speaker] = c;
    return c;
}

void CaptionManager::ClearSpeakerColours() {
    m_speakerColours.clear();
    m_nextColour = 0;
}

std::vector<SubtitleDrawLine> CaptionManager::Lines() const {
    std::vector<SubtitleDrawLine> out;
    for (const Entry& e : m_visible) {
        const bool described = e.line.kind != SubtitleKind::Speech;
        if (described ? !settings.soundCaptions : !settings.enabled) continue;
        float alpha = 1.0f;
        if (fadeIn > 0.0f) alpha = std::min(alpha, e.elapsed / fadeIn);
        if (e.elapsed > e.duration) alpha = std::min(alpha, fadeOut > 0.0f ? 1.0f - (e.elapsed - e.duration) / fadeOut : 0.0f);
        alpha = Clamp(alpha, 0.0f, 1.0f);

        SubtitleDrawLine d;
        d.id = e.id;
        d.kind = e.line.kind;
        d.speaker = e.line.speaker;
        d.speakerLabel = settings.speakerNames && !e.line.speaker.empty() ? e.line.speaker + ": " : std::string();
        d.rows = Wrap(e.text, settings.maxCharsPerLine, Cells(d.speakerLabel));
        Color who = Color::White;
        if (!e.line.speaker.empty() || e.line.hasColour) {
            if (e.line.hasColour) who = e.line.colour;
            else if (settings.speakerColours) {
                auto it = m_speakerColours.find(e.line.speaker);
                if (it != m_speakerColours.end()) who = it->second;
            }
        }
        d.speakerColour = Color(who.r, who.g, who.b, settings.textOpacity * alpha);
        d.textColour = Color(1.0f, 1.0f, 1.0f, settings.textOpacity * alpha);
        d.backgroundColour = Color(0.0f, 0.0f, 0.0f, settings.background ? settings.backgroundOpacity * alpha : 0.0f);
        d.alpha = alpha;
        d.scale = settings.textScale;
        d.italic = described;
        d.priority = e.line.priority;
        out.push_back(std::move(d));
    }
    return out;
}

} // namespace okay
