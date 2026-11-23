// Bodies moved out of the header (docs/engine/modularization_plan.md section 3.1).
#include "okay/Components/ModelAnimator.hpp"

namespace okay {

    void ModelAnimator::Update(float dt) {
        OKAY_PROFILE("Anim.Model");
        // Locomotion speed (world XZ units/sec) + vertical speed, measured from
        // how the object moved. A blend tree poses the bones itself (externalDrive);
        // the speed is still read first so empty slots and stride matching keep
        // working under a graph.
        float spd = -1.0f, vy = 0.0f;
        const float prevVy = m_lastVy;
        if (driveByMovement && dt > 0.0f && transform) {
            Vec3 p = transform->Position();
            if (m_haveLast) {
                // What moved us since last frame was last frame's physics step:
                // measure it over that frame's time. Over this one's, an uneven
                // frame rate read as the speed jumping between idle and run.
                const float span = m_lastDt > 0.0f ? m_lastDt : dt;
                float dx = p.x - m_lastPos.x, dz = p.z - m_lastPos.z;
                spd = std::sqrt(dx*dx + dz*dz) / span;
                vy = (p.y - m_lastPos.y) / span;
                // A teleport / respawn isn't speed: keep last frame's reading.
                if ((p - m_lastPos).SqrMagnitude() > 4.0f) { spd = m_lastSpd; vy = m_lastVy; }
                // Neither is a one-frame pop (a step lift, a platform's nudge): a
                // reading over twice the last plus 2 m/s waits a frame and counts
                // only if the next frame agrees, so the blend doesn't surge to run.
                else if (spd > m_lastSpd * 2.0f + 2.0f && !m_spikeHeld) { m_spikeHeld = true; spd = m_lastSpd; vy = m_lastVy; }
                else m_spikeHeld = false;
                m_lastSpd = spd; m_lastVy = vy;
            }
            m_lastPos = p; m_haveLast = true; m_lastDt = dt;
            // A held clip (script / visual script Play) picks the clip: movement-driven
            // selection waits (the position keeps being tracked, so it resumes without
            // a speed spike). A graph also picks the clips, but the speed stays — empty
            // slots still fall back to the built-in animation and walk/run still stride.
            if (m_held) spd = -1.0f;
        }
        const bool graph = StateMachineDrives();   // the graph picks clips; this still reads speed
        const bool bi = spd >= 0.0f && BuiltinFallbackUsable();   // empty slots are played by the built-in animation
        bool builtinNow = false;                                   // ...and does so this frame

        // Air states: launch -> jump clip, past the apex -> fall clip, touchdown
        // -> land one-shot; locomotion stands down while airborne. Driven purely
        // by the capsule's vertical motion, so every controller "just works".
        if (spd >= 0.0f && (!jumpClip.empty() || !fallClip.empty() || bi)) {
            if (!m_inAir) {
                // Rising two frames running: a step up a curb or stair lifts the
                // body in a single frame, which read as a jump.
                bool launch = vy > airUpVel && prevVy > airUpVel;
                if (vy < -airDownVel) m_airT += dt; else m_airT = 0.0f;
                if (launch || m_airT > 0.12f) {          // brief drops (steps) don't count
                    m_inAir = true; m_airT = 0.0f; m_fell = !launch;
                    m_airSlot = (launch && (SlotFilled(SlotJump) || bi)) ? SlotJump : SlotFall;
                    if (!graph && SlotFilled(m_airSlot)) Play(SlotClip(m_airSlot));
                }
            } else {
                // Apex: a jump turns into a fall once we're heading down.
                if (vy < -1.0f && m_airSlot == SlotJump) {
                    m_airSlot = SlotFall;
                    // An empty jump slot never played the jump clip, so the body is
                    // still on idle. The fall clip still takes over on the way down.
                    if (!graph && SlotFilled(SlotFall) &&
                        (jumpClip.empty() || CurrentName() == jumpClip))
                        Play(fallClip);
                }
                // Touchdown: vertical motion dies out (the ground stops us). A jump's
                // apex also passes zero, so that counts only once we've been
                // falling, or with ground right under the feet (landing on a ledge
                // on the way up).
                if (vy < -1.0f) m_fell = true;
                if (std::fabs(vy) < 0.4f && (m_fell || GroundUnderFeet())) m_airT += dt; else m_airT = 0.0f;
                if (m_airT > 0.06f) {
                    m_inAir = false; m_airT = 0.0f;
                    if (!graph && !landClip.empty()) PlayOnce(landClip);   // locomotion resumes after
                }
            }
            builtinNow = bi && m_inAir && !m_once && !SlotFilled(m_airSlot);
        }

        // Stance: the player's crouch / prone picks the clip set.
        if (spd >= 0.0f) {
            const int st = BodyStance();
            if (st != m_stance) {
                const int from = m_stance;
                m_stance = st;
                m_stanceFade = StanceChangeTime(from, st);
                if (!graph && from == 2 && st == 0 && !getUpClip.empty() && !m_inAir && PlayOnce(getUpClip)) {
                    m_onceReturnTo = FindClip(idleClip);   // the get-up hands over to standing
                    m_stanceFade = -1.0f;
                }
            }
        }

        // Continuous 1D blend (idle<->walk<->run by speed): drives the bones
        // directly and replaces the discrete machinery below while it's active.
        // A one-shot (attack etc.) or an air clip suspends locomotion. (In the air
        // with no clip for it, e.g. walking off a ledge with no fall clip, it
        // carries on: stopping it snapped the body to a stale frame of the walk.)
        const bool airClip = m_inAir && (CurrentName() == jumpClip || CurrentName() == fallClip);
        // A graph (or its blend tree) already poses the model. The smooth blend
        // would pick clips on its own and fight that.
        // Direction from the character: 0 forward, +90 right, 180 backward.
        // Without a back clip, a forward walk played backward is a backpedal,
        // not a moonwalk. A back or strafe clip plays as authored instead.
        // The backward sector has hysteresis (enter past 105, leave under 95), so a
        // direction that hovers near 100 does not flip the clip back and forth.
        m_cycleSign = 1;
        float travel = 0.0f;
        if (spd > 0.2f && TravelDeg(travel)) {
            if (std::fabs(travel) > 105.0f) m_backTravel = true;
            else if (std::fabs(travel) < 95.0f) m_backTravel = false;
        } else if (spd < 0.1f) m_backTravel = false;
        if (m_backTravel && loop && !m_once && (legsFollowTravel || HasDirectionalClips()) &&
            !SlotFilled(SlotWalkBack) && !SlotFilled(SlotRunBack)) m_cycleSign = -1;
        if (!graph && !externalDrive && spd >= 0.0f && !m_once && !airClip && !builtinNow && m_stance == 0 &&
            HasDirectionalClips() && DirectionalBlendStep(dt, spd)) return;
        if (!graph && !externalDrive && spd >= 0.0f && smoothLocomotion && !m_once && !airClip && !builtinNow) {
            // Blend by the speed eased over ~0.1 s: a body stopped dead by a wall
            // (or a respawn) snapped from the walk to the idle in one frame.
            m_blendSpd += (spd - m_blendSpd) * (1.0f - std::exp(-dt / 0.1f));
            if (SmoothBlendStep(dt, m_blendSpd)) return;
        }

        // Fire clip events crossed by this frame's playback window. The clock
        // mirrors the node Animators' time (reset together in PlayIndex).
        if (!externalDrive && dt > 0.0f && active >= 0 && active < (int)clips.size() && !m_builtinOwns && !builtinNow) {
            float len = ClipLength(active);
            // Stride matching: the walk / run clips step at the pace the body moves.
            const float rate = (strideMatch && !rootMotion && spd >= 0.0f && !m_once && !m_inAir) ? m_strideRate : 1.0f;
            const float dir = (float)CycleSignFor(m_slot, m_slot);
            m_appliedSign = spd > 0.2f ? (int)dir : 0;
            float t0 = m_clock, t1 = m_clock + dt * speed * rate * dir;
            const bool first = m_fromStart && t1 > t0;
            if (t1 > t0) m_fromStart = false;
            // A jump clip holds its last frame when the body is still in the air.
            // Fall keeps looping (it's a cycle). Wrapping the jump replayed the
            // takeoff for as long as the jump lasted.
            const bool holdJump = m_inAir && !m_once && !jumpClip.empty() && CurrentName() == jumpClip;
            const bool wrap = loop && !m_once && !holdJump;
            if (!clips[active].events.empty() && len > 0.0f && t1 > t0) {
                if (wrap && t1 > len) { FireWindow(active, t0, len, first); FireWindow(active, 0.0f, std::fmod(t1, len), true); }
                else                  FireWindow(active, t0, (holdJump && t1 > len) ? len : t1, first);
            }
            // Root motion: what the root bone travelled this frame moves the object
            // instead (measured in world space through the bone's parent, so FBX
            // armature scales and rotated armatures come out right).
            if (rootMotion && len > 0.0f) {
                const bool wrapLoop = wrap;
                float e0 = wrapLoop ? std::fmod(std::fmod(t0, len) + len, len) : std::fmin(t0, len);
                float e1 = wrapLoop ? std::fmod(std::fmod(t1, len) + len, len) : std::fmin(t1, len);
                Vec3 d; float yaw = 0.0f;
                RootDelta(active, e0, e1, len, d, yaw);   // none (idle clip): a zero move stops a rigidbody
                if (m_stateMirror) d.x = -d.x;
                ApplyRootMotion(d, yaw, dt);
            }
            float clock = t1;
            if (len > 0.0f) {
                if (wrap) {
                    clock = std::fmod(clock, len);
                    if (clock < 0.0f) clock += len;
                } else clock = std::fmin(std::fmax(clock, 0.0f), len);
            }
            m_clock = clock;
            if (len > 0.0f) m_phase = m_clock / len;   // a hand-over to the smooth blend keeps the step
            ApplyPose(active, m_clock);
            // A finished one-shot returns to the clip that was playing before it
            // (crossfaded like any switch).
            if (m_once && len > 0.0f && m_clock >= len) {
                m_once = false;
                int rt = m_onceReturnTo; m_onceReturnTo = -1;
                if (rt >= 0 && rt < (int)clips.size() && rt != active) PlayIndex(rt);
            }
        }

        // Discrete locomotion: switch to the clip for this speed band (crossfaded
        // by blendTime, since Play goes through PlayIndex). Suspended by a
        // one-shot or while airborne (the jump/fall clip owns the body).
        if (spd < 0.0f || m_once) { m_builtinOwns = false; return; }
        if (m_inAir) { m_builtinOwns = builtinNow; return; }
        const int slot = GroundSlot(spd, bi);
        m_slot = slot;
        m_strideRate = StrideRate(slot, spd);
        if (bi && !SlotFilled(slot)) {   // this slot has no clip: the built-in animation poses the model
            m_builtinOwns = true;
            m_stanceFade = -1.0f;
            return;
        }
        const std::string& want = SlotClip(slot);
        const bool handBack = m_builtinOwns;   // the clip takes over from the built-in animation: ease in
        m_builtinOwns = false;
        if (graph) { m_prevSlot = slot; return; }   // the graph holds the clip; the rate above is for its playback
        if (!want.empty() && (want != CurrentName() || handBack)) {   // switch only on change
            // Stride clips swap at the same point of their step (walk <-> run keeps the feet in step).
            const int prev = active, to = FindClip(want);
            const bool stepToStep = !handBack && prev >= 0 && to >= 0 && IsStrideSlot(m_prevSlot) && IsStrideSlot(slot) &&
                                    ClipLength(prev) > 0.0f && ClipLength(to) > 0.0f;
            const float phase = stepToStep ? m_clock / ClipLength(prev) : 0.0f;
            Play(want, m_stanceFade);
            if (stepToStep) { m_clock = phase * ClipLength(to); ApplyPose(to, m_clock); }
            m_stanceFade = -1.0f;
        }
        m_prevSlot = slot;
    }

    void ModelAnimator::YawLowerBody(float dt) {
        if (!transform) return;
        UndoYaw();
        Transform* hips = HipsT();
        Transform* spine = SpineT();
        float target = 0.0f;
        float ang = 0.0f;
        Character* ch = nullptr;
        if (legsFollowTravel && TravelDeg(ang))
            for (Transform* t = transform; t && !ch; t = t->Parent())
                if (t->gameObject) ch = t->gameObject->GetComponent<Character>();
        const bool strafing = m_slot == SlotWalkBack || m_slot == SlotRunBack || m_slot == SlotStrafeLeft || m_slot == SlotStrafeRight;
        if (ch && !strafing) {
            if (ch->moveSpeed > 0.2f) m_yawOn = true;
            else if (ch->moveSpeed < 0.1f) m_yawOn = false;
            if (m_yawOn) {
                target = m_backTravel ? (ang > 0.0f ? ang - 180.0f : ang + 180.0f) : ang;
                if (target > 80.0f) target = 80.0f;
                if (target < -80.0f) target = -80.0f;
                if (std::fabs(target) < 1.0f) target = 0.0f;
            }
        } else m_yawOn = false;
        float step = (target - m_hipYaw) * (1.0f - std::exp(-dt / 0.08f));
        const float maxStep = 720.0f * dt;
        if (step > maxStep) step = maxStep;
        if (step < -maxStep) step = -maxStep;
        m_hipYaw += step;
        if (target == 0.0f && std::fabs(m_hipYaw) < 0.5f) m_hipYaw = 0.0f;
        const float yaw = m_hipYaw;
        if (yaw == 0.0f || !hips) return;
        // World up, written in the hips' parent space. Parent-space up is not
        // vertical on a tilted armature, and yawing around it tips the pelvis.
        // local' = AngleAxis(θ, parent⁻¹ · worldUp) · local is a pure world yaw.
        Transform* parent = hips->Parent();
        Vec3 axis = parent ? parent->Rotation().Inverse() * Vec3::Up : Vec3::Up;
        if (axis.SqrMagnitude() < 1e-8f) axis = Vec3::Up;
        // Positive moveAngle is toward the body's right (+X when facing -Z).
        // A positive turn about up swings that facing toward -X, so the sign flips
        // and the legs turn toward the travel instead of away from it.
        m_hipsPre = hips->localRotation;
        hips->localRotation = (Quat::AngleAxis(-yaw, axis) * hips->localRotation).Normalized();
        m_hipsPost = hips->localRotation;
        m_spinePre = m_spinePost = Quat();
        m_yawWritten = true;
        // The hip yaw turns the chest with the legs. Twist the spine back by the
        // same world yaw so the upper body keeps facing the aim.
        if (!spine) return;
        Transform* sp = spine->Parent();
        Vec3 saxis = sp ? sp->Rotation().Inverse() * Vec3::Up : Vec3::Up;
        if (saxis.SqrMagnitude() < 1e-8f) saxis = Vec3::Up;
        m_spinePre = spine->localRotation;
        spine->localRotation = (Quat::AngleAxis(yaw, saxis) * spine->localRotation).Normalized();
        m_spinePost = spine->localRotation;
    }

    bool ModelAnimator::DirectionalBlendStep(float dt, float spd) {
        if (!gameObject || !gameObject->scene()) return false;
        float ang = 0.0f;
        TravelDeg(ang);
        // Strafe clips with no back clip: the back sector is the walk-only path,
        // the forward walk played in reverse. The 2D blend has nothing behind it.
        if (m_backTravel && loop && !SlotFilled(SlotWalkBack) && !SlotFilled(SlotRunBack)) {
            m_cycleSign = -1;
            m_appliedSign = spd > 0.2f ? -1 : 0;
            return false;
        }
        const float rad = ang * 0.017453292f;
        const float mx = spd * std::sin(rad);
        const float mz = -spd * std::cos(rad);
        const float wAt = std::fmax(0.2f, WalkStartSpeed());
        const float rAt = std::fmax(wAt + 0.2f, RunSwitchSpeed());
        struct Mot { int clip; float x, y; };
        std::vector<Mot> mot;
        auto add = [&](int slot, float x, float y) {
            const int ci = FindClip(SlotClip(slot));
            if (ci >= 0 && ClipLength(ci) > 0.0f) mot.push_back({ci, x, y});
        };
        add(SlotIdle, 0.0f, 0.0f);
        add(SlotWalk, 0.0f, -wAt);
        add(SlotRun, 0.0f, -rAt);
        add(SlotStrafeLeft, -wAt, 0.0f);
        add(SlotStrafeRight, wAt, 0.0f);
        add(SlotWalkBack, 0.0f, wAt);
        add(SlotRunBack, 0.0f, rAt);
        if (mot.size() < 2) return false;
        std::vector<float> w(mot.size(), 0.0f);
        float sum = 0.0f;
        for (std::size_t i = 0; i < mot.size(); ++i) {
            float wi = 1.0f;
            for (std::size_t j = 0; j < mot.size() && wi > 0.0f; ++j) {
                if (i == j) continue;
                const float vx = mot[j].x - mot[i].x, vy = mot[j].y - mot[i].y;
                const float l2 = vx * vx + vy * vy;
                if (l2 < 1e-8f) continue;
                float h = 1.0f - ((mx - mot[i].x) * vx + (mz - mot[i].y) * vy) / l2;
                if (h < 0.0f) h = 0.0f;
                if (h > 1.0f) h = 1.0f;
                wi = std::fmin(wi, h);
            }
            w[i] = wi; sum += wi;
        }
        int a = 0, b = 0;
        if (sum <= 1e-6f) {
            float best = 1e30f;
            for (std::size_t i = 0; i < mot.size(); ++i) {
                const float dx = mx - mot[i].x, dy = mz - mot[i].y, d = dx * dx + dy * dy;
                if (d < best) { best = d; a = (int)i; }
            }
            b = a; w[a] = 1.0f;
        } else {
            float wa = -1.0f, wb = -1.0f;
            for (std::size_t i = 0; i < w.size(); ++i) {
                if (w[i] >= wa) { wb = wa; b = a; wa = w[i]; a = (int)i; }
                else if (w[i] > wb) { wb = w[i]; b = (int)i; }
            }
        }
        const float wa = w[a], wb = (a == b) ? 0.0f : w[b];
        float blend = (wa + wb) > 1e-4f ? wb / (wa + wb) : 0.0f;
        // The two winners trade places when their weights cross. The blend is
        // "how much of B", so a swap turns 0.49 of the strafe into 0.51 of the
        // walk and the pose pops. Keep last frame's clip order and flip the weight.
        if (m_haveDirPair && a != b) {
            const int ca = mot[a].clip, cb = mot[b].clip;
            if (ca == m_dirB && cb == m_dirA) {
                const int tmp = a; a = b; b = tmp;
                blend = 1.0f - blend;
            }
        }
        if (a != b) {
            m_dirA = mot[a].clip;
            m_dirB = mot[b].clip;
            m_haveDirPair = true;
        }
        const float rate = std::fmin(kStrideMaxRate, std::fmax(0.5f, spd / std::fmax(wAt, 0.2f)));
        m_appliedSign = spd > 0.2f ? 1 : 0;
        m_slot = a == b || blend < 0.5f ? SlotOfClip(mot[a].clip) : SlotOfClip(mot[b].clip);
        m_prevSlot = m_slot;
        m_strideRate = rate;
        m_cycleSign = 1;
        return BlendClips(dt * rate, mot[a].clip, mot[b].clip, blend);
    }

    bool ModelAnimator::SmoothBlendStep(float dt, float spd) {
        if (!gameObject || !gameObject->scene()) return false;
        const bool bi = BuiltinFallbackUsable();
        const bool stride = strideMatch && !rootMotion;
        // The two clips around this speed + the blend weight between them, and
        // the rate that makes the steps match the ground speed.
        int A, B, slotA, slotB; float w, rate = 1.0f;
        // A clip's own root travel when it has one, else the threshold it's full at.
        auto clipSpeed = [&](int ci, float thr) { float v = ClipRootSpeed(ci); return v > 1e-3f ? v : thr; };
        if (m_stance != 0) {   // crouched / prone: still <-> moving up to that stance's pace
            slotA = m_stance == 2 ? SlotProne : SlotCrouch;
            slotB = m_stance == 2 ? SlotCrawl : SlotCrouchWalk;
            if (!bi) {   // one stands in for a missing other
                if (SlotClip(slotA).empty()) slotA = slotB;
                if (SlotClip(slotB).empty()) slotB = slotA;
            }
            const float full = walkThreshold * (m_stance == 1 ? 0.5f : 0.25f);
            A = FindClip(SlotClip(slotA)); B = FindClip(SlotClip(slotB));
            w = full > 1e-4f ? spd / full : 1.0f;
            const float hi = stride ? SlotClipSpeed(slotB) : clipSpeed(B, full);
            const float clipSpd = hi * (w < 0.0f ? 0.0f : (w > 1.0f ? 1.0f : w));
            if (!rootMotion && clipSpd > 1e-3f) rate = std::fmin(2.0f, std::fmax(0.5f, spd / clipSpd));
            rate = 1.0f + (rate - 1.0f) * (w < 0.0f ? 0.0f : (w > 1.0f ? 1.0f : w));
        } else if (stride) {
            // Anchors from the clips' own pace: idle -> walk over the first steps,
            // walk alone, a phase-matched walk -> run blend, then run alone.
            const float W = SlotClipSpeed(SlotWalk), R = SlotClipSpeed(SlotRun);
            const float idleFull = std::fmax(0.05f, WalkStartSpeed()), walkEnd = WalkEndSpeed(), runStart = RunStartSpeed();
            auto clampRate = [&](float r) { return std::fmin(kStrideMaxRate, std::fmax(kStrideMinRate, r)); };
            if (spd <= idleFull) {
                slotA = SlotIdle; slotB = SlotWalk; w = spd / idleFull;
                rate = 1.0f + (clampRate(spd / W) - 1.0f) * w;   // idle plays at its own pace
            } else if (spd <= walkEnd || (!bi && SlotClip(SlotRun).empty())) {
                slotA = slotB = SlotWalk; w = 0.0f; rate = clampRate(spd / W);
            } else if (spd < runStart) {
                slotA = SlotWalk; slotB = SlotRun; w = (spd - walkEnd) / std::fmax(runStart - walkEnd, 1e-4f);
                rate = clampRate(spd / (W + (R - W) * w));
            } else {
                slotA = slotB = SlotRun; w = 0.0f; rate = clampRate(spd / R);
            }
            A = FindClip(SlotClip(slotA)); B = FindClip(SlotClip(slotB));
        } else {
            float lo, hi;   // the ground speed each clip is made for
            if (spd <= walkThreshold) {
                slotA = SlotIdle; slotB = SlotWalk;
                w = walkThreshold > 1e-4f ? spd / walkThreshold : 1.0f;
                lo = 0.0f; hi = clipSpeed(FindClip(walkClip), walkThreshold);
            } else {
                slotA = SlotWalk; slotB = SlotRun;
                w = (spd - walkThreshold) / std::fmax(runThreshold - walkThreshold, 1e-4f);
                lo = clipSpeed(FindClip(walkClip), walkThreshold); hi = clipSpeed(FindClip(runClip), runThreshold);
            }
            A = FindClip(SlotClip(slotA)); B = FindClip(SlotClip(slotB));
            const float cw = w < 0.0f ? 0.0f : (w > 1.0f ? 1.0f : w);
            const float clipSpd = lo + (hi - lo) * cw;
            if (!rootMotion && clipSpd > 1e-3f) rate = std::fmin(2.0f, std::fmax(0.5f, spd / clipSpd));
            if (spd <= walkThreshold) rate = 1.0f + (rate - 1.0f) * cw;
        }
        // A slot with no clip belongs to the built-in animation: leave the blend to it.
        if (bi && (!SlotFilled(slotA) || !SlotFilled(slotB))) return false;
        w = std::fmin(1.0f, std::fmax(0.0f, w));
        if (m_builtinOwns) {   // a clip takes over from the built-in animation: ease in
            CaptureFade(A >= 0 ? A : B, blendTime);
            m_builtinOwns = false;
        } else if (m_stanceFade >= 0.0f) {
            CaptureFade(A >= 0 ? A : B, m_stanceFade);   // ease into the stance's clips
        }
        m_stanceFade = -1.0f;
        // Stepping off from a stand starts the walk on the same foot every time.
        if (slotA == SlotIdle && m_smoothW <= 0.001f && w > 0.001f) m_phase = 0.0f;
        m_smoothW = slotA == SlotIdle ? w : 1.0f;
        m_slot = w < 0.5f ? slotA : slotB;
        m_prevSlot = m_slot;
        m_strideRate = rate;
        const int dir = CycleSignFor(slotA, slotB);
        m_appliedSign = (slotA == SlotIdle && w < 0.15f) ? 0 : dir;
        const bool posed = BlendClips(dt * rate * (float)dir, A, B, w);
        // Keep the single-clip clock on the blend's phase, so a hand-over to the discrete
        // path (a slot left to the built-in animation) carries on from the same step.
        if (posed && active >= 0 && active < (int)clips.size()) m_clock = m_phase * ClipLength(active);
        return posed;
    }

    bool ModelAnimator::BlendClips(float dt, int A, int B, float w) {
        if (!gameObject || !gameObject->scene()) return false;
        if (w < 0.0f) w = 0.0f; if (w > 1.0f) w = 1.0f;
        if (A >= (int)clips.size()) A = -1;
        if (B >= (int)clips.size()) B = -1;
        if (A < 0 && B < 0) return false;
        if (A < 0) { A = B; w = 1.0f; }
        if (B < 0) { B = A; w = 0.0f; }
        float lenA = ClipLength(A), lenB = ClipLength(B);
        if (lenA <= 0.0f && lenB <= 0.0f) return false;
        if (lenA <= 0.0f) lenA = lenB;
        if (lenB <= 0.0f) lenB = lenA;

        // Shared normalized phase: both clips sample the same fraction of their
        // cycle, so left/right footfalls stay aligned through the blend.
        float prevPhase = m_phase;
        float cycle = lenA + (lenB - lenA) * w;
        // A graph blend tree poses through here. Smooth locomotion already scaled
        // `dt` by the stride rate, so only the tree (externalDrive) applies it.
        float step = dt * speed;
        if (externalDrive && strideMatch && !rootMotion && !m_inAir && !m_builtinOwns)
            step *= m_strideRate;
        m_phase = AdvanceBlendPhase(prevPhase, step / std::fmax(cycle, 1e-4f));
        float tA = m_phase * lenA, tB = m_phase * lenB;
        active = w < 0.5f ? A : B;   // the dominant clip (UI + events)

        // Root motion: both clips' root deltas, blended by the same weight.
        if (rootMotion) {
            Vec3 d{0, 0, 0}, dA, dB; float yaw = 0.0f, yA = 0.0f, yB = 0.0f;
            bool any = false;
            if (RootDelta(A, prevPhase * lenA, tA, lenA, dA, yA)) { d = d + dA * (1.0f - w); yaw += yA * (1.0f - w); any = true; }
            if (RootDelta(B, prevPhase * lenB, tB, lenB, dB, yB)) { d = d + dB * w; yaw += yB * w; any = true; }
            (void)any;
            if (m_stateMirror) d.x = -d.x;
            ApplyRootMotion(d, yaw, dt);
        }

        // Events: the dominant clip's markers, over this frame's phase window.
        {
            float len = active == A ? lenA : lenB;
            float e0 = prevPhase * len, e1 = m_phase * len;
            const bool first = m_fromStart && e1 != e0;
            if (e1 != e0) m_fromStart = false;
            if (e1 < e0) { FireWindow(active, e0, len, first); FireWindow(active, 0.0f, e1, true); }
            else FireWindow(active, e0, e1, first);
        }

        // Drive the union of both clips' nodes directly. Nodes in only one clip
        // use it unblended. The A->B node pairing is cached per clip pair.
        // Bind first: it refreshes the node cache when the scene changed, which
        // also drops the cached pairings - a PairMap taken before it would dangle.
        const auto& bA = Bind(A);
        const auto& bB = Bind(B);
        const PairMap& pm = Pair(A, B);
        auto blendWrite = [&](Transform* t, int ia, int ib) {
            Vec3 pA = t->localPosition, sA = t->localScale; Quat rA = t->localRotation;
            Vec3 pB = pA, sB = sA; Quat rB = rA;
            if (ia >= 0) SampleNode(A, ia, tA, pA, rA, sA);
            if (ib >= 0) SampleNode(B, ib, tB, pB, rB, sB);
            float bw = (ia >= 0 && ib >= 0) ? w : (ib >= 0 ? 1.0f : 0.0f);
            float dq = rA.x*rB.x + rA.y*rB.y + rA.z*rB.z + rA.w*rB.w;
            if (dq < 0.0f) { rB.x = -rB.x; rB.y = -rB.y; rB.z = -rB.z; rB.w = -rB.w; }
            Quat r{rA.x + (rB.x - rA.x) * bw, rA.y + (rB.y - rA.y) * bw,
                   rA.z + (rB.z - rA.z) * bw, rA.w + (rB.w - rA.w) * bw};
            t->localPosition = pA + (pB - pA) * bw;
            t->localScale    = sA + (sB - sA) * bw;
            t->localRotation = r.Normalized();
        };
        for (std::size_t n = 0; n < bA.size(); ++n)
            if (bA[n]) blendWrite(bA[n], (int)n, A != B ? pm.aToB[n] : (int)n);
        if (A != B)
            for (int nb : pm.onlyB)
                if (bB[nb]) blendWrite(bB[nb], -1, nb);
        if (m_stateMirror) {
            std::vector<std::pair<std::string, Transform*>> ns;
            for (std::size_t n = 0; n < bA.size(); ++n) ns.push_back({clips[A].nodes[n].node, bA[n]});
            for (std::size_t n = 0; n < bB.size(); ++n) ns.push_back({clips[B].nodes[n].node, bB[n]});
            MirrorPairs(ns);
        }
        ApplyShapeWeights(active, active == A ? tA : tB);   // face shapes follow the dominant clip
        return true;
    }

    bool ModelAnimator::BlendMany(float dt, const std::vector<int>& clipIdx, const std::vector<float>& weights) {
        if (!gameObject || !gameObject->scene()) return false;
        // Scratch kept on the component: no per-frame allocation.
        std::vector<int>& ci = m_bmClip; std::vector<float>& w = m_bmW; std::vector<float>& len = m_bmLen;
        ci.clear(); w.clear(); len.clear();
        float sum = 0.0f;
        for (std::size_t i = 0; i < clipIdx.size() && i < weights.size(); ++i) {
            int c = clipIdx[i];
            if (c < 0 || c >= (int)clips.size() || weights[i] <= 1e-4f) continue;
            float l = ClipLength(c);
            if (l <= 0.0f) continue;
            ci.push_back(c); w.push_back(weights[i]); len.push_back(l); sum += weights[i];
        }
        if (ci.empty()) return false;
        float cycle = 0.0f; int dom = 0;
        for (std::size_t i = 0; i < w.size(); ++i) { w[i] /= sum; cycle += len[i] * w[i]; if (w[i] > w[dom]) dom = (int)i; }
        float prevPhase = m_phase;
        m_phase = AdvanceBlendPhase(prevPhase, dt * speed / std::fmax(cycle, 1e-4f));
        active = ci[dom];
        if (rootMotion) {
            Vec3 d{0, 0, 0}; float yaw = 0.0f;
            for (std::size_t i = 0; i < ci.size(); ++i) {
                Vec3 di; float yi;
                if (RootDelta(ci[i], prevPhase * len[i], m_phase * len[i], len[i], di, yi)) { d = d + di * w[i]; yaw += yi * w[i]; }
            }
            if (m_stateMirror) d.x = -d.x;
            ApplyRootMotion(d, yaw, dt);
        }
        {   // events of the dominant clip over this frame's phase window
            float L = len[dom], e0 = prevPhase * L, e1 = m_phase * L;
            const bool first = m_fromStart && e1 != e0;
            if (e1 != e0) m_fromStart = false;
            if (e1 < e0) { FireWindow(ci[dom], e0, L, first); FireWindow(ci[dom], 0.0f, e1, true); }
            else FireWindow(ci[dom], e0, e1, first);
        }
        std::vector<std::pair<int, float>>& ct = m_bmCT;
        ct.clear();
        for (std::size_t i = 0; i < ci.size(); ++i) ct.push_back({ci[i], m_phase * len[i]});
        PoseWeighted(ct, w, nullptr, 1.0f);
        ApplyShapeWeights(ci[dom], m_phase * len[dom]);   // face shapes follow the heaviest clip
        return true;
    }

    void ModelAnimator::PoseWeighted(const std::vector<std::pair<int, float>>& clipTimes, const std::vector<float>& weights,
                      const std::unordered_set<Transform*>* mask, float amount, bool additive) {
        std::vector<PwAcc>& acc = m_pwAcc;              // scratch kept on the component
        std::unordered_map<Transform*, int>& at = m_pwAt;
        acc.clear(); at.clear();
        for (std::size_t k = 0; k < clipTimes.size() && k < weights.size(); ++k) {
            int c = clipTimes[k].first;
            float wk = weights[k];
            if (c < 0 || c >= (int)clips.size() || wk <= 0.0f) continue;
            const auto& b = Bind(c);
            for (std::size_t n = 0; n < b.size(); ++n) {
                Transform* t = b[n];
                if (!t || (mask && !mask->count(t))) continue;
                auto it = at.find(t);
                int ai;
                if (it == at.end()) { ai = (int)acc.size(); at.emplace(t, ai); acc.push_back({t}); acc.back().ref = t->localRotation; }
                else ai = it->second;
                Vec3 p = t->localPosition, sc = t->localScale; Quat r = t->localRotation;
                SampleNode(c, (int)n, clipTimes[k].second, p, r, sc);
                Vec3 rp = p, rsc = sc; Quat rr = r;
                if (additive) SampleNode(c, (int)n, 0.0f, rp, rr, rsc);
                PwAcc& a = acc[ai];
                if (a.w == 0.0f) a.ref = r;
                if (Quat::Dot(a.ref, r) < 0.0f) r = Quat{-r.x, -r.y, -r.z, -r.w};
                a.p = a.p + p * wk; a.s = a.s + sc * wk;
                a.q = Quat{a.q.x + r.x * wk, a.q.y + r.y * wk, a.q.z + r.z * wk, a.q.w + r.w * wk};
                if (additive) {
                    if (a.w == 0.0f) a.rref = rr;
                    if (Quat::Dot(a.rref, rr) < 0.0f) rr = Quat{-rr.x, -rr.y, -rr.z, -rr.w};
                    a.rp = a.rp + rp * wk; a.rs = a.rs + rsc * wk;
                    a.rq = Quat{a.rq.x + rr.x * wk, a.rq.y + rr.y * wk, a.rq.z + rr.z * wk, a.rq.w + rr.w * wk};
                }
                a.w += wk;
            }
        }
        for (PwAcc& a : acc) {
            if (a.w <= 1e-6f) continue;
            float iw = 1.0f / a.w;
            Vec3 p = a.p * iw, sc = a.s * iw;
            Quat q = Quat{a.q.x * iw, a.q.y * iw, a.q.z * iw, a.q.w * iw}.Normalized();
            Transform* t = a.t;
            if (additive) {
                Vec3 rp = a.rp * iw, rs = a.rs * iw;
                Quat rq = Quat{a.rq.x * iw, a.rq.y * iw, a.rq.z * iw, a.rq.w * iw}.Normalized();
                Quat delta = (rq.Inverse() * q).Normalized();
                Quat added = (t->localRotation * delta).Normalized();
                const float u = std::fmin(std::fmax(amount, 0.0f), 1.0f);
                t->localPosition = t->localPosition + (p - rp) * u;
                t->localScale    = t->localScale + (sc - rs) * u;
                t->localRotation = Quat::Slerp(t->localRotation, added, u).Normalized();
                continue;
            }
            if (amount >= 0.999f) { t->localPosition = p; t->localScale = sc; t->localRotation = q; continue; }
            t->localPosition = t->localPosition + (p - t->localPosition) * amount;
            t->localScale    = t->localScale + (sc - t->localScale) * amount;
            t->localRotation = Quat::Slerp(t->localRotation, q, amount).Normalized();
        }
        if (m_stateMirror) {
            std::vector<std::pair<std::string, Transform*>> ns;
            for (const PwAcc& a : acc)
                if (a.w > 1e-6f && a.t && a.t->gameObject) ns.push_back({a.t->gameObject->name, a.t});
            MirrorPairs(ns);
        }
    }

    void ModelAnimator::PoseLayer(const std::vector<std::pair<int, float>>& clipTimes, const std::vector<float>& weights,
                   const std::string& maskBone, float amount, bool additive,
                   const std::vector<std::string>* moreBones) {
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        const std::uint64_t ver = sc ? sc->StructureVersion() : 0;
        std::string key = maskBone;
        if (moreBones) for (const std::string& b : *moreBones) { key.push_back('|'); key += b; }
        if (ver != m_maskVer || m_maskName != key) {   // each mask root's subtree, cached
            m_mask.clear(); m_maskVer = ver; m_maskName = key;
            auto addTree = [&](const std::string& bone) {
                if (bone.empty()) return;
                if (GameObject* mb = FindNode(bone)) {
                    std::vector<Transform*> stack{mb->transform};
                    while (!stack.empty()) {
                        Transform* t = stack.back(); stack.pop_back();
                        if (!t) continue;
                        m_mask.insert(t);
                        for (Transform* c : t->Children()) stack.push_back(c);
                    }
                }
            };
            addTree(maskBone);
            if (moreBones) for (const std::string& b : *moreBones) addTree(b);
        }
        const bool masked = !key.empty();
        if (masked && m_mask.empty()) return;   // mask bones missing: drive nothing
        PoseWeighted(clipTimes, weights, masked ? &m_mask : nullptr, amount, additive);
    }

} // namespace okay
