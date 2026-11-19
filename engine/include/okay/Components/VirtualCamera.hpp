#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Math/Vec2.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/CameraCollision.hpp"
#include "okay/Components/CameraMath.hpp"
#include "okay/Components/CameraRoom.hpp"
#include "okay/Components/DollyPath.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Spline.hpp"
#include <cmath>
#include <string>
#include <unordered_set>
#include <vector>

namespace okay {

/// A Cinemachine-style virtual camera. Unlike a real Camera it never renders;
/// instead it describes a *shot* — where to stand (Follow), what to aim at
/// (LookAt), the lens (field of view) and how smoothly to ease there (damping),
/// plus optional handheld shake. A CinemachineBrain on the real (main) camera
/// picks the highest-`priority` enabled virtual camera and blends the real
/// camera toward its solved pose. Author several and switch shots by changing
/// priorities — exactly like Cinemachine.
class VirtualCamera : public Behaviour {
public:
    /// Higher wins. The brain makes the highest-priority enabled vcam "live".
    int priority = 10;

    /// How the follow offset is interpreted (Cinemachine's Binding Mode):
    ///  - WorldSpace:    offset is fixed in world axes (a static framing).
    ///  - LockToTarget:  offset rotates with the follow target — an orbital/3rd-person
    ///                   chase cam that stays behind the target as it turns.
    enum class BindingMode { WorldSpace, LockToTarget };
    BindingMode bindingMode = BindingMode::WorldSpace;

    /// Name of the GameObject to position relative to (the body). Empty = stay put.
    std::string follow;
    /// Name of the GameObject to aim at (the aim). Empty = keep follow's facing.
    std::string lookAt;
    /// World-space offset from the follow target (e.g. behind & above for 3rd person).
    Vec3 followOffset{0.0f, 3.0f, -8.0f};
    /// Extra offset added to the LookAt target before aiming (e.g. aim at the head,
    /// not the feet). Cinemachine's "Tracked Object Offset".
    Vec3 lookAtOffset{0.0f, 0.0f, 0.0f};
    /// Aim dead zone (degrees): the camera only re-aims once the target drifts beyond
    /// this angle from screen center, so small target motion doesn't jitter the shot
    /// (a simplified Cinemachine Composer dead zone). 0 = always track.
    float aimDeadZone = 0.0f;

    /// Easing rates (higher = snappier; <= 0 = instant). Frame-rate independent.
    float positionDamping = 2.0f;
    float rotationDamping  = 6.0f;

    /// Lens: the field of view (deg) the brain applies to the real camera when live.
    float fieldOfView = 60.0f;
    /// Dutch angle (Cinemachine): a constant roll of the shot around the view axis,
    /// in degrees. 0 = level horizon. Tilt for tension / stylised framing.
    float dutch = 0.0f;

    /// Confiner (Cinemachine): clamp the solved camera position to an axis-aligned
    /// box so the shot never leaves a region (keep the camera inside the level).
    bool confine = false;
    Vec3 confineMin{-1e9f, -1e9f, -1e9f};
    Vec3 confineMax{ 1e9f,  1e9f,  1e9f};

    /// Handheld noise: peak positional shake (world units) and its speed.
    float shakeAmplitude = 0.0f;
    float shakeFrequency = 1.0f;
    /// How fast a one-shot impulse (AddImpulse) decays (per second). Higher = snappier.
    float impulseDecay = 3.0f;

    /// Fire a transient camera shake — a "Cinemachine Impulse". Call it on an event
    /// (explosion, landing, hit) and the live camera kicks, then settles. Additive.
    void AddImpulse(float strength) { m_impulse += strength; }

    /// FreeLook (Cinemachine's orbital rig): orbit the Follow target on a sphere,
    /// driven by yaw/pitch. When on, this overrides the body offset and aim — the
    /// camera always frames the target. Great for 3rd-person player cameras.
    bool freeLook = false;
    float orbitYaw = 0.0f;       ///< horizontal orbit angle (deg)
    float orbitPitch = 15.0f;    ///< vertical orbit angle (deg), clamped below
    float orbitRadius = 8.0f;    ///< distance from the target
    float orbitHeight = 1.0f;    ///< height of the orbit pivot above the target
    float orbitMinPitch = -20.0f, orbitMaxPitch = 70.0f;
    /// Drive yaw/pitch from the mouse. orbitButton = mouse button to hold (-1 = always);
    /// mouseSensitivity scales the look speed.
    bool orbitInput = true;
    int  orbitButton = 1;        ///< 1 = right mouse button
    float mouseSensitivity = 0.2f;

    /// Tracked Dolly (Cinemachine): constrain the body to a DollyPath rail. The aim
    /// still uses LookAt/Follow, so the camera can glide along a track while framing a
    /// subject. autoDolly slides to the path point nearest the Follow target.
    /// A Spline works as the rail too (dollyPosition is then a fraction of its length).
    bool dolly = false;
    std::string dollyPath;       ///< name of the GameObject holding a DollyPath or a Spline
    float dollyPosition = 0.0f;  ///< normalized position along the path (0..1)
    bool  autoDolly = false;     ///< track the Follow target's nearest path point

    // ---- Camera rig layer (docs/camera_upgrade.md) ------------------------------------
    // Everything below is off or "classic" by default, so a scene saved before it
    // plays exactly as it did. The "Camera Rig" entries in Add Component turn on a
    // good set for each style.

    /// How the body eases toward where it should be.
    ///  - Smooth: the classic exponential ease (rate = positionDamping).
    ///  - Spring: a critically damped spring (same rate): no overshoot, and the
    ///    camera keeps its speed when the target changes direction, so it never
    ///    stops dead and never jerks.
    enum class DampingMode { Smooth, Spring };
    DampingMode dampingMode = DampingMode::Smooth;

    /// Several things to frame at once. The camera aims at the middle of the group
    /// (plus the Follow object if there is one) and steps back far enough to keep
    /// them all in view. Add and remove members while playing from scripts.
    std::vector<std::string> groupTargets;
    float groupPadding = 1.0f;       ///< extra room round the group (world units)
    bool  groupFrame = true;         ///< step back (or zoom a 2D camera) to fit the whole group

    /// Look ahead: frame a little in front of a moving target. Seconds of travel to
    /// look ahead by, the most it may shift (world units) and how quickly it eases.
    float lookaheadTime = 0.0f;
    float lookaheadMax = 3.0f;
    float lookaheadSmooth = 3.0f;

    /// Screen zones: the target may move freely inside the dead zone without the
    /// camera moving at all; in the soft zone the camera eases it back to the edge
    /// of the dead zone; beyond the soft zone the camera catches up at once. All
    /// four are fractions of the screen (0.2 = the middle fifth).
    bool  useZones = false;
    float deadZoneW = 0.15f, deadZoneH = 0.15f;
    float softZoneW = 0.7f,  softZoneH = 0.7f;
    float zoneDamping = 4.0f;        ///< how quickly the camera eases the target back out of the soft zone

    /// 2D camera: slide sideways and up and down with the target (the camera never
    /// turns), seeing `orthoSize` units up (0 = keep the camera's own size).
    bool  ortho2D = false;
    float orthoSize = 0.0f;
    /// Keep the edges of the picture inside the confine box, not just the camera's
    /// centre, so the view never shows outside the level (2D).
    bool  confineToView = false;

    /// Rooms: when on, the camera is held inside the Camera Room the target stands
    /// in, and slides to the next room when the target crosses over (2D).
    bool  useRooms = false;
    float roomBlendTime = 0.6f;

    /// Collision: when something solid is between the target and the camera, the
    /// camera moves in so it can see (a sphere sweep; triggers never count).
    bool  collision = false;
    float collisionRadius = 0.25f;   ///< how fat the probe is
    float collisionSkin = 0.1f;      ///< gap kept from the wall
    float collisionRecover = 3.0f;   ///< how quickly the camera glides back out
    float collisionPullIn = 12.0f;   ///< how quickly it eases in when "snap in" is off
    bool  collisionSnapIn = true;    ///< true: jump in at once (never inside a wall); false: ease in
    std::uint32_t collisionMask = 0xFFFFFFFFu;   ///< layers that block the camera (bit n = layer n)

    /// Fade out things that stand between the camera and the target (they become
    /// see-through, then come back).
    bool  fadeOccluders = false;
    float occlusionAlpha = 0.25f;
    float occlusionFadeSpeed = 8.0f;

    /// Blend into this shot with its own time (seconds; -1 = use the brain's) and style (-1 = the brain's;
    /// otherwise a cam::BlendStyle number: 0 Cut, 1 Linear, 2 Ease In and Out, 3 Ease In, 4 Ease Out, 5 Custom).
    float blendIn = -1.0f;
    int   blendInStyle = -1;

    /// With no LookAt object, aim at the Follow object (or the middle of the group)
    /// instead of copying its facing. The "Camera Rig" styles turn this on.
    bool  aimAtFollow = false;

    /// Add or remove a group member (by object name).
    void AddGroupTarget(const std::string& name) {
        if (name.empty()) return;
        for (const auto& n : groupTargets) if (n == name) return;
        groupTargets.push_back(name);
    }
    void RemoveGroupTarget(const std::string& name) {
        groupTargets.erase(std::remove(groupTargets.begin(), groupTargets.end(), name), groupTargets.end());
    }
    void ClearGroup() { groupTargets.clear(); }

    /// Start a shot style in one go: 0 Follow, 1 Orbit, 2 Look At. What the
    /// "Camera Rig" entries in Add Component call. Settings you have already
    /// changed that the style does not care about are left alone.
    void ApplyStyle(int style) {
        dampingMode = DampingMode::Spring;
        positionDamping = 5.0f; rotationDamping = 8.0f;
        collision = true; fadeOccluders = false; aimAtFollow = true;
        if (style == 1) {          // Orbit
            freeLook = true; orbitInput = true; orbitRadius = 6.0f; orbitHeight = 1.5f; orbitPitch = 15.0f;
            useZones = false; lookaheadTime = 0.0f;
        } else if (style == 2) {   // Look At
            freeLook = false; followOffset = {0.0f, 2.0f, -8.0f};
            useZones = true; deadZoneW = 0.12f; deadZoneH = 0.12f; softZoneW = 0.6f; softZoneH = 0.6f;
            collision = false;
        } else {                   // Follow
            freeLook = false; bindingMode = BindingMode::WorldSpace; followOffset = {0.0f, 3.0f, -8.0f};
            lookaheadTime = 0.25f; lookaheadMax = 2.0f;
            useZones = true; deadZoneW = 0.1f; deadZoneH = 0.1f; softZoneW = 0.6f; softZoneH = 0.6f;
        }
    }

    /// Advance the solved pose one step toward the targets. Driven by the brain so
    /// every vcam solves exactly once per frame, in a defined order. `live` says
    /// whether this is the shot on screen: collision and fading things out only
    /// run for the live one (the others just keep tracking so a blend starts from
    /// the right place).
    void Solve(float dt, bool live = true) {
        Scene* s = GetScene();
        if (!s || !transform) return;
        if (!(dt > 0.0f) || !std::isfinite(dt)) dt = 0.0f;
        m_zoneDt = dt;

        Camera* rc = s->mainCamera;
        const float aspect = (rc && rc->viewportAspect > 0.05f) ? rc->viewportAspect : 16.0f / 9.0f;
        const Targets tg = GatherTargets(s, aspect);
        GameObject* f = tg.f;
        GameObject* l = tg.l;
        const bool haveF = f != nullptr;
        Vec3 fp = tg.fp;
        const bool haveFp = tg.haveFp;
        const float fitDist = tg.fitDist, fitOrtho = tg.fitOrtho;
        const Vec3 fpReal = tg.fp;   // where the target really is: collision and fading measure from here, not from the look-ahead point
        // Look ahead: lean the framing toward where the target is going.
        if (haveFp && lookaheadTime > 0.0f) fp = fp + m_look.Step(fp, lookaheadTime, lookaheadMax, lookaheadSmooth, dt);
        else m_look.Reset();

        // FreeLook orbit rig: position is a point on a sphere around the target pivot.
        bool freeLookActive = freeLook && haveFp;
        Vec3 flPivot{0.0f, 0.0f, 0.0f};

        Vec3 desiredPos = transform->Position();
        if (freeLookActive) {
            if (orbitInput) {
                Vec2 m = Input::MousePosition();
                if (!m_mouseInit) { m_lastMouse = m; m_mouseInit = true; }
                Vec2 d = m - m_lastMouse;
                m_lastMouse = m;
                bool active = (orbitButton < 0) || Input::GetMouseButton(orbitButton);
                if (active) {
                    orbitYaw   += d.x * mouseSensitivity;
                    orbitPitch -= d.y * mouseSensitivity;
                }
            }
            orbitPitch = Mathf::Clamp(orbitPitch, orbitMinPitch, orbitMaxPitch);
            flPivot = fp + Vec3{0.0f, orbitHeight, 0.0f};
            Quat orot = Quat::Euler(orbitPitch, orbitYaw, 0.0f);
            desiredPos = flPivot + orot * Vec3{0.0f, 0.0f, -std::max(orbitRadius, fitDist)};
        } else if (dolly) {
            // Tracked Dolly: constrain the body to a rail; aim is handled below.
            GameObject* pg = dollyPath.empty() ? nullptr : s->Find(dollyPath);
            DollyPath* dp = pg ? pg->GetComponent<DollyPath>() : nullptr;
            const Spline* sp = (!dp && pg) ? pg->GetComponent<Spline>() : nullptr;
            if (dp) {
                if (autoDolly && haveFp)
                    dollyPosition = dp->NearestPosition(fp);
                desiredPos = dp->EvaluatePosition(dollyPosition);
            } else if (sp && sp->Count() > 0) {
                const float len = sp->Length();
                if (autoDolly && haveFp && len > 0.0f)
                    dollyPosition = sp->NearestDistance(fp) / len;
                desiredPos = sp->SampleAtDistance(dollyPosition * len).position;
            } else if (haveFp) {
                desiredPos = fp + followOffset;
            }
        } else if (haveFp) {
            // LockToTarget orbits the offset with the target's heading (chase cam);
            // WorldSpace keeps it axis-aligned (a fixed framing).
            Vec3 worldOffset = (bindingMode == BindingMode::LockToTarget && haveF)
                                   ? (f->transform->Rotation() * followOffset)
                                   : followOffset;
            if (fitDist > 0.0f) {     // a group: never closer than the offset, back far enough to fit
                const float len = worldOffset.Magnitude();
                if (len > 1e-4f && fitDist > len) worldOffset = worldOffset * (fitDist / len);
            }
            desiredPos = fp + worldOffset;
        }

        // The 2D camera's size: ours, or the real camera's, never smaller than the group needs.
        float ortho = orthoSize > 0.0f ? orthoSize : (rc ? rc->EffectiveOrthoSize() : 5.0f);
        if (ortho2D && fitOrtho > 0.0f) ortho = std::max(ortho, fitOrtho);
        m_solvedOrtho = (ortho2D && (orthoSize > 0.0f || fitOrtho > 0.0f)) ? ortho : 0.0f;

        if (!m_init) { m_pos = desiredPos; m_vel = {0.0f, 0.0f, 0.0f}; }
        else if (ortho2D && useZones && haveFp) {
            // Zones on the screen plane: the camera holds still while the target is in the dead zone.
            const float halfW = std::max(0.01f, ortho * aspect), halfH = std::max(0.01f, ortho);
            const Vec3 focus = fp + Vec3{followOffset.x, followOffset.y, 0.0f};
            const float nx = (focus.x - m_pos.x) / halfW, ny = (focus.y - m_pos.y) / halfH;
            m_pos.x += cam::ZoneCorrection(nx, deadZoneW, softZoneW, zoneDamping, dt) * halfW;
            m_pos.y += cam::ZoneCorrection(ny, deadZoneH, softZoneH, zoneDamping, dt) * halfH;
            float vz = 0.0f, z = m_pos.z;
            cam::SpringStep(z, vz, desiredPos.z, positionDamping <= 0.0f ? 0.0f : positionDamping, dt);
            m_pos.z = z;
        } else if (dampingMode == DampingMode::Spring) {
            cam::SpringStep(m_pos, m_vel, desiredPos, positionDamping <= 0.0f ? 0.0f : positionDamping, dt);
        } else {
            float tp = (positionDamping <= 0.0f) ? 1.0f : (1.0f - std::exp(-positionDamping * dt));
            m_pos = Vec3::Lerp(m_pos, desiredPos, tp);
        }
        if (!cam::AllFinite(m_pos)) { m_pos = desiredPos; m_vel = {0.0f, 0.0f, 0.0f}; }

        // Confiner: keep the body inside the allowed box (or the room the target is in).
        Vec3 cmin = confineMin, cmax = confineMax;
        bool boxOn = confine;
        if (useRooms && haveFp) {
            if (RoomBox(s, fp, dt, cmin, cmax)) boxOn = true;
        }
        if (boxOn) {
            if (confineToView || useRooms) {
                const float halfH = std::max(0.01f, ortho), halfW = std::max(0.01f, ortho * aspect);
                cam::Box2 b; b.minX = cmin.x; b.minY = cmin.y; b.maxX = cmax.x; b.maxY = cmax.y;
                const Vec2 c = cam::ConfineView({m_pos.x, m_pos.y}, {halfW, halfH}, b);
                m_pos.x = c.x; m_pos.y = c.y;
                m_pos.z = Mathf::Clamp(m_pos.z, cmin.z, cmax.z);
            } else {
                m_pos.x = Mathf::Clamp(m_pos.x, cmin.x, cmax.x);
                m_pos.y = Mathf::Clamp(m_pos.y, cmin.y, cmax.y);
                m_pos.z = Mathf::Clamp(m_pos.z, cmin.z, cmax.z);
            }
        }

        // Aim from the (damped) body position so the shot stays framed while easing.
        Quat desiredRot = m_rot;
        bool zonedAim = false;
        // What to aim at: the LookAt object, else (when asked, or for a group) the followed point.
        bool haveAim = false; Vec3 aimPoint{0.0f, 0.0f, 0.0f};
        if (l && l->transform) { aimPoint = l->transform->Position() + lookAtOffset; haveAim = true; }
        else if (haveFp && (aimAtFollow || !groupTargets.empty())) { aimPoint = fp + lookAtOffset; haveAim = true; }
        if (ortho2D) {
            desiredRot = Quat::Identity;
        } else if (freeLookActive) {
            Vec3 dir = flPivot - m_pos;
            if (dir.SqrMagnitude() > 1e-8f) desiredRot = AimRotation(dir);
        } else if (haveAim) {
            Vec3 dir = aimPoint - m_pos;
            if (dir.SqrMagnitude() > 1e-8f) {
                if (useZones && m_init) {
                    desiredRot = ZoneAim(dir, aspect);
                    zonedAim = true;
                } else {
                    // Composer dead zone: keep the current aim while the target stays within
                    // aimDeadZone degrees of screen center; only re-aim past it.
                    bool reaim = true;
                    if (m_init && aimDeadZone > 0.0f) {
                        Vec3 fwd = ViewForward(m_rot);
                        float c = Vec3::Dot(fwd.Normalized(), dir.Normalized());
                        if (c > std::cos(aimDeadZone * Mathf::Deg2Rad)) reaim = false;
                    }
                    if (reaim) desiredRot = AimRotation(dir);
                }
            }
        } else if (haveF) {
            desiredRot = f->transform->Rotation();
        }
        // Dutch: add a constant roll around the view axis to the framed orientation.
        if (std::fabs(dutch) > 1e-4f) desiredRot = desiredRot * Quat::Euler(0.0f, 0.0f, dutch);
        if (!m_init || zonedAim) m_rot = desiredRot;   // the zone rule already eases the aim
        else {
            float tr = (rotationDamping <= 0.0f) ? 1.0f : (1.0f - std::exp(-rotationDamping * dt));
            m_rot = Quat::Slerp(m_rot, desiredRot, tr);
        }
        m_init = true;

        // Collision and see-through: the live shot only.
        Vec3 bodyPos = m_pos;
        if (!fadeOccluders && !m_fade.Entries().empty()) RestoreFaded();   // switched off while the rig stays live
        if (live && (collision || fadeOccluders)) {
            GameObject* pivotGo = f ? f : (l ? l : gameObject);   // a group-only rig has neither: never null
            Vec3 pivot; bool havePivot = false;
            if (l && l->transform) { pivot = l->transform->Position() + lookAtOffset; havePivot = true; }
            else if (haveFp) { pivot = freeLookActive ? fpReal + Vec3{0.0f, orbitHeight, 0.0f} : fpReal; havePivot = true; }
            if (havePivot) {
                const Vec3 arm = m_pos - pivot;
                const float dist = arm.Magnitude();
                if (collision && dist > 1e-3f) {
                    const Vec3 dn = arm * (1.0f / dist);
                    const float len = m_arm.Step(*s, pivotGo, pivot, dn, dist, collisionRadius, collisionSkin, collisionMask,
                                                 collisionPullIn, collisionRecover, dt, collisionSnapIn);
                    bodyPos = pivot + dn * std::min(len, dist);
                } else if (!collision) m_arm.Reset();
                if (fadeOccluders && dist > 1e-3f) FadeStep(s, pivotGo, bodyPos, pivot, dt);
            }
        } else {
            m_arm.Reset();
            if (m_fade.Entries().size()) RestoreFaded();
        }

        // Handheld shake (steady) + impulse (one-shot, decaying): smooth gradient
        // noise, decorrelated per axis. Impulse rides a faster carrier so a single
        // kick reads as a sharp jolt even with no steady shake.
        Vec3 shake{0.0f, 0.0f, 0.0f};
        if (shakeAmplitude > 1e-5f) {
            m_noiseT += dt * shakeFrequency;
            shake += shakeAmplitude * Vec3{Noise(m_noiseT, 0.0f), Noise(m_noiseT, 17.3f), Noise(m_noiseT, 41.7f)};
        }
        if (m_impulse > 1e-5f) {
            m_impulseT += dt * 12.0f;
            shake += m_impulse * Vec3{Noise(m_impulseT, 3.1f), Noise(m_impulseT, 23.7f), Noise(m_impulseT, 57.2f)};
            m_impulse *= std::exp(-impulseDecay * dt);
            if (m_impulse < 1e-4f) m_impulse = 0.0f;
        }
        m_solvedPos = bodyPos + shake;

        // Mirror the solved pose onto our own transform so the vcam GameObject sits
        // where the shot is (handy for gizmos / scene-view debugging at runtime).
        transform->SetPosition(m_solvedPos);
        if (!transform->Parent()) transform->localRotation = m_rot;
    }

    /// What the shot is pointed at this frame (shared by Solve and the editor's preview).
    struct Targets {
        GameObject* f = nullptr;      ///< the Follow object (null when missing or at a broken position)
        GameObject* l = nullptr;      ///< the Look At object
        Vec3 fp{0.0f, 0.0f, 0.0f};    ///< the point followed: the Follow object, or the middle of the group
        bool haveFp = false;
        float fitDist = 0.0f;         ///< how far back a perspective group shot must stand to fit everyone
        float fitOrtho = 0.0f;        ///< the view height a 2D group shot needs
        float groupRadius = 0.0f;
        int   members = 0;
    };
    Targets GatherTargets(Scene* s, float aspect) {
        Targets t;
        t.f = follow.empty() ? nullptr : s->Find(follow);
        t.l = lookAt.empty() ? nullptr : s->Find(lookAt);
        // A target at a broken (NaN or infinite) position counts as missing: the camera stays where it is.
        if (t.f && (!t.f->transform || !cam::AllFinite(t.f->transform->Position()))) t.f = nullptr;
        if (t.l && (!t.l->transform || !cam::AllFinite(t.l->transform->Position()))) t.l = nullptr;
        if (t.f) { t.fp = t.f->transform->Position(); t.haveFp = true; }
        if (!groupTargets.empty()) {
            m_pts.clear();
            if (t.f) m_pts.push_back(t.fp);
            for (const std::string& n : groupTargets) {
                GameObject* o = s->Find(n);
                if (o && o->transform && o->active) m_pts.push_back(o->transform->Position());
            }
            const cam::GroupFrame gf = cam::FrameGroup(m_pts.data(), (int)m_pts.size(), groupPadding);
            if (gf.valid) {
                t.fp = gf.center; t.haveFp = true; t.groupRadius = gf.radius; t.members = (int)m_pts.size();
                if (groupFrame && m_pts.size() > 1) {
                    t.fitDist = cam::DistanceToFit(gf.radius, fieldOfView, aspect);
                    t.fitOrtho = cam::OrthoSizeToFit(gf.radius, aspect);
                }
            }
        }
        return t;
    }

    /// Where the shot would stand with no smoothing, no look-ahead, no collision: what the editor
    /// shows for the selected rig when nothing is playing. False when there is nothing to show.
    bool IdealPose(Vec3& pos, Quat& rot) {
        Scene* s = GetScene();
        if (!s || !transform) return false;
        const Targets t = GatherTargets(s, 16.0f / 9.0f);
        pos = transform->Position();
        rot = transform->Rotation();
        if (!t.haveFp) return true;
        Vec3 aim = t.fp;
        if (freeLook) {
            const Vec3 pivot = t.fp + Vec3{0.0f, orbitHeight, 0.0f};
            pos = pivot + Quat::Euler(orbitPitch, orbitYaw, 0.0f) * Vec3{0.0f, 0.0f, -std::max(orbitRadius, t.fitDist)};
            aim = pivot;
        } else if (!dolly) {
            Vec3 off = (bindingMode == BindingMode::LockToTarget && t.f) ? (t.f->transform->Rotation() * followOffset) : followOffset;
            const float len = off.Magnitude();
            if (t.fitDist > len && len > 1e-4f) off = off * (t.fitDist / len);
            if (!follow.empty() || !groupTargets.empty()) pos = t.fp + off;
        }
        if (!freeLook && t.l) aim = t.l->transform->Position() + lookAtOffset;
        else if (!freeLook) aim = t.fp + lookAtOffset;
        const Vec3 d = aim - pos;
        if (ortho2D) rot = Quat::Identity;
        else if (d.SqrMagnitude() > 1e-8f && (freeLook || t.l || aimAtFollow || !groupTargets.empty())) rot = AimRotation(d);
        if (std::fabs(dutch) > 1e-4f) rot = rot * Quat::Euler(0.0f, 0.0f, dutch);
        return true;
    }

    /// Put things this camera made see-through back to how they were.
    void RestoreFaded() {
        for (const auto& e : m_fade.Entries()) if (auto* mr = static_cast<MeshRenderer*>(const_cast<void*>(e.id))) { mr->color.a = e.original; mr->fadeBaseAlpha = -1.0f; }
        m_fade.Clear();
    }
    /// Disabled or removed: put things back. Only renderers that still exist are touched.
    void OnDisable() override { RestoreStillAlive(); }
    void OnDestroy() override { RestoreStillAlive(); }
    void OnObjectsDestroyed(const std::unordered_set<const GameObject*>& dying) override {
        // A fading object that is going away: forget it (its renderer pointer would dangle).
        std::vector<const void*> gone;
        for (const auto& e : m_fade.Entries()) {
            const MeshRenderer* mr = static_cast<const MeshRenderer*>(e.id);
            if (mr && mr->gameObject && dying.count(mr->gameObject)) gone.push_back(e.id);
        }
        for (const void* id : gone) m_fade.Forget(id);
    }

    void RestoreStillAlive() {
        if (m_fade.Entries().empty()) return;
        Scene* s = GetScene();
        if (!s) { m_fade.Clear(); return; }
        const std::vector<MeshRenderer*> live = s->FindObjectsOfType<MeshRenderer>();
        for (const auto& e : m_fade.Entries()) {
            auto* mr = static_cast<MeshRenderer*>(const_cast<void*>(e.id));
            if (std::find(live.begin(), live.end(), mr) != live.end()) { mr->color.a = e.original; mr->fadeBaseAlpha = -1.0f; }
        }
        m_fade.Clear();
    }
    const Vec3& SolvedPosition() const { return m_solvedPos; }
    const Quat& SolvedRotation() const { return m_rot; }
    /// The 2D view size this shot wants (0 = leave the camera's size alone).
    float SolvedOrthoSize() const { return m_solvedOrtho; }
    /// The point being framed after look ahead, for the editor's gizmo and for tests.
    const Vec3& LookaheadOffset() const { return m_look.offset; }
    /// The distance the collision probe reached last frame (< 0 before the first).
    float CollisionLength() const { return m_arm.Length(); }
    /// Forget the damping state: the next solve starts exactly on its target (a teleport).
    void Snap() { m_init = false; m_look.Reset(); m_arm.Reset(); m_vel = {0.0f, 0.0f, 0.0f}; m_roomInit = false; }

    /// A camera looks down its own -Z (the Camera component's view matrix says so), so to face
    /// `dir` the shot's +Z has to point the other way. (Until the camera upgrade this was
    /// LookRotation(dir) and the shot aimed away from its target.)
    static Quat AimRotation(const Vec3& dir) { return Quat::LookRotation(dir * -1.0f); }
    /// Where a shot with this rotation looks.
    static Vec3 ViewForward(const Quat& q) { return q * Vec3{0.0f, 0.0f, -1.0f}; }

private:
    /// Turn toward `dir` by the zone rule: nothing in the dead zone, an ease back to its edge in the soft zone,
    /// a catch-up beyond the soft zone. Works in the screen-plane (fractions of half the view).
    Quat ZoneAim(const Vec3& dir, float aspect) const {
        const Vec3 fwd = ViewForward(m_rot), right = m_rot * Vec3::Right, up = m_rot * Vec3::Up;
        const float lz = Vec3::Dot(dir, fwd);
        if (lz <= 1e-3f) return AimRotation(dir);           // behind the camera: turn round
        const float tanV = std::tan(Mathf::Clamp(fieldOfView, 1.0f, 170.0f) * Mathf::Deg2Rad * 0.5f);
        const float tanH = tanV * aspect;
        const float nx = Vec3::Dot(dir, right) / (lz * tanH), ny = Vec3::Dot(dir, up) / (lz * tanV);
        // Use the last dt the solver saw: the caller folds it in through m_zoneDt.
        const float cx = cam::ZoneCorrection(nx, deadZoneW, softZoneW, zoneDamping, m_zoneDt);
        const float cy = cam::ZoneCorrection(ny, deadZoneH, softZoneH, zoneDamping, m_zoneDt);
        if (cx == 0.0f && cy == 0.0f) return m_rot;
        // Where the target should sit after the camera has turned, in camera-local terms.
        const Vec3 before = (right * (nx * tanH) + up * (ny * tanV) + fwd).Normalized();       // = dir, in world
        const Vec3 after  = (right * ((nx - cx) * tanH) + up * ((ny - cy) * tanV) + fwd).Normalized();
        // Turn the view so what is at `before` now appears where `after` was: rotate by the arc after -> before.
        const Quat turn = Quat::FromToRotation(after, before);
        const Vec3 newFwd = turn * fwd;
        // Keep the horizon level (the arc alone would add a little roll).
        return AimRotation(newFwd);
    }

    bool RoomBox(Scene* s, const Vec3& p, float dt, Vec3& outMin, Vec3& outMax);
    void FadeStep(Scene* s, GameObject* ignore, const Vec3& camPos, const Vec3& pivot, float dt);

    bool m_init = false;
    Vec3 m_pos{0.0f, 0.0f, 0.0f};
    Vec3 m_vel{0.0f, 0.0f, 0.0f};
    Quat m_rot = Quat::Identity;
    Vec3 m_solvedPos{0.0f, 0.0f, 0.0f};
    float m_solvedOrtho = 0.0f;
    float m_zoneDt = 0.0f;
    float m_noiseT = 0.0f;
    float m_impulse = 0.0f;   // current one-shot impulse amplitude (decays)
    float m_impulseT = 0.0f;
    Vec2 m_lastMouse{0.0f, 0.0f};
    bool m_mouseInit = false;
    std::vector<Vec3> m_pts;
    cam::LookaheadFilter m_look;
    SpringArm m_arm;
    cam::FadeTracker m_fade;
    bool m_roomInit = false;
    int m_room = -1;
    cam::Box2 m_roomBox;
    float m_roomV[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    /// Smooth 1-D value noise in [-1,1] (interpolated hash). Good enough for shake.
    static float Noise(float t, float seed) {
        t += seed;
        float fl = std::floor(t);
        int i = (int)fl;
        float fr = t - fl;
        float u = fr * fr * (3.0f - 2.0f * fr);   // smoothstep
        auto h = [](int cell) {   // unsigned: the classic hash's wraparound without signed overflow
            unsigned n = (unsigned)cell;
            n = (n << 13) ^ n;
            unsigned m = (n * (n * n * 15731u + 789221u) + 1376312589u) & 0x7fffffffu;
            return 1.0f - (float)m / 1073741824.0f;   // [-1,1]
        };
        return h(i) * (1.0f - u) + h(i + 1) * u;
    }
};

inline bool VirtualCamera::RoomBox(Scene* s, const Vec3& p, float dt, Vec3& outMin, Vec3& outMax) {
    std::vector<CameraRoom*> rooms = s->FindObjectsOfType<CameraRoom>();
    std::vector<cam::Box2> boxes; boxes.reserve(rooms.size());
    for (CameraRoom* r : rooms) {
        if (!r->enabled || !r->gameObject || !r->gameObject->active || !r->transform) { boxes.push_back(cam::Box2{1e9f, 1e9f, -1e9f, -1e9f}); continue; }
        boxes.push_back(r->Bounds());
    }
    if (boxes.empty()) return false;
    const int room = cam::PickRoom(boxes.data(), (int)boxes.size(), p.x, p.y, m_room);
    if (room < 0) return false;
    m_room = room;
    const cam::Box2& tgt = boxes[(std::size_t)room];
    if (!m_roomInit) { m_roomBox = tgt; m_roomInit = true; m_roomV[0] = m_roomV[1] = m_roomV[2] = m_roomV[3] = 0.0f; }
    else {
        // Each edge of the box glides with a critically damped spring: the slide starts and ends softly.
        const float w = roomBlendTime > 1e-3f ? 6.0f / roomBlendTime : 0.0f;
        cam::SpringStep(m_roomBox.minX, m_roomV[0], tgt.minX, w, m_zoneDt);
        cam::SpringStep(m_roomBox.maxX, m_roomV[1], tgt.maxX, w, m_zoneDt);
        cam::SpringStep(m_roomBox.minY, m_roomV[2], tgt.minY, w, m_zoneDt);
        cam::SpringStep(m_roomBox.maxY, m_roomV[3], tgt.maxY, w, m_zoneDt);
    }
    (void)dt;
    outMin.x = m_roomBox.minX; outMin.y = m_roomBox.minY; outMax.x = m_roomBox.maxX; outMax.y = m_roomBox.maxY;
    return true;
}

inline void VirtualCamera::FadeStep(Scene* s, GameObject* ignore, const Vec3& camPos, const Vec3& pivot, float dt) {
    const Vec3 d = pivot - camPos;
    const float dist = d.Magnitude();
    if (dist > 0.05f) {
        std::vector<RaycastHit3D> hits;
        {
            Physics3D::IgnoreHierarchy own(ignore);
            hits = s->physics3D().RaycastAll(*s, camPos, d * (1.0f / dist), dist, ignore, collisionMask, /*queryTriggers=*/false);
        }
        for (const RaycastHit3D& h : hits) {
            GameObject* g = h.gameObject;
            if (!g) continue;
            MeshRenderer* mr = g->GetComponent<MeshRenderer>();
            if (!mr && g->transform && g->transform->Parent()) mr = g->transform->Parent()->gameObject->GetComponent<MeshRenderer>();
            if (mr) {
                const float orig = mr->fadeBaseAlpha >= 0.0f ? mr->fadeBaseAlpha : mr->color.a;
                m_fade.Mark(mr, orig);
                mr->fadeBaseAlpha = orig;     // a scene saved meanwhile stores this, not the faded value
            }
        }
    }
    std::vector<cam::FadeTracker::Entry> done;
    m_fade.Step(occlusionAlpha, occlusionFadeSpeed, dt, done);
    for (const auto& e : m_fade.Entries()) static_cast<MeshRenderer*>(const_cast<void*>(e.id))->color.a = e.alpha;
    for (const auto& e : done) { auto* mr = static_cast<MeshRenderer*>(const_cast<void*>(e.id)); mr->color.a = e.original; mr->fadeBaseAlpha = -1.0f; }
}

} // namespace okay
