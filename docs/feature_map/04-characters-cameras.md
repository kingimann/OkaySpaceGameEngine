# 4. Characters & cameras

Everything that makes a body move under player control and everything that frames it: the one Player Controller (first and third person), stances, 2D and 2.5D controllers, shooter, top-down, click-to-move and vehicle controllers, the orbit and first-person cameras and hands, virtual cameras with dolly rails, the camera component, and the blocky customizable Character.
About 35 headers under `Components/` (most of them header-only and large: `FirstPersonController` 960 lines, `ThirdPersonController` 890, `Platformer25DController` 680) plus `editor/FpHandsWindow.*`; the editor inspectors live in `editor/main.cpp`.
Open pull request #554 (Grok's sessions, in flight) touches the controller panel (PlayerSetup status and a "wire missing animation and IK" step) and Play-mode cursor release; head and walk jitter are stated there as not fixed. Animation and IK pages: 5.

## 4. Characters & cameras

### 4.1 Player Controller
- **What:** One component, Player Controller, makes a first- or third-person character (View = First, Third or Both). Its View setting says which views exist (First only, Third only, or Both with a toggle key and a blended camera). The movement and camera code lives in parts it adds and removes itself.
- **Parts:**
  - 4.1.1 Player Controller wrapper - `Components/PlayerController` - ViewMode First/Third/Both, SetViewMode, Add, Adopt (wraps old scenes' separate controllers), CurrentView; script set_view/get_view
  - 4.1.2 First-person movement part - `Components/FirstPersonController` - walk/run, jump, crouch/prone, lean (Q/E), slide, dodge, dive, vault, swim, view bob, right-mouse zoom, IK look
  - 4.1.3 Third-person movement part - `Components/ThirdPersonController` - same movement set plus the orbit camera, aim, shoulder offset and spring arm (4.6)
  - 4.1.4 View toggle and camera blend - `Components/PlayerView` - V or gamepad Back, Switch Blend Time, Easing, Snap, Eye Offset, Third Person Distance, head and neck hidden near the eyes
  - 4.1.5 Shared feel pieces - `Components/ControllerFeel` - AnimLink (feeds Animator parameters), FovKick, Landing, LookSmoother, FollowPivot, SpringArm
  - 4.1.6 Move-speed and death hooks - `Components/MoveSpeed`, `Components/DeathLock` - one MoveSpeedScale for load and encumbrance slow-downs; a dead body reads no move input
- **Code:** `Components/PlayerController`, `Components/PlayerView`, `Components/FirstPersonController`, `Components/ThirdPersonController`, `Components/ControllerFeel`, `Components/MoveSpeed`, `Components/DeathLock`, `editor/main.cpp`
- **API:** `PlayerController`, `PlayerView`, `FirstPersonController`, `ThirdPersonController`, `MoveSpeedScale`; scripts `set_view(0|1)`, `get_view()`, `SetViewMode(0|1|2)`; Add Component > Gameplay > Player Controller
- **Editor:** Player Controller panel in the Inspector (View selector, Both section, movement, jump, sprint, crouch, prone, lean, slide, dodge, vault, foot IK, eye, orbit, Set Character Model...)
- **Docs:** `docs/making_a_game.md` (The Player Controller, Controller movement extras)
- **Tests:** `player_controller`, `view_upgrade`, `player`, `player_fixes`, `view_ground`, `run_feel`, `mp_controllers`, `net_player_spawn`, `net_auth_move`, `net_predict`
- **Status:** Works - player_controller, view_upgrade and player tests cover the View modes, toggle and old-scene loading; I read PlayerController and PlayerView headers, not the full controllers
- **Open:** in flight in #554 (Grok): controller panel gets an animation setup status; head and walk jitter are stated there as not fixed; a scene saved with the Player Controller records cannot be opened by a build from before that change (per docs)
- **Related:** 4.2, 4.6, 4.7, 5.1, 5.6, 5.9, 16.8
- **Words:** Player Controller, first person, third person, View, Both, toggle view, V key, FirstPersonController, ThirdPersonController, PlayerView, spring arm, FOV kick, bob
- **Seen as:** the player floats, slides or jumps badly; V does not switch view or the camera pops; the head shows inside the first-person lens; a remote player's controller fights the network; wrong move speed after attaching a model
- **Ask me like:** "make the view switch faster"; "the player stops too slowly when I release the key"

### 4.2 Stances and movement
- **What:** The movement behaviours shared by the player controllers: a stance level that eases between standing, crouching and prone (collider, camera and speed follow), stairs and curbs, jumps and landings, slope limits, slides, dodges, dolphin dives, vaults and ledges, and swimming.
- **Parts:**
  - 4.2.1 Stance levels and timings - `Components/Stance`, `Components/StanceControl` - 0 stand, 1 crouch, 2 prone eased over StanceTimes (0.25 s, 0.5 s, 0.8 s); keys, toggle or hold, Allow Prone, Resize Body, headroom check, SetStance
  - 4.2.2 Step-up, ground latch, platform ride - `Components/StepUp` - GroundLatch, PlatformRide (riders turn with rotating platforms), StanceBody; Step Offset 0.35, three-probe stair test
  - 4.2.3 Jump, landing, stop settle, safety nets - `Components/ControllerFeel`, `Components/FirstPersonController` - Landing, jump buffer, apex counts as airborne, Slope Limit (50 deg), Fall Reset Y (-100) with a missing-collider hint
  - 4.2.4 Slide, dodge, dolphin dive - `Components/DodgeAction`, `Components/DiveAction` - sprint then crouch slides; canDodge and I-Frames default off; sprint then prone dives (Dive on prone while sprinting, default on)
  - 4.2.5 Vault, mantle, ledge grab - `Components/Traversal` - TraverseKind, TraverseProbe, TraverseJob; canVault, canMantle, canLedgeGrab default off; vault() and mantle() still probe
  - 4.2.6 Swimming - `Components/FirstPersonController`, `Components/ThirdPersonController` - waist-deep water (read from a Water probe) switches to swim speeds
  - 4.2.7 Collision for transform-only players - `Physics/PlayerCollision` - a sphere pushed out of solid geometry when there is no Rigidbody3D
- **Code:** `Components/Stance`, `Components/StanceControl`, `Components/StepUp`, `Components/ControllerFeel`, `Components/Traversal`, `Components/DodgeAction`, `Components/DiveAction`, `Physics/PlayerCollision`
- **API:** `StanceControl`, `StanceTimes`, `GroundLatch`, `TraverseKind`; scripts `crouch()`, `prone()`, `stand()`, `set_stance(0|1|2)`, `SetStance`, `dodge(dir)`, `dive()`, `vault()`, `mantle()`
- **Editor:** Player Controller panel sections (Crouch / prone, Slide, Dodge, Vault and mantle, Stance changes on the Animator)
- **Docs:** `docs/making_a_game.md` (Crouch and prone), `docs/animation.md` (Actions: dive, roll, vault)
- **Tests:** `stance`, `stance_save`, `jump`, `stop_settle`, `run_feel`, `view_ground`, `player_fixes`
- **Status:** Works - stance, stance_save, jump and stop_settle exist; vault, dodge and dive default off by design
- **Open:** old map: verify prone on first and third person; hands-on-ledge Limb IK is not wired (Traversal.hpp comment); dive, hit, vault and emote actions are not sent to remote players (per docs, see 5.12)
- **Related:** 4.1, 5.12, 5.6, 5.9, 6.2
- **Words:** crouch, prone, crawl, stance, step up, stairs, curb, slope limit, coyote, wall slide, slide, dodge, dive, vault, mantle, ledge, fall reset, swim
- **Seen as:** the player cannot crouch or go prone; stands up inside low cover; snags on stairs or sills; falls through the floor forever; saved crouched player stands up inside a ceiling; slide or dodge does nothing
- **Ask me like:** "verify prone works in first and third person"; "turn on vaulting for the third person player"

### 4.3 2D and 2.5D controllers
- **What:** Ready-made controllers for flat and side-on games: a top-down or platformer 2D character controller, a top-down 2D controller with dash and combat, and a 2.5D platformer with depth layers, ledges and grabbing.
- **Parts:**
  - 4.3.1 Character Controller 2D - `Components/CharacterController2D` - TopDown or Platformer mode, sprint, acceleration, air control, coyote time, jump buffer, variable and multi-jump, wall slide and wall jump
  - 4.3.2 Top Down Controller 2D - `Components/TopDownController2D` - movement, dash, facing, animation, world limits, combat
  - 4.3.3 2.5D Platformer controller - `Components/Platformer25DController` - depth layers (3 by default), step height 35 cm, variable jump, coyote time, grab and drag, ledge hang and climb, side camera, Drive Animation
  - 4.3.4 Sprite animation hookup - `Components/SpriteAnimator` - controllers pick idle, run, jump clips from movement (see 5.10)
- **Code:** `Components/CharacterController2D`, `Components/TopDownController2D`, `Components/Platformer25DController`
- **API:** `CharacterController2D::Mode`, `TopDownController2D`, `Platformer25DController`; script events `on_jump`, `on_land`, `on_layer`, `on_grab`, `on_release`, `on_climb`
- **Editor:** Inspector panels per controller; GameObject > Player > Top Down (2D), 2.5D Platformer (LittleBigPlanet)
- **Docs:** `docs/shooter_and_25d_controllers.md`
- **Tests:** `charctrl`, `shooter25d`, `spriteanim`
- **Status:** Works - charctrl covers 2D top-down, platformer, 3D and rotating platforms; shooter25d covers the 2.5D controller (layers, jump, grab, saves)
- **Open:** old map: improve the 2.5D controller (no specifics recorded); the 2.5D controller keeps its own ledge motion and only sets the `hanging` animation bool
- **Related:** 4.4, 5.10, 6.1, 6.2
- **Words:** 2D controller, platformer, top-down, 2.5D, LittleBigPlanet, depth layers, coyote time, wall jump, dash, ledge grab
- **Seen as:** player sticks to a wall in the air; layer switch gets stuck between two layers; the 2.5D character runs on the spot against a wall; jump feels floaty
- **Ask me like:** "add wall cling to the 2.5D controller"; "the 2D controller should buffer jumps longer"

### 4.4 Shooter and top-down
- **What:** The other ready-made 3D controllers: third-person shooter, twin-stick top-down shooter, fixed-angle top-down, click-to-move, a free-roam fly camera and a plain 3D character controller.
- **Parts:**
  - 4.4.1 Third-person shooter - `Components/ThirdPersonShooterController` - look, right-mouse aim, left-mouse fire, cursor lock, lean
  - 4.4.2 Top-down shooter - `Components/TopDownShooterController` - mouse aim, bullets, projectile prefab or hitscan, pellets, ammo and reload, dash, kicking follow camera
  - 4.4.3 Top-down (3D) - `Components/TopDownController` - world- or camera-relative move, fixed-angle follow camera
  - 4.4.4 Click to move - `Components/ClickToMoveController` - MoveTo, walks to the click point, trailing camera, expanding click marker
  - 4.4.5 Free roam fly camera - `Components/FreeRoamController` - WASD along the view, Space/Ctrl or E/Q up and down, sprint
  - 4.4.6 Character Controller 3D - `Components/CharacterController3D` - plain WASD controller on a Rigidbody3D (or Transform), sprint, momentum, optional animation and foot IK
  - 4.4.7 Crosshair reticle - `Components/Crosshair` - centre-screen reticle with ticks, dot and outline
- **Code:** `Components/ThirdPersonShooterController`, `Components/TopDownShooterController`, `Components/TopDownController`, `Components/ClickToMoveController`, `Components/FreeRoamController`, `Components/CharacterController3D`, `Components/Crosshair`
- **API:** `TopDownShooterController`, `ClickToMoveController::MoveTo`, `FreeRoamController`; script events `on_fire`, `on_reload`, `on_reloaded`, `on_empty`, `on_dash`
- **Editor:** Inspector panels per controller; GameObject > Player > Third Person Shooter, Top Down (3D), Top Down Shooter (3D), Click To Move
- **Docs:** `docs/shooter_and_25d_controllers.md`, `docs/making_a_game.md`
- **Tests:** `shooter25d`, `clicktomove`, `player`, `mp_controllers`, `charctrl`
- **Status:** Works - shooter25d, clicktomove and player tests exist; Projectile 3D is in 6.6
- **Open:** none recorded; the fall-back for animation is the shared Character locomotion (5.3, 4.9)
- **Related:** 4.1, 4.3, 6.6, 8.1
- **Words:** third-person shooter, top-down shooter, twin-stick, click to move, RuneScape, free roam, fly camera, crosshair, dash, reload, ammo
- **Seen as:** aim cursor offset from the crosshair; the shooter gets stuck on stairs; fire key fires while typing in a field; click-to-move walks through walls; the fly camera is too fast
- **Ask me like:** "add a second weapon slot to the top-down shooter"; "click-to-move should path around obstacles"

### 4.5 Vehicles
- **What:** Arcade car controllers: throttle, steering, braking, handbrake and grip, with an opt-in raycast suspension, plus a 2D top-down or side-view version.
- **Parts:**
  - 4.5.1 Arcade vehicle - `Components/VehicleController` - maxSpeed, reverseSpeed, acceleration, brakeForce, drag, turnSpeed, grip, handbrakeGrip, followCamera, Speed(), IsGrounded()
  - 4.5.2 Raycast suspension - `Components/VehicleController` - four down-rays at wheelBase x trackWidth, spring to rideHeight, chassis roll and pitch, maxTilt
  - 4.5.3 2D vehicle - `Components/VehicleController2D` - top-down (local +Y) or side view
- **Code:** `Components/VehicleController`, `Components/VehicleController2D`
- **API:** `VehicleController`, `VehicleController2D`, `Speed()`, `IsGrounded()`
- **Editor:** Add Component > Gameplay > Vehicle Controller (Car) / Vehicle Controller 2D
- **Docs:** `docs/vehicle_controller.md`
- **Tests:** `vehicle`
- **Status:** Works - vehicle test covers throttle, steering, grip and serialization; suspension is opt-in
- **Open:** no get-in/get-out or driver-seat mechanic found in VehicleController (grep); torque-based body dynamics are a roadmap item in the doc
- **Related:** 4.4, 6.2
- **Words:** vehicle, car, drift, handbrake, grip, suspension, ride height, VehicleController
- **Seen as:** the car skates sideways; it does not stay on slopes; it flips; the camera sits inside the roof
- **Ask me like:** "let the player get into the car"; "add engine sound pitch from Speed"

### 4.6 Third-person camera
- **What:** The over-the-shoulder orbit camera used by the third-person controller and the view toggle, and the simple chase camera for 2D and 3D scenes.
- **Parts:**
  - 4.6.1 Shared orbit placement - `Components/PlayerCamera` - PlaceThirdPersonOrbit: pivot, aim blend, look smoothing, spring arm and landing shake in one function so controller and toggle agree
  - 4.6.2 Spring arm and wall collision - `Components/ControllerFeel` - SpringArm sphere sweep against solid colliders (triggers ignored), snaps in, glides out
  - 4.6.3 Follow pivot, look smoothing, auto-recenter - `Components/ControllerFeel`, `Components/ThirdPersonController` - FollowPivot (head point, separate horizontal and vertical lag, snaps on teleports over 2 m), LookSmoother, autoRecenter with delay and speed
  - 4.6.4 Chase camera - `Components/CameraFollow` - eases toward a named target plus offset
- **Code:** `Components/PlayerCamera`, `Components/CameraFollow`, `Components/ControllerFeel`, `Components/ThirdPersonController`
- **API:** `OrbitPose`, `PlaceThirdPersonOrbit`, `SpringArm`, `FollowPivot`, `CameraFollow::targetName`
- **Editor:** Player Controller panel > Third person (orbit, aim, spring arm); Inspector for Camera Follow
- **Docs:** `docs/making_a_game.md`
- **Tests:** `player`, `view_ground`, `run_feel`
- **Status:** Works - player test checks the wall pull-in; view_ground checks the lens stays above ground through hitches; CameraFollow has no dedicated test I found
- **Open:** in flight in #554 (Grok): head and walk jitter are stated there as not fixed; no other open item recorded
- **Related:** 4.1, 4.8, 4.10
- **Words:** third-person camera, orbit, spring arm, camera collision, shoulder offset, CameraFollow, chase camera, recenter
- **Seen as:** the camera behind the player is jittery or clips through walls; it spins on a strafe; it pumps in and out at edges; look-down turns into a top-down view
- **Ask me like:** "make the orbit camera lag less vertically"; "camera clips through railings"

### 4.7 First-person camera and hands
- **What:** The first-person eye, the owner's own body and hands in view, and the hands panel. The owner's camera skips the head and neck but still draws shirt and legs; a copy of the hand brick is drawn in the lower right, and other players see the real arms.
- **Parts:**
  - 4.7.1 First-person eye feel - `Components/FirstPersonController`, `Components/ControllerFeel` - bob (up and down only), landing dip, FOV kick, zoom, lean camera roll, eye height by stance
  - 4.7.2 Own body and head hiding - `Components/FirstPersonHand`, `Components/Character` - FirstPersonHand flags the viewmodel (shirt, legs, hand copy), head and neck active but unflagged, eye kept clear of body parts
  - 4.7.3 First Person Hands component - `Components/FirstPersonHands` - Show When Empty, hold mode (one hand, other hand, both), per-hand position, rotation, scale, size, colour, sleeve; shared bob, sway, punch and hand field of view; Others View
  - 4.7.4 Hands window and preview - `editor/FpHandsWindow.*`, `Components/FpHandsPreview` - Open Preview renders the owner's camera and a front view of a throwaway character in Edit and Play
  - 4.7.5 Punch and run behaviour - `Components/Character` - punch swings the arm forward; a run eases the hand copy off the corner
- **Code:** `Components/FirstPersonHand`, `Components/FirstPersonHands`, `Components/FpHandsPreview`, `Components/FirstPersonController`, `Components/Crosshair`, `editor/FpHandsWindow.*`
- **API:** `FirstPersonHands`, `FpHandsSnap`, `Character::ApplyOwnerVisibility`, `Character::SetViewmodelBlend`, `Character::SetHeldHiddenFromOwner`
- **Editor:** First Person Hands component panel (Open Preview), "First Person Hands" window
- **Docs:** `docs/animation.md` (Holding objects: first-person hands and view switch), `docs/making_a_game.md`
- **Tests:** `fp_hand`, `fp_punch`, `view_ground`, `run_feel`
- **Status:** Works - fp_hand is the largest test in the area (about 1,000 lines); view switch and hands described in docs
- **Open:** old map: Blocky Character arms in first person; the hand is one block with no finger bones, finger joints are never posed (per docs); none of the first-person pose is sent over the network (per docs)
- **Related:** 4.1, 4.9, 5.9, 16.8
- **Words:** first person, hands, viewmodel, arms, FirstPersonHand, FirstPersonHands, head hiding, bob, sway, punch, hand FOV, sleeve
- **Seen as:** hands or arms missing or floating in first person; the head or neck shows in the lens; looking down shows the inside of the shirt; held item in the wrong place; other players see no arm movement
- **Ask me like:** "show empty hands in first person"; "hands are too low on screen while running"

### 4.8 Virtual cameras and dollies
- **What:** Shot-based cameras: virtual cameras describe where to stand and what to look at, a brain on the real camera blends to the highest-priority one, dolly paths (rails) carry them along a smooth curve, and the scene keeps an ordered camera stack.
- **Parts:**
  - 4.8.1 Virtual camera - `Components/VirtualCamera` - priority, Follow and LookAt by name, binding mode (World Space or Lock To Target), damping, dead zone, field of view, Dutch angle, confiner box, handheld shake, AddImpulse
  - 4.8.2 Cinemachine brain - `Components/CinemachineBrain` - on the real camera: picks the live vcam by priority, eases pose and FOV over blendTime (0 = cut), ease in/out
  - 4.8.3 Dolly path and cart - `Components/DollyPath` - Catmull-Rom waypoints (open or looped), DollyCart rides it; Tracked Dolly vcam
  - 4.8.4 Camera stack - `Components/Camera`, `Scene/EnsureCamera` - Scene::CameraStack sorted by depth, ClearPlan (Skybox, SolidColor, DepthOnly, DontClear), depth decides the main camera (#570), EnsureSceneCamera guarantees one camera
  - 4.8.5 Cutscene camera shots - `Components/SequencePlayer` (owned by 10.1) - the Sequence camera track drives the main camera and hands it back at the end
- **Code:** `Components/VirtualCamera`, `Components/CinemachineBrain`, `Components/DollyPath`, `Components/Camera`, `Scene/EnsureCamera`
- **API:** `VirtualCamera::priority`, `BindingMode`, `AddImpulse`, `CinemachineBrain::blendTime`, `LiveCamera()`, `DollyPath`, `DollyCart`, `Scene::CameraStack()`, `Camera::ClearPlan`
- **Editor:** Inspector panels for Virtual Camera, Cinemachine Brain, Dolly Path, Dolly Cart; Add Component menu "Virtual Camera"
- **Docs:** `docs/layers_tags_cameras.md` (Cameras), `docs/splines.md`, `docs/cutscenes.md`
- **Tests:** `cinemachine`, `layers_tags_cameras`, `scene_cameras`, `camera_cine`
- **Status:** Works - cinemachine, scene_cameras and layers_tags_cameras tests exist; the depth rule for the main camera landed in #570
- **Open:** the player and editor still draw only the main camera; looping their render path over `CameraStack()` with ClearPlan, viewport rect and culling mask is not done (per docs/layers_tags_cameras.md)
- **Related:** 4.10, 10.1, 2.7, 1.3
- **Words:** virtual camera, vcam, Cinemachine, brain, priority, dolly, track, rail, Follow, LookAt, confiner, Dutch, camera stack, depth, main camera, shot
- **Seen as:** camera cuts instead of blending; the wrong camera is live; two cameras fight for main; a scene has no camera and renders black; the camera leaves the level
- **Ask me like:** "blend between two shots with an ease curve"; "render two cameras on top of each other in Play"

### 4.9 Blocky Character and customization
- **What:** The built-in low-poly character with one height slider, colours, hair, clothing and accessories, bound to a 15-bone humanoid skeleton, with an optional editable part rig and the ability to bind any mesh to it.
- **Parts:**
  - 4.9.1 Proportions, colours and clothing - `Components/Character` - height, skin, shirt, pants, hair; hair styles, beards, shirt and leg styles, belt, jacket, gloves
  - 4.9.2 Accessories - `Components/Character` - hats (cap to crown), glasses, mask, scarf, shoulder pads, backpack, cape
  - 4.9.3 Part rig (Separate Into Parts) - `Components/Character` - BuildParts creates a real child "Rig" with one object per bone to select, recolour and attach to (`Components/Sockets`); RemoveParts
  - 4.9.4 Custom mesh and external rig - `Components/Character` - BindCustomMesh (auto-rig), driveExternal for an imported rig fed by HumanoidRetarget
  - 4.9.5 Blocky feel - `Components/Character` - takeoff dip, turn-in-place step, motion lean (tested), fidget while standing (per docs); gait maths are in 5.4
  - 4.9.6 Character panel and creator - `editor/main.cpp` - DrawCharacterAnim and the Character inspector; the planned Character creator is area 30
- **Code:** `Components/Character`, `Components/Sockets`, `editor/main.cpp`
- **API:** `Character` (`height`, `skin`, `hairStyle`, `hatStyle`, `BuildParts()`, `RemoveParts()`, `BindCustomMesh`, `separateParts`, `driveExternal`); GameObject > 3D > Character
- **Editor:** Character inspector, "Separate Into Parts (editable rig)", Unrig, Animation tab
- **Docs:** `docs/animation.md`
- **Tests:** `character_parts`, `char_editor_parts`, `character_feel`, `player_fixes`
- **Status:** Works - parts and feel tests exist; customization is a fixed list of styles and colours
- **Open:** old map: more customization options; no cloth, morph or secondary-motion for the blocky body (searched for cloth, jiggle, spring bone); creator UI is planned in 30.1
- **Related:** 3.7, 4.7, 5.2, 5.3, 30.1
- **Words:** Character, blocky character, Unturned style, customization, hair style, hat, backpack, cape, part rig, Separate Into Parts, Rig node, avatar
- **Seen as:** the part rig is duplicated after reload; colours not saved; arms stay when parts are separated; the body is hidden after a model swap
- **Ask me like:** "add a ponytail and a new hat style"; "recolour the shirt per player"

### 4.10 Camera component, shake and zoom
- **What:** The Camera component itself: perspective or orthographic projection, lens settings, trauma-based shake, smooth zoom, pixel-perfect 2D, screen-to-world conversions, culling mask and viewport rectangle.
- **Parts:**
  - 4.10.1 Projection and lens - `Components/Camera` - fieldOfView, orthographicSize, physicalCamera, lens shift, Dutch angle, cullingMask, viewport rect
  - 4.10.2 Shake and zoom - `Components/Camera` - Shake(strength) trauma squared noise with recovery, ZoomTo(target, seconds)
  - 4.10.3 Screen conversions - `Components/Camera` - WorldToScreen, WorldToViewportPoint, ScreenPointToRay, IsPointVisible
  - 4.10.4 Pixel-perfect 2D - `Components/Camera` - pixelPerfect and pixelsPerUnit snapping in RenderPosition
- **Code:** `Components/Camera`
- **API:** `Camera`, `Camera::Shake`, `ZoomTo`, `WorldToScreen`, `ScreenPointToRay`, `ViewMatrix`, `ProjectionMatrix`; scripts such as `look_at`, camera builtins; visual-script Camera Shake node
- **Editor:** Camera inspector; GameObject > Create Camera
- **Docs:** `docs/layers_tags_cameras.md`
- **Tests:** `camera_cine`, `script_camera`, `scene_cameras`
- **Status:** Works - camera_cine checks projection and visibility maths; script_camera covers script control
- **Open:** none recorded; GPU culling uses the camera frustum (2.8)
- **Related:** 4.8, 2.1, 2.8, 13.4
- **Words:** camera, field of view, orthographic, perspective, shake, trauma, zoom, pixel perfect, WorldToScreen, culling mask, viewport rect
- **Seen as:** the screen shakes too long or never; the 2D game looks blurry or jitters; world-to-screen is off; an object disappears when the camera turns
- **Ask me like:** "shake the camera on landing"; "zoom the 2D camera smoothly to a target"

### 4.11 Seats and sitting
- **What:** A place to sit: the nearest local player presses E to be moved onto the seat point, the Animator plays the sit-down, loop and stand-up clips, and moving or jumping stands up.
- **Parts:**
  - 4.11.1 Seat component - `Components/Seat` - seatHeight (0.45), sitPoint, facing, type (chair, ground, bench), exitPoint, useRadius, prompt, Claim and Release
  - 4.11.2 Sit actions and scripts - `Animation/HumanoidActions` - sit(), stand_up(), is_sitting(), interact(), seat_prompt(); clips in 5.12
- **Code:** `Components/Seat`, `Animation/HumanoidActions`
- **API:** `Seat`, scripts `sit()`, `sit("Chair")`, `stand_up()`, `is_sitting()`, `interact()`, `seat_prompt()`; visual scripts Sit, Stand Up, Is Sitting, Interact, Seat Prompt
- **Editor:** Seat inspector panel
- **Docs:** `docs/animation.md` (Actions: sitting)
- **Tests:** `ik_state_weight`
- **Status:** Works - per docs and the action framework test in ik_state_weight (sit then stand); I read Seat.hpp
- **Open:** there is no shared interact key (first-person lean-right is also E); death does not teleport to the exit point
- **Related:** 5.12, 4.1
- **Words:** seat, sit, chair, bench, stand up, interact, sit down, exit point
- **Seen as:** the player sits and also leans; the chair pose floats above the seat; cannot stand up
- **Ask me like:** "make a sofa that seats two"; "add an interact key setting"

### 4.12 Premade players
- **What:** The GameObject > Player menu: one-click players already wired with controller, camera, collider and animation, so a new scene is playable straight away.
- **Parts:**
  - 4.12.1 Player menu - `editor/main.cpp` - Third Person, Humanoid (foot IK), First Person, Third Person Shooter, Top Down (3D), Top Down Shooter (3D), 2.5D Platformer, Top Down (2D), Click To Move
- **Code:** `editor/main.cpp`, `Components/PlayerController`
- **API:** `PlayerController::Add`, `AttachCharacterModel`
- **Editor:** GameObject > Player submenu
- **Docs:** `docs/making_a_game.md`, `docs/shooter_and_25d_controllers.md`
- **Tests:** `player`, `make_playable`, `scene_cameras`
- **Status:** Works - player test checks that the shortcuts build fully wired players; the Mixamo items depend on the network
- **Open:** the two Mixamo samples rely on a third-party URL and an external curl process (possible fragility)
- **Related:** 3.7, 4.1, 15.2
- **Words:** premade player, Player menu, template, starter
- **Seen as:** the Mixamo menu item logs "could not download"; a new player has no camera; two players after adding a premade one
- **Ask me like:** "add a Vehicle driver to the Player menu"; "ship the sample Soldier in the repo instead of downloading"
