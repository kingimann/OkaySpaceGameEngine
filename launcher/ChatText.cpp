// Emoji, chat text formatting and link previews (ChatText.hpp). Moved out of launcher/main.cpp unchanged
// (docs/engine/modularization_plan.md, Part C section 5, batch L1).
#include "LauncherShared.hpp"
#include "ChatText.hpp"
#include "NetGuard.hpp"                 // okay::netguard: size and host checks for link previews
#include "okay/Core/ImageDecode.hpp"   // LoadRgbaFile
#include "okay/Platform/NativeFetch.hpp"   // own HTTP client in front of curl for link previews (only with OKAY_OWN_HTTP)

namespace okl {

int EmojiId(const std::string& t) {
    for (const auto& e : kEmojis) if (t == e.code || (e.alt && t == e.alt)) return e.id;
    if (t == ":emoji?:") return kEmojiUnknown;
    if (t.size() > 4 && t.back() == '>') {
        if (t.compare(0, 3, "<e:") == 0) return kEmojiUnicode;
        if (t.compare(0, 2, "<:") == 0 && t.find(':', 2) != std::string::npos) return kEmojiCustom;
    }
    return -1;
}
// Draws any emoji token (shortcode, Unicode cluster, custom image) centred at c;
// defined with the chat media code further down.

// Real Unicode emoji (typed on a phone, pasted, or from the picker) have no
// glyphs in the UI font, so each emoji *cluster* (incl. skin tones, ZWJ
// families, flags and keycaps) becomes a token "<e:1F44D-1F3FB>" that the chat
// renders from the cached Twemoji sheet (DrawEmojiToken).
static bool EmojiIsPictograph(unsigned cp) {
    return (cp >= 0x1F000 && cp <= 0x1FAFF) || (cp >= 0x2600 && cp <= 0x27BF) || cp == 0x2B50 || cp == 0x2B55 ||
           (cp >= 0x2B05 && cp <= 0x2B07) || cp == 0x2B1B || cp == 0x2B1C || cp == 0x231A || cp == 0x231B ||
           cp == 0x2328 || cp == 0x23CF || (cp >= 0x23E9 && cp <= 0x23F3) || (cp >= 0x23F8 && cp <= 0x23FA) ||
           cp == 0x24C2 || cp == 0x25B6 || cp == 0x25C0 || (cp >= 0x25FB && cp <= 0x25FE) || cp == 0x2934 || cp == 0x2935 ||
           cp == 0x3030 || cp == 0x303D || cp == 0x3297 || cp == 0x3299;
}
static unsigned Utf8Decode(const std::string& s, std::size_t i, int& len) {
    unsigned char c0 = (unsigned char)s[i];
    len = c0 < 0x80 ? 1 : (c0 >> 5) == 0x6 ? 2 : (c0 >> 4) == 0xE ? 3 : (c0 >> 3) == 0x1E ? 4 : 1;
    if (i + len > s.size()) { len = 1; return c0; }
    if (len == 2) return ((c0 & 0x1Fu) << 6) | (s[i + 1] & 0x3F);
    if (len == 3) return ((c0 & 0x0Fu) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F);
    if (len == 4) return ((c0 & 0x07u) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F);
    return c0;
}
std::string Utf8Encode(unsigned cp) {
    std::string o;
    if (cp < 0x80) o += (char)cp;
    else if (cp < 0x800) { o += (char)(0xC0 | (cp >> 6)); o += (char)(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) { o += (char)(0xE0 | (cp >> 12)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    else { o += (char)(0xF0 | (cp >> 18)); o += (char)(0x80 | ((cp >> 12) & 0x3F)); o += (char)(0x80 | ((cp >> 6) & 0x3F)); o += (char)(0x80 | (cp & 0x3F)); }
    return o;
}
std::string NormalizeEmoji(const std::string& in) {
    bool any = false;
    for (unsigned char ch : in) if (ch >= 0xC2) { any = true; break; }
    if (!any) return in;
    std::string out;
    out.reserve(in.size() + 16);
    std::size_t i = 0, n = in.size();
    auto hex = [](unsigned cp) { char b[12]; std::snprintf(b, sizeof(b), "%X", cp); return std::string(b); };
    while (i < n) {
        int len = 1;
        unsigned cp = Utf8Decode(in, i, len);
        int nlen = 0;
        unsigned next = (i + len < n) ? Utf8Decode(in, i + len, nlen) : 0;
        const bool ri = cp >= 0x1F1E6 && cp <= 0x1F1FF;
        const bool keycap = (cp == '#' || cp == '*' || (cp >= '0' && cp <= '9')) && (next == 0xFE0F || next == 0x20E3);
        const bool textPresent = (cp == 0xA9 || cp == 0xAE || cp == 0x203C || cp == 0x2049 || cp == 0x2122 || cp == 0x2139 ||
                                  (cp >= 0x2194 && cp <= 0x21AA)) && next == 0xFE0F;
        if (cp == 0xFE0F || cp == 0xFE0E || cp == 0x200D) { i += len; continue; }   // stray joiners/selectors
        if (!(EmojiIsPictograph(cp) || ri || keycap || textPresent)) { out.append(in, i, (std::size_t)len); i += len; continue; }
        std::vector<unsigned> cl{cp};
        std::size_t j = i + len;
        if (ri && next >= 0x1F1E6 && next <= 0x1F1FF) { cl.push_back(next); j += nlen; }   // flag = two letters
        while (j < n) {
            int l2 = 0; unsigned c2 = Utf8Decode(in, j, l2);
            if (c2 == 0xFE0F || c2 == 0x20E3 || (c2 >= 0x1F3FB && c2 <= 0x1F3FF) || (c2 >= 0xE0020 && c2 <= 0xE007F)) {
                cl.push_back(c2); j += l2; continue;
            }
            if (c2 == 0x200D && j + l2 < n) {   // ZWJ sequence: keep going if a pictograph follows
                int l3 = 0; unsigned c3 = Utf8Decode(in, j + l2, l3);
                if (EmojiIsPictograph(c3)) { cl.push_back(c2); cl.push_back(c3); j += l2 + l3; continue; }
            }
            break;
        }
        std::string tok = "<e:";
        for (std::size_t k = 0; k < cl.size(); ++k) { if (k) tok += '-'; tok += hex(cl[k]); }
        tok += '>';
        if (!out.empty() && out.back() != ' ' && out.back() != '\n') out += ' ';
        out += tok;
        out += ' ';
        i = j;
    }
    return out;
}

int ReactionEmojiId(const std::string& key) {
    for (const auto& r : kReactions) if (key == r.key) return r.id;
    return 3;   // default to the heart glyph for unknown keys
}

// True when `body` is nothing but 1-3 emoji shortcodes (Telegram renders those
// big and background-less). Reports how many via `count`.
bool IsEmojiOnly(const std::string& bodyIn, int& count) {
    const std::string body = NormalizeEmoji(bodyIn);
    count = 0;
    std::size_t i = 0, n = body.size();
    while (i < n) {
        while (i < n && (body[i] == ' ' || body[i] == '\n')) ++i;
        if (i >= n) break;
        std::size_t j = i; while (j < n && body[j] != ' ' && body[j] != '\n') ++j;
        if (EmojiId(body.substr(i, j - i)) < 0) return false;
        ++count; i = j;
    }
    return count > 0 && count <= 3;
}
// Draw emoji `id` centered at `c`, fitting a box of side `s`.
void DrawEmoji(ImDrawList* dl, ImVec2 c, float s, int id) {
    float r = s * 0.46f;
    ImU32 face = IM_COL32(245, 200, 70, 255), dark = IM_COL32(60, 48, 20, 255), white = IM_COL32(250,250,250,255);
    auto eyes = [&](float ey, float er){ dl->AddCircleFilled(ImVec2(c.x-r*0.34f,c.y+ey), er, dark, 10);
                                         dl->AddCircleFilled(ImVec2(c.x+r*0.34f,c.y+ey), er, dark, 10); };
    auto mouthArc = [&](ImVec2 ctr, float rad, float a0, float a1){
        dl->PathClear(); dl->PathArcTo(ctr, rad, a0, a1, 16); dl->PathStroke(dark, 0, 2.2f); };
    const float PI = 3.14159265f;
    if (id == kEmojiUnknown) {   // neutral tile: "an emoji was here"
        dl->AddRectFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), IM_COL32(255, 255, 255, 30), r * 0.45f);
        dl->AddCircleFilled(ImVec2(c.x - r * 0.32f, c.y - r * 0.12f), r * 0.11f, IM_COL32(255, 255, 255, 120), 8);
        dl->AddCircleFilled(ImVec2(c.x + r * 0.32f, c.y - r * 0.12f), r * 0.11f, IM_COL32(255, 255, 255, 120), 8);
        dl->AddLine(ImVec2(c.x - r * 0.3f, c.y + r * 0.35f), ImVec2(c.x + r * 0.3f, c.y + r * 0.35f), IM_COL32(255, 255, 255, 120), 1.5f);
        return;
    }
    switch (id) {
        case 0: // smile
            dl->AddCircleFilled(c, r, face, 24); eyes(-r*0.18f, r*0.10f);
            mouthArc(ImVec2(c.x, c.y - r*0.05f), r*0.42f, 0.18f*PI, 0.82f*PI); break;
        case 1: // grin (open smile w/ teeth)
            dl->AddCircleFilled(c, r, face, 24); eyes(-r*0.2f, r*0.10f);
            dl->PathClear(); dl->PathArcTo(ImVec2(c.x,c.y+r*0.02f), r*0.5f, 0.1f*PI, 0.9f*PI, 20); dl->PathFillConvex(dark);
            dl->AddRectFilled(ImVec2(c.x-r*0.4f,c.y+r*0.08f), ImVec2(c.x+r*0.4f,c.y+r*0.2f), white); break;
        case 2: // sad
            dl->AddCircleFilled(c, r, face, 24); eyes(-r*0.18f, r*0.10f);
            mouthArc(ImVec2(c.x, c.y + r*0.55f), r*0.42f, 1.18f*PI, 1.82f*PI); break;
        case 3: { // heart
            ImU32 red = IM_COL32(232, 72, 92, 255);
            dl->AddCircleFilled(ImVec2(c.x-r*0.28f, c.y-r*0.12f), r*0.34f, red, 16);
            dl->AddCircleFilled(ImVec2(c.x+r*0.28f, c.y-r*0.12f), r*0.34f, red, 16);
            dl->AddTriangleFilled(ImVec2(c.x-r*0.58f,c.y+r*0.02f), ImVec2(c.x+r*0.58f,c.y+r*0.02f), ImVec2(c.x,c.y+r*0.62f), red); break; }
        case 4: { // fire
            dl->AddTriangleFilled(ImVec2(c.x,c.y-r*0.7f), ImVec2(c.x-r*0.5f,c.y+r*0.5f), ImVec2(c.x+r*0.5f,c.y+r*0.5f), IM_COL32(236,120,40,255));
            dl->AddTriangleFilled(ImVec2(c.x,c.y-r*0.1f), ImVec2(c.x-r*0.26f,c.y+r*0.5f), ImVec2(c.x+r*0.26f,c.y+r*0.5f), IM_COL32(250,210,70,255)); break; }
        case 5: { // star
            ImU32 gold = IM_COL32(245, 205, 80, 255); ImVec2 p[10];
            for (int k=0;k<10;++k){ float a=-PI/2+k*PI/5; float rr=(k%2==0)?r:r*0.42f; p[k]=ImVec2(c.x+std::cos(a)*rr,c.y+std::sin(a)*rr);}
            for (int k=0;k<10;++k) dl->AddTriangleFilled(c,p[k],p[(k+1)%10],gold); break; }
        case 6: // laugh (grin + closed happy eyes)
            dl->AddCircleFilled(c, r, face, 24);
            mouthArc(ImVec2(c.x-r*0.34f, c.y-r*0.05f), r*0.18f, 1.1f*PI, 1.9f*PI);
            mouthArc(ImVec2(c.x+r*0.34f, c.y-r*0.05f), r*0.18f, 1.1f*PI, 1.9f*PI);
            dl->PathClear(); dl->PathArcTo(ImVec2(c.x,c.y+r*0.05f), r*0.5f, 0.08f*PI, 0.92f*PI, 20); dl->PathFillConvex(dark); break;
        case 7: // wow (o mouth)
            dl->AddCircleFilled(c, r, face, 24); eyes(-r*0.2f, r*0.12f);
            dl->AddCircleFilled(ImVec2(c.x, c.y+r*0.32f), r*0.2f, dark, 14); break;
        case 8: // cool (sunglasses)
            dl->AddCircleFilled(c, r, face, 24);
            dl->AddRectFilled(ImVec2(c.x-r*0.6f,c.y-r*0.28f), ImVec2(c.x-r*0.08f,c.y+r*0.02f), dark, 2);
            dl->AddRectFilled(ImVec2(c.x+r*0.08f,c.y-r*0.28f), ImVec2(c.x+r*0.6f,c.y+r*0.02f), dark, 2);
            dl->AddLine(ImVec2(c.x-r*0.08f,c.y-r*0.16f), ImVec2(c.x+r*0.08f,c.y-r*0.16f), dark, 2.0f);
            mouthArc(ImVec2(c.x, c.y + r*0.02f), r*0.36f, 0.18f*PI, 0.82f*PI); break;
        case 9: { // thumbs up
            ImU32 sk = IM_COL32(240, 196, 120, 255);
            dl->AddRectFilled(ImVec2(c.x-r*0.1f,c.y-r*0.6f), ImVec2(c.x+r*0.32f,c.y+r*0.05f), sk, r*0.12f); // thumb
            dl->AddRectFilled(ImVec2(c.x-r*0.5f,c.y-r*0.05f), ImVec2(c.x+r*0.5f,c.y+r*0.6f), sk, r*0.14f);  // fist
            break; }
        default: break;
    }
}

// A heart centered at c with lobe-radius ~r, in an arbitrary color (used for
// the like toggle, where the color conveys liked/not-liked state).
void DrawHeart(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    dl->AddCircleFilled(ImVec2(c.x - r*0.45f, c.y - r*0.2f), r*0.55f, col, 14);
    dl->AddCircleFilled(ImVec2(c.x + r*0.45f, c.y - r*0.2f), r*0.55f, col, 14);
    dl->AddTriangleFilled(ImVec2(c.x - r*0.95f, c.y + r*0.05f), ImVec2(c.x + r*0.95f, c.y + r*0.05f), ImVec2(c.x, c.y + r), col);
}

// A small emoji button that opens a picker grid; clicking an emoji appends its
// shortcode to `buf` (space-separated so RenderChatBody tokenizes it).
// Emoji button + picker for any text box (inserts real Unicode emoji).
void EmojiPicker(const char* id, char* buf, std::size_t cap) {
    ChatMediaPicker(id, buf, cap, nullptr, false);
}

// ---- Chat text formatting (Discord / Messenger style) ---------------------------
// Blocks: ``` fenced code and "> " quotes. Inline: **bold**, *italic* / _italic_,
// ~~strike~~, `code`, ||spoiler||, plus links, @mentions and emoji tokens. One
// tokenizer feeds both drawing and width measurement, so a chat bubble always
// hugs exactly what gets drawn.
ImFont* g_fontMono = nullptr;   // JetBrains Mono for `code` (loaded in main)
// Merge the system's emoji font into the font just added, so emoji typed into
// text boxes draw as emoji rather than "?" (message bodies use Twemoji images).
// Dear ImGui's own font rasterizer draws their outlines in one colour. Missing font = no-op.
void MergeEmojiFallback(float size) {
    static const char* kPaths[] = {
#if defined(_WIN32)
        "C:\\Windows\\Fonts\\seguiemj.ttf",
#elif defined(__APPLE__)
        "/System/Library/Fonts/Apple Symbols.ttf",
#else
        "/usr/share/fonts/truetype/noto/NotoEmoji-Regular.ttf",
        "/usr/share/fonts/truetype/ancient-scripts/Symbola_hint.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
#endif
    };
    for (const char* path : kPaths) {
        std::error_code ec;
        if (!fs::exists(path, ec)) continue;
        ImFontConfig fc; fc.MergeMode = true; fc.OversampleH = 1; fc.OversampleV = 1;
        if (ImGui::GetIO().Fonts->AddFontFromFileTTF(path, size, &fc)) return;
    }
}
enum { kMdBold = 1, kMdItalic = 2, kMdStrike = 4, kMdCode = 8, kMdSpoiler = 16 };
struct ChatTok { std::string text; int style = 0; bool glue = false; bool newline = false; };
struct ChatBlock { int kind = 0; std::string text; std::vector<ChatTok> toks; };   // kind: 0 text, 1 quote, 2 code

static std::vector<ChatTok> ChatInlineTokens(const std::string& s) {
    const std::size_t n = s.size();
    std::vector<int> st(n, 0);
    std::vector<char> hide(n, 0);
    auto ws = [&](std::size_t k) { return k >= n || s[k] == ' ' || s[k] == '\n'; };
    // Pair an opening marker with a closing one (opener followed by non-space,
    // closer preceded by non-space); wordEdge = "_" style, only at word edges.
    auto pairUp = [&](const char* mk, int flag, bool wordEdge, bool sameLine) {
        const std::size_t L = std::strlen(mk);
        std::size_t i = 0;
        while (i + L <= n) {
            if (hide[i] || (st[i] & kMdCode) || s.compare(i, L, mk) != 0 || ws(i + L) ||
                (wordEdge && i > 0 && !ws(i - 1) && std::strchr("([{\"'", s[i - 1]) == nullptr)) { ++i; continue; }
            std::size_t found = std::string::npos;
            for (std::size_t j = i + L + 1; j + L <= n; ++j) {
                if (sameLine && s[j] == '\n') break;
                if (hide[j] || ((st[j] & kMdCode) && flag != kMdCode)) continue;
                if (s.compare(j, L, mk) == 0 && !ws(j - 1) &&
                    (!wordEdge || ws(j + L) || std::strchr(".,!?:;)]}\"'", s[j + L]) != nullptr)) { found = j; break; }
            }
            if (found == std::string::npos) { i += L; continue; }
            for (std::size_t k = 0; k < L; ++k) { hide[i + k] = 1; hide[found + k] = 1; }
            for (std::size_t k = i + L; k < found; ++k) st[k] |= flag;
            i = found + L;
        }
    };
    pairUp("`", kMdCode, false, true);
    pairUp("||", kMdSpoiler, false, false);
    pairUp("**", kMdBold, false, false);
    pairUp("~~", kMdStrike, false, false);
    pairUp("*", kMdItalic, false, false);
    pairUp("_", kMdItalic, true, false);
    // Words; a style change mid-word splits it into "glued" pieces (no space).
    std::vector<ChatTok> out;
    std::string cur; int curSt = 0; bool space = false, lineStart = true;
    auto flush = [&]() {
        if (cur.empty()) return;
        ChatTok t; t.text = cur; t.style = curSt; t.glue = !space && !lineStart;
        out.push_back(t);
        cur.clear(); space = false; lineStart = false;
    };
    for (std::size_t k = 0; k < n; ++k) {
        if (hide[k]) continue;
        const char c = s[k];
        const bool inCode = (st[k] & kMdCode) != 0;
        if (c == '\n') { flush(); ChatTok t; t.newline = true; out.push_back(t); space = false; lineStart = true; continue; }
        if (c == ' ' && !inCode) { flush(); space = true; continue; }
        if (!cur.empty() && st[k] != curSt) flush();
        if (cur.empty()) curSt = st[k];
        cur += c;
    }
    flush();
    return out;
}
static std::vector<ChatBlock> ChatBlocks(const std::string& body) {
    std::vector<ChatBlock> out;
    std::vector<std::string> lines;
    { std::size_t p = 0; while (true) { std::size_t q = body.find('\n', p); lines.push_back(body.substr(p, q == std::string::npos ? std::string::npos : q - p)); if (q == std::string::npos) break; p = q + 1; } }
    auto push = [&](int kind, const std::string& t) {
        ChatBlock b; b.kind = kind; b.text = t;
        if (kind != 2) b.toks = ChatInlineTokens(t);
        out.push_back(b);
    };
    std::string para; bool havePara = false;
    std::size_t i = 0;
    while (i < lines.size()) {
        const std::string& L = lines[i];
        if (L.rfind("```", 0) == 0) {
            if (havePara) { push(0, para); para.clear(); havePara = false; }
            std::string rest = L.substr(3);
            std::size_t endInline = rest.find("```");
            if (endInline != std::string::npos) { push(2, rest.substr(0, endInline)); ++i; continue; }   // ```one line```
            std::string code; bool any = false;
            for (++i; i < lines.size() && lines[i].rfind("```", 0) != 0; ++i) { if (any) code += '\n'; code += lines[i]; any = true; }
            if (i < lines.size()) ++i;   // closing fence
            push(2, code);
            continue;
        }
        if (!L.empty() && L[0] == '>' && (L.size() == 1 || L[1] == ' ')) {
            if (havePara) { push(0, para); para.clear(); havePara = false; }
            std::string q; bool any = false;
            for (; i < lines.size() && !lines[i].empty() && lines[i][0] == '>'; ++i) {
                std::string x = lines[i].substr(1);
                if (!x.empty() && x[0] == ' ') x.erase(0, 1);
                if (any) q += '\n';
                q += x; any = true;
            }
            push(1, q);
            continue;
        }
        if (havePara) para += '\n';
        para += L; havePara = true; ++i;
    }
    if (havePara || out.empty()) push(0, para);
    return out;
}
static bool ChatBodyIsPlain(const std::string& body) {
    return body.find_first_of("*_~`|>@:<") == std::string::npos && body.find("http") == std::string::npos &&
           body.find("xD") == std::string::npos;
}
// Lay out (and optionally draw) inline tokens wrapped at `avail`. Returns the
// widest line. `key` makes spoiler reveal state stable per message.
static float ChatLayoutTokens(const std::vector<ChatTok>& toks, float avail, bool draw, std::size_t key) {
    static std::set<std::size_t> revealed;   // clicked-open spoilers
    const float spaceW = ImGui::CalcTextSize(" ").x, em = ImGui::GetTextLineHeight();
    ImDrawList* dl = draw ? ImGui::GetWindowDrawList() : nullptr;
    const ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);
    auto monoW = [&](const std::string& t) {
        if (g_fontMono) ImGui::PushFont(g_fontMono, 0.0f);
        float w = ImGui::CalcTextSize(t.c_str()).x;
        if (g_fontMono) ImGui::PopFont();
        return w;
    };
    float lineX = 0.0f, maxLine = 0.0f;
    bool lineStarted = false;
    for (std::size_t ti = 0; ti < toks.size(); ++ti) {
        const ChatTok& t = toks[ti];
        if (t.newline) {
            maxLine = std::max(maxLine, lineX);
            if (!lineStarted && draw) ImGui::Dummy(ImVec2(0, em));   // empty line
            lineStarted = false; lineX = 0.0f;
            continue;
        }
        const bool code = (t.style & kMdCode) != 0;
        const int eid = code ? -1 : EmojiId(t.text);
        float tw = eid >= 0 ? em : code ? monoW(t.text) + 8.0f : ImGui::CalcTextSize(t.text.c_str()).x;
        if (!code && eid < 0) { if (t.style & kMdBold) tw += 1.0f; if (t.style & kMdItalic) tw += 2.0f; }
        float gap = (lineStarted && !t.glue) ? spaceW : 0.0f;
        if (lineStarted && lineX + gap + tw > avail) { maxLine = std::max(maxLine, lineX); lineStarted = false; lineX = 0.0f; gap = 0.0f; }
        if (draw) {
            if (lineStarted) ImGui::SameLine(0, gap);
            ImVec2 p = ImGui::GetCursorScreenPos();
            const bool link = !code && (t.text.rfind("http://", 0) == 0 || t.text.rfind("https://", 0) == 0);
            const bool mention = !code && t.text.size() > 1 && t.text[0] == '@';
            const bool spoiler = (t.style & kMdSpoiler) != 0;
            const std::size_t sk = key ^ (ti * 0x9E3779B97F4A7C15ull);
            if (spoiler && !revealed.count(sk)) {   // hidden: a solid pill until clicked
                ImGui::PushID((int)ti);
                if (ImGui::InvisibleButton("##spoiler", ImVec2(tw, em))) revealed.insert(sk);
                const bool h = ImGui::IsItemHovered();
                ImGui::PopID();
                dl->AddRectFilled(p, ImVec2(p.x + tw, p.y + em), h ? IM_COL32(92, 100, 122, 255) : IM_COL32(70, 76, 94, 255), 4.0f);
                if (h) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("Spoiler: click to reveal"); }
            } else if (eid >= 0) {
                DrawEmojiToken(dl, ImVec2(p.x + em * 0.5f, p.y + em * 0.5f), em * 1.12f, t.text);
                ImGui::Dummy(ImVec2(em, em));
                if (eid == kEmojiCustom && ImGui::IsItemHovered())
                    ImGui::SetTooltip(":%s:", t.text.substr(2, t.text.find(':', 2) - 2).c_str());
            } else if (link) {
                const ImU32 linkCol = ImGui::GetColorU32(kAccent);
                ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(linkCol), "%s", t.text.c_str());
                ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
                dl->AddLine(ImVec2(mn.x, mx.y - 1), ImVec2(mx.x, mx.y - 1), linkCol, 1.0f);
                if (ImGui::IsItemHovered()) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("%s", t.text.c_str()); }
                if (ImGui::IsItemClicked()) OpenExternal(t.text);
            } else {
                ImU32 col = mention ? IM_COL32(140, 199, 255, 255) : textCol;
                if (code) {
                    dl->AddRectFilled(ImVec2(p.x, p.y + 1), ImVec2(p.x + tw, p.y + em - 1), IM_COL32(0, 0, 0, 70), 4.0f);
                    dl->AddRect(ImVec2(p.x, p.y + 1), ImVec2(p.x + tw, p.y + em - 1), IM_COL32(255, 255, 255, 22), 4.0f);
                    dl->AddText(g_fontMono ? g_fontMono : ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(p.x + 4, p.y), IM_COL32(242, 196, 150, 255), t.text.c_str());
                } else {
                    const int v0 = dl->VtxBuffer.Size;
                    dl->AddText(p, col, t.text.c_str());
                    if ((t.style & kMdBold) || mention) dl->AddText(ImVec2(p.x + 0.7f, p.y), col, t.text.c_str());   // faux bold
                    if (t.style & kMdItalic) {   // shear the glyph quads around the baseline = real italics
                        const float base = p.y + em * 0.8f;
                        for (int v = v0; v < dl->VtxBuffer.Size; ++v) dl->VtxBuffer[v].pos.x += (base - dl->VtxBuffer[v].pos.y) * 0.20f;
                    }
                    if (t.style & kMdStrike) dl->AddLine(ImVec2(p.x, p.y + em * 0.56f), ImVec2(p.x + tw, p.y + em * 0.56f), col, 1.3f);
                }
                ImGui::Dummy(ImVec2(tw, em));
            }
        }
        lineX += gap + tw; lineStarted = true;
    }
    return std::max(maxLine, lineX);
}
// A ``` block: monospace lines in a dark rounded box (long lines are clipped).
static float ChatCodeBlock(const std::string& code, float avail, bool draw) {
    if (g_fontMono) ImGui::PushFont(g_fontMono, 0.0f);
    float w = 0.0f; int lines = 1;
    { std::size_t p = 0; while (true) { std::size_t q = code.find('\n', p); std::string ln = code.substr(p, q == std::string::npos ? std::string::npos : q - p);
        w = std::max(w, ImGui::CalcTextSize(ln.c_str()).x); if (q == std::string::npos) break; ++lines; p = q + 1; } }
    const float lh = ImGui::GetTextLineHeight(), bw = std::min(avail, w + 20.0f), bh = lines * lh + 14.0f;
    if (draw) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + bh), IM_COL32(0, 0, 0, 80), 8.0f);
        dl->AddRect(p, ImVec2(p.x + bw, p.y + bh), IM_COL32(255, 255, 255, 20), 8.0f);
        dl->PushClipRect(ImVec2(p.x + 6, p.y), ImVec2(p.x + bw - 6, p.y + bh), true);
        std::size_t s0 = 0; float y = p.y + 7;
        while (true) {
            std::size_t q = code.find('\n', s0);
            std::string ln = code.substr(s0, q == std::string::npos ? std::string::npos : q - s0);
            dl->AddText(ImVec2(p.x + 10, y), IM_COL32(222, 228, 240, 255), ln.c_str());
            y += lh;
            if (q == std::string::npos) break;
            s0 = q + 1;
        }
        dl->PopClipRect();
        ImGui::Dummy(ImVec2(bw, bh));
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Right-click to copy the code");
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) { ImGui::SetClipboardText(code.c_str()); Toast("Code copied", 1); }
    }
    if (g_fontMono) ImGui::PopFont();
    return bw;
}

// ---- Link previews ----------------------------------------------------------
// The first web-page link in a message gets a card (site, title, description,
// thumbnail) like Messenger/Telegram. Pages are fetched once in the background
// (curl, 8 s cap, first 256 KB) and their Open Graph / <title> tags parsed.
struct LinkPreview { int state = 0; std::string site, title, desc, image; };   // 0 loading, 1 ready, 2 nothing to show
static okay::thr::Mutex* g_lpMutex = nullptr;
static std::map<std::string, LinkPreview> g_linkPrev;   // (guarded) by URL
static std::vector<std::string> g_lpQueue;              // (guarded)
static bool g_lpWorkerRunning = false;                  // (guarded)

// First http(s) link in the text that isn't an image/GIF (those render inline).
static std::string FirstPageLink(const std::string& body) {
    std::size_t i = 0;
    while ((i = body.find("http", i)) != std::string::npos) {
        if (body.compare(i, 7, "http://") != 0 && body.compare(i, 8, "https://") != 0) { i += 4; continue; }
        if (i > 0 && body[i - 1] == ':') { i += 4; continue; }   // inside a custom emoji "<:name:url>"
        std::size_t j = i;
        while (j < body.size() && !std::isspace((unsigned char)body[j]) && body[j] != '<' && body[j] != '>') ++j;
        std::string u = body.substr(i, j - i);
        while (!u.empty() && std::strchr(".,!?:;)'\"*_~|`", u.back())) u.pop_back();
        std::string low = u; for (auto& c : low) c = (char)std::tolower((unsigned char)c);
        const std::size_t q = low.find_first_of("?#"); if (q != std::string::npos) low.erase(q);
        bool media = false;
        for (const char* ext : {".png", ".jpg", ".jpeg", ".bmp", ".gif", ".webp", ".wav", ".mp3", ".ogg", ".zip", ".pdf", ".exe"})
            if (low.size() > std::strlen(ext) && low.compare(low.size() - std::strlen(ext), std::strlen(ext), ext) == 0) media = true;
        if (!media && low.find("/storage/v1/object/") == std::string::npos && u.size() > 10) return u;
        i = j;
    }
    return std::string();
}
static std::string HtmlDecode(const std::string& in) {
    std::string out; out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] != '&') { out += (in[i] == '\n' || in[i] == '\r' || in[i] == '\t') ? ' ' : in[i]; continue; }
        const std::size_t semi = in.find(';', i);
        if (semi == std::string::npos || semi - i > 10) { out += '&'; continue; }
        const std::string e = in.substr(i + 1, semi - i - 1);
        unsigned long cp = 0;
        if (e == "amp") cp = '&'; else if (e == "lt") cp = '<'; else if (e == "gt") cp = '>';
        else if (e == "quot") cp = '"'; else if (e == "apos") cp = '\''; else if (e == "nbsp") cp = ' ';
        else if (e.size() > 1 && e[0] == '#') cp = (e[1] == 'x' || e[1] == 'X') ? std::strtoul(e.c_str() + 2, nullptr, 16) : std::strtoul(e.c_str() + 1, nullptr, 10);
        if (!cp || cp > 0x10FFFF) { out += '&'; continue; }
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
        i = semi;
    }
    std::string t; bool sp = false;   // collapse runs of spaces, trim
    for (char c : out) { if (c == ' ') { if (!sp && !t.empty()) t += ' '; sp = true; } else { t += c; sp = false; } }
    while (!t.empty() && t.back() == ' ') t.pop_back();
    return t;
}
// Value of attribute `name` inside one tag's text (tag is lower-cased copy `low`).
static std::string HtmlAttr(const std::string& tag, const std::string& low, const char* name) {
    std::size_t at = 0;
    const std::string key = std::string(name) + "=";
    while ((at = low.find(key, at)) != std::string::npos) {
        if (at > 0 && !std::isspace((unsigned char)low[at - 1])) { at += key.size(); continue; }
        std::size_t v = at + key.size();
        if (v >= tag.size()) return std::string();
        const char qc = tag[v];
        if (qc == '"' || qc == '\'') { const std::size_t e = tag.find(qc, v + 1); return e == std::string::npos ? std::string() : tag.substr(v + 1, e - v - 1); }
        std::size_t e = v; while (e < tag.size() && !std::isspace((unsigned char)tag[e]) && tag[e] != '>') ++e;
        return tag.substr(v, e - v);
    }
    return std::string();
}
static std::string Utf8Clip(std::string s, std::size_t n) {
    if (s.size() <= n) return s;
    std::size_t cut = n; while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) --cut;
    s.erase(cut); while (!s.empty() && s.back() == ' ') s.pop_back();
    return s + "\xE2\x80\xA6";
}
static LinkPreview ParseLinkPreview(const std::string& url, const std::string& html) {
    LinkPreview lp; lp.state = 2;
    std::string low = html; for (auto& c : low) c = (char)std::tolower((unsigned char)c);
    std::map<std::string, std::string> meta;
    for (std::size_t at = 0; (at = low.find("<meta", at)) != std::string::npos; ) {
        const std::size_t end = low.find('>', at); if (end == std::string::npos) break;
        const std::string tag = html.substr(at, end - at), ltag = low.substr(at, end - at);
        std::string k = HtmlAttr(tag, ltag, "property"); if (k.empty()) k = HtmlAttr(tag, ltag, "name");
        for (auto& c : k) c = (char)std::tolower((unsigned char)c);
        if (!k.empty() && !meta.count(k)) meta[k] = HtmlAttr(tag, ltag, "content");
        at = end;
    }
    auto pick = [&](std::initializer_list<const char*> keys) { for (const char* k : keys) { auto it = meta.find(k); if (it != meta.end() && !it->second.empty()) return HtmlDecode(it->second); } return std::string(); };
    lp.title = pick({"og:title", "twitter:title"});
    if (lp.title.empty()) {
        const std::size_t a = low.find("<title"), b = a == std::string::npos ? a : low.find('>', a), c = b == std::string::npos ? b : low.find("</title", b);
        if (c != std::string::npos) lp.title = HtmlDecode(html.substr(b + 1, c - b - 1));
    }
    lp.desc = pick({"og:description", "twitter:description", "description"});
    lp.site = pick({"og:site_name"});
    std::string img = pick({"og:image", "og:image:url", "twitter:image", "twitter:image:src"});
    // Origin of the page for relative image paths, and the bare host as a fallback site name.
    const std::size_t hs = url.find("://") + 3, he = url.find_first_of("/?#", hs);
    const std::string origin = url.substr(0, he), host = url.substr(hs, he == std::string::npos ? std::string::npos : he - hs);
    if (img.rfind("//", 0) == 0) img = "https:" + img;
    else if (!img.empty() && img[0] == '/') img = origin + img;
    if (ShellSafeUrl(img)) lp.image = img;
    if (lp.site.empty()) lp.site = host.rfind("www.", 0) == 0 ? host.substr(4) : host;
    lp.title = Utf8Clip(lp.title, 110); lp.desc = Utf8Clip(lp.desc, 180); lp.site = Utf8Clip(lp.site, 40);
    if (!lp.title.empty()) lp.state = 1;
    return lp;
}
static int LinkPreviewWorker(void*) {
    while (true) {
        std::string url;
        okay::thr::LockMutex(g_lpMutex);
        if (g_lpQueue.empty()) { g_lpWorkerRunning = false; okay::thr::UnlockMutex(g_lpMutex); break; }
        url = g_lpQueue.front(); g_lpQueue.erase(g_lpQueue.begin());
        okay::thr::UnlockMutex(g_lpMutex);
        LinkPreview lp; lp.state = 2;
        // SSRF guard: never fetch a link whose host resolves to a private,
        // loopback or link-local address (chat links come from other people).
        if (ShellSafeUrl(url) &&
            okay::netguard::HostIsPubliclyRoutable(okay::netguard::UrlHostForResolve(url))) {
            std::error_code ec;
            const fs::path tmp = fs::temp_directory_path(ec) / ("okay_lp_" + std::to_string(std::hash<std::string>{}(url)) + ".html");
            fs::remove(tmp, ec);
#if defined(OKAY_OWN_HTTP_ACTIVE)
            bool ownDone = false;   // own client first (opt-in); an unclear answer falls through to curl below
            if (okay::http::NativeAvailable()) {
                std::string ownHtml;
                const okay::http::OwnOutcome o = okay::http::OwnFetchPrefix(url, "Mozilla/5.0 (compatible; OkaySpace link preview)", 3000000, 262143, 8000, ownHtml);
                if (o == okay::http::OwnOutcome::Ok) { if (ownHtml.size() > 262144) ownHtml.resize(262144); lp = ParseLinkPreview(url, ownHtml); }
                ownDone = o != okay::http::OwnOutcome::NotHandled;   // a clear refusal (404, too big) means no preview, as with curl -f
            }
            if (!ownDone) {
#endif
            std::string cmd = "curl -L -s -f -m 8 --max-filesize 3000000 -r 0-262143 "
                              "-A \"Mozilla/5.0 (compatible; OkaySpace link preview)\" -o \"" + tmp.string() + "\" \"" + url + "\"";
#if !defined(_WIN32)
            cmd += " 2>/dev/null";
#endif
            RunCmd(cmd, true);
            std::ifstream f(tmp, std::ios::binary);
            if (f) {
                std::string html((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
                if (html.size() > 262144) html.resize(262144);
                lp = ParseLinkPreview(url, html);
            }
            f.close(); fs::remove(tmp, ec);
#if defined(OKAY_OWN_HTTP_ACTIVE)
            }
#endif
        }
        okay::thr::LockMutex(g_lpMutex); g_linkPrev[url] = lp; okay::thr::UnlockMutex(g_lpMutex);
    }
    return 0;
}
// The preview for `url` once it's ready (nullptr while loading / when there's none).
static const LinkPreview* LinkPreviewFor(const std::string& url) {
    if (!g_linkPreviews || url.empty()) return nullptr;
    if (!g_lpMutex) g_lpMutex = okay::thr::CreateMutex();
    static std::map<std::string, LinkPreview> ready;   // UI-thread copy (stable pointers while drawing)
    if (auto it = ready.find(url); it != ready.end()) return it->second.state == 1 ? &it->second : nullptr;
    okay::thr::LockMutex(g_lpMutex);
    auto it = g_linkPrev.find(url);
    if (it == g_linkPrev.end()) {
        g_linkPrev[url] = LinkPreview{};
        g_lpQueue.push_back(url);
        const bool start = !g_lpWorkerRunning;
        if (start) g_lpWorkerRunning = true;
        okay::thr::UnlockMutex(g_lpMutex);
        if (start) {
            if (okay::thr::Thread* t = okay::thr::CreateThread(LinkPreviewWorker, "okay-linkprev", nullptr)) okay::thr::DetachThread(t);
            else { okay::thr::LockMutex(g_lpMutex); g_lpWorkerRunning = false; okay::thr::UnlockMutex(g_lpMutex); }
        }
        return nullptr;
    }
    const LinkPreview lp = it->second;
    okay::thr::UnlockMutex(g_lpMutex);
    if (lp.state == 0) return nullptr;
    auto& r = ready[url] = lp;
    return r.state == 1 ? &r : nullptr;
}
static constexpr float kLinkCardMaxW = 320.0f;
// The card: accent bar, site, bold title, two lines of description, thumbnail on top.
static void DrawLinkPreviewCard(const std::string& url, const LinkPreview& lp, float avail) {
    const float w = std::min(avail, kLinkCardMaxW), pad = 10.0f;
    ImGui::Dummy(ImVec2(0, 4));
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImGui::PushID(url.c_str());
    float y = p.y + pad;
    int iw = 0, ih = 0;
    SDL_Texture* tex = lp.image.empty() ? nullptr : ChatImageTexture(lp.image, &iw, &ih);
    float imgH = 0.0f;
    if (tex && iw > 0 && ih > 0) imgH = std::min(140.0f, (w - pad * 2 - 6) * (float)ih / (float)iw);
    // Measure text first so the background can be drawn behind it.
    const float tw = w - pad * 2 - 6;
    ImFont* f = ImGui::GetFont(); const float fs = ImGui::GetFontSize();
    const ImVec2 siteSz = f->CalcTextSizeA(fs * 0.85f, FLT_MAX, tw, lp.site.c_str());
    const ImVec2 titleSz = f->CalcTextSizeA(fs, FLT_MAX, tw, lp.title.c_str());
    std::string desc = lp.desc;
    ImVec2 descSz = desc.empty() ? ImVec2(0, 0) : f->CalcTextSizeA(fs * 0.9f, FLT_MAX, tw, desc.c_str());
    const float maxDescH = fs * 0.9f * 2.6f;   // about two lines
    if (descSz.y > maxDescH) descSz.y = maxDescH;
    const float h = pad + (imgH > 0 ? imgH + 8 : 0) + siteSz.y + 2 + titleSz.y + (desc.empty() ? 0 : 3 + descSz.y) + pad;
    const bool hov = ImGui::IsMouseHoveringRect(p, ImVec2(p.x + w, p.y + h));
    d->AddRectFilled(p, ImVec2(p.x + w, p.y + h), hov ? IM_COL32(0, 0, 0, 70) : IM_COL32(0, 0, 0, 50), 8.0f);
    d->AddRectFilled(p, ImVec2(p.x + 3, p.y + h), ImGui::GetColorU32(kAccent), 8.0f, ImDrawFlags_RoundCornersLeft);
    const float x = p.x + pad + 4;
    if (imgH > 0) {
        d->AddImageRounded((ImTextureID)(intptr_t)tex, ImVec2(x, y), ImVec2(x + tw, y + imgH), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 6.0f);
        y += imgH + 8;
    }
    d->AddText(f, fs * 0.85f, ImVec2(x, y), IM_COL32(170, 182, 205, 255), lp.site.c_str(), nullptr, tw); y += siteSz.y + 2;
    d->AddText(f, fs, ImVec2(x, y), IM_COL32(150, 196, 255, 255), lp.title.c_str(), nullptr, tw);
    d->AddText(f, fs, ImVec2(x + 0.6f, y), IM_COL32(150, 196, 255, 255), lp.title.c_str(), nullptr, tw);   // faux bold
    y += titleSz.y;
    if (!desc.empty()) {
        y += 3;
        d->PushClipRect(ImVec2(x, y), ImVec2(x + tw, y + descSz.y), true);
        d->AddText(f, fs * 0.9f, ImVec2(x, y), IM_COL32(205, 212, 226, 220), desc.c_str(), nullptr, tw);
        d->PopClipRect();
    }
    ImGui::SetCursorScreenPos(p);
    if (ImGui::InvisibleButton("##lpcard", ImVec2(w, h))) OpenExternal(url);
    if (ImGui::IsItemHovered()) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("%s", url.c_str()); }
    ImGui::PopID();
}

// Render a chat message body with word-wrapping and formatting (see above).
// maxW > 0 constrains wrapping to that pixel width (e.g. a chat bubble); 0 uses
// the remaining content width.
static void RenderChatBodyText(const std::string& bodyIn, float maxW);
void RenderChatBody(const std::string& bodyIn, float maxW) {
    RenderChatBodyText(bodyIn, maxW);
    const std::string link = g_linkPreviews ? FirstPageLink(bodyIn) : std::string();
    if (const LinkPreview* lp = LinkPreviewFor(link))
        DrawLinkPreviewCard(link, *lp, maxW > 0.0f ? maxW : ImGui::GetContentRegionAvail().x);
}
static void RenderChatBodyText(const std::string& bodyIn, float maxW) {
    const std::string body = NormalizeEmoji(bodyIn);
    if (ChatBodyIsPlain(body)) {
        if (maxW > 0.0f) ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + maxW);
        ImGui::TextWrapped("%s", body.c_str());
        if (maxW > 0.0f) ImGui::PopTextWrapPos();
        return;
    }
    const float avail = maxW > 0.0f ? maxW : ImGui::GetContentRegionAvail().x;
    const std::size_t key = std::hash<std::string>{}(body);
    const auto blocks = ChatBlocks(body);
    for (std::size_t bi = 0; bi < blocks.size(); ++bi) {
        const ChatBlock& b = blocks[bi];
        ImGui::PushID((int)bi);
        if (b.kind == 2) ChatCodeBlock(b.text, avail, true);
        else if (b.kind == 1) {   // quote: dim text with an accent bar
            ImVec2 q0 = ImGui::GetCursorScreenPos();
            ImGui::Indent(12.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ChatLayoutTokens(b.toks, avail - 12.0f, true, key + bi);
            ImGui::PopStyleColor();
            ImGui::Unindent(12.0f);
            float y1 = ImGui::GetItemRectMax().y;
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(q0.x + 1, q0.y), ImVec2(q0.x + 4, y1), ImGui::GetColorU32(kAccent), 1.5f);
        } else ChatLayoutTokens(b.toks, avail, true, key + bi);
        ImGui::PopID();
    }
}

// The pixel WIDTH RenderChatBody will use for `body` wrapped at maxW — the width
// of its longest wrapped line (<= maxW). Same tokenizer, so bubbles hug exactly.
static float MeasureChatBodyText(const std::string& bodyIn, float maxW);
float MeasureChatBodyWidth(const std::string& bodyIn, float maxW) {
    float w = MeasureChatBodyText(bodyIn, maxW);
    if (g_linkPreviews && LinkPreviewFor(FirstPageLink(bodyIn))) w = std::max(w, std::min(maxW, kLinkCardMaxW));
    return w;
}
static float MeasureChatBodyText(const std::string& bodyIn, float maxW) {
    const std::string body = NormalizeEmoji(bodyIn);
    if (ChatBodyIsPlain(body)) {
        float w = ImGui::CalcTextSize(body.c_str(), nullptr, false, maxW).x;
        return w < maxW ? w : maxW;
    }
    float w = 0.0f;
    for (const ChatBlock& b : ChatBlocks(body)) {
        if (b.kind == 2) w = std::max(w, ChatCodeBlock(b.text, maxW, false));
        else if (b.kind == 1) w = std::max(w, 12.0f + ChatLayoutTokens(b.toks, maxW - 12.0f, false, 0));
        else w = std::max(w, ChatLayoutTokens(b.toks, maxW, false, 0));
    }
    return w < maxW ? w : maxW;
}

// Rounded square tile showing the item's initial — a lightweight "thumbnail"
// that needs no image assets. Advances the layout like a normal item.
void IconTile(const std::string& name, float size) {
    ImVec4 raw = NameColor(name);
    // Mute the hue toward a neutral steel so a list reads as a clean, uniform set
    // instead of a rainbow of saturated tiles.
    auto mute = [](float c) { return 0.40f + (c - 0.40f) * 0.38f; };
    ImVec4 col(mute(raw.x), mute(raw.y), mute(raw.z), 1.0f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float rnd = size * 0.14f;
    dl->AddRectFilled(p, ImVec2(p.x + size, p.y + size),
                      ImGui::GetColorU32(ImVec4(col.x, col.y, col.z, 0.30f)), rnd);
    dl->AddRect(p, ImVec2(p.x + size, p.y + size),
                ImGui::GetColorU32(ImVec4(col.x, col.y, col.z, 0.55f)), rnd, 0, 1.0f);
    char init[2] = { name.empty() ? '?' : (char)std::toupper((unsigned char)name[0]), 0 };
    ImVec2 ts = ImGui::CalcTextSize(init);
    dl->AddText(ImVec2(p.x + (size - ts.x) * 0.5f, p.y + (size - ts.y) * 0.5f),
                ImGui::GetColorU32(ImVec4(0.88f, 0.90f, 0.94f, 1.0f)), init);
    ImGui::Dummy(ImVec2(size, size));
}

// A small line drawing for each project template (the Create page's gallery), so the
// cards read at a glance instead of showing a letter. Draws into a size x size box.
void TemplateGlyph(ImDrawList* d, const std::string& t, ImVec2 p, float size, ImU32 col) {
    const float s = size, k = s / 40.0f, th = 1.7f * k;
    auto P = [&](float x, float y) { return ImVec2(p.x + x * k, p.y + y * k); };
    auto person = [&](float cx, float cy, float sc) {
        d->AddCircle(P(cx, cy - 7 * sc), 3.6f * sc * k, col, 14, th);
        d->PathArcTo(P(cx, cy + 8 * sc), 7.0f * sc * k, 3.4f, 6.02f, 12); d->PathStroke(col, 0, th);
    };
    if (t == "2D Scene") {
        d->AddRect(P(8, 10), P(32, 30), col, 3 * k, 0, th);
        d->AddLine(P(8, 24), P(16, 17), col, th); d->AddLine(P(16, 17), P(22, 23), col, th);
        d->AddLine(P(22, 23), P(26, 19), col, th); d->AddLine(P(26, 19), P(32, 25), col, th);
        d->AddCircle(P(26, 14), 2.0f * k, col, 10, th);
    } else if (t == "3D Scene" || t == "Empty") {
        ImVec2 top = P(20, 8), l = P(8, 14), r = P(32, 14), m = P(20, 20), lb = P(8, 27), rb = P(32, 27), mb = P(20, 33);
        if (t == "Empty") {
            for (int i = 0; i < 4; ++i) { float a = 8 + i * 6.5f; d->AddLine(P(a, 10), P(a + 3.5f, 10), col, th); d->AddLine(P(a, 30), P(a + 3.5f, 30), col, th); }
            for (int i = 0; i < 3; ++i) { float a = 12 + i * 6.5f; d->AddLine(P(8, a), P(8, a + 3.5f), col, th); d->AddLine(P(32, a), P(32, a + 3.5f), col, th); }
        } else {
            d->AddQuad(top, r, m, l, col, th); d->AddLine(l, lb, col, th); d->AddLine(r, rb, col, th);
            d->AddLine(m, mb, col, th); d->AddLine(lb, mb, col, th); d->AddLine(mb, rb, col, th);
        }
    } else if (t == "First Person") {
        d->AddCircle(P(20, 20), 9 * k, col, 24, th);
        d->AddLine(P(20, 6), P(20, 13), col, th); d->AddLine(P(20, 27), P(20, 34), col, th);
        d->AddLine(P(6, 20), P(13, 20), col, th); d->AddLine(P(27, 20), P(34, 20), col, th);
        d->AddCircleFilled(P(20, 20), 1.6f * k, col, 8);
    } else if (t == "Third Person" || t == "First and Third Person") {
        person(20, 21, 1.15f);
        if (true) { d->PathArcTo(P(20, 33), 13 * k, 3.6f, 5.8f, 16); d->PathStroke(col, 0, th * 0.8f); }
    } else if (t == "Third Person Shooter") {
        person(15, 21, 1.0f);
        d->AddCircle(P(29, 15), 5 * k, col, 16, th); d->AddLine(P(29, 8), P(29, 22), col, th * 0.8f); d->AddLine(P(22, 15), P(36, 15), col, th * 0.8f);
    } else if (t == "Point & Click") {
        const ImVec2 cur[] = {P(13, 8), P(13, 30), P(18, 25), P(22, 33), P(25, 31), P(21, 23), P(28, 23)};
        d->AddPolyline(cur, 7, col, ImDrawFlags_Closed, th);
    } else if (t == "Platformer") {
        d->AddLine(P(6, 32), P(16, 32), col, th); d->AddLine(P(16, 32), P(16, 24), col, th);
        d->AddLine(P(16, 24), P(26, 24), col, th); d->AddLine(P(26, 24), P(26, 16), col, th); d->AddLine(P(26, 16), P(34, 16), col, th);
        d->AddRect(P(9, 22), P(14, 29), col, 1.5f * k, 0, th);
        d->PathArcTo(P(20, 22), 7 * k, 3.6f, 5.8f, 10); d->PathStroke(col, 0, th * 0.7f);
    } else if (t == "Top-Down Shooter") {
        d->AddRect(P(8, 8), P(32, 32), col, 3 * k, 0, th);
        d->AddCircleFilled(P(16, 24), 3 * k, col, 12);
        d->AddLine(P(16, 24), P(28, 12), col, th * 0.8f);
        d->AddCircle(P(28, 12), 3.5f * k, col, 14, th * 0.8f);
    } else if (t == "2.5D Platformer") {
        d->AddRect(P(6, 26), P(34, 32), col, 1.5f * k, 0, th);
        d->AddRect(P(10, 20), P(30, 26), col, 1.5f * k, 0, th * 0.7f);
        d->AddRect(P(14, 14), P(26, 20), col, 1.5f * k, 0, th * 0.5f);
        d->AddCircleFilled(P(20, 9), 3 * k, col, 12);
    } else if (t == "Top-Down") {
        d->AddRect(P(8, 8), P(32, 32), col, 3 * k, 0, th);
        d->AddLine(P(20, 8), P(20, 17), col, th); d->AddLine(P(8, 22), P(16, 22), col, th);
        d->AddCircleFilled(P(25, 26), 3 * k, col, 12);
    } else if (t == "Main Menu") {
        d->AddRect(P(9, 7), P(31, 33), col, 3 * k, 0, th);
        for (int i = 0; i < 3; ++i) d->AddRect(P(13, 12 + i * 7), P(27, 16.5f + i * 7), col, 2 * k, 0, th * 0.8f);
    } else if (t == "Multiplayer") {
        person(13, 22, 0.95f); person(27, 22, 0.95f);
        d->AddLine(P(17, 9), P(23, 9), col, th); d->AddLine(P(21, 7), P(23, 9), col, th);
    } else if (t == "Sandbox") {
        d->AddRect(P(8, 20), P(19, 31), col, 1.5f * k, 0, th); d->AddRect(P(19, 20), P(30, 31), col, 1.5f * k, 0, th);
        d->AddRect(P(13.5f, 9), P(24.5f, 20), col, 1.5f * k, 0, th);
    } else if (t.rfind("Vehicle", 0) == 0) {
        d->PathLineTo(P(6, 26)); d->PathLineTo(P(6, 20)); d->PathLineTo(P(12, 19)); d->PathLineTo(P(16, 13));
        d->PathLineTo(P(26, 13)); d->PathLineTo(P(30, 19)); d->PathLineTo(P(34, 20)); d->PathLineTo(P(34, 26));
        d->PathStroke(col, ImDrawFlags_Closed, th);
        d->AddCircle(P(13, 27), 3.4f * k, col, 12, th); d->AddCircle(P(27, 27), 3.4f * k, col, 12, th);
    } else {
        ImVec2 ts = ImGui::CalcTextSize(t.c_str(), t.c_str() + (t.empty() ? 0 : 1));
        d->AddText(ImVec2(p.x + (s - ts.x) * 0.5f, p.y + (s - ts.y) * 0.5f), col, t.c_str(), t.c_str() + (t.empty() ? 0 : 1));
    }
}

// A wide illustrated preview for a template card: a small scene, not a letter tile.
void DrawTemplateStage(ImDrawList* d, const std::string& title, ImVec2 a, ImVec2 b) {
    const bool flat = title.find("2D") != std::string::npos || title == "Platformer" || title == "Main Menu";
    const bool empty = title == "Empty";
    ImU32 sky = empty ? IM_COL32(38, 40, 48, 255) : flat ? IM_COL32(36, 48, 86, 255) : IM_COL32(92, 148, 206, 255);
    ImU32 ground = empty ? IM_COL32(28, 30, 36, 255) : flat ? IM_COL32(22, 28, 48, 255) : IM_COL32(168, 132, 96, 255);
    const float horizon = a.y + (b.y - a.y) * (empty ? 1.0f : 0.62f);
    d->AddRectFilled(a, ImVec2(b.x, horizon), sky);
    d->AddRectFilled(ImVec2(a.x, horizon), b, ground);
    if (!empty) d->AddLine(ImVec2(a.x + 8, horizon), ImVec2(b.x - 8, horizon), IM_COL32(255, 255, 255, 50), 1.5f);
    if (flat && !empty) {
        for (int i = 1; i < 4; ++i) {
            float y = a.y + (b.y - a.y) * (0.25f * i);
            d->AddLine(ImVec2(a.x, y), ImVec2(b.x, y), IM_COL32(255, 255, 255, 18), 1.0f);
        }
    }
    const float s = std::min(b.x - a.x, b.y - a.y) * 0.72f;
    TemplateGlyph(d, title, ImVec2(a.x + (b.x - a.x - s) * 0.5f, a.y + (b.y - a.y - s) * 0.36f), s, IM_COL32(248, 250, 255, 235));
}

// Decodes an image file to a malloc'd RGBA8 buffer (free with std::free); nullptr on failure.
unsigned char* LoadRgbaFile(const std::string& path, int* w, int* h) {
    okay::DecodedImage img;
    if (!okay::DecodeImageFile(path, img) || img.rgba.empty()) return nullptr;
    unsigned char* px = static_cast<unsigned char*>(std::malloc(img.rgba.size()));
    if (!px) return nullptr;
    std::memcpy(px, img.rgba.data(), img.rgba.size());
    *w = img.width; *h = img.height;
    return px;
}

} // namespace okl
