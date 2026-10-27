#pragma once
// One player, two views. Sits next to a First Person or Third Person controller
// and blends the camera between them (GTA-style). With Allow Toggle off, and
// until a script calls SetView, the controller's own camera is left alone.
#include "okay/Components/ControllerFeel.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include <algorithm>
#include <cmath>

namespace okay {

class PlayerView : public Behaviour {
public:
    enum class View { First = 0, Third = 1 };

    /// Runs after the controllers (order -10) have placed their own camera, and
    /// before IK (100), so a blend overwrites the view the player actually sees.
    int ExecutionOrder() const override { return 40; }

    bool  allowToggle = false;          ///< V / gamepad switches view. Off: controller is unchanged.
    View  defaultView = View::Third;    ///< view a fresh play starts in
    char  toggleKey = 'v';              ///< 0 = no key
    int   toggleGamepadButton = 4;      ///< Back. -1 = none. (No separate stick-click id.)
    float blendTime = 0.35f;            ///< seconds for a full switch; a reverse starts from here
    Vec3  eyeOffset{0, 0, 0};           ///< added at the eyes (local: +X right, +Y up, +Z back)
    float thirdPersonDistance = -1.0f;  ///< orbit distance while this view is driving; < 0 = the controller's own
    float shoulderOffset = 0.0f;        ///< over-the-shoulder shift while this view is driving

    /// 0 = first person, 1 = third person. The view asked for (the camera eases there).
    void SetView(int view);
    int  GetView() const;
    void Toggle();

    /// The controller should run its normal camera. False when the toggle is off
    /// and no script has called SetView.
    bool Driving() const { return allowToggle || m_scripted; }
    /// True while the camera is between the two views.
    bool Blending() const;
    /// 0 at the eyes, 1 at the orbit. Controllers fade bob and landing shake with this.
    float ThirdAmount() const { return m_blend; }
    float FirstAmount() const { return 1.0f - m_blend; }
    bool  TargetFirst() const { return m_target == View::First; }

    /// Mouse-look limits for this frame, in the controller's own pitch convention.
    /// While a blend is in progress the limits widen so the look doesn't snap.
    void LookLimits(bool thirdController, float nativeMin, float nativeMax, float& lo, float& hi) const;
    /// Ease `pitch` into the destination view's limits over the blend. No-op when
    /// the toggle isn't driving or the pitch is already legal.
    void ConformPitch(float& pitch, bool thirdController, float nativeMin, float nativeMax, float dt);

    /// The Player View on this object, a parent, or (create = false) the first one
    /// in the scene. `create` adds one to a first- or third-person controller.
    static PlayerView* FindForScript(GameObject* self, bool create);

    void Awake() override { ArmDefault(); }
    void Start() override { ArmDefault(); m_live = true; }
    void Update(float dt) override;
    void LateUpdate(float dt) override;

private:
    View  m_target = View::Third;
    float m_blend = 1.0f;             ///< 0 first .. 1 third
    bool  m_scripted = false;
    bool  m_live = false;
    bool  m_armed = false;
    FollowPivot m_pivot;
    SpringArm   m_arm;
    LookSmoother m_smooth;
    float m_aimT = 0.0f;
    float m_stanceDrop = 0.0f;
    float m_shake = 0.0f;

    void ArmDefault();
    void StepBlend(float dt);
    void DestLimits(bool thirdController, float nativeMin, float nativeMax, float& lo, float& hi) const;
    void ApplyHead();
    void PlaceCamera(float dt);
};

} // namespace okay
