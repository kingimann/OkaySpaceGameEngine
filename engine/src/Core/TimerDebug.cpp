#include "okay/Core/TimerDebug.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <utility>

namespace okay {

namespace {

constexpr const char* kNone = "(none)";

// The groups of a name -> count table, biggest first, ties by name (stable text from run to run).
std::vector<TimerDebug::GroupCount> Sorted(const std::map<std::string, std::size_t>& counts) {
    std::vector<TimerDebug::GroupCount> out;
    out.reserve(counts.size());
    for (const auto& kv : counts) out.push_back({kv.first, kv.second});
    std::stable_sort(out.begin(), out.end(), [](const TimerDebug::GroupCount& a, const TimerDebug::GroupCount& b) {
        return a.count > b.count;
    });
    return out;
}

// The `options.topOldest` entries that have run longest, longest first (ties keep their order). Only that many are
// copied: sorting a copy of every entry would cost more than the rest of a capture on a scene with thousands of them.
template <class Info, class Age>
std::vector<Info> TopByAge(const std::vector<Info>& all, const TimerDebug::Options& options, Age age) {
    const std::size_t want = std::min<std::size_t>(all.size(), (std::size_t)std::max(0, options.topOldest));
    std::vector<Info> out;
    if (want == 0) return out;
    std::vector<std::size_t> idx(all.size());
    for (std::size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    std::partial_sort(idx.begin(), idx.begin() + (std::ptrdiff_t)want, idx.end(), [&](std::size_t a, std::size_t b) {
        const double x = age(all[a]), y = age(all[b]);
        return x != y ? x > y : a < b;
    });
    out.reserve(want);
    for (std::size_t i = 0; i < want; ++i) out.push_back(all[idx[i]]);
    return out;
}

std::string Hex(const void* p) {
    char buf[40];
    std::snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)(std::uintptr_t)p);
    return buf;
}

std::string GroupsText(const char* label, const std::vector<TimerDebug::GroupCount>& groups, int top) {
    if (groups.empty()) return {};
    std::string out = std::string(label) + ":";
    const std::size_t shown = std::min<std::size_t>(groups.size(), (std::size_t)std::max(1, top));
    for (std::size_t i = 0; i < shown; ++i)
        out += (i ? ", " : " ") + groups[i].name + " x" + std::to_string(groups[i].count);
    if (groups.size() > shown) out += ", +" + std::to_string(groups.size() - shown) + " more";
    return out;
}

TimerDebug::Options NoDetail() {
    TimerDebug::Options o;
    o.topGroups = 0; o.topOldest = 0; o.showGroups = false; o.showOldest = false;
    return o;
}

std::string Plural(std::size_t n, const char* one, const char* many) {
    return std::to_string(n) + " " + (n == 1 ? one : many);
}

std::string TaskText(const Scheduler::TaskInfo& t) {
    std::string out = "#" + std::to_string(t.handle) + " ";
    switch (t.kind) {
        case Scheduler::TaskInfo::Kind::Delay:     out += "delay " + TimerDebug::FormatSeconds(t.period); break;
        case Scheduler::TaskInfo::Kind::Repeating: out += "repeat every " + TimerDebug::FormatSeconds(t.period); break;
        case Scheduler::TaskInfo::Kind::Tween:     out += "tween " + TimerDebug::FormatSeconds(t.period); break;
    }
    if (!t.tag.empty()) out += " [" + t.tag + "]";
    out += " running " + TimerDebug::FormatSeconds(t.age);
    return out;
}

std::string TweenText(const TweenManager::TweenInfo& t) {
    const char* kind = t.kind == TweenManager::TweenInfo::Kind::Sequence ? "sequence"
                     : t.kind == TweenManager::TweenInfo::Kind::Value    ? "value" : "custom";
    std::string out = "#" + std::to_string(t.handle) + " " + kind;
    if (!t.id.empty()) out += " \"" + t.id + "\"";
    out += " running " + TimerDebug::FormatSeconds(t.age);
    out += t.loops < 0 ? ", loop " + std::to_string(t.loop + 1) + "/forever"
                       : ", loop " + std::to_string(std::min(t.loop + 1, t.loops)) + "/" + std::to_string(t.loops);
    return out;
}

} // namespace

std::string TimerDebug::FormatSeconds(double s) {
    if (std::isinf(s) && s > 0.0) return "never";
    if (!(s > 0.0)) return "0s";
    char buf[48];
    if (s < 59.95) {
        std::snprintf(buf, sizeof(buf), "%.3gs", s);
        return buf;
    }
    const long long whole = (long long)std::llround(std::min(s, 1.0e12));
    if (whole < 3600) {
        std::snprintf(buf, sizeof(buf), "%lldm%02llds", whole / 60, whole % 60);
    } else {
        const long long minutes = (whole + 30) / 60;   // hours and minutes: seconds are noise by then
        std::snprintf(buf, sizeof(buf), "%lldh%02lldm", minutes / 60, minutes % 60);
    }
    return buf;
}

TimerDebug::SchedulerStats TimerDebug::Capture(const Scheduler& scheduler) { return Capture(scheduler, Options{}); }
TimerDebug::TweenStats TimerDebug::Capture(const TweenManager& tweens) { return Capture(tweens, Options{}); }
std::vector<std::string> TimerDebug::BuildLines(const Scheduler* scheduler, const TweenManager* tweens) {
    return BuildLines(scheduler, tweens, Options{});
}

TimerDebug::SchedulerStats TimerDebug::Capture(const Scheduler& scheduler, const Options& options) {
    SchedulerStats st;
    const std::vector<Scheduler::TaskInfo> tasks = scheduler.Tasks();
    std::map<std::string, std::size_t> tags, owners;
    const bool groups = options.showGroups || options.topGroups > 0;   // a one-line summary does not pay for the tables
    for (const Scheduler::TaskInfo& t : tasks) {
        ++st.total;
        switch (t.kind) {
            case Scheduler::TaskInfo::Kind::Delay:     ++st.delays; break;
            case Scheduler::TaskInfo::Kind::Repeating: ++st.repeating; break;
            case Scheduler::TaskInfo::Kind::Tween:     ++st.tweens; break;
        }
        if (t.paused) ++st.paused;
        if (t.overdue > 0.0) ++st.overdue;
        if (!t.ownerAlive) ++st.ownerGone;
        if (!t.clock.empty()) ++st.onClock;
        else if (t.unscaled) ++st.unscaled;
        if (groups) {
            ++tags[t.tag.empty() ? std::string(kNone) : t.tag];
            std::string who = kNone;
            if (t.owner) who = !t.objectName.empty() ? t.objectName : t.hasObject ? std::string("(object gone)") : Hex(t.owner);
            ++owners[who];
        }
        // The soonest task that is really waiting: not paused, still owned, and with a finite time.
        if (!t.paused && t.ownerAlive && std::isfinite(t.remaining) && (st.nextDue < 0.0f || t.remaining < st.nextDue)) {
            st.nextDue = t.remaining;
            st.nextHandle = t.handle;
        }
    }
    st.byTag = Sorted(tags);
    st.byOwner = Sorted(owners);
    st.oldest = TopByAge(tasks, options, [](const Scheduler::TaskInfo& t) { return t.age; });
    return st;
}

TimerDebug::TweenStats TimerDebug::Capture(const TweenManager& tweens, const Options& options) {
    TweenStats st;
    const std::vector<TweenManager::TweenInfo> infos = tweens.Infos();
    std::map<std::string, std::size_t> ids, tags;
    const bool groups = options.showGroups || options.topGroups > 0;
    for (const TweenManager::TweenInfo& t : infos) {
        ++st.total;
        if (t.paused) ++st.paused;
        else if (!t.complete) ++st.playing;
        if (t.kind == TweenManager::TweenInfo::Kind::Sequence) { ++st.sequences; st.nested += (std::size_t)t.children; }
        if (t.loops < 0) ++st.endless;
        if (t.complete && !t.autoKill) ++st.doneKept;
        if (!t.clock.empty()) ++st.onClock;
        else if (t.unscaled) ++st.unscaled;
        if (t.hasTarget) ++st.withTarget;
        if (groups) {
            ++ids[t.id.empty() ? std::string(kNone) : t.id];
            if (!t.tag.empty()) ++tags[t.tag];
        }
    }
    st.byId = Sorted(ids);
    st.byTag = Sorted(tags);
    st.oldest = TopByAge(infos, options, [](const TweenManager::TweenInfo& t) { return t.age; });
    return st;
}

std::vector<std::string> TimerDebug::BuildLines(const Scheduler* scheduler, const TweenManager* tweens, const Options& options) {
    std::vector<std::string> lines;
    Options capture = options;   // capture only what the lines will show
    if (!options.showGroups) capture.topGroups = 0;
    if (!options.showOldest) capture.topOldest = 0;
    if (scheduler) {
        const SchedulerStats st = Capture(*scheduler, capture);
        std::string head = "Scheduler: " + Plural(st.total, "task", "tasks");
        if (st.total > 0) {
            head += " (" + Plural(st.delays, "delay", "delays") + ", " + std::to_string(st.repeating) + " repeating, " +
                    Plural(st.tweens, "tween", "tweens") + ")";
            if (st.paused) head += ", " + std::to_string(st.paused) + " paused";
        }
        lines.push_back(head);
        if (st.total > 0) {
            std::string due;
            if (st.nextDue >= 0.0f) due = "next due in " + FormatSeconds((double)st.nextDue) + " (#" + std::to_string(st.nextHandle) + ")";
            else due = "nothing waiting";
            if (st.overdue) due += ", " + std::to_string(st.overdue) + " overdue";
            if (st.ownerGone) due += ", " + std::to_string(st.ownerGone) + " owner gone";
            lines.push_back("  " + due);
            if (options.showGroups) {
                const std::string tags = GroupsText("tags", st.byTag, options.topGroups);
                const std::string owners = GroupsText("owners", st.byOwner, options.topGroups);
                // Skip a line that says only "(none) x N": it is no information.
                const bool onlyNoneTags = st.byTag.size() == 1 && st.byTag[0].name == kNone;
                const bool onlyNoneOwners = st.byOwner.size() == 1 && st.byOwner[0].name == kNone;
                if (!tags.empty() && !onlyNoneTags) lines.push_back("  " + tags);
                if (!owners.empty() && !onlyNoneOwners) lines.push_back("  " + owners);
            }
            if (options.showOldest && !st.oldest.empty()) {
                std::string o = "  oldest:";
                for (std::size_t i = 0; i < st.oldest.size(); ++i) o += (i ? "; " : " ") + TaskText(st.oldest[i]);
                lines.push_back(o);
            }
        }
    }
    if (tweens) {
        const TweenStats st = Capture(*tweens, capture);
        std::string head = "Tweens: " + std::to_string(st.total);
        if (st.total > 0) {
            head += " (" + std::to_string(st.playing) + " playing";
            if (st.paused) head += ", " + std::to_string(st.paused) + " paused";
            head += ")";
            if (st.doneKept) head += ", " + std::to_string(st.doneKept) + " done and kept";
            if (st.endless) head += ", " + std::to_string(st.endless) + " endless";
        }
        lines.push_back(head);
        if (st.total > 0) {
            if (st.sequences)
                lines.push_back("  " + Plural(st.sequences, "sequence", "sequences") + " holding " + std::to_string(st.nested) + " parts");
            if (options.showGroups) {
                const std::string ids = GroupsText("ids", st.byId, options.topGroups);
                const std::string tags = GroupsText("tags", st.byTag, options.topGroups);
                const bool onlyNoneIds = st.byId.size() == 1 && st.byId[0].name == kNone;
                if (!ids.empty() && !onlyNoneIds) lines.push_back("  " + ids);
                if (!tags.empty()) lines.push_back("  " + tags);
            }
            if (options.showOldest && !st.oldest.empty()) {
                std::string o = "  oldest:";
                for (std::size_t i = 0; i < st.oldest.size(); ++i) o += (i ? "; " : " ") + TweenText(st.oldest[i]);
                lines.push_back(o);
            }
        }
    }
    return lines;
}

std::string TimerDebug::Summary(const Scheduler* scheduler, const TweenManager* tweens) {
    std::string out;
    if (scheduler) {
        const SchedulerStats st = Capture(*scheduler, NoDetail());
        out = "Timers: " + std::to_string(st.total);
        std::string extra;
        if (st.paused) extra += std::to_string(st.paused) + " paused";
        if (st.nextDue >= 0.0f) extra += std::string(extra.empty() ? "" : ", ") + "next " + FormatSeconds((double)st.nextDue);
        if (st.overdue) extra += std::string(extra.empty() ? "" : ", ") + std::to_string(st.overdue) + " overdue";
        if (!extra.empty()) out += " (" + extra + ")";
    }
    if (tweens) {
        const TweenStats st = Capture(*tweens, NoDetail());
        if (!out.empty()) out += " | ";
        out += "Tweens: " + std::to_string(st.total);
        if (st.total > 0) out += " (" + std::to_string(st.playing) + " playing)";
    }
    return out;
}

} // namespace okay
