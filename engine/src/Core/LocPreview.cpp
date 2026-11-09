#include "okay/Core/LocPreview.hpp"
#include "okay/Core/LocCheck.hpp"
#include "okay/Core/Utf8.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace okay {

namespace {

const std::size_t npos = std::string::npos;

std::uint32_t Accented(std::uint32_t c) {
    switch (c) {
        case 'a': return 0xE5;  case 'c': return 0xE7;  case 'd': return 0x10F; case 'e': return 0xE9;
        case 'g': return 0x11F; case 'h': return 0x125; case 'i': return 0xED;  case 'j': return 0x135;
        case 'k': return 0x137; case 'l': return 0x13E; case 'n': return 0xF1;  case 'o': return 0xF6;
        case 'r': return 0x155; case 's': return 0x161; case 't': return 0x163; case 'u': return 0xFC;
        case 'w': return 0x175; case 'y': return 0xFD;  case 'z': return 0x17E;
        case 'A': return 0xC5;  case 'C': return 0xC7;  case 'D': return 0x10E; case 'E': return 0xC9;
        case 'G': return 0x11E; case 'H': return 0x124; case 'I': return 0xCD;  case 'J': return 0x134;
        case 'K': return 0x136; case 'L': return 0x13D; case 'N': return 0xD1;  case 'O': return 0xD6;
        case 'R': return 0x154; case 'S': return 0x160; case 'T': return 0x162; case 'U': return 0xDC;
        case 'W': return 0x174; case 'Y': return 0xDD;  case 'Z': return 0x17D;
        default:  return c;
    }
}

// Accented letters of one text run; counts the code points that were letters or anything else.
std::string AccentRun(const std::string& text, bool accents, std::size_t& count) {
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        const std::uint32_t cp = utf8::Next(text, i);
        utf8::Append(out, accents ? Accented(cp) : cp);
        ++count;
    }
    return out;
}

std::string Emit(const std::vector<LocNode>& nodes, bool accents, std::size_t& count) {
    std::string out;
    for (const LocNode& n : nodes) {
        switch (n.kind) {
            case LocNode::Kind::Text: out += AccentRun(n.text, accents, count); break;
            case LocNode::Kind::Arg: out += n.text; break;
            case LocNode::Kind::Plural: {
                out += "{" + n.name + ", plural,";
                for (const auto& f : n.forms) out += " " + f.first + " {" + Emit(f.second, accents, count) + "}";
                out += "}";
                break;
            }
        }
    }
    return out;
}

// Reverse one line. A unit is a letter with its combining marks, or a whole {...} placeholder.
std::string MirrorLine(const std::string& line) {
    std::vector<std::string> units;
    for (std::size_t i = 0; i < line.size();) {
        if (line[i] == '{' && !(i + 1 < line.size() && line[i + 1] == '{')) {
            int depth = 0;
            std::size_t j = i;
            for (; j < line.size(); ++j) {
                if (line[j] == '{') ++depth;
                else if (line[j] == '}' && --depth == 0) break;
            }
            if (j < line.size()) { units.push_back(line.substr(i, j - i + 1)); i = j + 1; continue; }
        }
        const std::size_t start = i;
        utf8::Next(line, i);
        // combining marks stay with the character before them
        while (i < line.size()) {
            std::size_t k = i;
            const std::uint32_t cp = utf8::Next(line, k);
            const bool mark = (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x0591 && cp <= 0x05C7) || (cp >= 0x064B && cp <= 0x065F) ||
                              (cp >= 0xFE00 && cp <= 0xFE0F);
            if (!mark) break;
            i = k;
        }
        std::string u = line.substr(start, i - start);
        if (u.size() == 1) {
            switch (u[0]) {
                case '(': u = ")"; break;
                case ')': u = "("; break;
                case '[': u = "]"; break;
                case ']': u = "["; break;
                case '<': u = ">"; break;
                case '>': u = "<"; break;
                default: break;
            }
        }
        units.push_back(std::move(u));
    }
    std::string out;
    for (auto it = units.rbegin(); it != units.rend(); ++it) out += *it;
    return out;
}

std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

} // namespace

std::string LocMirrorText(const std::string& text) {
    std::string out;
    std::size_t from = 0;
    for (;;) {
        const std::size_t nl = text.find('\n', from);
        out += MirrorLine(text.substr(from, nl == npos ? npos : nl - from));
        if (nl == npos) break;
        out += '\n';
        from = nl + 1;
    }
    return out;
}

bool LocIsRtl(const std::string& lang) {
    const std::size_t dash = lang.find_first_of("-_");
    const std::string base = Lower(dash == npos ? lang : lang.substr(0, dash));
    return base == "ar" || base == "he" || base == "iw" || base == "fa" || base == "ur" || base == "yi" || base == "ps" ||
           base == "sd" || base == "ug" || base == "dv";
}

std::string LocPseudoText(const std::string& message, const LocPseudoOptions& o) {
    if (message.empty()) return message;
    std::vector<LocNode> nodes;
    LocParseMessage(message, nodes);
    std::size_t count = 0;
    std::string body = Emit(nodes, o.accents, count);
    const std::size_t pad = o.expand > 0.0f ? (std::size_t)std::ceil((double)count * (double)o.expand) : 0;
    body += std::string(pad, '~');
    std::string out = o.markers ? "[!!! " + body + " !!!]" : body;
    return o.mirror ? LocMirrorText(out) : out;
}

// ---- LocPreview -------------------------------------------------------------------------
void LocPreview::SetLanguage(const std::string& code) {
    if (code == Localization::kPseudo) {
        m_language.clear();
        m_pseudo = true;
        return;
    }
    m_language = code;
}

std::string LocPreview::Language() const {
    return !m_language.empty() && m_table->FindLanguage(m_language) ? m_language : m_table->DefaultLanguage();
}

void LocPreview::SetPseudo(bool on, const LocPseudoOptions& options) {
    m_pseudo = on;
    m_options = options;
}

bool LocPreview::TryGet(const std::string& key, std::string& out) const {
    const std::string lang = Language();
    if (const std::string* v = m_table->Text(key, lang)) { out = *v; return true; }
    if (const std::string* v = m_table->Text(key, m_table->DefaultLanguage())) { out = *v; return true; }
    return false;
}

std::string LocPreview::Get(const std::string& key) const {
    std::string text;
    if (!TryGet(key, text)) text = key;
    return Render(text);
}

std::string LocPreview::Render(const std::string& message) const {
    std::string out = m_pseudo ? LocPseudoText(message, LocPseudoOptions{m_options.accents, m_options.markers, m_options.expand, false}) : message;
    return Mirrored() ? LocMirrorText(out) : out;
}

std::string LocPreview::LocalizeArgs(const std::string& key, const std::vector<LocArg>& args) const {
    std::string text;
    if (!TryGet(key, text)) text = key;
    if (m_pseudo) text = LocPseudoText(text, LocPseudoOptions{m_options.accents, m_options.markers, m_options.expand, false});
    std::string out = Localization::Format(text, args, Language());
    return Mirrored() ? LocMirrorText(out) : out;
}

StringTable LocPreview::Bake() const {
    StringTable out;
    const std::string lang = Language();
    if (lang.empty()) return out;
    const LocLanguage* l = m_table->FindLanguage(lang);
    LocLanguage& mine = out.AddLanguage(lang, l ? l->name : lang);
    if (l) mine.font = l->font;
    out.defaultLanguage = lang;
    for (const auto& kv : m_table->Entries()) {
        LocEntry& e = out.Add(kv.first);
        e.note = kv.second.note;
        std::string text;
        if (!TryGet(kv.first, text)) continue;
        if (m_pseudo) text = LocPseudoText(text, LocPseudoOptions{m_options.accents, m_options.markers, m_options.expand, false});
        out.Set(kv.first, lang, text);
    }
    return out;
}

} // namespace okay
