#pragma once
// ---------------------------------------------------------------------------
// SystemCost: "which system eats the frame?". Turns the Profiler's last frames into one short list of
// systems with their milliseconds, for the second page of the F3 overlay in the player and the Stats
// overlay of the editor Game view:
//
//     Systems (avg of last 120 frames)
//     Physics     1.20 ms  peak 3.10
//     Scripts     0.45 ms  peak 0.90
//     Animation   0.80 ms  peak 1.30
//     Audio       0.10 ms  peak 0.20
//     UI          0.30 ms  peak 0.40
//     Render      4.50 ms  peak 6.00
//     Terrain     0.00 ms  peak 0.00
//     Other CPU   0.20 ms
//     Idle/wait   9.40 ms         (vsync, frame cap)
//     frame 16.65 ms  (graph: last 120 frames)
//
// A system is a profiler scope name prefix: Physics.*, Scripts.*, Anim.*, Audio*, UI*, Render.*,
// Voxel.* / Terrain.*. Times are SELF times (a scope inside another is not counted twice), so the rows
// add up to the CPU time of the frame. The profiler only collects while `Prof().enabled`, so a host turns
// it on while the overlay is open (the scopes cost a few nanoseconds each when it is off).
// Written from the Profiler's own interface; nothing else.
// ---------------------------------------------------------------------------
#include "okay/Core/Profiler.hpp"
#include <cstdio>
#include <string>
#include <vector>

namespace okay {

struct SystemCost {
    static constexpr int kWindow = 120;

    struct Row { std::string name; double avgMs = 0.0, peakMs = 0.0; };

    /// The rows over the newest `frames` profiled frames: the seven systems, then "Other CPU" (CPU time
    /// outside every system) and "Idle/wait" (frame time minus CPU time: vsync, the frame cap).
    static std::vector<Row> Rows(const Profiler& p, int frames = kWindow) {
        static const struct { const char* name; const char* prefix[2]; } kSystems[] = {
            {"Physics",   {"Physics", nullptr}},
            {"Scripts",   {"Scripts", nullptr}},
            {"Animation", {"Anim", nullptr}},
            {"Audio",     {"Audio", nullptr}},
            {"UI",        {"UI", nullptr}},
            {"Render",    {"Render", nullptr}},
            {"Terrain",   {"Voxel", "Terrain"}},
        };
        constexpr int kN = 7;
        std::vector<Row> rows(kN + 2);
        for (int i = 0; i < kN; ++i) rows[i].name = kSystems[i].name;
        rows[kN].name = "Other CPU";
        rows[kN + 1].name = "Idle/wait";
        const int count = p.Count();
        const int n = frames < count ? frames : count;
        if (n <= 0) return rows;
        for (int k = count - n; k < count; ++k) {
            const Profiler::Frame& f = p.FrameAt(k);
            double listed = 0.0;
            for (int i = 0; i < kN; ++i) {
                double ms = 0.0;
                for (const char* pre : kSystems[i].prefix) if (pre) ms += Profiler::CategorySelf(f, pre);
                listed += ms;
                rows[i].avgMs += ms;
                if (ms > rows[i].peakMs) rows[i].peakMs = ms;
            }
            double other = f.cpu - listed;
            if (other < 0.0) other = 0.0;
            double idle = f.total - f.cpu;
            if (idle < 0.0) idle = 0.0;
            rows[kN].avgMs += other;
            rows[kN + 1].avgMs += idle;
        }
        for (Row& r : rows) r.avgMs /= n;
        return rows;
    }

    /// The overlay text: a heading, one line per row, the frame time.
    static std::vector<std::string> Lines(const Profiler& p, int frames = kWindow) {
        std::vector<std::string> out;
        if (!p.enabled || p.Count() <= 0) { out.push_back("Systems: collecting..."); return out; }
        out.push_back("Systems (avg of last " + std::to_string(frames < p.Count() ? frames : p.Count()) + " frames)");
        char b[96];
        for (const Row& r : Rows(p, frames)) {
            const bool hasPeak = r.name != "Other CPU" && r.name != "Idle/wait";
            if (hasPeak) std::snprintf(b, sizeof b, "%-10s %6.2f ms  peak %.2f", r.name.c_str(), r.avgMs, r.peakMs);
            else         std::snprintf(b, sizeof b, "%-10s %6.2f ms", r.name.c_str(), r.avgMs);
            for (char* c = b; *c; ++c) if (*c == ',') *c = '.';
            out.push_back(b);
        }
        std::snprintf(b, sizeof b, "frame %.2f ms", p.lastFrameMs);
        out.push_back(b);
        return out;
    }

    /// The newest `n` frame times (ms), oldest first, for a graph. Fewer frames pad the front with 0.
    static std::vector<float> Plot(const Profiler& p, int n = kWindow) {
        std::vector<float> v((size_t)(n > 0 ? n : 0), 0.0f);
        const int count = p.Count();
        for (int i = 0; i < n && i < count; ++i)
            v[(size_t)(n - 1 - i)] = (float)p.FrameAt(count - 1 - i).total;
        return v;
    }
};

}  // namespace okay
