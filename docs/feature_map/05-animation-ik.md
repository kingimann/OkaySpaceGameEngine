# 5. Animation & IK

Keyframe clips, the Animator state machine and its two editor windows, playing imported model clips with locomotion, the built-in humanoid motion and retargeting, root motion and skinning, the whole IK family (foot, limb, chain, look-at, aim, full body), ragdolls, object holding, built-in action clips and sprite animation.
About 35 headers under `Components/` and `Animation/` (`ModelAnimator` 1,800 lines and `AnimStateMachine` 1,100 are header-only), `Animation/HumanoidActions.cpp` (2,180 lines), the 2,900-line `editor/AnimatorWindow.cpp` and the Animation tab inside `editor/main.cpp`.
Grok's sessions own the open animation work: PR #554 (animation integration, in flight; #548-#551 were closed as duplicates and #535-#537, #539, #563 are already on main). Document only; ask before touching.

## 5. Animation & IK

### 5.1 Animator and state machines
- **What:** The Animator is a state machine: states name a clip or a blend tree, transitions fire on parameters, clip end or exit time, layers play on part of the skeleton. The same machine plays through an imported model's Model Animator or the blocky Character's own clips.
- **Parts:**
  - 5.1.1 States, transitions, conditions - `Components/AnimStateMachine` - OnClipEnd, Float >/<, Bool, Trigger, Always, Int ==/!=; extra conditions all must pass; exit time, Any State, interruption modes, priority, mute, Start at, Keep phase
  - 5.1.2 Blend trees - `Components/AnimStateMachine` - 1D by thresholds, 2D on two parameters (strafe on moveX/moveZ), Match speed, thresholds from clip speed
  - 5.1.3 Layers, masks, additive - `Components/AnimStateMachine` - layer > 0 plays on a mask bone and its children over the base pose, weight, Additive; layers share the base layer's parameters
  - 5.1.4 Per-state extras - `Components/AnimStateMachine` - speed multiplier parameter, mirror, root motion Inherit/Off/On, enter and exit events, IK weight and curve, lookWeight, "If missing" fallback clip, history
  - 5.1.5 Backends - `Components/AnimBackend`, `Components/CharacterAnimBackend`, `Components/ModelAnimator` - the machine does not care whether a model or the blocky Character plays the clips
  - 5.1.6 Animator window - `editor/AnimatorWindow.*` - canvas, minimap, side panel (State, Transition, Any State, Animator), Parameters, Layer, Issues and Clips tabs, play-mode highlights, undo
  - 5.1.7 Live preview - `editor/AnimatorPreview.hpp` - one preview at a time (state, clip, blend between two states), keeps playing with the Animation window closed, restores the pose on stop
  - 5.1.8 Default graph for the built-in body - `Animation/HumanoidLibrary`, `Components/ControllerFeel` - Idle, Walk, Run, Crouch, Prone, Jump, Fall, Land, Hang and gestures; AnimLink feeds speed, run, crouch, prone, stance, airTime, verticalSpeed; GaitCrossfades (idle-walk 0.28 s, walk-run 0.22 s, into air 0.12 s, onto ground 0.24 s)
- **Code:** `Components/AnimStateMachine`, `Components/AnimBackend`, `Components/CharacterAnimBackend`, `Animation/HumanoidLibrary`, `editor/AnimatorWindow.*`, `editor/AnimatorPreview.hpp`
- **API:** `AnimStateMachine` (`State`, `Transition`, `Cond`, `Param`, `SetFloat/SetBool/SetTrigger`); scripts `anim_set_float`, `anim_set_bool`, `anim_set_int`, `anim_trigger`, `anim_state()`, `anim_goto`, `anim_set_layer_weight`
- **Editor:** View > Editors & Tools > Animator (state graph); "Open Animator Graph" on the Animator component; per-state Preview in the Inspector
- **Docs:** `docs/animation.md` (Animation state machine, State machines, The Animator window)
- **Tests:** `anim_state_machine`, `anim_sm_layers`, `anim_sm_realloc`, `anim_layers`, `char_state_anim`, `clip_graph`, `ik_state_weight`
- **Status:** Works - seven test suites cover transitions, layers, reallocation safety and graphs; I read the AnimStateMachine and AnimBackend headers, not the 2,900-line window
- **Open:** possible doc bug: docs/animation.md gives crossfades of 0.2, 0.1 and 0.08 s but GaitCrossfades defaults (Stance.hpp) are 0.28, 0.22, 0.12 and 0.24 s; in flight in #554 (Grok): a slot change rewrites only the Locomotion state and keeps the other states' speed, IK weights and events (InsertLocomotion); two inspectors are titled "Animator" (see 5.3)
- **Related:** 5.2, 5.3, 5.7, 5.11, 15.5
- **Words:** Animator, AnimStateMachine, Animation State Machine, state, transition, blend tree, 2D blend, layer, mask, additive, Any State, exit time, interruption, parameter, trigger, Animator window, state graph
- **Seen as:** the wrong clip plays or two clips fight; transition does not fire or fires at once; blend tree feet slide; a state shows a warning glyph; the graph loses its layout; the character snaps instead of blending
- **Ask me like:** "add an attack layer on the upper body"; "why does the hit state not interrupt Walk"

### 5.2 Clips and curves
- **What:** The ways animation data is authored and stored: text `.okayanim` clips of per-bone Euler keys for the blocky Character, track-per-property clips for any object and for imported nodes, one-click presets, named events, the Animation tab with dope sheet, curves and onion skin, and recording a live pose to a clip.
- **Parts:**
  - 5.2.1 Character clips (`.okayanim`) - `Components/AnimClip` - clip/key/bone text format, root track, loop, once, override, easing words, LoadClips, PlayClip, clipIdle/clipWalk bindings
  - 5.2.2 Track clips and curves - `Animation/AnimationClip`, `Animation/AnimationCurve` - one curve per property, smooth or linear, keyframe reduction, Reverse, cubic tangents
  - 5.2.3 Object Animator component - `Components/Animator` - plays an AnimationClip on a Transform (position, rotation, scale tracks), events
  - 5.2.4 One-click presets - `Animation/ClipPresets` - Bob, Hover, Spin, Spin 2D, Pulse, Shake, Sway, Bounce, Pop In, Slide In
  - 5.2.5 Animation events - `Components/AnimClip`, `Components/ModelAnimator` - named markers; reach OnAnimationEvent, `anim_event()`, visual-script On Anim Event
  - 5.2.6 Animation tab - `editor/main.cpp`, `editor/MainShared.hpp` - DrawCharacterAnim, DrawModelAnim, DrawObjectAnimTimeline, DrawAnimationEditor: dope sheet, curve editor, Record, onion skin, shared timeline widgets
  - 5.2.7 Imported-clip tools - `Components/ModelAnimator` - Rename, Duplicate, Delete, Copy/Paste, Split, Reverse, Scale Time, Bake In-Place, events
  - 5.2.8 Pose Capture - `Components/PoseCapture` - in Play, Save pose or Record seconds as a clip on the Model Animator (`.okayclip`); `capture_pose()`
- **Code:** `Components/AnimClip`, `Animation/AnimationClip`, `Animation/AnimationCurve`, `Animation/ClipPresets`, `Components/Animator`, `Components/PoseCapture`, `editor/MainShared.hpp`, `editor/main.cpp`
- **API:** `AnimClip`, `AnimKey`, `AnimEvent`, `AnimationClip`, `AnimationCurve`, `ClipPreset`, `Character::LoadClipsFromFile/PlayClip/StopClip`, `Animator::onEvent`, `PoseCapture`
- **Editor:** View > Animation (clip list, dope sheet, curves, onion skin, presets, Record); Clip tools on imported models; Pose Capture panel
- **Docs:** `docs/animation.md` (first sections, Animating any object, Animation events), `docs/ik.md` (Pose Capture)
- **Tests:** `animclip`, `anim_edit`, `anim_clip_tools`, `animation`, `animator`, `animator_edit`, `char_anim`, `char_anim_events`, `char_blend`, `modelanim_save`, `anim_playback`, `anim_ik_components`
- **Status:** Works - clip parsing, editing, events, save round-trips and blending all have tests; the Animation tab is not covered by a UI test I know of
- **Open:** possible doc gap: `docs/animation.md` is the only description of the `.okayanim` format; an imported clip once changed shape after save and reopen (fixed, modelanim_save); in flight in #554: Pose Capture and IK Target get small edits (19 and 4 lines)
- **Related:** 5.1, 5.3, 3.6, 15.5, 10.1
- **Words:** clip, keyframe, okayanim, dope sheet, curve editor, onion skin, Record, events, preset, AnimClip, AnimationClip, Pose Capture, Bake, loop, once
- **Seen as:** the clip plays too fast or does not loop; a pose pops at a key; events fire twice or never; the edited clip is not saved; preview leaves the model in a strange pose
- **Ask me like:** "add a ping-pong loop mode to clips"; "record a pose while the game is running"

### 5.3 Model clip player and locomotion
- **What:** Stride matching, idle fallback, strafe and backpedal: the Model Animator on an imported model holds all its clips, crossfades between them and switches idle, walk, run, jump, fall, land and stance clips from how the body really moves; slots with no clip fall back to the built-in animation.
- **Parts:**
  - 5.3.1 Model Animator - `Components/ModelAnimator`, `Components/ModelAnimatorPose` - clip library per node, blend time, binds each node to its Transform once, samples straight into transforms
  - 5.3.2 Locomotion slots and auto-mapping - `Components/ModelAnimator`, `Render/ModelImport` - Idle, Walk, Run, Jump, Fall, Land, Crouch, Crouch Walk, Prone, Crawl, Get Up; AutoMapLocomotion by clip name; Fix animations
  - 5.3.3 Stride matching and speed fit - `Components/ModelAnimator` - rate = body speed over clip speed within 0.6x-1.6x, nominal speeds per slot; controllers' default walk and run speeds fit to the clips (#541, #561)
  - 5.3.4 Air and stance clips - `Components/ModelAnimator`, `Components/Stance` - jump, fall, land from vertical motion; crouch and prone clips crossfaded over stance times; get-up
  - 5.3.5 Strafe and backward clips - `Components/ModelAnimator`, `Animation/ClipGraph` - Strafe left/right, Walk/Run back as a 2D blend on moveX/moveZ; legsFollowTravel; reverse walk (#537)
  - 5.3.6 Locomotion graph - `Animation/ClipGraph` - builds an Animator graph from the mapped clips, keeps the built-in fallback and stride matching
- **Code:** `Components/ModelAnimator`, `Components/ModelAnimatorPose`, `Animation/ClipGraph`, `Render/ModelImport`
- **API:** `ModelAnimator` (`Slot`, `onRootMotion`, `Play`), `ClipGraph::Build`, `AutoMapLocomotion`, `FixCharacterAnimations`; scripts `play_clip`, `stop_clip`, `play_clip_once`, `playing_clip`, `clip_finished`, `clip_duration`
- **Editor:** Character Model panel (slots, Stride Match, clip speeds, Body lean, Fix animations, Add animation files); Model Animator inspector (Blend, Locomotion, strafe and back clips)
- **Docs:** `docs/animation.md` (Imported model animations, Mixed animation, Air states, Crouch and prone, Which components do I edit)
- **Tests:** `mixamo_locomotion`, `strafe_blend`, `clip_graph`, `anim_playback`, `imported_glitches`, `rig_sweep`, `footik_drift`
- **Status:** Works - mixamo_locomotion (580 lines), strafe_blend and clip_graph cover slots, stride matching and strafe blends; strafe clips landed in #537
- **Open:** in flight in #554 (Grok): the slot editor under Model Animator > Locomotion (Fill empty slots from names, Use, Preview) and its docs; "two components named Animator": confirmed in main.cpp, the Animator state machine and the keyframe Animator inspectors both use the header "Animator"
- **Related:** 5.1, 5.4, 4.2, 3.7
- **Words:** Model Animator, ModelAnimator, locomotion, idle fallback, idle walk run, stride match, strafe, backpedal, Mixamo clips, crossfade, blend time, slots, Fix animations
- **Seen as:** walking animation feels off, feet slide or the wrong speed; walk-only file has no idle; the model stays upright while the collider crouches; jump plays when stepping off a curb; imported character is in a T-pose
- **Ask me like:** "fill the empty slots from clip names again"; "the run clip plays too slowly at 8 m/s"

### 5.4 Humanoid retarget and library
- **What:** The engine's own humanoid animation (a 15-bone skeleton with procedural gait, stances, gestures and emotions) and the machinery that lets it drive any imported humanoid: a shared bone-name mapper, the Humanoid Retarget component, baked default clips and the default Animator graph.
- **Parts:**
  - 5.4.1 Humanoid bone mapper - `Animation/HumanoidBones` - MapHumanoidBones for Mixamo, UE mannequin, Rigify, VRoid, plain names; checks the skeleton's shape so a dog or robot arm is rejected; helper bones (ik_, twist, fingers) never match
  - 5.4.2 Humanoid Retarget component - `Components/HumanoidRetarget` - the Character runs in drive-external mode, its pose is transferred bone by bone on top of a rest-stance alignment; weight, leanScale, hand `boneMap`, Re-Detect Bones
  - 5.4.3 Procedural humanoid motion - `Animation/HumanoidMotion` - stride from ground speed, foot plants, hip bob, knee bend, walk/run/crouch-walk styles, stance and air poses, crawl, gestures, emotions (19 built-in states)
  - 5.4.4 Default clip library - `Animation/HumanoidLibrary` - baked once per run: gaits in eight directions at several speeds, crouch, prone, crawl, drop, get-up, jump, fall, land, hang, gestures; seamless loops
  - 5.4.5 Enable built-in animation on a model - `Render/ModelImport` - EnableBuiltinAnimations (needs 8 or more of the 15 bones), CountHumanoidBones, AddAnimationsFromFile (bones matched by name, rotations plus height-scaled hips)
  - 5.4.6 Map Humanoid Bones window - `editor/main.cpp` - DrawBoneMap for rigs the auto-detect misses
- **Code:** `Animation/HumanoidBones`, `Components/HumanoidRetarget`, `Animation/HumanoidMotion`, `Animation/HumanoidLibrary`, `Components/Character`, `Render/ModelImport`, `editor/main.cpp`
- **API:** `humanoid::MapHumanoidBones`, `HumanoidRetarget`, `HumanoidMotion::PoseFor`, `HumanoidLibrary::Get`, `Character::driveExternal`
- **Editor:** Humanoid Retarget inspector (Weight, Body lean, Re-Detect Bones); Hierarchy right-click > Map Humanoid Bones... / Use Built-in Animations
- **Docs:** `docs/animation.md` (Built-in locomotion, Built-in animations on imported rigs)
- **Tests:** `humanoid_bones`, `humanoid_retarget`, `humanoid_template`, `locomotion`, `gait_continuity`, `character_feel`, `rig_sweep`, `char_blend`
- **Status:** Works - humanoid_bones (347 lines), locomotion and rig_sweep (11 rigs) cover mapping, gait and many rig conventions
- **Open:** old map "retargeting by avatar slot": hand mapping of a rig's bones landed (#522, git log); clips from a differently proportioned rig transfer rotations only; no other open item recorded
- **Related:** 3.7, 5.3, 5.12, 4.9
- **Words:** humanoid, retarget, HumanoidRetarget, avatar, bone map, Mixamo, VRoid, Rigify, built-in animation, gait, procedural animation, T-pose
- **Seen as:** imported character is in a T-pose or has the wrong animation; arms stay out; one bone twists; feet skate; legs bend backwards; walk hips snap at every footfall
- **Ask me like:** "map a Blender rig's fingers too"; "make the built-in walk less bouncy"

### 5.5 Root motion and skinning
- **What:** Letting the animation's root bone move the character, and deforming a rigged mesh with its bones.
- **Parts:**
  - 5.5.1 RootMotion component - `Components/RootMotion` - reads the root bone's travel, moves the object, re-centres the bone, explicit physics or animation mode
  - 5.5.2 Model Animator root motion - `Components/ModelAnimator` - Root Motion toggle, + Vertical, + Turning, Moves: Parent (velocity to a Rigidbody3D), Animate in place, `onRootMotion`, Bake In-Place
  - 5.5.3 Skinned mesh - `Components/SkinnedMesh` - up to 4 joints per vertex, Linear (GPU in the viewport and player) or Dual quaternion (CPU), Normalize weights, only re-skins when a bone moved
  - 5.5.4 Skeleton overlay - `editor/main.cpp` - View > Scene Overlays > Skeletons (Selected model, All, Off), hover and click a bone
- **Code:** `Components/RootMotion`, `Components/SkinnedMesh`, `Components/ModelAnimator`, `editor/main.cpp`
- **API:** `RootMotion`, `SkinnedMesh` (`SkinningMode`), `ModelAnimator::onRootMotion`; script `set_root_motion(on)`
- **Editor:** Model Animator inspector (Root Motion), Skinned Mesh inspector (bone tree, statistics), Scene overlay
- **Docs:** `docs/animation.md` (Root motion, Imported characters: skeleton, skinning, GPU skinning)
- **Tests:** `anim_core`, `anim_ik`, `skinning`
- **Status:** Works - anim_core checks root motion on centimetre and rotated armatures and joints inside one model; skinning test covers vertex-to-joint binding
- **Open:** bones beyond about 256 per mesh fall back to CPU skinning (per docs); GPU skinning and culling code live in the renderers (2.8)
- **Related:** 3.6, 5.3, 2.8
- **Words:** root motion, skinning, skinned mesh, dual quaternion, linear blend, GPU skinning, in place, bone weights, candy-wrapper
- **Seen as:** the character slides inside its capsule or walks without the capsule; wrists twist like a candy wrapper; the mesh stretches; the model pops at the screen edge
- **Ask me like:** "apply root motion only on the ground plane"; "switch the arms to dual quaternion skinning"

### 5.6 Foot, limb and chain IK
- **What:** Solvers that bend bones so a foot, hand or chain tip reaches a goal, run after all animation each frame: feet planted on slopes and steps, one arm or leg reaching a target, and long chains (tails, tentacles, spider legs).
- **Parts:**
  - 5.6.1 Foot IK - `Components/FootIK` - raycast per foot, two-bone leg solve, Adjust Pelvis, Plant Down, Align To Slope, Toe & Heel rays, foot lock, smoothing; wires itself on the blocky Character
  - 5.6.2 Limb IK - `Components/LimbIK` - one arm or leg: target, pole, joint limits, Stretch, Softness, Match Target Rotation
  - 5.6.3 Chain IK - `Components/ChainIK` - FABRIK or CCD, pole, per-joint limits, keeps bone lengths and root
  - 5.6.4 Solver maths - `Math/TwoBoneIK`, `Math/Fabrik`, `Math/CCD`, `Math/IKMath`, `Math/JointLimit`, `Math/IKFade` - closed-form two-bone, FABRIK, CCD, finite/normalize guards, hinge and ball limits with presets, fade in and out
  - 5.6.5 Shared plumbing - `Components/IKBones`, `Components/IKRestore`, `Components/IKPose`, `Components/CharacterIK` - bone lookup near the character, per-frame restore so corrections never pile up, IK posing in the Scene view, controllers' footIK flag
  - 5.6.6 Poles, limits and target blending - `Components/LimbIK`, `Math/JointLimit` - Pole Mode (Auto keeps the animation's bend, Character, World), joint-limit presets, Target Blend 0.15 s
  - 5.6.7 IK posing and helpers in the editor - `editor/main.cpp` - IK toggle in the Scene toolbar, Set up humanoid IK, IKJointLimitUI, MakeIKHelper
- **Code:** `Components/FootIK`, `Components/LimbIK`, `Components/ChainIK`, `Components/IKBones`, `Components/IKRestore`, `Components/IKPose`, `Components/CharacterIK`, `Math/TwoBoneIK`, `Math/Fabrik`, `Math/CCD`, `Math/IKMath`, `Math/JointLimit`, `Math/IKFade`
- **API:** `FootIK`, `LimbIK`, `ChainIK`, `IKJointLimit`, `TwoBoneIK`, `Fabrik`; scripts `ik_enable`, `ik_set_weight`, `ik_set_target`, `ik_set_pole`
- **Editor:** Add Component > Animation; Auto-Detect Bones buttons; Foot IK probe gizmos when selected; Model Animator > Set up humanoid IK
- **Docs:** `docs/ik.md`, `docs/animation.md` (IK on imported rigs, Foot IK, IK sections)
- **Tests:** `anim_ik`, `foot_planting`, `footik_drift`, `ik_probes`, `ik_core`, `controllers_ik`
- **Status:** Works - IK plan PRs 1-7 and the docs (#424) are on main; Foot IK, ground probes, drift and chain solvers have their own tests
- **Open:** old map "IK rewrite plan" appears done (git log shows plan PRs 1-7); possible doc gap: the solve-order table in docs/ik.md omits IK Target (70) and Aim Offset (102), which the code orders at 70 (before the solvers) and 102 (after Full Body IK); hands-on-ledge Limb IK is not wired (Traversal.hpp comment)
- **Related:** 5.7, 5.1, 5.9, 4.2
- **Words:** IK, foot IK, limb IK, chain IK, FABRIK, CCD, two-bone, pole, knee, elbow, joint limit, pelvis, plant, slope, foot lock, IKRestore
- **Seen as:** feet don't touch the ground or sink; feet slide on slopes; a knee or elbow bends the wrong way; a limb flips; a hand snaps to a new target; corrections pile up
- **Ask me like:** "plant the feet on stairs without popping"; "give the spider legs a joint limit"

### 5.7 Look-at, aim and full-body IK
- **What:** Head tracking, single-bone aiming, whole-humanoid reach with four effectors, goal objects, camera-driven aim offsets with recoil, and the script calls and per-state weights that control them.
- **Parts:**
  - 5.7.1 Look-At IK - `Components/LookAtIK` - spine, neck, head chain with shares, Clamp, eye bones, smoothing, Max Distance, Auto-Detect Chain, forward axis read from the rest pose
  - 5.7.2 Aim IK - `Components/AimIK` - one exact aim with up axis, weight, angle clamp, smoothing
  - 5.7.3 Full Body IK - `Components/FullBodyIK` - hand and foot effectors, hints, Body Pull, Spine Reach, Soft Reach, Stretch, look, blend speed, `reached` flags
  - 5.7.4 IK Target - `Components/IKTarget` - an empty object a limb reaches for; hands and feet drive Full Body IK, the head drives Look-At; previews outside Play
  - 5.7.5 Aim Offset and recoil - `Components/AimOffset` - pitch and yaw on spine, chest and arms from the camera or a named target; `recoil(strength)`
  - 5.7.6 IK script tools - `Script/AnimIkTools` - one table behind the OkayScript builtins and the typed API: kind weights (foot, look, aim, hand_l, hand_r, full), limb targets, layer weights, root motion, model swap
  - 5.7.7 State-driven IK weights - `Components/AnimStateMachine` - per-state IK weight and curve, look weight; death, sit, gestures and emotes switch look and aim off
- **Code:** `Components/LookAtIK`, `Components/AimIK`, `Components/FullBodyIK`, `Components/IKTarget`, `Components/AimOffset`, `Script/AnimIkTools`
- **API:** `LookAtIK`, `AimIK`, `FullBodyIK`, `IKTarget`, `AimOffset`, `IkKind`; scripts `ik_look_at`, `ik_look`, `ik_hand`, `ik_foot`, `ik_release`, `ik_reached`, `ik_set_weight(kind, w)`, `ik_clear_target`, `recoil`
- **Editor:** Add Component > Animation; right-click a character > Add IK Target; Look-At cone and weight label when selected; visual-script IK Look At and IK Hand Reach nodes
- **Docs:** `docs/ik.md`, `docs/animation.md` (IK, IK: smoothing and limits)
- **Tests:** `ik_core`, `ik_look_stretch`, `ik_rigs`, `ik_state_weight`, `anim_ik_components`, `anim_ik_tools`
- **Status:** Works - IK Target, Aim Offset and Pose Capture landed in #539, per-kind script tools in #563, state weights in #535; head and walk jitter reported as not fixed in #554
- **Open:** in flight in #554 (Grok): small edits to IK Target (4 lines) and Pose Capture (19 lines); head and animation jitter stated there as not fixed
- **Related:** 5.6, 5.9, 5.1, 5.11
- **Words:** look at, head tracking, aim IK, full body IK, FullBodyIK, IK Target, Aim Offset, recoil, reach, effector, hint, body pull, spine reach
- **Seen as:** the head stares at the origin or the target behind; the neck snaps; hands don't reach things; the chest dips when reaching just past the hand; the spine bends the wrong way on a model that faces -Z
- **Ask me like:** "make the guard look at the player only within 10 m"; "add recoil to the pistol"

### 5.8 Ragdolls and hit reactions
- **What:** A humanoid goes limp on death or flinches on a hit that does not kill it. The ragdoll is a particle simulation over the 15 humanoid bones, not rigid bodies and joints.
- **Parts:**
  - 5.8.1 Verlet ragdoll - `Components/Ragdoll` - particles per bone plus tips, distance constraints, torso brace, elbow and knee minimum reach, ray collisions against 3D colliders and terrain, 60 Hz after animation and IK (order 900)
  - 5.8.2 Death and revive - `Components/Ragdoll` - ragdollOnDeath from a HealthStat death, push away from the hit, animators, IK and controllers switched off while limp and restored after
  - 5.8.3 Physical hit reactions - `Components/Ragdoll` - hitReactions shove the struck part which springs back over Recover Time
  - 5.8.4 Script and C++ calls - `Components/Ragdoll` - `ragdoll()`, `ragdoll(0)`, `ragdoll_push`, `is_ragdoll()`, Activate, Deactivate, Hit, IsActive, IsSettled
- **Code:** `Components/Ragdoll`
- **API:** `Ragdoll`; scripts `ragdoll()`, `ragdoll_push(...)`, `is_ragdoll()`
- **Editor:** Add Component > Ragdoll (death + hit reactions); Ragdoll inspector
- **Docs:** `docs/ragdolls.md`
- **Tests:** `ragdoll`
- **Status:** Works - ragdoll test covers limp, landing in one piece, standing up, falling away from a hit and flinching
- **Open:** no cloth, spring-bone or secondary motion exists (searched for cloth, jiggle, spring bone); animated hit and death clips are separate, see 5.12
- **Related:** 5.12, 5.6, 11.7, 6.2
- **Words:** ragdoll, limp, death, hit reaction, flinch, Verlet, Ragdoll component, Death Push, Recover Time
- **Seen as:** character falls apart or flops; tumbles through the floor; does not get back up after a revive; body twitches on a hit
- **Ask me like:** "add a heavy ragdoll preset"; "keep the head from clipping the ground"

### 5.9 Object holding
- **What:** A character picks an object up and holds it: hands go to the object's grips through Full Body IK, the elbows bend down and out, a heavy load leans the torso and slows the character, and the object follows the chest, a socket or the first-person hands.
- **Parts:**
  - 5.9.1 Holding component - `Components/ProceduralObjectHolding` - Primary Hand, Holding Style, transition, drop with the character's velocity, hands never stretch (order 90)
  - 5.9.2 Holdable Object - `Components/HoldableObject` - styles Pistol, Rifle, Box, Flashlight, Sword, Cup, Heavy, Custom; grip points; weight; Automatic style from name, tag, size and weight
  - 5.9.3 Body adjustment and carry animations - `Components/ProceduralObjectHolding`, `Components/MoveSpeed` - torso lean as an additive layer, speed down to 0.55x, Animator parameters holding, carryStyle, carryWeight
  - 5.9.4 Sockets - `Components/Sockets` - named attach point on a character (holster, back slot, hand), shared with Equipment
  - 5.9.5 Pickup and drop prompt - `Components/HoldPrompt` - on-screen "E to pick up" and "E or G to drop" line for the stock player templates; drop lives here, not in the holding solver
  - 5.9.6 First-person view of held items - `Components/FirstPersonHands` - hands panel and the owner copy (see 4.7)
  - 5.9.7 Multiplayer holding and grip editor - not on main - PR 4 on branch `claude/holding-net` (adds a `replicate` switch and server RPCs), PR 5 on `claude/holding-editor` (grip editor, SetStyle, script and visual-script API); no open PR
- **Code:** `Components/ProceduralObjectHolding`, `Components/HoldableObject`, `Components/Sockets`, `Components/MoveSpeed`, `Components/HoldPrompt`
- **API:** `ProceduralObjectHolding`, `HoldableObject`, `SpeedMultiplier()`, `MoveSpeedScale`; scripts `hold_nearest(range)`, `hold_pickup("Crate")`, `hold_drop()`, `hold_object()`
- **Editor:** Add Component > Animation (Procedural Object Holding, Holdable Object); First Person Hands panel
- **Docs:** `docs/animation.md` (Holding objects)
- **Tests:** `object_holding`, `inventory_holding`
- **Status:** Partial - holding styles and body adjustment are on main (PRs 1-3: #362, #377, #389); multiplayer sync and the grip editor exist only on unmerged branches
- **Open:** old map: holding PRs 3-4: body adjust is done (#389), multiplayer sync (PR 4, branch `claude/holding-net`) and grip editor (PR 5, branch `claude/holding-editor`) are not merged; finger joints are never posed (hand is one block)
- **Related:** 4.7, 5.7, 11.1, 16.8
- **Words:** holding, grip, hold object, Procedural Object Holding, Holdable Object, carry, pistol grip, rifle, socket, body adjust, pick up, throw
- **Seen as:** character holding a gun or tool looks wrong; hands float off the object; the object sinks into the chest; heavy box does not slow the player; other players see the item in the wrong place
- **Ask me like:** "hold a bow with a custom grip"; "merge multiplayer holding"

### 5.10 Sprite animation
- **What:** Frame-by-frame animation for 2D sprites: a list of frames or one row of an atlas, named clips, play and loop, with controllers that choose idle, run or jump from movement.
- **Parts:**
  - 5.10.1 Sprite Animator - `Components/SpriteAnimator` - frames, atlas cells through the SpriteRenderer's uv region, named SpriteClips (idle, run, jump), Play(name), non-looping stops on the last frame
  - 5.10.2 Controller hookup - `Components/CharacterController2D`, `Components/Platformer25DController` - controllers pick the sprite clip from movement (spriteanim test)
- **Code:** `Components/SpriteAnimator`
- **API:** `SpriteAnimator`, `SpriteClip`, `Play(name)`
- **Editor:** Sprite Animator inspector; View > Editors & Tools > Sprite Editor
- **Docs:** `docs/making_a_game.md`
- **Tests:** `spriteanim`
- **Status:** Works - spriteanim covers frames, atlas mode, named clips, non-looping and serialization
- **Open:** none recorded
- **Related:** 4.3, 2.1, 2.9
- **Words:** sprite animation, flip-book, atlas, frames, SpriteAnimator, sprite clip, idle run jump
- **Seen as:** the sprite animation does not advance; it shows the wrong frame after loading; wrong cell of the atlas
- **Ask me like:** "add animation events to sprite frames"; "flip the sprite clip with the facing"

### 5.11 Animation script control
- **What:** The script-facing side of animation (OkayScript, visual scripts, typed scripts): which animation component a script drives, play and hold a clip, set Animator parameters, change IK weights and targets, swap the character model, capture poses. One implementation serves text scripts, typed scripts and the generated visual-script nodes.
- **Parts:**
  - 5.11.1 Clip commands - `Components/AnimControl` - finds the animator on the object, then children breadth-first, then parents; play_clip holds until stop_clip; play_clip_once; clip_speed; clip_normalized
  - 5.11.2 Parameter and state calls - `Components/AnimStateMachine` - anim_set_float/bool/int, anim_trigger, anim_state, anim_prev_state, anim_goto, layer weights
  - 5.11.3 IK and model calls - `Script/AnimIkTools` - ik_* kinds and limb targets, set_root_motion, set_character_model, FindAnimatorComponent
- **Code:** `Components/AnimControl`, `Script/AnimIkTools`
- **API:** `play_clip`, `stop_clip`, `play_clip_once`, `anim_event()`, `anim_set_float`, `anim_trigger`, `ik_set_weight`, `set_character_model`, `capture_pose`; typed `Animator` and `IK` components; visual-script Play Clip, Play Action, On Anim Event
- **Editor:** Visual script nodes (Play Clip, Hit React, Roll, Dodge, Sit...); Script Editor completions
- **Docs:** `docs/animation.md` (From a script), `docs/ik.md` (Scripting, Animator and IK calls)
- **Tests:** `anim_script_control`, `script_anim`, `anim_ik_tools`, `anim_clip_tools`
- **Status:** Works - four test suites; per-kind script tools landed in #563
- **Open:** C# and Lua keep their own animation APIs (per docs/ik.md); a played clip holds until stop_clip, movement-driven animation waits
- **Related:** 5.1, 5.7, 13.4, 14.4
- **Words:** play_clip, stop_clip, anim_set_float, anim_trigger, ik_enable, set_character_model, capture_pose, Play Clip node
- **Seen as:** a script clip is overwritten by locomotion; a state machine and locomotion fight; events at time 0 never fire; script plays on the wrong model part
- **Ask me like:** "play a wave then return to locomotion from a script"; "set the aim weight from a visual script"

### 5.12 Built-in action clips
- **What:** Death, hits, roll, dive, vault, sit, swim and emotes: a set of baked humanoid action clips plus the states and Any-State transitions that add them to the default Animator. Gameplay displacement stays on the controllers; the clips are visual. A saved graph records which revision of the set it has.
- **Parts:**
  - 5.12.1 Action framework - `Animation/HumanoidActions` - revision counter (death 1, dive 2, sit 3, hits 4, roll 5, vault 6, swim 7, emotes 8), a deleted state is not put back, Add built-in actions
  - 5.12.2 Death and revive - `Animation/HumanoidActions` - Death_F/B/L/R and DeadHold, 1D tree on deathDir, collider shrink, Play Death Anim on Health
  - 5.12.3 Hit reactions - `Animation/HumanoidActions` - Hit_F/B/L/R light on a torso layer, Hit_Heavy, Hit_Crouch, Hit_Prone; hit_react(), staggerScale
  - 5.12.4 Roll, dodge, slide - `Animation/HumanoidActions` - Roll_F, Dodge_L/R/B, Slide and SlideEnd; roll(), dodge()
  - 5.12.5 Dive, vault, mantle, ledge - `Animation/HumanoidActions` - Dive/DiveLand, Vault, Mantle_Low/High, LedgeGrab, shimmy, ClimbUp, LetGo
  - 5.12.6 Sitting, swimming, emotes - `Animation/HumanoidActions` - SitDown/Sit/StandUp for chair and ground, Swim tread and forward, Emote_Nod/Shake/Shrug
  - 5.12.7 Remote players - `Components/NetworkSync` (owned by 16.3) - owner's death, roll, sit, swim and head look ride the end of the animation sync message (action ids 1-4); a proxy plays the action and eases its head to the owner's look
- **Code:** `Animation/HumanoidActions`, `Animation/HumanoidLibrary`, `Components/AnimStateMachine`
- **API:** `HumanoidActions`, `play_action`, `die`, `revive`, `hit_react`, `roll`, `dodge`, `dive`, `sit`, `stand_up`, `vault`, `mantle`, `climb_up`, `let_go`; visual-script Die, Revive, Hit React, Roll, Dodge, Dive, Sit
- **Editor:** Animator inspector "Add built-in actions"; Preview on each state
- **Docs:** `docs/animation.md` (Actions)
- **Tests:** `ik_state_weight`, `net_anim_actions`, `net_proxy_gait`
- **Status:** Works - all eight action sets are on main (#435, #506); remote sync covers death, roll, sit, swim and look only
- **Open:** in flight in #554 (Grok): a `netActionParam` after the look fields (dodge direction or swim speed) and a roll-left check; dive, hit, vault and emotes are not replicated; hands-on-ledge Limb IK is not wired; swim has no water volume in the clip set (the controllers read a Water probe)
- **Related:** 5.1, 5.8, 4.2, 4.11, 16.8
- **Words:** actions, death clip, hit reaction, roll, dodge, slide, dive, vault, mantle, ledge, sit, swim, emote, HumanoidActions
- **Seen as:** death snaps to the floor or gets cut short by jump or fall; hit flinch freezes the legs; sit pose floats above the chair; remote player does not roll or die; a deleted state comes back
- **Ask me like:** "add a wave emote"; "replicate the dive to other players"

### 5.13 Player animation setup
- **What:** In flight, not on main: a controller-panel status read-out ("what animation and IK does this player have") and one repeatable step that adds Foot IK, Look-At and the default Animator only where missing, then re-runs the animation fix without touching weights, chains or custom graphs.
- **Parts:**
  - 5.13.1 Inspect and wire - Animation/PlayerSetup (on branch `grok/anim-integrate` only) - InspectPlayerAnimation and WirePlayerAnimation
  - 5.13.2 Head and walk stability tests - tests head_stability, player_setup and destroy_unstarted (on the same branch only)
  - 5.13.3 Play-mode cursor fix - editor change in #554: Esc releases the cursor even when the key is already up, Ctrl+P still stops Play
- **Code:** -
- **API:** `InspectPlayerAnimation`, `WirePlayerAnimation`, `PlayerAnimStatus` (in flight)
- **Editor:** controller panel "setup status" (in flight)
- **Docs:** `docs/animation.md` additions on the branch only
- **Tests:** -
- **Status:** In flight - PR #554 "Integrate owned animation and IK work", head on `grok/anim-integrate` (Grok's sessions); not on main
- **Open:** per the PR body: head and walk jitter are not fixed; Play-cursor retest on the box; its body lists the `localization` test (missing font) as the one failing ctest in its run; do not edit, ask first
- **Related:** 3.7, 5.3, 5.7, 4.1
- **Words:** PlayerSetup, wire animation, setup status, Play cursor, head stability, jitter
- **Seen as:** the player has no foot IK or Animator until set up by hand; the head or body jitters while walking; Esc does not release the mouse in Play
- **Ask me like:** "what does #554 change"; "is the head jitter fixed"
