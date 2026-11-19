#pragma once
// The mapping layer for code ported from Dear ImGui: vector and colour types and flag enums, converted without this header
// knowing anything about ImGui. Nothing here includes ImGui or SDL.
//
//   * Vector / colour types: any type with public x, y (and z, w) members converts, so a host that has ImVec2 / ImVec4 writes
//     `OkayUI::FromVec4(col)` and `OkayUI::ToVec4<ImVec4>(v)` with no adapter class.
//   * Flags and enums: the host lists its pairs ({ImGuiWindowFlags_NoTitleBar, OkayUI::Window_NoTitleBar}, ...) in a FlagMap table
//     (the host names its own constants; this header has no copy of any other library's values) and calls MapFlags / MapEnum.
//     A flag with no pair is reported, not silently dropped, so a port can see what OkayUI cannot do.
// The ready-made ImGui tables are in shared/OkayUIImGuiMap.hpp (host side, includes imgui.h).
#include "okay/UI/OkayUI.hpp"

namespace OkayUI {

/// Any {x, y} / {x, y, z, w} struct to OkayUI's types and back. A missing component is 0 (or 1 for w when making a Vec4 from three).
template <class V> inline Vec2 FromVec2(const V& v) { Vec2 r; r.x = (float)v.x; r.y = (float)v.y; return r; }
template <class V> inline Vec4 FromVec4(const V& v) { Vec4 r; r.x = (float)v.x; r.y = (float)v.y; r.z = (float)v.z; r.w = (float)v.w; return r; }
template <class V> inline V ToVec2(Vec2 v) { V r; r.x = v.x; r.y = v.y; return r; }
template <class V> inline V ToVec4(Vec4 v) { V r; r.x = v.x; r.y = v.y; r.z = v.z; r.w = v.w; return r; }
/// A float colour (0..1 per channel) as OkayUI's packed colour; non-finite channels count as 0, out of range ones are clamped.
template <class V> inline Color32 FromColor(const V& v) { return ColorConvertFloat4ToU32(FromVec4(v)); }
/// A packed colour back to a float colour struct.
template <class V> inline V ToColor(Color32 c) { return ToVec4<V>(ColorConvertU32ToFloat4(c)); }

/// One pair in a flag or enum table: the host's value and OkayUI's.
struct FlagMap { int from; int to; };

/// OR together the `to` of every pair whose `from` bits are all set in `flags` (a pair with from == 0 is skipped). Bits no pair
/// consumed are returned in *unmapped when it is not null, so the caller can log "flag not supported".
inline int MapFlags(int flags, const FlagMap* table, int count, int* unmapped = nullptr) {
    int out = 0, used = 0;
    if (table) for (int i = 0; i < count; ++i) {
        const int f = table[i].from;
        if (f != 0 && (flags & f) == f) { out |= table[i].to; used |= f; }
    }
    if (unmapped) *unmapped = flags & ~used;
    return out;
}

/// Exact lookup of an enum value; `fallback` when no pair has it.
inline int MapEnum(int value, const FlagMap* table, int count, int fallback = -1) {
    if (table) for (int i = 0; i < count; ++i) if (table[i].from == value) return table[i].to;
    return fallback;
}

} // namespace OkayUI
