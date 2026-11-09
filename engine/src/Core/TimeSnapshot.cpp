#include "okay/Core/TimeSnapshot.hpp"
#include "okay/Core/SaveFramework.hpp"

#include <cmath>
#include <locale>
#include <sstream>

namespace okay {

namespace {

std::string FormatDouble(double v) {
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o.precision(17);
    o << v;
    return o.str();
}

double ParseDouble(const std::string& s, double def) {
    std::istringstream is(s);
    is.imbue(std::locale::classic());
    double v = 0.0;
    if (is >> v) return v;
    return def;
}

double CleanElapsed(double v) { return (v > 0.0 && std::isfinite(v)) ? v : 0.0; }
float CleanScale(float v) { return v > 0.0f ? (v < Time::kMaxTimeScale ? v : Time::kMaxTimeScale) : 0.0f; }

}  // namespace

TimeSnapshot TimeSnapshot::Capture(const ClockSet& set, bool sharedRandom) {
    TimeSnapshot snap;
    snap.timeScale = Time::BaseTimeScale();
    for (const std::string& name : set.Names()) {
        const Clock* c = set.Find(name);
        if (!c) continue;
        ClockSnapshot s;
        s.name = c->Name();
        s.source = c->GetSource();
        s.holdsWhileGamePaused = c->HoldsWhileGamePaused();
        s.paused = c->Paused();
        s.scale = c->Scale();
        s.elapsed = c->Elapsed();
        s.ticks = c->Ticks();
        s.ramping = c->Ramping();
        s.rampTarget = c->RampTarget();
        s.rampSecondsLeft = c->RampSecondsLeft();
        snap.clocks.push_back(std::move(s));
    }
    if (sharedRandom) snap.randoms["shared"] = Random::Shared().GetState();
    return snap;
}

bool TimeSnapshot::ApplyRandom(const std::string& name, Random& rng) const {
    auto it = randoms.find(name);
    if (it == randoms.end()) return false;
    rng.SetState(it->second);
    return true;
}

void TimeSnapshot::Apply(ClockSet& set, const ApplyOptions& options) const {
    if (options.timeScale && hasTimeScale) Time::SetTimeScale(timeScale);
    if (options.clocks) {
        for (const ClockSnapshot& s : clocks) {
            if (s.name.empty()) continue;
            Clock& c = set.Add(s.name, s.source, s.holdsWhileGamePaused);   // an existing clock keeps its code-defined settings
            c.SetScale(s.scale);
            c.SetPaused(s.paused);
            c.Restore(s.elapsed, s.ticks);
            if (s.ramping && s.rampSecondsLeft > 0.0f) c.RampScale(s.rampTarget, s.rampSecondsLeft);
        }
    }
    if (options.random) {
        auto it = randoms.find("shared");
        if (it != randoms.end()) Random::Shared().SetState(it->second);
    }
}

void TimeSnapshot::Write(SaveWriter w) const {
    w.Int("v", kVersion);
    w.Float("timeScale", timeScale);
    w.Int("clocks.n", (int)clocks.size());
    for (std::size_t i = 0; i < clocks.size(); ++i) {
        const ClockSnapshot& c = clocks[i];
        SaveWriter cw = w.Scope("clocks").Scope(std::to_string(i));
        cw.String("name", c.name);
        cw.Int("source", c.source == Clock::Source::Scaled ? 0 : 1);
        cw.Bool("holds", c.holdsWhileGamePaused);
        cw.Bool("paused", c.paused);
        cw.Float("scale", c.scale);
        cw.String("elapsed", FormatDouble(c.elapsed));
        cw.Int64("ticks", (long long)c.ticks);
        if (c.ramping) {
            cw.Float("rampTarget", c.rampTarget);
            cw.Float("rampLeft", c.rampSecondsLeft);
        }
    }
    w.Int("randoms.n", (int)randoms.size());
    int i = 0;
    for (const auto& kv : randoms) {
        SaveWriter rw = w.Scope("randoms").Scope(std::to_string(i++));
        rw.String("name", kv.first);
        rw.Int64("s0", (long long)kv.second.s0);
        rw.Int64("s1", (long long)kv.second.s1);
    }
}

bool TimeSnapshot::Read(const SaveReader& r, TimeSnapshot& out) {
    if (r.Int("v", 0) < 1) return false;
    TimeSnapshot snap;
    snap.hasTimeScale = r.Has("timeScale");
    snap.timeScale = CleanScale(r.Float("timeScale", 1.0f));
    int n = r.Int("clocks.n", 0);
    if (n < 0) n = 0;
    if (n > kMaxClocks) n = kMaxClocks;
    for (int i = 0; i < n; ++i) {
        const SaveReader cr = r.Scope("clocks").Scope(std::to_string(i));
        ClockSnapshot c;
        c.name = cr.String("name", "");
        if (c.name.empty()) continue;
        c.source = cr.Int("source", 1) == 0 ? Clock::Source::Scaled : Clock::Source::Unscaled;
        c.holdsWhileGamePaused = cr.Bool("holds", false);
        c.paused = cr.Bool("paused", false);
        c.scale = CleanScale(cr.Float("scale", 1.0f));
        c.elapsed = CleanElapsed(ParseDouble(cr.String("elapsed", "0"), 0.0));
        const long long t = cr.Int64("ticks", 0);
        c.ticks = t > 0 ? (std::uint64_t)t : 0;
        if (cr.Has("rampTarget")) {
            c.rampTarget = CleanScale(cr.Float("rampTarget", 1.0f));
            c.rampSecondsLeft = cr.Float("rampLeft", 0.0f);
            c.ramping = c.rampSecondsLeft > 0.0f && std::isfinite(c.rampSecondsLeft);
        }
        snap.clocks.push_back(std::move(c));
    }
    int m = r.Int("randoms.n", 0);
    if (m < 0) m = 0;
    if (m > kMaxRandoms) m = kMaxRandoms;
    for (int i = 0; i < m; ++i) {
        const SaveReader rr = r.Scope("randoms").Scope(std::to_string(i));
        const std::string name = rr.String("name", "");
        if (name.empty() || !rr.Has("s0") || !rr.Has("s1")) continue;
        Random::State st;
        st.s0 = (std::uint64_t)rr.Int64("s0", 1);
        st.s1 = (std::uint64_t)rr.Int64("s1", 2);
        snap.randoms[name] = st;
    }
    out = std::move(snap);
    return true;
}

bool TimeSnapshot::operator==(const TimeSnapshot& o) const {
    if (timeScale != o.timeScale || clocks.size() != o.clocks.size() || randoms.size() != o.randoms.size()) return false;
    for (std::size_t i = 0; i < clocks.size(); ++i) {
        const ClockSnapshot& a = clocks[i];
        const ClockSnapshot& b = o.clocks[i];
        if (a.name != b.name || a.source != b.source || a.holdsWhileGamePaused != b.holdsWhileGamePaused || a.paused != b.paused ||
            a.scale != b.scale || a.elapsed != b.elapsed || a.ticks != b.ticks || a.ramping != b.ramping ||
            (a.ramping && (a.rampTarget != b.rampTarget || a.rampSecondsLeft != b.rampSecondsLeft)))
            return false;
    }
    for (const auto& kv : randoms) {
        auto it = o.randoms.find(kv.first);
        if (it == o.randoms.end() || it->second.s0 != kv.second.s0 || it->second.s1 != kv.second.s1) return false;
    }
    return true;
}

}  // namespace okay
