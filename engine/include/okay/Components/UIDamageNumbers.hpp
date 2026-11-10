#pragma once
// Floating combat text: damage numbers, heals, "MISS", XP gains. Put it on any UI object (the Canvas
// is a good home) and call Spawn(). Each number is a small screen-space text that rises, drifts and
// fades, then removes itself. It uses ordinary text objects, so it draws on every backend.
//
//   dn->Spawn("-25", UIDamageNumbers::Kind::Normal, {640, 300});          // screen position
//   dn->SpawnAtWorld("CRIT 90", UIDamageNumbers::Kind::Crit, {3, 1.8, 0});  // follows a 3D point
//
// Scripts: `ui_damage_number("-25", 640, 300)` or `ui_damage_number("CRIT", 640, 300, "crit")`.
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/UIWorldContext.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Render/Color.hpp"
#include <string>
#include <vector>

namespace okay {

class UIDamageNumbers final : public Behaviour {
public:
    enum class Kind { Normal = 0, Crit = 1, Heal = 2, Miss = 3, Info = 4 };

    float lifetime = 1.1f;          ///< seconds a number lives
    float riseSpeed = 70.0f;        ///< pixels per second upward
    float gravity = 0.0f;           ///< pixels per second squared pulling the number down (a lob)
    float spread = 36.0f;           ///< pixels of sideways drift, picked per number (the same every run)
    float textSize = 3.0f;          ///< font pixel size of a normal number
    float critScale = 1.6f;         ///< crits are drawn this much bigger
    float popScale = 1.35f;         ///< every number starts this much bigger and settles
    int   maxActive = 32;           ///< the oldest number goes when there are more
    Vec2  spawnAt{640.0f, 300.0f};  ///< where the Hit / Crit / Heal button actions put a number (canvas pixels)
    Color normalColor{1.0f, 0.86f, 0.30f, 1.0f};
    Color critColor{1.0f, 0.38f, 0.20f, 1.0f};
    Color healColor{0.35f, 0.92f, 0.52f, 1.0f};
    Color missColor{0.72f, 0.74f, 0.80f, 1.0f};
    Color infoColor{0.55f, 0.80f, 1.0f, 1.0f};

    /// A number at a point in canvas pixels (its centre). The number is a child of the scene's Canvas.
    GameObject* Spawn(const std::string& text, Kind kind, Vec2 at);
    /// A number that stays over a 3D point (needs a camera: the host fills UIWorld().project).
    /// Falls back to nothing on screen while the point is behind the camera.
    GameObject* SpawnAtWorld(const std::string& text, Kind kind, Vec3 at);
    /// Remove every number now.
    void Clear();
    int  ActiveCount() const { return (int)m_live.size(); }
    /// The text object of the i-th live number (oldest first), or nullptr.
    GameObject* NumberObject(int i) const { return i >= 0 && i < (int)m_live.size() ? m_live[(std::size_t)i].go : nullptr; }

    /// What a button's On Click calls: "Hit", "Crit", "Heal", "Miss" or "Info", with the amount as the argument.
    bool RunAction(const std::string& fn, float amount);
    static Kind KindFromName(const std::string& name);
    Color ColorFor(Kind k) const;

    void Update(float dt) override;
    void OnDisable() override { Clear(); }
    void OnDestroy() override { m_live.clear(); }

private:
    struct Live {
        GameObject* go = nullptr;
        Vec2 start{};
        Vec3 world{};
        bool inWorld = false;
        float age = 0.0f;
        float drift = 0.0f;
        float scale = 1.0f;
        Color color;
    };
    GameObject* Make(const std::string& text, Kind kind, Live& l);
    void Place(Live& l);
    std::vector<Live> m_live;
    unsigned m_counter = 0;
};

} // namespace okay
