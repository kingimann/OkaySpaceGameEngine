#pragma once
// Pure part of the own Windows backend's XInput controllers: which of the four XInput slots are connected, the
// hot-plug changes between two scans (GamepadAdded / GamepadRemoved), device index <-> slot, and the rumble timer.
// No Windows headers: tested on Linux by tests/test_platform_win32_pure.cpp; OwnWin32.cpp feeds it XInputGetState results.
//
// Why a tracker. XInput has no "controller connected" message: the only way to know is to ask each slot with
// XInputGetState, and Microsoft's XInput guidance says asking a slot that has no controller is slow and should not be
// done every frame. So connected slots are asked on every scan (cheap, and a pad pulled out is noticed at once), the
// empty slots only on a full scan: every kEmptySlotScanMs, or at once after a WM_DEVICECHANGE hint.
//
// Rumble. XInputSetState holds a motor speed until it is changed, while okay::platform::Gamepad::Rumble asks for
// "this strength for `ms` milliseconds" (the SDL meaning, and what PadSet's rumble model expects). RumbleTimer
// remembers when each slot has to be switched off; the backend sends the zero speeds when Expired() says so.
//
// Written from Microsoft's documentation of XInputGetState (ERROR_SUCCESS / ERROR_DEVICE_NOT_CONNECTED, the four
// user indices) and XInputSetState (XINPUT_VIBRATION, wLeftMotorSpeed / wRightMotorSpeed held until changed) and its
// "Getting Started With XInput" advice on polling disconnected controllers. Our own design otherwise.
#include <cstdint>
#include <deque>

namespace okay::platform::win32xi {

constexpr int kSlots = 4;
constexpr std::uint64_t kEmptySlotScanMs = 1000;
constexpr int kFirstInstanceId = 1;   // slot s has instance id 1 + s (the Raw Input pads start at 100, OwnWin32Hid.cpp)

inline int InstanceIdOfSlot(int slot) { return kFirstInstanceId + slot; }

struct SlotChange {
    bool added = false;
    int slot = -1;
    int instanceId = -1;
};

class SlotTracker {
public:
    /// The slots to ask with XInputGetState now: the connected ones always, every slot on a full scan.
    unsigned SlotsToAsk(std::uint64_t nowMs) const { return FullScanDue(nowMs) ? 0xFu : mask_; }
    bool FullScanDue(std::uint64_t nowMs) const { return !scanned_ || hint_ || nowMs < lastFull_ || nowMs - lastFull_ >= kEmptySlotScanMs; }
    /// WM_DEVICECHANGE (a device arrived or left somewhere): the next scan asks every slot.
    void DeviceChangeHint() { hint_ = true; }

    /// The answers of one scan. `asked`: the slots that were asked (from SlotsToAsk); `answered`: those that returned
    /// ERROR_SUCCESS. Slots not asked keep their state. The very first scan only sets the starting state, silently:
    /// pads plugged in before it are found by PadSet::OpenAll, not by events.
    void Update(unsigned asked, unsigned answered, std::uint64_t nowMs) {
        asked &= 0xFu; answered &= asked;
        const unsigned next = (mask_ & ~asked) | answered;
        if (asked == 0xFu) { lastFull_ = nowMs; hint_ = false; }
        if (!scanned_) { scanned_ = true; mask_ = next; return; }
        for (int s = 0; s < kSlots; ++s) {   // removals first, so an index freed by a removal is not handed out twice
            const unsigned bit = 1u << s;
            if ((mask_ & bit) && !(next & bit)) changes_.push_back({false, s, InstanceIdOfSlot(s)});
        }
        for (int s = 0; s < kSlots; ++s) {
            const unsigned bit = 1u << s;
            if (!(mask_ & bit) && (next & bit)) changes_.push_back({true, s, InstanceIdOfSlot(s)});
        }
        mask_ = next;
    }
    bool PopChange(SlotChange& c) { if (changes_.empty()) return false; c = changes_.front(); changes_.pop_front(); return true; }

    bool Scanned() const { return scanned_; }
    unsigned Mask() const { return mask_; }
    bool Connected(int slot) const { return slot >= 0 && slot < kSlots && ((mask_ >> slot) & 1u); }
    int Count() const { int n = 0; for (int s = 0; s < kSlots; ++s) n += (int)((mask_ >> s) & 1u); return n; }
    /// Device index (as Backend::OpenGamepad takes it) -> slot: the index-th connected slot, or -1.
    int SlotOfIndex(int index) const {
        if (index < 0) return -1;
        int seen = 0;
        for (int s = 0; s < kSlots; ++s) if ((mask_ >> s) & 1u) { if (seen == index) return s; ++seen; }
        return -1;
    }
    /// Slot -> device index, or -1 when the slot is not connected.
    int IndexOfSlot(int slot) const {
        if (!Connected(slot)) return -1;
        int i = 0;
        for (int s = 0; s < slot; ++s) i += (int)((mask_ >> s) & 1u);
        return i;
    }

private:
    unsigned mask_ = 0;
    bool scanned_ = false, hint_ = false;
    std::uint64_t lastFull_ = 0;
    std::deque<SlotChange> changes_;
};

/// When each slot's motors must be switched off. Start() for every Rumble call; the backend calls Expired(now) once
/// per event poll and sends zero speeds to the slots it returns.
class RumbleTimer {
public:
    /// A rumble of strength lo/hi for `ms` began at `nowMs`. A zero strength or zero duration means "off now":
    /// the backend sends what was asked (zero speeds for a zero duration) and nothing is left to switch off later.
    /// Returns the speeds to send now.
    void Start(int slot, std::uint16_t lo, std::uint16_t hi, std::uint32_t ms, std::uint64_t nowMs, std::uint16_t& sendLo, std::uint16_t& sendHi) {
        if (slot < 0 || slot >= kSlots) { sendLo = sendHi = 0; return; }
        const bool on = (lo || hi) && ms > 0;
        active_[slot] = on;
        until_[slot] = on ? nowMs + ms : 0;
        sendLo = on ? lo : 0; sendHi = on ? hi : 0;
    }
    /// The slots (bit mask) whose rumble time is over; they are forgotten, so each is returned once.
    unsigned Expired(std::uint64_t nowMs) {
        unsigned out = 0;
        for (int s = 0; s < kSlots; ++s)
            if (active_[s] && nowMs >= until_[s]) { active_[s] = false; out |= 1u << s; }
        return out;
    }
    bool Active(int slot) const { return slot >= 0 && slot < kSlots && active_[slot]; }
    /// The pad left: nothing to switch off any more.
    void Forget(int slot) { if (slot >= 0 && slot < kSlots) active_[slot] = false; }

private:
    bool active_[kSlots] = {false, false, false, false};
    std::uint64_t until_[kSlots] = {0, 0, 0, 0};
};

} // namespace okay::platform::win32xi
