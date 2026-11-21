#pragma once
// Gamepad hot-plug and player slots for the UWP backend (pure logic, no Windows headers, no threads).
// The glue calls OnAdded / OnRemoved from Windows.Gaming.Input.Gamepad.GamepadAdded / GamepadRemoved (documented events) and Tick(nowMs)
// once a frame. Written from our own design: a pad gets the lowest free player slot; if it vanishes (battery, cable, a short radio drop)
// its slot and instance id are kept for a grace time, so the same pad coming back is the same player; after the grace time the slot is
// freed and the "removed" event is sent. More pads than slots are refused, not dropped silently. UNVERIFIED on a real console or PC.
#include <cstdint>
#include <string>
#include <vector>

namespace okay::xbox {

struct PadHubEvent {
    enum Kind { Added, Reconnected, Lost, Removed, Refused } kind = Added;   // Lost = gone but slot still held; Removed = slot freed
    std::string key; int slot = -1; int instanceId = -1;
};

class PadHub {
public:
    explicit PadHub(int maxSlots = 8, std::uint64_t graceMs = 10000, int firstInstanceId = 1)
        : slots_((size_t)(maxSlots < 1 ? 1 : maxSlots)), grace_(graceMs), nextId_(firstInstanceId) {}

    /// A pad appeared. `key` is a stable device id (the glue uses the pad's NonRoamableId). Returns its slot, or -1 when all slots are taken.
    int OnAdded(const std::string& key, std::uint64_t nowMs) {
        Tick(nowMs);
        for (size_t i = 0; i < slots_.size(); ++i)
            if (slots_[i].used && slots_[i].key == key) {
                const bool wasLost = !slots_[i].present;
                slots_[i].present = true;
                if (wasLost) Push(PadHubEvent::Reconnected, (int)i);
                return (int)i;
            }
        for (size_t i = 0; i < slots_.size(); ++i)
            if (!slots_[i].used) {
                slots_[i] = Slot(); slots_[i].used = true; slots_[i].present = true; slots_[i].key = key; slots_[i].id = nextId_++;
                Push(PadHubEvent::Added, (int)i); return (int)i;
            }
        PadHubEvent e; e.kind = PadHubEvent::Refused; e.key = key; events_.push_back(e); return -1;
    }
    /// A pad went away. Its slot is held for the grace time.
    void OnRemoved(const std::string& key, std::uint64_t nowMs) {
        Tick(nowMs);
        for (size_t i = 0; i < slots_.size(); ++i)
            if (slots_[i].used && slots_[i].present && slots_[i].key == key) {
                slots_[i].present = false; slots_[i].lostAt = nowMs; Push(PadHubEvent::Lost, (int)i); return;
            }
    }
    /// Frees slots whose pad stayed away longer than the grace time.
    void Tick(std::uint64_t nowMs) {
        for (size_t i = 0; i < slots_.size(); ++i)
            if (slots_[i].used && !slots_[i].present && nowMs >= slots_[i].lostAt && nowMs - slots_[i].lostAt >= grace_) {
                Push(PadHubEvent::Removed, (int)i); slots_[i] = Slot();
            }
    }
    int SlotOf(const std::string& key) const {
        for (size_t i = 0; i < slots_.size(); ++i) if (slots_[i].used && slots_[i].key == key) return (int)i;
        return -1;
    }
    int InstanceId(int slot) const { return Valid(slot) && slots_[(size_t)slot].used ? slots_[(size_t)slot].id : -1; }
    bool Present(int slot) const { return Valid(slot) && slots_[(size_t)slot].used && slots_[(size_t)slot].present; }
    int PresentCount() const { int n = 0; for (const Slot& s : slots_) if (s.used && s.present) ++n; return n; }
    int SlotCount() const { return (int)slots_.size(); }
    /// Takes the events since the last call.
    std::vector<PadHubEvent> TakeEvents() { std::vector<PadHubEvent> e; e.swap(events_); return e; }
private:
    struct Slot { bool used = false, present = false; std::string key; int id = -1; std::uint64_t lostAt = 0; };
    bool Valid(int s) const { return s >= 0 && (size_t)s < slots_.size(); }
    void Push(PadHubEvent::Kind k, int slot) {
        PadHubEvent e; e.kind = k; e.slot = slot; e.key = slots_[(size_t)slot].key; e.instanceId = slots_[(size_t)slot].id; events_.push_back(e);
    }
    std::vector<Slot> slots_; std::uint64_t grace_; int nextId_; std::vector<PadHubEvent> events_;
};

} // namespace okay::xbox
