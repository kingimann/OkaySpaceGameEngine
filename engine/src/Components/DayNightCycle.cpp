// DayNightCycle: the exact clock step, the calendar and the events. Written from the plain rules of a
// 24 hour clock (no outside code): the hours are kept in a double, each frame adds
// dt * speed * 24 / dayLength hours, and the events a frame crosses are found by walking the hours of
// each day it covers.
#include "okay/Components/DayNightCycle.hpp"
#include "okay/Core/Clock.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/ActionList.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace okay {

namespace {
struct Ev { int kind; int value; };   // 0 hour, 1 day, 2 sunrise, 3 sunset, 4 season
constexpr int kMaxDaysPerStep = 400;  // a frame that covers more than this many days stops counting events

// A named event to one object: its Visual Script "On Custom Event" nodes, its scripts' OnEvent_<name>(value)
// functions and its Actions "On Message". The same three receivers as a Broadcast Event node reaches.
void Deliver(GameObject* o, const std::string& name, float value, GameObject* sender) {
    const vs::VsValue v(value);
    for (auto* vc : o->GetComponents<VisualScriptComponent>()) vc->SendEvent(name, v, sender);
    for (auto* al : o->GetComponents<ActionList>()) {
        ActionList::Vars()["event_value"] = value;
        al->ReceiveMessage(name);
    }
    for (auto* sc : o->GetComponents<ScriptComponent>())
        if (sc->VM()) sc->VM()->CallEventWith("OnEvent_" + name, v);
}
}

std::string DayNightCycle::ClockText() const {
    char b[16];
    std::snprintf(b, sizeof(b), "%02d:%02d", HourOfDay(), Minute());
    return b;
}

void DayNightCycle::PublishCalendar() {
    const int minute = Minute();
    const int key = HourOfDay() * 60 + minute;
    if (key == m_lastMinute) return;   // text and numbers only change when the minute does
    m_lastMinute = key;
    Prefs::SetInt("minute", minute);
    Prefs::SetInt("day", day);
    Prefs::SetInt("season", Season());
    Prefs::SetString("seasonName", SeasonName());
    Prefs::SetString("clock", ClockText());
}

void DayNightCycle::Advance(float dt) {
    double step = (double)dt;
    if (!clockName.empty()) {
        // Follow a named Clock channel: take what it advanced since this component last looked.
        ClockSet& set = ClockSet::Global();
        set.TickOncePerFrame();
        const Clock* c = set.Find(clockName);
        if (!c) { step = 0.0; }
        else {
            ClockCursor cur; cur.primed = m_primed; cur.ticks = m_ticks; cur.elapsed = m_elapsed;
            step = (double)cur.Take(c->Ticks(), c->Elapsed(), c->Delta());
            m_primed = cur.primed; m_ticks = cur.ticks; m_elapsed = cur.elapsed;
        }
    }
    if (paused || !(step > 0.0) || !std::isfinite(step)) return;
    if (!(dayLengthSeconds > 0.0f) || !(speed > 0.0f) || !std::isfinite(speed)) return;
    MoveForward(step * (double)speed * 24.0 / (double)dayLengthSeconds);
}

void DayNightCycle::AddHours(float h) {
    SyncHours();
    if (h > 0.0f && std::isfinite(h)) MoveForward((double)h);
    else if (std::isfinite(h)) { time = Wrap(time + h); SyncHours(); }
}

void DayNightCycle::SkipToHour(float hour) {
    SyncHours();
    if (!std::isfinite(hour)) return;
    double adv = std::fmod((double)Wrap(hour) - m_hours + 24.0, 24.0);
    if (adv <= 1e-9) adv = 24.0;
    MoveForward(adv);
}

void DayNightCycle::MoveForward(double hours) {
    if (!(hours > 0.0) || !std::isfinite(hours)) return;
    std::vector<Ev> events;
    const double sr = (double)Wrap(sunriseHour), ss = (double)Wrap(sunsetHour);
    double cur = m_hours, left = hours;
    int days = 0;
    while (left > 0.0 && days < kMaxDaysPerStep) {
        const double end = std::min(cur + left, 24.0);
        // Hours 1..23 passed inside this day (hour 24 is the next day's 0, handled at the wrap).
        for (int h = (int)std::floor(cur) + 1; h <= (int)std::floor(end) && h < 24; ++h)
            if ((double)h > cur) events.push_back({0, h});
        if (cur < sr && sr <= end) events.push_back({2, 0});
        if (cur < ss && ss <= end) events.push_back({3, 0});
        left -= end - cur;
        cur = end;
        if (cur >= 24.0) {   // midnight: a new day (and maybe a new season)
            cur = 0.0;
            const int oldSeason = Season();
            if (day < 2000000000) ++day;
            ++days;
            events.push_back({0, 0});
            events.push_back({1, day});
            if (Season() != oldSeason) events.push_back({4, Season()});
        }
    }
    if (left > 0.0) cur = std::fmod(cur + left, 24.0);   // a gap of years in one frame: the events are dropped, the time is right
    m_hours = cur;
    time = (float)m_hours < 24.0f ? (float)m_hours : std::nextafter(24.0f, 0.0f);
    m_synced = true;

    Scene* sc = gameObject ? gameObject->scene() : nullptr;
    for (const Ev& e : events) {
        switch (e.kind) {
            case 0: if (onHour) onHour(e.value); break;
            case 1: if (onDay) onDay(e.value); break;
            case 2: if (onSunrise) onSunrise(); break;
            case 3: if (onSunset) onSunset(); break;
            default: if (onSeason) onSeason(e.value); break;
        }
        if (!sc) continue;
        const char* name = e.kind == 0 ? "OnHour" : e.kind == 1 ? "OnDay" : e.kind == 2 ? "OnSunrise" : e.kind == 3 ? "OnSunset" : "OnSeason";
        std::vector<GameObject*> all;   // collect first: a handler may spawn or destroy objects
        for (const auto& up : sc->Objects()) if (up && up->active) all.push_back(up.get());
        for (GameObject* o : all) Deliver(o, name, (float)e.value, gameObject);
    }
}

} // namespace okay
