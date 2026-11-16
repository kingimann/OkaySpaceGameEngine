#pragma once
// A small strict XML reader for the own 3MF / AMF / X3D readers. Internal: not installed.
//
// Reads elements, attributes and text into a flat node array. It has hard caps (file size, element count,
// nesting depth, attributes per element, attribute and name length) so a hostile file costs a bounded
// amount of memory and time. It never reads a DTD: a DOCTYPE is skipped, and when its internal subset declares
// an <!ENTITY the file is refused (no entity is ever expanded, so there is no billion-laughs and no external
// entity). Only the five predefined entities and numeric references are decoded; any other &name; fails the file.
// Namespace prefixes are dropped from element and attribute names ("p:UUID" -> "UUID"); xmlns attributes are
// ignored. UTF-8 only: a UTF-16 byte order mark is refused, a UTF-8 one is skipped.
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace okay {
namespace detail {
namespace xml {

struct Limits {
    std::size_t maxBytes = 512u * 1024u * 1024u;
    std::size_t maxElements = 2000000;
    int         maxDepth = 256;
    int         maxAttrs = 256;
    std::size_t maxNameBytes = 256;
    std::size_t maxTextBytes = 512u * 1024u * 1024u;   // all character data together
};

struct Node {
    std::string name;
    std::vector<std::pair<std::string, std::string>> attrs;
    std::vector<int> kids;
    std::string text;
};

struct Doc {
    std::vector<Node> n;   // n[0] is the root element
    const std::string* Attr(int i, const char* key) const {
        if (i < 0 || (std::size_t)i >= n.size()) return nullptr;
        for (const auto& a : n[(std::size_t)i].attrs) if (a.first == key) return &a.second;
        return nullptr;
    }
    std::string AttrOr(int i, const char* key, const char* dflt = "") const { const std::string* s = Attr(i, key); return s ? *s : std::string(dflt); }
    int First(int i, const char* name) const {
        if (i < 0) return -1;
        for (int k : n[(std::size_t)i].kids) if (n[(std::size_t)k].name == name) return k;
        return -1;
    }
    const std::string& Name(int i) const { return n[(std::size_t)i].name; }
    const std::string& Text(int i) const { return n[(std::size_t)i].text; }
    const std::vector<int>& Kids(int i) const { return n[(std::size_t)i].kids; }
};

inline bool IsWs(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; }

inline bool AppendUtf8(std::string& dst, long cp) {
    if (cp <= 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    if (cp < 0x80) dst += (char)cp;
    else if (cp < 0x800) { dst += (char)(0xC0 | (cp >> 6)); dst += (char)(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { dst += (char)(0xE0 | (cp >> 12)); dst += (char)(0x80 | ((cp >> 6) & 63)); dst += (char)(0x80 | (cp & 63)); }
    else { dst += (char)(0xF0 | (cp >> 18)); dst += (char)(0x80 | ((cp >> 12) & 63)); dst += (char)(0x80 | ((cp >> 6) & 63)); dst += (char)(0x80 | (cp & 63)); }
    return true;
}

// Appends s[a,b) with entity references decoded; false on an unknown or malformed reference.
inline bool Decode(std::string& dst, const std::string& s, std::size_t a, std::size_t b) {
    std::size_t i = a;
    while (i < b) {
        // searched inside [i, b) only: a find() to the end of the file for every text node would be quadratic on a file with no '&'
        const void* hit = std::memchr(s.data() + i, '&', b - i);
        const std::size_t amp = hit ? (std::size_t)(static_cast<const char*>(hit) - s.data()) : std::string::npos;
        if (amp == std::string::npos) { dst.append(s, i, b - i); return true; }
        dst.append(s, i, amp - i);
        std::size_t e = amp + 1;
        const std::size_t lim = std::min(b, amp + 11);   // a reference is short: never scan further (a long run of '&' would be quadratic)
        while (e < lim && s[e] != ';') ++e;
        if (e >= lim || s[e] != ';') return false;
        const std::string ent = s.substr(amp + 1, e - amp - 1);
        if (ent == "amp") dst += '&';
        else if (ent == "lt") dst += '<';
        else if (ent == "gt") dst += '>';
        else if (ent == "quot") dst += '"';
        else if (ent == "apos") dst += '\'';
        else if (ent.size() > 1 && ent[0] == '#') {
            long cp = 0;
            bool ok = true;
            if (ent[1] == 'x' || ent[1] == 'X') {
                ok = ent.size() > 2;
                for (std::size_t k = 2; k < ent.size() && ok; ++k) {
                    const char c = ent[k];
                    const int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
                    if (d < 0) ok = false; else cp = cp * 16 + d;
                }
            } else {
                for (std::size_t k = 1; k < ent.size() && ok; ++k) { if (ent[k] < '0' || ent[k] > '9') ok = false; else cp = cp * 10 + (ent[k] - '0'); }
            }
            if (!ok || !AppendUtf8(dst, cp)) return false;
        } else return false;
        i = e + 1;
    }
    return true;
}

inline std::string StripPrefix(const std::string& s) {
    const std::size_t c = s.find(':');
    return c == std::string::npos ? s : s.substr(c + 1);
}

inline bool Parse(const std::string& s, Doc& x, std::string& err, const Limits& lim = Limits()) {
    x.n.clear();
    auto fail = [&](const char* m) { err = m; x.n.clear(); return false; };
    if (s.size() > lim.maxBytes) return fail("XML: file too large");
    const std::size_t n = s.size();
    std::size_t p = 0;
    if (n >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) p = 3;
    else if (n >= 2 && (((unsigned char)s[0] == 0xFF && (unsigned char)s[1] == 0xFE) || ((unsigned char)s[0] == 0xFE && (unsigned char)s[1] == 0xFF)))
        return fail("XML: UTF-16 is not supported (re-save as UTF-8)");
    std::vector<int> stack;
    bool haveRoot = false, rootClosed = false;
    std::size_t textBytes = 0;
    {   // room for every element at once (the array grew by doubling, holding old and new copies while it moved): count
        // the '<' that can open one; comments and CDATA make this a little high, never more than the element cap
        std::size_t starts = 0;
        for (const char* c = s.data() + p, *e = s.data() + n; (c = (const char*)std::memchr(c, '<', (std::size_t)(e - c))) != nullptr; ++c)
            if (c + 1 < e && c[1] != '/' && c[1] != '!' && c[1] != '?') ++starts;
        x.n.reserve(std::min(starts, lim.maxElements));
    }
    std::vector<std::pair<std::string, std::string>> attrs;   // one element's attributes, then moved in at their exact size
    while (p < n) {
        if (s[p] != '<') {
            std::size_t e = s.find('<', p);
            if (e == std::string::npos) e = n;
            if (stack.empty()) {   // outside the root only white space is allowed
                for (std::size_t k = p; k < e; ++k) if (!IsWs(s[k])) return fail("XML: text outside the root element");
            } else {
                textBytes += e - p;
                if (textBytes > lim.maxTextBytes) return fail("XML: too much character data");
                if (!Decode(x.n[(std::size_t)stack.back()].text, s, p, e)) return fail("XML: unknown or malformed entity reference");
            }
            p = e;
            continue;
        }
        if (s.compare(p, 4, "<!--") == 0) {
            const std::size_t e = s.find("-->", p + 4);
            if (e == std::string::npos) return fail("XML: unterminated comment");
            p = e + 3; continue;
        }
        if (s.compare(p, 9, "<![CDATA[") == 0) {
            const std::size_t e = s.find("]]>", p + 9);
            if (e == std::string::npos) return fail("XML: unterminated CDATA");
            if (stack.empty()) return fail("XML: CDATA outside the root element");
            textBytes += e - p - 9;
            if (textBytes > lim.maxTextBytes) return fail("XML: too much character data");
            x.n[(std::size_t)stack.back()].text.append(s, p + 9, e - p - 9);
            p = e + 3; continue;
        }
        if (s.compare(p, 2, "<?") == 0) {
            const std::size_t e = s.find("?>", p + 2);
            if (e == std::string::npos) return fail("XML: unterminated processing instruction");
            p = e + 2; continue;
        }
        if (s.compare(p, 2, "<!") == 0) {   // DOCTYPE: skipped, never expanded
            if (haveRoot) return fail("XML: unexpected declaration inside the document");
            std::size_t q = p + 2;
            int depth = 0;
            char quote = 0;
            for (; q < n; ++q) {
                const char c = s[q];
                if (quote) { if (c == quote) quote = 0; continue; }
                if (c == '"' || c == '\'') quote = c;
                else if (c == '[') ++depth;
                else if (c == ']') --depth;
                else if (c == '>' && depth <= 0) break;
                else if (c == '<' && s.compare(q, 9, "<!ENTITY ") == 0) return fail("XML: DTD entity declarations are not supported");
                else if (c == '<' && s.compare(q, 4, "<!--") == 0) { const std::size_t e = s.find("-->", q + 4); if (e == std::string::npos) return fail("XML: unterminated comment"); q = e + 2; }
            }
            if (q >= n) return fail("XML: unterminated DOCTYPE");
            p = q + 1; continue;
        }
        if (s.compare(p, 2, "</") == 0) {
            std::size_t q = p + 2;
            const std::size_t b = q;
            while (q < n && !IsWs(s[q]) && s[q] != '>') ++q;
            if (q - b > lim.maxNameBytes) return fail("XML: name too long");
            const std::string nm = StripPrefix(s.substr(b, q - b));
            while (q < n && IsWs(s[q])) ++q;
            if (q >= n || s[q] != '>') return fail("XML: malformed end tag");
            if (stack.empty() || x.n[(std::size_t)stack.back()].name != nm) return fail("XML: mismatched end tag");
            stack.pop_back();
            if (stack.empty()) rootClosed = true;
            p = q + 1; continue;
        }
        // start tag
        if (rootClosed) return fail("XML: more than one root element");
        std::size_t q = p + 1;
        const std::size_t b = q;
        while (q < n && !IsWs(s[q]) && s[q] != '/' && s[q] != '>') ++q;
        if (q == b) return fail("XML: empty tag name");
        if (q - b > lim.maxNameBytes) return fail("XML: name too long");
        if (x.n.size() >= lim.maxElements) return fail("XML: too many elements");
        if ((int)stack.size() >= lim.maxDepth) return fail("XML: nesting too deep");
        Node node;
        node.name = StripPrefix(s.substr(b, q - b));
        bool selfClose = false;
        attrs.clear();
        for (;;) {
            while (q < n && IsWs(s[q])) ++q;
            if (q >= n) return fail("XML: unterminated tag");
            if (s[q] == '>') { ++q; break; }
            if (s[q] == '/') {
                if (q + 1 >= n || s[q + 1] != '>') return fail("XML: malformed tag");
                selfClose = true; q += 2; break;
            }
            const std::size_t ab = q;
            while (q < n && !IsWs(s[q]) && s[q] != '=' && s[q] != '>' && s[q] != '/') ++q;
            if (q == ab) return fail("XML: malformed attribute");
            if (q - ab > lim.maxNameBytes) return fail("XML: name too long");
            const std::string an = s.substr(ab, q - ab);
            while (q < n && IsWs(s[q])) ++q;
            if (q >= n || s[q] != '=') return fail("XML: attribute without a value");
            ++q;
            while (q < n && IsWs(s[q])) ++q;
            if (q >= n || (s[q] != '"' && s[q] != '\'')) return fail("XML: attribute value not quoted");
            const char quote = s[q++];
            const std::size_t ve = s.find(quote, q);
            if (ve == std::string::npos) return fail("XML: unterminated attribute value");
            if (std::memchr(s.data() + q, '<', ve - q) != nullptr) return fail("XML: '<' in an attribute value");
            if ((int)attrs.size() >= lim.maxAttrs) return fail("XML: too many attributes on one element");
            std::string av;
            if (!Decode(av, s, q, ve)) return fail("XML: unknown or malformed entity reference");
            const std::string key = StripPrefix(an);
            if (an != "xmlns" && an.compare(0, 6, "xmlns:") != 0) {
                attrs.emplace_back(key, std::move(av));
            }
            q = ve + 1;
        }
        if (!attrs.empty()) {
            node.attrs.reserve(attrs.size());
            for (auto& a : attrs) node.attrs.push_back(std::move(a));
        }
        const int idx = (int)x.n.size();
        x.n.push_back(std::move(node));
        if (!stack.empty()) x.n[(std::size_t)stack.back()].kids.push_back(idx);
        else haveRoot = true;
        if (!selfClose) stack.push_back(idx); else if (stack.empty()) rootClosed = true;
        p = q;
    }
    if (!stack.empty()) return fail("XML: unclosed element");
    if (!haveRoot) return fail("XML: no root element");
    return true;
}


// The same reader with a compact result, for big files whose elements are many and small (OGRE XML meshes): element
// and attribute names and values are kept as offsets into the source text (which must outlive the FlatDoc), children as
// a first-child / next-sibling chain, and character data is checked but not kept. It accepts and refuses exactly the
// files Parse does, with the same messages; only an attribute value with an entity reference is decoded and stored.
struct FlatDoc {
    struct N { std::uint32_t nameOff = 0, nameLen = 0, attr0 = 0, attrN = 0; int first = -1, last = -1, next = -1; };
    struct A { std::uint32_t keyOff = 0, keyLen = 0, valOff = 0, valLen = 0; int dec = -1; };   // dec: index into `decoded`
    const std::string* src = nullptr;
    std::vector<N> n;   // n[0] is the root element
    std::vector<A> a;
    std::vector<std::string> decoded;
    std::string_view Name(int i) const { const N& x = n[(std::size_t)i]; return std::string_view(src->data() + x.nameOff, x.nameLen); }
    // the first attribute `key` of element i (as Doc::Attr), false when there is none
    bool Attr(int i, std::string_view key, std::string_view& val) const {
        if (i < 0 || (std::size_t)i >= n.size()) return false;
        const N& x = n[(std::size_t)i];
        for (std::uint32_t k = x.attr0; k < x.attr0 + x.attrN; ++k) {
            const A& at = a[k];
            if (std::string_view(src->data() + at.keyOff, at.keyLen) != key) continue;
            val = at.dec >= 0 ? std::string_view(decoded[(std::size_t)at.dec]) : std::string_view(src->data() + at.valOff, at.valLen);
            return true;
        }
        return false;
    }
    std::string AttrOr(int i, std::string_view key, const char* dflt = "") const { std::string_view v; return Attr(i, key, v) ? std::string(v) : std::string(dflt); }
    int First(int i, std::string_view name) const {
        if (i < 0) return -1;
        for (int k = n[(std::size_t)i].first; k >= 0; k = n[(std::size_t)k].next) if (Name(k) == name) return k;
        return -1;
    }
    int Kid(int i) const { return n[(std::size_t)i].first; }
    int Next(int i) const { return n[(std::size_t)i].next; }
};

inline bool ParseFlat(const std::string& s, FlatDoc& x, std::string& err, const Limits& lim = Limits()) {
    x.n.clear(); x.a.clear(); x.decoded.clear();
    x.src = &s;
    auto fail = [&](const char* m) { err = m; x.n.clear(); x.a.clear(); x.decoded.clear(); return false; };
    if (s.size() > lim.maxBytes) return fail("XML: file too large");
    if (s.size() >= 0xFFFFFFFFu) return fail("XML: file too large");   // offsets are 32-bit
    const std::size_t n = s.size();
    std::size_t p = 0;
    if (n >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) p = 3;
    else if (n >= 2 && (((unsigned char)s[0] == 0xFF && (unsigned char)s[1] == 0xFE) || ((unsigned char)s[0] == 0xFE && (unsigned char)s[1] == 0xFF)))
        return fail("XML: UTF-16 is not supported (re-save as UTF-8)");
    std::vector<int> stack;
    bool haveRoot = false, rootClosed = false;
    std::size_t textBytes = 0;
    std::string scratch;   // character data is decoded here to check it, then dropped
    {   // room for every element at once (see Parse)
        std::size_t starts = 0;
        for (const char* c = s.data() + p, *e = s.data() + n; (c = (const char*)std::memchr(c, '<', (std::size_t)(e - c))) != nullptr; ++c)
            if (c + 1 < e && c[1] != '/' && c[1] != '!' && c[1] != '?') ++starts;
        x.n.reserve(std::min(starts, lim.maxElements));
    }
    // a name with its namespace prefix dropped, as StripPrefix does
    auto stripped = [&](std::size_t b, std::size_t e, std::uint32_t& off, std::uint32_t& len) {
        const void* c = std::memchr(s.data() + b, ':', e - b);
        if (c) b = (std::size_t)(static_cast<const char*>(c) - s.data()) + 1;
        off = (std::uint32_t)b; len = (std::uint32_t)(e - b);
    };
    while (p < n) {
        if (s[p] != '<') {
            const void* hit = std::memchr(s.data() + p, '<', n - p);
            const std::size_t e = hit ? (std::size_t)(static_cast<const char*>(hit) - s.data()) : n;
            if (stack.empty()) {
                for (std::size_t k = p; k < e; ++k) if (!IsWs(s[k])) return fail("XML: text outside the root element");
            } else {
                textBytes += e - p;
                if (textBytes > lim.maxTextBytes) return fail("XML: too much character data");
                if (std::memchr(s.data() + p, '&', e - p)) { scratch.clear(); if (!Decode(scratch, s, p, e)) return fail("XML: unknown or malformed entity reference"); }
            }
            p = e;
            continue;
        }
        if (s.compare(p, 4, "<!--") == 0) {
            const std::size_t e = s.find("-->", p + 4);
            if (e == std::string::npos) return fail("XML: unterminated comment");
            p = e + 3; continue;
        }
        if (s.compare(p, 9, "<![CDATA[") == 0) {
            const std::size_t e = s.find("]]>", p + 9);
            if (e == std::string::npos) return fail("XML: unterminated CDATA");
            if (stack.empty()) return fail("XML: CDATA outside the root element");
            textBytes += e - p - 9;
            if (textBytes > lim.maxTextBytes) return fail("XML: too much character data");
            p = e + 3; continue;
        }
        if (s.compare(p, 2, "<?") == 0) {
            const std::size_t e = s.find("?>", p + 2);
            if (e == std::string::npos) return fail("XML: unterminated processing instruction");
            p = e + 2; continue;
        }
        if (s.compare(p, 2, "<!") == 0) {   // DOCTYPE: skipped, never expanded
            if (haveRoot) return fail("XML: unexpected declaration inside the document");
            std::size_t q = p + 2;
            int depth = 0;
            char quote = 0;
            for (; q < n; ++q) {
                const char c = s[q];
                if (quote) { if (c == quote) quote = 0; continue; }
                if (c == '"' || c == '\'') quote = c;
                else if (c == '[') ++depth;
                else if (c == ']') --depth;
                else if (c == '>' && depth <= 0) break;
                else if (c == '<' && s.compare(q, 9, "<!ENTITY ") == 0) return fail("XML: DTD entity declarations are not supported");
                else if (c == '<' && s.compare(q, 4, "<!--") == 0) { const std::size_t e = s.find("-->", q + 4); if (e == std::string::npos) return fail("XML: unterminated comment"); q = e + 2; }
            }
            if (q >= n) return fail("XML: unterminated DOCTYPE");
            p = q + 1; continue;
        }
        if (s.compare(p, 2, "</") == 0) {
            std::size_t q = p + 2;
            const std::size_t b = q;
            while (q < n && !IsWs(s[q]) && s[q] != '>') ++q;
            if (q - b > lim.maxNameBytes) return fail("XML: name too long");
            std::uint32_t off, len;
            stripped(b, q, off, len);
            while (q < n && IsWs(s[q])) ++q;
            if (q >= n || s[q] != '>') return fail("XML: malformed end tag");
            if (stack.empty() || x.Name(stack.back()) != std::string_view(s.data() + off, len)) return fail("XML: mismatched end tag");
            stack.pop_back();
            if (stack.empty()) rootClosed = true;
            p = q + 1; continue;
        }
        // start tag
        if (rootClosed) return fail("XML: more than one root element");
        std::size_t q = p + 1;
        const std::size_t b = q;
        while (q < n && !IsWs(s[q]) && s[q] != '/' && s[q] != '>') ++q;
        if (q == b) return fail("XML: empty tag name");
        if (q - b > lim.maxNameBytes) return fail("XML: name too long");
        if (x.n.size() >= lim.maxElements) return fail("XML: too many elements");
        if ((int)stack.size() >= lim.maxDepth) return fail("XML: nesting too deep");
        FlatDoc::N node;
        stripped(b, q, node.nameOff, node.nameLen);
        node.attr0 = (std::uint32_t)x.a.size();
        bool selfClose = false;
        int count = 0;
        for (;;) {
            while (q < n && IsWs(s[q])) ++q;
            if (q >= n) return fail("XML: unterminated tag");
            if (s[q] == '>') { ++q; break; }
            if (s[q] == '/') {
                if (q + 1 >= n || s[q + 1] != '>') return fail("XML: malformed tag");
                selfClose = true; q += 2; break;
            }
            const std::size_t ab = q;
            while (q < n && !IsWs(s[q]) && s[q] != '=' && s[q] != '>' && s[q] != '/') ++q;
            if (q == ab) return fail("XML: malformed attribute");
            if (q - ab > lim.maxNameBytes) return fail("XML: name too long");
            const std::size_t ae = q;
            while (q < n && IsWs(s[q])) ++q;
            if (q >= n || s[q] != '=') return fail("XML: attribute without a value");
            ++q;
            while (q < n && IsWs(s[q])) ++q;
            if (q >= n || (s[q] != '"' && s[q] != '\'')) return fail("XML: attribute value not quoted");
            const char quote = s[q++];
            const void* qe = std::memchr(s.data() + q, quote, n - q);
            if (!qe) return fail("XML: unterminated attribute value");
            const std::size_t ve = (std::size_t)(static_cast<const char*>(qe) - s.data());
            if (std::memchr(s.data() + q, '<', ve - q) != nullptr) return fail("XML: '<' in an attribute value");
            if (count >= lim.maxAttrs) return fail("XML: too many attributes on one element");
            FlatDoc::A at;
            if (std::memchr(s.data() + q, '&', ve - q)) {
                std::string av;
                if (!Decode(av, s, q, ve)) return fail("XML: unknown or malformed entity reference");
                at.dec = (int)x.decoded.size();
                x.decoded.push_back(std::move(av));
            }
            const std::string_view an(s.data() + ab, ae - ab);
            if (an != "xmlns" && an.compare(0, 6, "xmlns:") != 0) {
                ++count;   // as Parse: only the attributes kept count towards the cap
                stripped(ab, ae, at.keyOff, at.keyLen);
                at.valOff = (std::uint32_t)q; at.valLen = (std::uint32_t)(ve - q);
                x.a.push_back(at);
            } else if (at.dec >= 0) x.decoded.pop_back();
            q = ve + 1;
        }
        node.attrN = (std::uint32_t)x.a.size() - node.attr0;
        const int idx = (int)x.n.size();
        x.n.push_back(node);
        if (!stack.empty()) {
            FlatDoc::N& par = x.n[(std::size_t)stack.back()];
            if (par.last >= 0) x.n[(std::size_t)par.last].next = idx; else par.first = idx;
            par.last = idx;
        } else haveRoot = true;
        if (!selfClose) stack.push_back(idx); else if (stack.empty()) rootClosed = true;
        p = q;
    }
    if (!stack.empty()) return fail("XML: unclosed element");
    if (!haveRoot) return fail("XML: no root element");
    return true;
}

} // namespace xml
} // namespace detail
} // namespace okay
