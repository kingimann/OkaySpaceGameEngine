// Material graph: .okaymat graph text (header "okaymatgraph 1"), reader and writer.
// The reader treats its input as hostile: size, line and count limits, strict numbers, no throw.
#include "okay/Render/MaterialGraph.hpp"
#include <charconv>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace okay::matgraph {

namespace {

std::string Num(float f) {
    char b[48];
    std::snprintf(b, sizeof b, "%.9g", (double)f);
    return b;
}

std::string EncodeText(const std::string& s) {
    if (s.empty()) return "~";
    std::string o;
    for (unsigned char c : s) {
        bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                     c == '.' || c == '_' || c == '/' || c == '-' || c == ':';
        if (plain) o += (char)c;
        else { char b[4]; std::snprintf(b, sizeof b, "%%%02X", c); o += b; }
    }
    return o;
}

int Hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
bool DecodeText(const std::string& t, std::string& out) {
    out.clear();
    if (t == "~") return true;
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] == '%') {
            if (i + 2 >= t.size()) return false;
            int h = Hex(t[i + 1]), l = Hex(t[i + 2]);
            if (h < 0 || l < 0) return false;
            out += (char)(h * 16 + l);
            i += 2;
        } else out += t[i];
    }
    return out.size() <= kMaxTextLen;
}

bool ParseInt(const std::string& t, int& out) {
    if (t.empty() || t.size() > 11) return false;
    long long v = 0; auto r = std::from_chars(t.data(), t.data() + t.size(), v);
    if (r.ec != std::errc() || r.ptr != t.data() + t.size()) return false;
    if (v < 0 || v > 1000000000LL) return false;
    out = (int)v; return true;
}
bool ParseFloat(const std::string& t, float& out) {
    if (t.empty() || t.size() > 40) return false;
    float v = 0; auto r = std::from_chars(t.data(), t.data() + t.size(), v);
    if (r.ec != std::errc() || r.ptr != t.data() + t.size() || !std::isfinite(v)) return false;
    out = v; return true;
}

} // namespace

std::string MaterialGraph::ToText() const {
    std::ostringstream o;
    o << "okaymatgraph 1\n";
    for (const Node& n : nodes) {
        const char* kn = ((int)n.kind < (int)NodeKind::Count) ? GetNodeDef(n.kind).name : "Output";
        o << "node " << n.id << ' ' << kn << ' ' << Num(n.x) << ' ' << Num(n.y) << ' ' << Num(n.v[0]) << ' ' << Num(n.v[1])
          << ' ' << Num(n.v[2]) << ' ' << Num(n.v[3]) << ' ' << EncodeText(n.text) << '\n';
    }
    for (const Link& l : links) o << "link " << l.fromNode << ' ' << l.fromPin << ' ' << l.toNode << ' ' << l.toPin << '\n';
    return o.str();
}

bool MaterialGraph::LooksLikeGraph(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        size_t a = line.find_first_not_of(" \t\r");
        if (a == std::string::npos || line[a] == '#') continue;
        return line.compare(a, 12, "okaymatgraph") == 0;
    }
    return false;
}

MaterialGraph::ParseResult MaterialGraph::FromText(const std::string& text, MaterialGraph& out) {
    ParseResult r;
    MaterialGraph g;
    auto fail = [&](int line, const std::string& m) { r.ok = false; r.line = line; r.error = m; return r; };
    if (text.size() > kMaxFileBytes) return fail(0, "file is too big");
    size_t pos = 0; int lineNo = 0; bool header = false;
    while (pos <= text.size()) {
        size_t e = text.find('\n', pos);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(pos, e - pos);
        pos = e + 1; ++lineNo;
        if (line.size() > 4096) return fail(lineNo, "line is too long");
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::vector<std::string> tok;
        {
            std::istringstream ls(line); std::string t;
            while (ls >> t) tok.push_back(t);
        }
        if (tok.empty() || tok[0][0] == '#') continue;
        if (!header) {
            if (tok.size() != 2 || tok[0] != "okaymatgraph" || tok[1] != "1") return fail(lineNo, "expected header 'okaymatgraph 1'");
            header = true; continue;
        }
        if (tok[0] == "node") {
            if (tok.size() != 10) return fail(lineNo, "node needs: id kind x y v0 v1 v2 v3 text");
            if ((int)g.nodes.size() >= kMaxNodes) return fail(lineNo, "too many nodes");
            Node n;
            if (!ParseInt(tok[1], n.id)) return fail(lineNo, "bad node id");
            if (!NodeKindFromName(tok[2], n.kind)) return fail(lineNo, "unknown node kind '" + tok[2].substr(0, 40) + "'");
            if (!ParseFloat(tok[3], n.x) || !ParseFloat(tok[4], n.y)) return fail(lineNo, "bad position");
            for (int k = 0; k < 4; ++k) if (!ParseFloat(tok[5 + k], n.v[k])) return fail(lineNo, "bad number");
            if (!DecodeText(tok[9], n.text)) return fail(lineNo, "bad text");
            g.nodes.push_back(std::move(n));
        } else if (tok[0] == "link") {
            if (tok.size() != 5) return fail(lineNo, "link needs: fromNode fromPin toNode toPin");
            if ((int)g.links.size() >= kMaxLinks) return fail(lineNo, "too many links");
            Link l;
            if (!ParseInt(tok[1], l.fromNode) || !ParseInt(tok[2], l.fromPin) || !ParseInt(tok[3], l.toNode) || !ParseInt(tok[4], l.toPin))
                return fail(lineNo, "bad link number");
            g.links.push_back(l);
        } else return fail(lineNo, "unknown line '" + tok[0].substr(0, 40) + "'");
        if (e == text.size()) break;
    }
    if (!header) return fail(0, "empty file");
    out = std::move(g);
    r.ok = true;
    return r;
}

bool MaterialGraph::SaveToFile(const std::string& path) const {
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << ToText();
    return (bool)f;
}

MaterialGraph::ParseResult MaterialGraph::LoadFromFile(const std::string& path, MaterialGraph& out) {
    ParseResult r;
    std::ifstream f(path, std::ios::binary);
    if (!f) { r.error = "cannot open file"; return r; }
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return FromText(s, out);
}

bool SameGraph(const MaterialGraph& a, const MaterialGraph& b) {
    if (a.nodes.size() != b.nodes.size() || a.links.size() != b.links.size()) return false;
    for (size_t i = 0; i < a.nodes.size(); ++i) {
        const Node &x = a.nodes[i], &y = b.nodes[i];
        if (x.id != y.id || x.kind != y.kind || x.x != y.x || x.y != y.y || x.text != y.text) return false;
        for (int k = 0; k < 4; ++k) if (x.v[k] != y.v[k]) return false;
    }
    for (size_t i = 0; i < a.links.size(); ++i) {
        const Link &x = a.links[i], &y = b.links[i];
        if (x.fromNode != y.fromNode || x.fromPin != y.fromPin || x.toNode != y.toNode || x.toPin != y.toPin) return false;
    }
    return true;
}

} // namespace okay::matgraph
