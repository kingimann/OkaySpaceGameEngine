#pragma once
// Keeps the UWP backend's pad hub (XboxPadHub.hpp through UwpBackend::PadConnected / PadDisconnected) in step with the pads the
// Windows side can see right now. Pure C++ (no Windows headers); tested on Linux by tests/test_xbox_uwp_runtime.cpp (PadSyncTests).
//
// How it is used. Once a frame, on the UI thread, the shell lists the connected pads by a STABLE key (the pad's
// RawGameController.NonRoamableId, which stays the same when the same pad comes back) and the engine bridge calls Sync with that
// list and a factory that makes a PadDevice reading the pad with that key. A key seen for the first time gets a device and a hub
// slot (GamepadAdded); a key that is gone loses its slot after the hub's grace time (GamepadRemoved), and its device is freed only
// AFTER the backend stopped pointing at it. A pad refused because every slot is taken is not kept and is offered again on the
// next Sync, so it joins as soon as a slot frees. Polling the list, rather than handling Gamepad.GamepadAdded / GamepadRemoved,
// keeps everything on the UI thread: those events arrive on another thread and the backend is not thread-safe.
// Our own design. UNVERIFIED with real pads.
#include "XboxUwpBackend.hpp"
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace okay::xbox {

class PadSync {
public:
    using Factory = std::function<std::unique_ptr<PadDevice>(const std::string& key)>;

    /// `keysNow`: the pads connected now (repeats and empty keys ignored). Returns how many pads got a slot in this call.
    int Sync(UwpBackend& b, const std::vector<std::string>& keysNow, const Factory& make) {
        std::set<std::string> now;
        for (const std::string& k : keysNow) if (!k.empty()) now.insert(k);
        for (auto it = devs_.begin(); it != devs_.end();) {
            if (now.count(it->first)) { ++it; continue; }
            b.PadDisconnected(it->first);   // the backend drops its pointer to the device first...
            it = devs_.erase(it);           // ...then the device goes
        }
        int joined = 0;
        for (const std::string& k : now) {
            if (devs_.count(k)) continue;
            std::unique_ptr<PadDevice> d = make ? make(k) : nullptr;
            if (!d) continue;
            if (b.PadConnected(k, d.get()) < 0) {
                b.PadDisconnected(k);       // refused: make sure nothing points at the device we are about to free
                continue;
            }
            devs_[k] = std::move(d);
            ++joined;
        }
        return joined;
    }
    /// Every pad leaves (shutting down): disconnect, then free.
    void Clear(UwpBackend& b) {
        for (auto& kv : devs_) b.PadDisconnected(kv.first);
        devs_.clear();
    }
    int Count() const { return (int)devs_.size(); }
    bool Has(const std::string& key) const { return devs_.count(key) != 0; }

private:
    std::map<std::string, std::unique_ptr<PadDevice>> devs_;
};

} // namespace okay::xbox
