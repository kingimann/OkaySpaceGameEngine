// Own Windows backend, game controllers that are not XInput devices: Raw Input (WM_INPUT) for the reports and
// the HidP_* functions for what the reports mean. The decisions that need no Windows (which usages are axes, hats and
// buttons, scaling, hat directions, the "IG_" XInput rule) are in Win32Hid.hpp and tested on Linux.
//
// Written from the Microsoft documentation of RegisterRawInputDevices (RIDEV_INPUTSINK, RIDEV_DEVNOTIFY),
// GetRawInputDeviceList, GetRawInputDeviceInfo (RIDI_PREPARSEDDATA, RIDI_DEVICEINFO), GetRawInputData (RAWHID),
// WM_INPUT_DEVICE_CHANGE, HidP_GetCaps / HidP_GetButtonCaps / HidP_GetValueCaps / HidP_GetUsages / HidP_GetUsageValue.
//
// STATUS: COMPILES (MinGW -fsyntax-only), NEVER RUN. No Windows machine and no real pad has touched this. Pads read
// through here have no built-in button layout (HasBuiltinMapping() is false): use RawAxis / RawButton / RawHat.
#ifdef _WIN32
#include "Internal.hpp"
#include "Win32Hid.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <ntdef.h>
#include <hidusage.h>
#include <hidpi.h>
#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>

namespace okay::platform::own {
namespace {
constexpr int kFirstRawPadId = 100;   // XInput slots use instance ids 1..4
}

struct RawPads::Impl {
    struct Dev {
        HANDLE handle = nullptr;
        int id = 0;
        std::string path, name;
        uint16_t vid = 0, pid = 0;
        std::vector<uint8_t> preparsed;
        win32hid::UsageLayout layout;
        std::vector<uint16_t> pressed;       // last successfully read button usages
        std::vector<uint32_t> rawAxes, rawHats;
    };
    std::vector<Dev> devs;
    std::deque<Change> changes;
    int nextId = kFirstRawPadId;
    HWND hwnd = nullptr;

    Dev* Find(HANDLE h) { for (Dev& d : devs) if (d.handle == h) return &d; return nullptr; }

    // Add a device if it is a joystick or game pad, not an XInput device, and has a usable layout.
    void Add(HANDLE h, bool announce) {
        if (!h || Find(h)) return;
        RID_DEVICE_INFO info; ZeroMemory(&info, sizeof(info)); info.cbSize = sizeof(info);
        UINT sz = sizeof(info);
        if (GetRawInputDeviceInfoW(h, RIDI_DEVICEINFO, &info, &sz) == (UINT)-1 || info.dwType != RIM_TYPEHID) return;
        if (info.hid.usUsagePage != HID_USAGE_PAGE_GENERIC || (info.hid.usUsage != HID_USAGE_GENERIC_JOYSTICK && info.hid.usUsage != HID_USAGE_GENERIC_GAMEPAD)) return;
        Dev d; d.handle = h;
        UINT n = 0;
        GetRawInputDeviceInfoW(h, RIDI_DEVICENAME, nullptr, &n);
        if (n > 0 && n < 4096) {
            std::vector<wchar_t> w((size_t)n + 1, L'\0');
            if (GetRawInputDeviceInfoW(h, RIDI_DEVICENAME, w.data(), &n) != (UINT)-1) {
                const int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), -1, nullptr, 0, nullptr, nullptr);
                if (len > 1) { d.path.resize((size_t)len - 1); WideCharToMultiByte(CP_UTF8, 0, w.data(), -1, &d.path[0], len, nullptr, nullptr); }
            }
        }
        if (win32hid::IsXInputDevicePath(d.path)) return;           // read through XInput
        d.vid = (uint16_t)info.hid.dwVendorId; d.pid = (uint16_t)info.hid.dwProductId;
        if (!d.vid && !d.pid) win32hid::ParseVidPid(d.path, d.vid, d.pid);
        UINT pn = 0;
        if (GetRawInputDeviceInfoW(h, RIDI_PREPARSEDDATA, nullptr, &pn) != 0 || pn == 0 || pn > (1u << 20)) return;
        d.preparsed.resize(pn);
        if (GetRawInputDeviceInfoW(h, RIDI_PREPARSEDDATA, d.preparsed.data(), &pn) == (UINT)-1) return;
        PHIDP_PREPARSED_DATA pp = (PHIDP_PREPARSED_DATA)d.preparsed.data();
        HIDP_CAPS caps;
        if (HidP_GetCaps(pp, &caps) < 0) return;
        std::vector<win32hid::ButtonCap> bc;
        std::vector<win32hid::ValueCap> vc;
        if (caps.NumberInputButtonCaps) {
            std::vector<HIDP_BUTTON_CAPS> b(caps.NumberInputButtonCaps);
            USHORT cnt = caps.NumberInputButtonCaps;
            if (HidP_GetButtonCaps(HidP_Input, b.data(), &cnt, pp) >= 0)
                for (USHORT i = 0; i < cnt; ++i) {
                    const HIDP_BUTTON_CAPS& c = b[i];
                    bc.push_back({c.ReportID, c.UsagePage, c.IsRange != 0, c.IsRange ? c.Range.UsageMin : c.NotRange.Usage, c.IsRange ? c.Range.UsageMax : c.NotRange.Usage});
                }
        }
        if (caps.NumberInputValueCaps) {
            std::vector<HIDP_VALUE_CAPS> v(caps.NumberInputValueCaps);
            USHORT cnt = caps.NumberInputValueCaps;
            if (HidP_GetValueCaps(HidP_Input, v.data(), &cnt, pp) >= 0)
                for (USHORT i = 0; i < cnt; ++i) {
                    const HIDP_VALUE_CAPS& c = v[i];
                    vc.push_back({c.ReportID, c.UsagePage, c.IsRange != 0, c.IsRange ? c.Range.UsageMin : c.NotRange.Usage, c.IsRange ? c.Range.UsageMax : c.NotRange.Usage,
                                  c.BitSize, c.ReportCount, (int32_t)c.LogicalMin, (int32_t)c.LogicalMax});
                }
        }
        d.layout = win32hid::BuildLayout(bc, vc);
        if (!d.layout.Usable()) return;
        d.rawAxes.assign(d.layout.axes.size(), 0);
        d.rawHats.assign(d.layout.hats.size(), 0);
        char nm[64]; std::snprintf(nm, sizeof(nm), "HID game controller %04X:%04X", d.vid, d.pid);
        d.name = nm;
        d.id = nextId++;
        devs.push_back(std::move(d));
        if (announce) changes.push_back({true, devs.back().id, (int)devs.size() - 1});
    }
    void Remove(HANDLE h) {
        for (size_t i = 0; i < devs.size(); ++i) if (devs[i].handle == h) { changes.push_back({false, devs[i].id, (int)i}); devs.erase(devs.begin() + (long)i); return; }
    }
    void Enumerate() {
        UINT n = 0;
        if (GetRawInputDeviceList(nullptr, &n, sizeof(RAWINPUTDEVICELIST)) == (UINT)-1 || n == 0 || n > 1024) return;
        std::vector<RAWINPUTDEVICELIST> l(n);
        if (GetRawInputDeviceList(l.data(), &n, sizeof(RAWINPUTDEVICELIST)) == (UINT)-1) return;
        for (UINT i = 0; i < n; ++i) if (l[i].dwType == RIM_TYPEHID) Add(l[i].hDevice, false);
    }
};

RawPads::RawPads() : m(new Impl) {}
RawPads::~RawPads() { delete m; }

bool RawPads::Register(void* hwnd) {
    m->hwnd = (HWND)hwnd;
    RAWINPUTDEVICE rid[2];
    for (int i = 0; i < 2; ++i) {
        rid[i].usUsagePage = HID_USAGE_PAGE_GENERIC;
        rid[i].usUsage = i == 0 ? HID_USAGE_GENERIC_JOYSTICK : HID_USAGE_GENERIC_GAMEPAD;
        rid[i].dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
        rid[i].hwndTarget = m->hwnd;
    }
    if (!RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE))) return false;
    m->Enumerate();
    return true;
}

void RawPads::OnDeviceChange(unsigned wParam, void* hDevice) {
    if (wParam == 1 /* GIDC_ARRIVAL */) m->Add((HANDLE)hDevice, true);
    else if (wParam == 2 /* GIDC_REMOVAL */) m->Remove((HANDLE)hDevice);
}

void RawPads::OnInput(void* hRawInput) {
    UINT size = 0;
    if (GetRawInputData((HRAWINPUT)hRawInput, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) != 0 || size < sizeof(RAWINPUTHEADER) || size > (1u << 16)) return;
    std::vector<uint8_t> buf(size);
    if (GetRawInputData((HRAWINPUT)hRawInput, RID_INPUT, buf.data(), &size, sizeof(RAWINPUTHEADER)) == (UINT)-1) return;
    const RAWINPUT* ri = (const RAWINPUT*)buf.data();
    if (ri->header.dwType != RIM_TYPEHID) return;
    Impl::Dev* d = m->Find(ri->header.hDevice);
    if (!d) return;
    const DWORD per = ri->data.hid.dwSizeHid, count = ri->data.hid.dwCount;
    if (per == 0 || count == 0 || (size_t)per * count > size) return;
    PHIDP_PREPARSED_DATA pp = (PHIDP_PREPARSED_DATA)d->preparsed.data();
    for (DWORD r = 0; r < count; ++r) {
        PCHAR report = (PCHAR)(ri->data.hid.bRawData + (size_t)r * per);
        // Buttons: a report of another id fails with an error and leaves the last answer in place.
        const ULONG maxUsages = HidP_MaxUsageListLength(HidP_Input, HID_USAGE_PAGE_BUTTON, pp);
        if (maxUsages > 0 && maxUsages <= 1024) {
            std::vector<USAGE> u(maxUsages);
            ULONG n = maxUsages;
            if (HidP_GetUsages(HidP_Input, HID_USAGE_PAGE_BUTTON, 0, u.data(), &n, pp, report, per) >= 0) {
                d->pressed.assign(u.begin(), u.begin() + (long)n);
            }
        }
        for (size_t i = 0; i < d->layout.axes.size(); ++i) {
            ULONG v = 0;
            if (HidP_GetUsageValue(HidP_Input, d->layout.axes[i].page, 0, d->layout.axes[i].usage, &v, pp, report, per) >= 0) d->rawAxes[i] = v;
        }
        for (size_t i = 0; i < d->layout.hats.size(); ++i) {
            ULONG v = 0;
            if (HidP_GetUsageValue(HidP_Input, d->layout.hats[i].page, 0, d->layout.hats[i].usage, &v, pp, report, per) >= 0) d->rawHats[i] = v;
        }
    }
}

int RawPads::Count() const { return (int)m->devs.size(); }
bool RawPads::PopChange(Change& c) { if (m->changes.empty()) return false; c = m->changes.front(); m->changes.pop_front(); return true; }

namespace {
class RawPad final : public Gamepad {
public:
    RawPad(const RawPads::Impl* impl, int id, std::string name, uint16_t vid, uint16_t pid) : m_impl(impl), m_id(id), m_name(std::move(name)), m_vid(vid), m_pid(pid) {}
    int InstanceId() const override { return m_id; }
    const char* Name() const override { return m_name.c_str(); }
    int16_t Axis(GamepadAxis) const override { return 0; }      // no standard layout known: use the raw readers
    bool Button(GamepadButton) const override { return false; }
    bool Rumble(uint16_t, uint16_t, uint32_t) override { return false; }
    uint16_t VendorId() const override { return m_vid; }
    uint16_t ProductId() const override { return m_pid; }
    bool HasBuiltinMapping() const override { return false; }
    int RawAxisCount() const override { const auto* d = Dev(); return d ? (int)d->layout.axes.size() : 0; }
    int RawButtonCount() const override { const auto* d = Dev(); return d ? (int)d->layout.buttons.size() : 0; }
    int RawHatCount() const override { const auto* d = Dev(); return d ? (int)d->layout.hats.size() : 0; }
    int16_t RawAxis(int i) const override { const auto* d = Dev(); if (!d || i < 0 || i >= RawAxisCount()) return 0; return State(*d).axes[i]; }
    bool RawButton(int i) const override { const auto* d = Dev(); if (!d || i < 0 || i >= RawButtonCount()) return false; return (State(*d).buttons >> i) & 1u; }
    uint8_t RawHat(int i) const override { const auto* d = Dev(); if (!d || i < 0 || i >= RawHatCount()) return 0; return State(*d).hats[i]; }
    int PowerLevel() const override { return -1; }
private:
    const RawPads::Impl::Dev* Dev() const { for (const auto& d : m_impl->devs) if (d.id == m_id) return &d; return nullptr; }   // null once unplugged: reads as 0
    static win32hid::PadState State(const RawPads::Impl::Dev& d) { return win32hid::Evaluate(d.layout, d.pressed, d.rawAxes, d.rawHats); }
    const RawPads::Impl* m_impl; int m_id; std::string m_name; uint16_t m_vid, m_pid;
};
}

std::unique_ptr<Gamepad> RawPads::Open(int index) {
    if (index < 0 || index >= (int)m->devs.size()) return nullptr;
    const Impl::Dev& d = m->devs[(size_t)index];
    return std::unique_ptr<Gamepad>(new RawPad(m, d.id, d.name, d.vid, d.pid));
}

} // namespace okay::platform::own
#endif
