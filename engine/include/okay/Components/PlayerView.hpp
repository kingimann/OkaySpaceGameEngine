#pragma once
// One player, two views. A part of the Player Controller (View = Both): it sits
// next to the First Person and Third Person controllers and blends the camera
// between them (GTA-style). When both are on the player, the view you are going to is the only one that moves the
// body: first person is always the FirstPersonController (eye, bob, zoom), and
// third person is always the orbit. The template only chooses the starting view.
// With Allow Toggle off, and until a script calls SetView, the controller's own
// camera is left alone.
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

    /// After the controllers (-10) and the object hold (90), before IK (100).
    /// The camera blend then sees the grip the item is already in this frame.
    int ExecutionOrder() const override { return 95; }

    bool  allowToggle = false;          ///< V / gamepad switches view. Off: controller is unchanged.
    View  defaultView = View::Third;    ///< view a fresh play starts in
    char  toggleKey = 'v';              ///< 0 = no key
    int   toggleGamepadButton = 4;      ///< Back. -1 = none. (No separate stick-click id.)
    enum class SwitchEase { Linear = 0, Smooth = 1, In = 2, Out = 3 };

    float blendTime = 0.30f;            ///< seconds for a full switch; a reverse starts from here
    SwitchEase switchEase = SwitchEase::Smooth;
    bool  snapSwitch = false;          ///< true: the camera jumps, no blend
    Vec3  eyeOffset{0, 0, 0};           ///< added at the eyes (local: +X right, +Y up, +Z back)
    float thirdPersonDistance = -1.0f;  ///< orbit distance while this view is driving; < 0 = the controller's own
    float shoulderOffset = 0.0f;        ///< over-the-shoulder shift while this view is driving

    /// 0 = first person, 1 = third person. The view asked for (the camera eases there).
    void SetView(int view);
    int  GetView() const;
    void Toggle();
    /// 0 at the eyes, 1 at the orbit, after easing. The raw blend is what the timer advances.
    float EasedThird() const;

    /// The controller should run its normal camera. False when the toggle is off
    /// and no script has called SetView.
    bool Driving() const { return allowToggle || m_scripted; }
    /// True while the camera is between the two views.
    bool Blending() const;
    /// 0 at the eyes, 1 at the orbit. Controllers fade bob and landing shake with this.
    float ThirdAmount() const { return EasedThird(); }
    float FirstAmount() const { return 1.0f - EasedThird(); }
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
    void Start() override { ArmDefault(); m_live = true; RememberRestPitch(); }
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
    float m_savedNear = -1.0f;   ///< camera near plane before first person pulled it in; < 0 = not held
    bool  m_fpApplied = false;   ///< this view has put the body into the first-person setup
    float m_restPitch = 18.0f;   ///< third-person pitch before first person widened the look
    bool  m_haveRestPitch = false;
    bool  m_pitchHoming = false; ///< an illegal orbit pitch is easing back to m_restPitch
    float m_homeFrom = 0.0f;     ///< pitch where that ease started, so the clamp can hold it

    void ArmDefault();
    void StepBlend(float dt);
    void RememberRestPitch();
    void NoteIllegalPitch();
    void EasePitchHome(float dt);
    float Ease01(float u) const;
    bool  CameraNearEyes(Camera* cam) const;
    void  PullCameraIn(Camera* cam, const Vec3& eye, Vec3& want) const;
    void DestLimits(bool thirdController, float nativeMin, float nativeMax, float& lo, float& hi) const;
    void ApplyHead();
    void PlaceCamera(float dt);
    void KeepLensClear(Camera* cam) const;
};

/// True when `go`'s FirstPersonController should move the body this frame.
/// Only that controller: always. Both: the Player View's target (first person
/// while switching to the eyes, third while switching to the orbit).
bool FirstPersonOwns(GameObject* go);
/// Both a first- and a third-person controller are on `go`.
bool PlayerHasBothViews(GameObject* go);
/// The eye camera should be written this frame. During a blend it is, so the
/// view can read the first-person pose; once settled in third person it is not.
bool FirstPersonWritesEye(GameObject* go);
/// The orbit should be written onto the camera. During a blend the view blends
/// toward the stored orbit instead, so the eye pose is left intact.
bool ThirdPersonWritesCamera(GameObject* go);
/// Copy look, stance and lean from the orbit controller. No-op until that
/// controller has actually run, so a fresh first-person start keeps its own pitch.
void HandOffToFirstPerson(GameObject* go);
/// Copy look, stance and lean from the eye controller onto the orbit.
void HandOffToThirdPerson(GameObject* go);

} // namespace okay
