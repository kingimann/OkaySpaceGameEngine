#pragma once
#include "okay/Components/UIWidgetUtil.hpp"
#include "okay/UI/UIShape.hpp"
#include "okay/Render/Color.hpp"
#include <cstdio>
#include <string>

namespace okay {

/// A date and time picker. Invalid dates are pulled back into a real calendar
/// day (31 February becomes the last day of February, month 13 becomes December).
/// The sibling script's on_change() runs when the date or time changes.
class UIDateTimePicker final : public Behaviour {
public:
    Vec2 position{20.0f, 20.0f};
    Vec2 size{460.0f, 64.0f};
    UIAnchor anchor = UIAnchor::TopLeft;
    int year = 2026;
    int month = 1;
    int day = 1;
    int hour = 12;
    int minute = 0;
    int minYear = 1900;
    int maxYear = 2100;
    bool showTime = true;
    bool interactable = true;
    bool focusable = true;
    Color background = Color::FromBytes(28, 32, 44);
    Color textColor = Color::White;
    Color button = Color::FromBytes(90, 150, 230);
    Color muted = Color::FromBytes(150, 158, 176);
    UIShape shape = UIShape::Rounded;
    float cornerRadius = 6.0f;

    bool IsFocused() const { return m_focused; }
    void SetFocused(bool f) { m_focused = f; }

    static bool IsLeap(int y) {
        return (y % 4 == 0) && ((y % 100 != 0) || (y % 400 == 0));
    }
    static int DaysInMonth(int y, int m) {
        static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (m < 1) m = 1;
        if (m > 12) m = 12;
        if (m == 2 && IsLeap(y)) return 29;
        return kDays[m - 1];
    }

    /// Pull year, month, day, hour and minute onto a real clock reading.
    void Clamp() {
        if (minYear > maxYear) { const int t = minYear; minYear = maxYear; maxYear = t; }
        if (year < minYear) year = minYear;
        if (year > maxYear) year = maxYear;
        if (month < 1) month = 1;
        if (month > 12) month = 12;
        const int dim = DaysInMonth(year, month);
        if (day < 1) day = 1;
        if (day > dim) day = dim;
        if (hour < 0) hour = 0;
        if (hour > 23) hour = 23;
        if (minute < 0) minute = 0;
        if (minute > 59) minute = 59;
    }

    void SetDate(int y, int m, int d) {
        const int oy = year, om = month, od = day, oh = hour, oni = minute;
        year = y; month = m; day = d;
        Changed(oy, om, od, oh, oni);
    }
    void SetTime(int h, int m) {
        const int oy = year, om = month, od = day, oh = hour, oni = minute;
        hour = h; minute = m;
        Changed(oy, om, od, oh, oni);
    }
    /// Move by whole days, rolling the month and the year.
    void StepDay(int dir) {
        if (dir == 0) return;
        int y = year, m = month, d = day + dir;
        while (d > DaysInMonth(y, m)) {
            d -= DaysInMonth(y, m);
            ++m;
            if (m > 12) { m = 1; ++y; }
        }
        while (d < 1) {
            --m;
            if (m < 1) { m = 12; --y; }
            d += DaysInMonth(y, m);
        }
        SetDate(y, m, d);
    }
    /// 0 year, 1 month, 2 day, 3 hour, 4 minute.
    void StepPart(int part, int dir) {
        if (dir == 0) return;
        if (part == 2) { StepDay(dir); return; }
        const int oy = year, om = month, od = day, oh = hour, oni = minute;
        if (part == 0) year += dir;
        else if (part == 1) month += dir;
        else if (part == 3) hour += dir;
        else if (part == 4) minute += dir;
        Changed(oy, om, od, oh, oni);
    }

    std::string Text() const {
        char buf[40];
        if (showTime)
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d  %02d:%02d", year, month, day, hour, minute);
        else
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year, month, day);
        return buf;
    }

    int Columns() const { return showTime ? 5 : 3; }

    void PressLocal(const Vec2& local) {
        if (!interactable) return;
        const int n = Columns();
        if (n < 1 || size.x <= 0.0f) return;
        const float col = size.x / (float)n;
        int part = (int)(local.x / col);
        if (part < 0) part = 0;
        if (part >= n) part = n - 1;
        const float x = local.x - col * (float)part;
        if (x <= 26.0f) StepPart(part, -1);
        else if (x >= col - 26.0f) StepPart(part, +1);
    }

    void Update(float) override {
        if (!enabled || !interactable) return;
        Vec2 local;
        if (UIWidgetPointerLocal(gameObject, anchor, position, size, local)) PressLocal(local);
    }

private:
    void Changed(int oy, int om, int od, int oh, int oni) {
        Clamp();
        if (year != oy || month != om || day != od || hour != oh || minute != oni)
            FireUIEvent(gameObject, "on_change");
    }
    bool m_focused = false;
};

} // namespace okay
