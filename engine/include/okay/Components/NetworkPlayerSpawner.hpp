#pragma once
// ---------------------------------------------------------------------------
// NetworkPlayerSpawner — drop-in networked players. Point it at a player TEMPLATE
// (an inactive object already in the scene, or a .okayprefab file) and, next to a
// NetworkManager, it:
//   * spawns a copy for the LOCAL player (keeps its controllers + camera + hand) and
//     registers it as the manager's broadcast avatar, and
//   * spawns a copy for EVERY remote peer that joins — stripped of input/camera so it
//     can't be driven locally, with a NetworkAvatarMotion added so it turns and
//     animates from the position the network already replicates.
//
// So remote players appear as real characters (not sprites) and move/face/animate
// correctly, with zero networking code. This is the "easy multiplayer" piece on top
// of the multiplayer-safe controllers.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Net/NetworkManager.hpp"
#include "okay/Components/NetworkAvatarMotion.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Components/ThirdPersonShooterController.hpp"
#include "okay/Components/TopDownController.hpp"
#include "okay/Components/ClickToMoveController.hpp"
#include "okay/Components/FreeRoamController.hpp"
#include "okay/Components/VehicleController.hpp"
#include "okay/Components/VehicleController2D.hpp"
#include "okay/Components/CharacterController2D.hpp"
#include "okay/Components/CharacterController3D.hpp"
#include "okay/Components/FirstPersonHand.hpp"
#include "okay/Components/Flashlight.hpp"
#include <string>

namespace okay {

class NetworkPlayerSpawner : public Behaviour {
public:
    std::string playerTemplate = "PlayerTemplate"; ///< name of an inactive scene object to clone
    std::string prefabFile;                         ///< OR a .okayprefab path (takes priority if set)
    Vec3  spawnPoint{0.0f, 0.0f, 0.0f};             ///< where players appear
    float spawnSpread = 1.5f;                       ///< stagger remotes so they don't stack
    bool  spawnLocalPlayer = true;                  ///< also spawn + own the local player
    char  glyph = '@';                              ///< avatar glyph for the manager roster

    void Start() override {
        NetworkManager* nm = FindManager();
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        if (!nm || !s) return;
        // Hide the template so it isn't a live player of its own.
        if (!playerTemplate.empty()) if (GameObject* t = s->Find(playerTemplate)) t->active = false;
        // Every peer that joins gets a real-character proxy.
        nm->SetRemoteFactory([this](std::uint32_t id, char) { return SpawnPlayer((int)id, false); });
        // Our own player.
        if (spawnLocalPlayer) {
            GameObject* me = SpawnPlayer(-1, true);
            if (me && me->transform) nm->SetLocalAvatar(me->transform, glyph);
        }
    }

    /// Spawn one player from the template/prefab. `local` keeps it fully driveable
    /// (controllers + camera); otherwise it's turned into a network proxy. Public so a
    /// script can spawn extra avatars or so tests can exercise it.
    GameObject* SpawnPlayer(int peerId, bool local) {
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        if (!s) return nullptr;
        GameObject* go = nullptr;
        if (!prefabFile.empty()) go = SceneSerializer::InstantiateFromFile(*s, prefabFile);
        if (!go && !playerTemplate.empty())
            if (GameObject* t = s->Find(playerTemplate)) go = s->Instantiate(*t);
        if (!go) return nullptr;
        go->active = true;
        go->name = local ? "LocalPlayer" : ("Player_" + std::to_string(peerId));
        Vec3 p = spawnPoint;
        if (!local) {                                  // fan remotes out around the spawn
            p.x += spawnSpread * float((peerId % 5) - 2);
            p.z += spawnSpread * float((peerId / 5) % 5);
        }
        if (go->transform) go->transform->localPosition = p;
        if (!local) MakeProxy(go);
        return go;
    }

private:
    // Turn a freshly cloned player into a remote proxy: remove everything that would
    // read local input or hijack rendering, and add a motion driver so it looks alive.
    static void MakeProxy(GameObject* go) {
        StripInput(go);
        if (!go->GetComponent<NetworkAvatarMotion>()) go->AddComponent<NetworkAvatarMotion>();
    }
    static void StripInput(GameObject* go) {
        if (!go) return;
        go->RemoveComponent<Camera>();
        go->RemoveComponent<FirstPersonController>();
        go->RemoveComponent<ThirdPersonController>();
        go->RemoveComponent<ThirdPersonShooterController>();
        go->RemoveComponent<TopDownController>();
        go->RemoveComponent<ClickToMoveController>();
        go->RemoveComponent<FreeRoamController>();
        go->RemoveComponent<VehicleController>();
        go->RemoveComponent<VehicleController2D>();
        go->RemoveComponent<CharacterController2D>();
        go->RemoveComponent<CharacterController3D>();
        go->RemoveComponent<FirstPersonHand>();
        go->RemoveComponent<Flashlight>();
        if (go->transform)
            for (Transform* c : go->transform->Children())
                if (c) StripInput(c->gameObject);
    }
    NetworkManager* FindManager() const {
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        if (!sc) return nullptr;
        for (const auto& up : sc->Objects())
            if (GameObject* g = up.get())
                if (auto* nm = g->GetComponent<NetworkManager>()) return nm;
        return nullptr;
    }
};

} // namespace okay
