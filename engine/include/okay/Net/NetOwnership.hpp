#pragma once
// ---------------------------------------------------------------------------
// NetOwnership — the one helper that makes every controller multiplayer-safe.
//
// In a networked session each peer should only DRIVE the player (vehicle, etc.) it
// owns; everyone else's copy is moved by NetworkSync from the owner's broadcasts. A
// controller that reads local input on a REMOTE proxy would fight the network and
// teleport other players around. So every input-reading controller gates its Update
// on IsLocallyControlled(): it returns true in single-player (no NetworkSync) and for
// the locally-owned object, and false for a remote proxy.
//
// Drop a NetworkSync on the player and the controllers below "just work" online — no
// other change needed to add multiplayer or Steam.
// ---------------------------------------------------------------------------
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/NetworkSync.hpp"

namespace okay {

/// True if LOCAL input should drive `go`: either nothing networks it, or a NetworkSync
/// governing it (on this object or any ancestor — e.g. the player root for a camera/
/// controller child) says this peer owns it. Remote proxies return false.
inline bool IsLocallyControlled(const GameObject* go) {
    for (Transform* t = go ? go->transform : nullptr; t; t = t->Parent())
        if (t->gameObject)
            if (auto* ns = t->gameObject->GetComponent<NetworkSync>())
                return ns->IsLocallyOwned();
    return true;   // no NetworkSync anywhere up the chain -> single-player / always local
}

} // namespace okay
