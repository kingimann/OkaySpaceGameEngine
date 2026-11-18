#pragma once
// Fewer wires to draw: where a newly dropped block should hook itself into the flow, and how a block dropped on
// a wire splits it in two. Pure functions on plain numbers (no ImGui), so tests can run them.
// Written for this project from its own rules: a block joins the block just before it; a wire has two ends.
#include <cmath>
#include <vector>

namespace okay::editor::vsed {

struct VsLayoutNode {
    int   id = 0;
    float x = 0, y = 0, w = 200, h = 60;   // top-left corner and size on the canvas
    bool  hasFlowOut = false;               // has a "then" plug on its right
    bool  flowOutUsed = false;              // that plug already has a wire
};

/// The block the new one should be connected after, or -1. The new block must have a "then" input
/// (newHasFlowIn). A candidate has a free "then" plug, sits at or to the left of the new block (or directly
/// above it) and is near; the closest one wins.
inline int VsAutoConnectFrom(const std::vector<VsLayoutNode>& nodes, float newX, float newY, bool newHasFlowIn,
                             float maxGap = 360.0f, float maxSideways = 140.0f) {
    if (!newHasFlowIn) return -1;
    int best = -1;
    float bestScore = 1e9f;
    for (const VsLayoutNode& n : nodes) {
        if (!n.hasFlowOut || n.flowOutUsed) continue;
        const float right = n.x + n.w;
        const float gap = newX - right;                     // beside it: the gap to its right edge
        const float dy = std::fabs(newY - n.y);
        float score = -1;
        if (gap >= -20.0f && gap <= maxGap && dy <= maxSideways) score = gap + dy;
        else if (std::fabs(newX - n.x) <= 60.0f && newY >= n.y + n.h - 10.0f && newY - (n.y + n.h) <= maxSideways)
            score = 40.0f + (newY - (n.y + n.h));           // right under it
        if (score >= 0 && score < bestScore) { bestScore = score; best = n.id; }
    }
    return best;
}

struct VsFlowLink { int from = 0, fromPin = 0, to = 0; };

/// A block dropped on the wire `wire`: the wire becomes two, from -> block and block -> to, so nothing
/// has to be re-wired by hand. The first link keeps the original output pin.
inline std::vector<VsFlowLink> VsInsertOnWire(const VsFlowLink& wire, int blockId, int blockOutPin = 0) {
    return { VsFlowLink{wire.from, wire.fromPin, blockId}, VsFlowLink{blockId, blockOutPin, wire.to} };
}

/// How far a point is from the straight line between two plugs. A wire counts as hit when this is small.
inline float VsDistanceToWire(float px, float py, float x1, float y1, float x2, float y2) {
    const float dx = x2 - x1, dy = y2 - y1;
    const float len2 = dx * dx + dy * dy;
    float t = len2 > 0.0f ? ((px - x1) * dx + (py - y1) * dy) / len2 : 0.0f;
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    const float cx = x1 + t * dx - px, cy = y1 + t * dy - py;
    return std::sqrt(cx * cx + cy * cy);
}

/// The wire (index into `wires`) the dropped block is on, or -1. Each wire is {x1, y1, x2, y2}.
struct VsWireLine { float x1, y1, x2, y2; };
inline int VsWireUnder(const std::vector<VsWireLine>& wires, float px, float py, float tolerance = 14.0f) {
    int best = -1;
    float bestD = tolerance;
    for (int i = 0; i < (int)wires.size(); ++i) {
        const float d = VsDistanceToWire(px, py, wires[i].x1, wires[i].y1, wires[i].x2, wires[i].y2);
        if (d <= bestD) { bestD = d; best = i; }
    }
    return best;
}

} // namespace okay::editor::vsed
