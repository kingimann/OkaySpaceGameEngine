// OkayUI plots with the Dear ImGui options: PlotLines and PlotHistogram over an array or a getter, a ring-buffer start, an overlay text, a
// size, a hover that highlights the point or bar under the pointer and names it in a tooltip. Built on the public OkayUI calls only.
// The plain PlotLines / PlotHistogram (scale and height only) stay in OkayUI.cpp. See docs/own_ui_toolkit.md section 4.34.
#include "okay/UI/OkayUI.hpp"

#include <algorithm>
#include <cmath>

namespace OkayUI {

namespace {

Vec2 V(float x, float y) { return Vec2{x, y}; }

struct Source {
    const float* values; float (*getter)(void*, int); void* data; int count, offset;
    // the i-th value shown (the ring starts at `offset`); the index into the data it came from goes to *index
    float At(int i, int* index = nullptr) const {
        long long k = ((long long)i + offset) % count; if (k < 0) k += count;
        if (index) *index = (int)k;
        return getter ? getter(data, (int)k) : values[k];
    }
};

int PlotImpl(const char* label, const Source& src, const PlotOptions& opt, bool histogram) {
    if (src.count <= 0 || (!src.values && !src.getter)) return -1;
    const int count = src.count;
    const char* name = label ? label : "";
    PushID(name);
    const float lw = CalcTextSize(name).x > 0.0f ? CalcTextSize(name).x + 8.0f : 0.0f;
    const float availW = GetContentRegionAvail().x;
    const float pw = std::max(24.0f, (opt.size.x > 0.0f && std::isfinite(opt.size.x) ? opt.size.x : availW - lw));
    const float ph = opt.size.y > 0.0f && std::isfinite(opt.size.y) ? opt.size.y : GetFrameHeight() * 2.4f;
    InvisibleButton("plot", pw, ph);
    const Rect r = GetItemRect();
    const bool hovered = IsItemHovered();
    PopID();

    // the range: given, or from the finite values
    float lo = opt.scaleMin, hi = opt.scaleMax;
    if (!(std::isfinite(lo) && std::isfinite(hi) && lo < hi)) {
        lo = 1e30f; hi = -1e30f;
        for (int i = 0; i < count; ++i) { const float v = src.At(i); if (std::isfinite(v)) { lo = std::min(lo, v); hi = std::max(hi, v); } }
        if (lo > hi) { lo = 0.0f; hi = 1.0f; }
        if (hi - lo < 1e-4f) hi = lo + 1.0f;
    }
    const float span = hi - lo;
    auto norm = [&](float v) { return std::max(0.0f, std::min(1.0f, (v - lo) / span)); };

    const DrawList& dl = GetWindowDrawList();
    if (lw > 0.0f) dl.AddText(V(r.x + r.w + 8.0f, r.y + (r.h - CalcTextSize(name).y) * 0.5f), GetColorU32(Col_Text), name);   // the label, to the right
    dl.AddRectFilled(V(r.x, r.y), V(r.x + r.w, r.y + r.h), GetColorU32(Col_Track));
    const float ix = r.x + 2.0f, iw = r.w - 4.0f, iy = r.y + 2.0f, ih = r.h - 4.0f;
    int hot = -1;
    if (hovered && iw > 0.0f) {
        const float mx = GetMousePos().x - ix;
        if (histogram) hot = (int)std::floor(mx / (iw / (float)count));
        else hot = count > 1 ? (int)std::floor(mx / (iw / (float)(count - 1)) + 0.5f) : 0;
        hot = std::max(0, std::min(count - 1, hot));
    }
    const Color32 normal = GetColorU32(histogram ? Col_PlotHistogram : Col_PlotLines);
    const Color32 hover = GetColorU32(histogram ? Col_PlotHistogramHovered : Col_PlotLinesHovered);
    const int stride = std::max(1, count / 4096);               // a huge array is drawn from every n-th value
    if (histogram) {
        const float bw = iw / (float)count;
        for (int i = 0; i < count; i += stride) {
            const float v = src.At(i);
            if (!std::isfinite(v)) continue;
            const float bh = norm(v) * ih;
            const float x0 = ix + bw * (float)i;
            dl.AddRectFilled(V(x0 + (bw > 3.0f ? 1.0f : 0.0f), iy + ih - bh), V(x0 + std::max(1.0f, bw - (bw > 3.0f ? 1.0f : 0.0f)), iy + ih), i == hot ? hover : normal);
        }
        if (hot >= 0 && hot % stride != 0) {       // the hovered bar even when the drawing skipped it
            const float v = src.At(hot);
            if (std::isfinite(v)) { const float bw2 = iw / (float)count, x0 = ix + bw2 * (float)hot; dl.AddRectFilled(V(x0, iy + ih - norm(v) * ih), V(x0 + std::max(1.0f, bw2), iy + ih), hover); }
        }
    } else {
        const float step = count > 1 ? iw / (float)(count - 1) : 0.0f;
        auto Y = [&](float v) { return iy + ih - norm(v) * ih; };
        for (int i = 0; i + 1 < count; i += stride) {
            const int j = std::min(count - 1, i + stride);
            const float a = src.At(i), b = src.At(j);
            if (!std::isfinite(a) || !std::isfinite(b)) continue;
            const bool on = hot >= 0 && (hot == i || hot == j);
            dl.AddLine(V(ix + step * (float)i, Y(a)), V(ix + step * (float)j, Y(b)), on ? hover : normal, 1.5f);
        }
        if (hot >= 0) {                              // a dot on the hovered point
            const float v = src.At(hot);
            if (std::isfinite(v)) dl.AddCircleFilled(V(ix + step * (float)hot, Y(v)), 3.0f, hover);
        }
    }
    if (opt.overlay && opt.overlay[0]) {
        const Vec2 ts = CalcTextSize(opt.overlay);
        dl.AddText(V(r.x + (r.w - ts.x) * 0.5f, r.y + 2.0f), GetColorU32(Col_Text), opt.overlay);
    }
    int hovered_index = -1;
    if (hot >= 0) {
        const float v = src.At(hot, &hovered_index);
        if (std::isfinite(v)) SetTooltip("%d: %.4g", hovered_index, (double)v);
        else SetTooltip("%d: -", hovered_index);
    }
    return hovered_index;
}

} // namespace

int PlotLines(const char* label, const float* values, int count, const PlotOptions& o) {
    return PlotImpl(label, Source{values, nullptr, nullptr, count, o.valuesOffset}, o, false);
}
int PlotLines(const char* label, float (*getter)(void*, int), void* data, int count, const PlotOptions& o) {
    return PlotImpl(label, Source{nullptr, getter, data, count, o.valuesOffset}, o, false);
}
int PlotHistogram(const char* label, const float* values, int count, const PlotOptions& o) {
    return PlotImpl(label, Source{values, nullptr, nullptr, count, o.valuesOffset}, o, true);
}
int PlotHistogram(const char* label, float (*getter)(void*, int), void* data, int count, const PlotOptions& o) {
    return PlotImpl(label, Source{nullptr, getter, data, count, o.valuesOffset}, o, true);
}

} // namespace OkayUI
