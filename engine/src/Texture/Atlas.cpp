// okay::tex atlas packer: MaxRects with the best-short-side-fit rule, written from the paper named in Atlas.hpp.
#include "okay/Texture/Atlas.hpp"
#include <algorithm>
#include <cstdio>

namespace okay::tex {

namespace {

struct Rect { int x, y, w, h; };

class MaxRects {
public:
    MaxRects(int w, int h) { free_.push_back({0, 0, w, h}); }
    // Finds the best spot; returns false when nothing fits. Does not modify state.
    bool Find(int w, int h, bool rotate, Rect* out, bool* rotated, int* score1, int* score2) const {
        bool found = false;
        int bs1 = 0, bs2 = 0;
        for (const Rect& f : free_) {
            for (int o = 0; o < (rotate ? 2 : 1); ++o) {
                int rw = o ? h : w, rh = o ? w : h;
                if (rw > f.w || rh > f.h) continue;
                int lw = f.w - rw, lh = f.h - rh;
                int s1 = std::min(lw, lh), s2 = std::max(lw, lh);
                if (!found || s1 < bs1 || (s1 == bs1 && s2 < bs2)) {
                    found = true; bs1 = s1; bs2 = s2;
                    *out = {f.x, f.y, rw, rh}; *rotated = o == 1;
                }
            }
        }
        if (found) { *score1 = bs1; *score2 = bs2; }
        return found;
    }
    void Place(const Rect& p) {
        std::vector<Rect> next;
        for (const Rect& f : free_) {
            if (p.x >= f.x + f.w || p.x + p.w <= f.x || p.y >= f.y + f.h || p.y + p.h <= f.y) { next.push_back(f); continue; }
            if (p.x > f.x) next.push_back({f.x, f.y, p.x - f.x, f.h});
            if (p.x + p.w < f.x + f.w) next.push_back({p.x + p.w, f.y, f.x + f.w - (p.x + p.w), f.h});
            if (p.y > f.y) next.push_back({f.x, f.y, f.w, p.y - f.y});
            if (p.y + p.h < f.y + f.h) next.push_back({f.x, p.y + p.h, f.w, f.y + f.h - (p.y + p.h)});
        }
        // remove free rectangles contained in another (keep the first of identical ones)
        std::vector<Rect> kept;
        for (std::size_t i = 0; i < next.size(); ++i) {
            bool inside = false;
            for (std::size_t j = 0; j < next.size() && !inside; ++j) {
                if (i == j) continue;
                const Rect &a = next[i], &b = next[j];
                bool in = a.x >= b.x && a.y >= b.y && a.x + a.w <= b.x + b.w && a.y + a.h <= b.y + b.h;
                if (in && !(a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h && i < j)) inside = true;
            }
            if (!inside) kept.push_back(next[i]);
        }
        free_.swap(kept);
    }
private:
    std::vector<Rect> free_;
};

struct Job { int index; int cw, ch; };

// Try to pack all jobs (already sorted) into a bin; fills pos/rot (indexed like jobs). True when everything fit.
bool TryPack(const std::vector<Job>& jobs, int binW, int binH, bool rotate, std::vector<Rect>* pos, std::vector<char>* rot) {
    MaxRects mr(binW, binH);
    pos->assign(jobs.size(), Rect{0, 0, 0, 0});
    rot->assign(jobs.size(), 0);
    for (std::size_t i = 0; i < jobs.size(); ++i) {
        Rect r; bool rt; int s1, s2;
        if (!mr.Find(jobs[i].cw, jobs[i].ch, rotate, &r, &rt, &s1, &s2)) return false;
        mr.Place(r);
        (*pos)[i] = r; (*rot)[i] = rt ? 1 : 0;
    }
    return true;
}

} // namespace

AtlasResult PackAtlas(const std::vector<AtlasItem>& items, const AtlasOptions& optIn) {
    AtlasResult res;
    AtlasOptions opt = optIn;
    opt.padding = std::max(0, opt.padding);
    opt.bleed = std::max(0, opt.bleed);
    const int margin = 2 * opt.bleed + opt.padding;
    // the bin is one padding larger so the last sprite needs no trailing gap
    const int limW = opt.maxWidth + opt.padding, limH = opt.maxHeight + opt.padding;

    std::vector<Job> jobs;
    long long totalArea = 0;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const AtlasItem& it = items[i];
        if (it.w <= 0 || it.h <= 0 || it.w > 1000000 || it.h > 1000000) { res.failed.push_back(int(i)); continue; }
        long long cw = (long long)it.w + margin, ch = (long long)it.h + margin;
        bool fits = (cw <= limW && ch <= limH) || (opt.allowRotate && ch <= limW && cw <= limH);
        if (!fits) { res.failed.push_back(int(i)); continue; }
        jobs.push_back({int(i), int(cw), int(ch)});
        totalArea += cw * ch;
    }
    std::sort(jobs.begin(), jobs.end(), [&](const Job& a, const Job& b) {
        int ma = std::max(a.cw, a.ch), mb = std::max(b.cw, b.ch);
        if (ma != mb) return ma > mb;
        long long aa = (long long)a.cw * a.ch, ab = (long long)b.cw * b.ch;
        if (aa != ab) return aa > ab;
        if (items[a.index].name != items[b.index].name) return items[a.index].name < items[b.index].name;
        return a.index < b.index;
    });

    std::vector<Rect> pos; std::vector<char> rot;
    int sheetW = 0, sheetH = 0;
    bool packed = jobs.empty();
    if (!jobs.empty()) {
        if (opt.powerOfTwo) {
            // candidate sheets: every power-of-two pair within the limit, smallest area first, squarer first
            std::vector<std::pair<int, int>> cands;
            for (int w = 1; w <= opt.maxWidth; w <<= 1)
                for (int h = 1; h <= opt.maxHeight; h <<= 1)
                    if ((long long)w * h >= totalArea) cands.push_back({w, h});
            std::sort(cands.begin(), cands.end(), [](auto& a, auto& b) {
                long long aa = (long long)a.first * a.second, ab = (long long)b.first * b.second;
                if (aa != ab) return aa < ab;
                int da = std::abs(a.first - a.second), db = std::abs(b.first - b.second);
                if (da != db) return da < db;
                return a.first < b.first;
            });
            for (auto& c : cands)
                if (TryPack(jobs, c.first + opt.padding, c.second + opt.padding, opt.allowRotate, &pos, &rot)) {
                    sheetW = c.first; sheetH = c.second; packed = true; break;
                }
        } else if (TryPack(jobs, limW, limH, opt.allowRotate, &pos, &rot)) {
            for (std::size_t i = 0; i < jobs.size(); ++i) {
                int rw = pos[i].x + pos[i].w - opt.padding, rh = pos[i].y + pos[i].h - opt.padding;
                sheetW = std::max(sheetW, rw); sheetH = std::max(sheetH, rh);
            }
            packed = true;
        }
    }
    if (packed && !jobs.empty()) {
        std::vector<AtlasPlacement> out;
        long long spriteArea = 0;
        for (std::size_t i = 0; i < jobs.size(); ++i) {
            const AtlasItem& it = items[jobs[i].index];
            AtlasPlacement p;
            p.item = jobs[i].index; p.name = it.name;
            p.x = pos[i].x + opt.bleed; p.y = pos[i].y + opt.bleed;
            p.rotated = rot[i] != 0;
            p.w = p.rotated ? it.h : it.w; p.h = p.rotated ? it.w : it.h;
            spriteArea += (long long)it.w * it.h;
            out.push_back(p);
        }
        std::sort(out.begin(), out.end(), [](const AtlasPlacement& a, const AtlasPlacement& b) { return a.item < b.item; });
        res.placements.swap(out);
        res.width = sheetW; res.height = sheetH;
        res.occupancy = double(spriteArea) / (double(sheetW) * double(sheetH));
        res.ok = res.failed.empty();
    } else if (jobs.empty()) {
        res.ok = res.failed.empty();
    } else {
        // nothing fit as a whole: report every valid item as failed, place none
        for (const Job& j : jobs) res.failed.push_back(j.index);
        std::sort(res.failed.begin(), res.failed.end());
        res.ok = false;
    }
    return res;
}

std::vector<std::uint8_t> ComposeAtlasRGBA8(const AtlasResult& r, const std::vector<AtlasImage>& images, int bleed) {
    std::vector<std::uint8_t> sheet;
    if (r.width <= 0 || r.height <= 0) return sheet;
    bleed = std::max(0, bleed);
    sheet.assign(std::size_t(r.width) * r.height * 4, 0);
    for (const AtlasPlacement& p : r.placements) {
        if (p.item < 0 || p.item >= int(images.size())) return {};
        const AtlasImage& im = images[p.item];
        if (!im.rgba || im.w <= 0 || im.h <= 0) return {};
        if (!(p.rotated ? (im.w == p.h && im.h == p.w) : (im.w == p.w && im.h == p.h))) return {};
        // stored pixel (u, v) of the sprite as laid out on the sheet
        auto stored = [&](int u, int v) -> const std::uint8_t* {
            u = std::min(std::max(u, 0), p.w - 1); v = std::min(std::max(v, 0), p.h - 1);
            if (!p.rotated) return im.rgba + (std::size_t(v) * im.w + u) * 4;
            // clockwise quarter turn: stored (u, v) came from source (v, im.h - 1 - u)
            return im.rgba + (std::size_t(im.h - 1 - u) * im.w + v) * 4;
        };
        for (int v = -bleed; v < p.h + bleed; ++v)
            for (int u = -bleed; u < p.w + bleed; ++u) {
                int x = p.x + u, y = p.y + v;
                if (x < 0 || y < 0 || x >= r.width || y >= r.height) continue;
                const std::uint8_t* s = stored(u, v);
                std::uint8_t* d = &sheet[(std::size_t(y) * r.width + x) * 4];
                d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
            }
    }
    return sheet;
}

static void JsonString(std::string& o, const std::string& s) {
    o += '"';
    for (unsigned char c : s) {
        if (c == '"') o += "\\\""; else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n"; else if (c == '\t') o += "\\t"; else if (c == '\r') o += "\\r";
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += char(c);
    }
    o += '"';
}

std::string AtlasToJson(const AtlasResult& r, const std::string& imageName, const AtlasOptions& opt) {
    std::vector<const AtlasPlacement*> order;
    for (const AtlasPlacement& p : r.placements) order.push_back(&p);
    std::sort(order.begin(), order.end(), [](const AtlasPlacement* a, const AtlasPlacement* b) {
        if (a->name != b->name) return a->name < b->name;
        return a->item < b->item;
    });
    std::string o = "{\n  \"image\": ";
    JsonString(o, imageName);
    o += ",\n  \"width\": " + std::to_string(r.width) + ",\n  \"height\": " + std::to_string(r.height);
    o += ",\n  \"padding\": " + std::to_string(opt.padding) + ",\n  \"bleed\": " + std::to_string(opt.bleed);
    o += ",\n  \"sprites\": [";
    for (std::size_t i = 0; i < order.size(); ++i) {
        const AtlasPlacement& p = *order[i];
        o += i ? ",\n    {" : "\n    {";
        o += "\"name\": "; JsonString(o, p.name);
        o += ", \"x\": " + std::to_string(p.x) + ", \"y\": " + std::to_string(p.y);
        o += ", \"w\": " + std::to_string(p.w) + ", \"h\": " + std::to_string(p.h);
        o += std::string(", \"rotated\": ") + (p.rotated ? "true" : "false") + "}";
    }
    o += order.empty() ? "]\n}\n" : "\n  ]\n}\n";
    return o;
}

} // namespace okay::tex
