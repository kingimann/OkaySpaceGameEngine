// Property registrations of the controllers, vehicles, spawners, navigation and
// networking components (the gameplay half of the built-in table; the core types are in
// BuiltinProperties.cpp and the UI widgets in BuiltinPropertiesUI.cpp). Each type
// registers every field its scene record keeps, with the same value that record
// stores, so a value set through the registry survives Save and Load (the
// cross-check in tests/test_property_registry.cpp proves it for every property).
// Fields the scene file does not keep (runtime pointers, caches) are left out. See
// docs/properties.md.
#include "BuiltinPropertiesShared.hpp"

#include "okay/Components/CharacterController2D.hpp"
#include "okay/Components/CharacterController3D.hpp"
#include "okay/Components/ClickToMoveController.hpp"
#include "okay/Components/DayNightCycle.hpp"
#include "okay/Components/Destructible.hpp"
#include "okay/Components/Draggable.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Components/Flashlight.hpp"
#include "okay/Components/FollowTarget2D.hpp"
#include "okay/Components/FreeRoamController.hpp"
#include "okay/Components/Joint2D.hpp"
#include "okay/Components/Joint3D.hpp"
#include "okay/Components/MusicPlayer.hpp"
#include "okay/Components/NavMeshAgent.hpp"
#include "okay/Components/NavMeshLink.hpp"
#include "okay/Components/NavMeshModifier.hpp"
#include "okay/Components/NavMeshObstacle.hpp"
#include "okay/Components/NavMeshSurface.hpp"
#include "okay/Components/NetworkObject.hpp"
#include "okay/Components/NetworkPlayerSpawner.hpp"
#include "okay/Components/NetworkSync.hpp"
#include "okay/Components/NoCode.hpp"
#include "okay/Components/Platformer25DController.hpp"
#include "okay/Components/PlayerController.hpp"
#include "okay/Components/PlayerView.hpp"
#include "okay/Components/Projectile3D.hpp"
#include "okay/Components/Spawner.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Components/ThirdPersonShooterController.hpp"
#include "okay/Components/TopDownController.hpp"
#include "okay/Components/TopDownController2D.hpp"
#include "okay/Components/TopDownShooterController.hpp"
#include "okay/Components/VehicleController.hpp"
#include "okay/Components/VehicleController2D.hpp"
#include "okay/Components/Volume.hpp"
#include "okay/Components/Water.hpp"

namespace okay {
namespace detail {
namespace {

// ---- PlayerController: one component that owns the first and third person parts ----
// Changing viewMode adds and removes the parts (and so their settings), exactly as the
// Inspector's View setting does.

void RegisterPlayerController() {
    PropertyBuilder<PlayerController> b("PlayerController", "playercontroller", "Controllers");
    b.CustomEnum("viewMode", {"First", "Third", "Both"},
                 [](const PlayerController& p) { return static_cast<int>(p.viewMode()); },
                 [](PlayerController& p, int v) { p.SetViewMode(static_cast<PlayerController::ViewMode>(v)); return true; })
        .Tooltip("Which views the player has: first or third person only, or both with a toggle. A view that is off is removed together with its settings");
    b.Adder([](GameObject& go) -> Component* { return PlayerController::Add(&go); });
    b.Commit();
}

// ---- Controllers -----------------------------------------------------------

// PlayerView
void RegisterPlayerView() {
    PropertyBuilder<PlayerView> b("PlayerView", "playerview", "Controllers");
    b.AddBool("allowToggle", &PlayerView::allowToggle).Tooltip("V / gamepad switches view. Off: controller is unchanged")
     .AddEnum("defaultView", &PlayerView::defaultView, {"First", "Third"}).Tooltip("View a fresh play starts in")
     .AddKey("toggleKey", &PlayerView::toggleKey)
     .AddInt("toggleGamepadButton", &PlayerView::toggleGamepadButton).Range(-1, 31, 1).Tooltip("Back. -1 = none. (No separate stick-click id.)")
     .AddFloat("blendTime", &PlayerView::blendTime).Range(0, 1000, 0.1).Tooltip("Seconds for a full switch; a reverse starts from here")
     .AddEnum("switchEase", &PlayerView::switchEase, {"Linear", "Smooth", "In", "Out"}).Tooltip("Easing of the view switch")
     .AddBool("snapSwitch", &PlayerView::snapSwitch).Tooltip("True: the camera jumps, no blend")
     .AddVec3("eyeOffset", &PlayerView::eyeOffset).Range(-kBig, kBig, 0.1).Tooltip("Added at the eyes (local: +X right, +Y up, +Z back)")
     .AddFloat("thirdPersonDistance", &PlayerView::thirdPersonDistance).Range(-1, kBig, 0.05).Tooltip("Orbit distance while this view is driving; below 0 = the controller's own")
     .AddFloat("shoulderOffset", &PlayerView::shoulderOffset).Range(-kBig, kBig, 0.05).Tooltip("Over-the-shoulder shift while this view is driving");
    b.Commit();
}

// FirstPersonController
void RegisterFirstPersonController() {
    PropertyBuilder<FirstPersonController> b("FirstPersonController", "fpctrl", "Controllers");
    b.Section("Movement")
     .AddFloat("walkSpeed", &FirstPersonController::walkSpeed).Range(0, 10000, 0.1).Tooltip("Walking speed, units per second")
     .AddFloat("runSpeed", &FirstPersonController::runSpeed).Range(0, 10000, 0.1).Tooltip("Speed while the sprint key is held")
     .AddFloat("jumpForce", &FirstPersonController::jumpForce).Range(0, kBig, 0.1).Tooltip("Upward speed of a jump")
     .AddFloat("walkAcceleration", &FirstPersonController::walkAcceleration).Range(0, kBig, 1).Tooltip("Accel while walking (m/s^2). Run uses `acceleration`")
     .AddFloat("acceleration", &FirstPersonController::acceleration).Range(0, kBig, 1).Tooltip("Accel while running (m/s^2)")
     .AddFloat("deceleration", &FirstPersonController::deceleration).Range(0, kBig, 1).Tooltip("How fast the speed drops with no input (units/s^2, 0 = instant)")
     .AddFloat("steerRate", &FirstPersonController::steerRate).Range(0, 36000, 1).Tooltip("How fast a turn drops the old heading (m/s^2)")
     .AddFloat("airControl", &FirstPersonController::airControl).Range(0, 1000, 0.01).Tooltip("Control while airborne (0 = none, 1 = full)")
     .AddFloat("coyoteTime", &FirstPersonController::coyoteTime).Range(0, 3600, 0.05).Tooltip("Forgiving jump timing: coyote time (jump just after a ledge) + jump buffer (press just before landing)")
     .AddFloat("jumpBufferTime", &FirstPersonController::jumpBufferTime).Range(0, 3600, 0.05).Tooltip("A jump pressed this long before landing still counts, seconds")
     .AddInt("maxJumps", &FirstPersonController::maxJumps).Range(1, 16, 1).Tooltip("1 = single jump, 2 = double jump, etc")
     .AddKey("sprintKey", &FirstPersonController::sprintKey).Tooltip("Hold (or tap, if toggleRun) to run; 0 = disabled")
     .AddBool("toggleRun", &FirstPersonController::toggleRun).Tooltip("Tap sprint to keep running instead of holding")
     .AddBool("canJump", &FirstPersonController::canJump).Tooltip("Allow jumping")
     .AddBool("driveAnimation", &FirstPersonController::driveAnimation).Tooltip("Set a sibling Character's anim from movement")
     .AddBool("footIK", &FirstPersonController::footIK).Tooltip("Plant the Character's feet on the ground (foot IK)")
     .Section("Look")
     .AddFloat("mouseSensitivity", &FirstPersonController::mouseSensitivity).Range(0, kBig, 0.1).Tooltip("Degrees per pixel of mouse movement")
     .AddBool("invertY", &FirstPersonController::invertY).Tooltip("Invert vertical mouse look")
     .Section("Head bob")
     .AddBool("headBob", &FirstPersonController::headBob).Tooltip("Up-and-down view bob while walking")
     .AddFloat("bobAmount", &FirstPersonController::bobAmount).Range(0, 1000, 0.01).Tooltip("Bob height (world units)")
     .AddFloat("bobSpeed", &FirstPersonController::bobSpeed).Range(0, 10000, 0.1).Tooltip("Bob cadence (≈ footstep rate)")
     .AddFloat("bobSway", &FirstPersonController::bobSway).Range(0, 15, 0.1).Tooltip("Side-to-side camera roll while walking, in degrees, once per stride in step with the bob (0 = off). Fades out in the air and when you stop")
     .AddFloat("bobRunScale", &FirstPersonController::bobRunScale).Range(0, 1000, 0.01).Tooltip("Bob size at run speed (x the walking bob)")
     .AddFloat("bobBlend", &FirstPersonController::bobBlend).Range(0, 1000, 0.1).Tooltip("How fast the bob fades in / out as you start / stop")
     .AddFloat("bobGroundGrace", &FirstPersonController::bobGroundGrace).Range(0, 3600, 0.05).Tooltip("S the bob and landing dip still count you as grounded (stairs, slopes)")
     .AddFloat("eyeStepSmoothing", &FirstPersonController::eyeStepSmoothing).Range(0, 1000, 0.1).Tooltip("How fast the eye eases over steps (0 = none)")
     .Section("Stance")
     .AddKey("crouchKey", &FirstPersonController::crouchKey)
     .AddKey("proneKey", &FirstPersonController::proneKey)
     .AddBool("toggleStance", &FirstPersonController::toggleStance).Tooltip("Tap to toggle vs hold to hold the stance")
     .AddFloat("crouchSpeed", &FirstPersonController::crouchSpeed).Range(0, 10000, 0.1).Tooltip("Speed while crouched")
     .AddFloat("proneSpeed", &FirstPersonController::proneSpeed).Range(0, 10000, 0.1).Tooltip("Speed while prone")
     .AddBool("allowProne", &FirstPersonController::allowProne).Tooltip("False: no prone at all (key and gamepad)")
     .AddBool("resizeBody", &FirstPersonController::resizeBody).Tooltip("Crouch / prone shrink the collider (crawl under things; no standing into a ceiling)")
     .AddFloat("crouchHeight", &FirstPersonController::crouchHeight).Range(0, kBig, 0.05).Tooltip("Crouched body height, fraction of standing")
     .AddFloat("proneHeight", &FirstPersonController::proneHeight).Range(0, kBig, 0.05).Tooltip("Prone body height, fraction of standing")
     .AddInt("stanceGamepadButton", &FirstPersonController::stanceGamepadButton).Range(-1, 31, 1).Tooltip("Tap = crouch / stand, hold = prone (-1 = none; 1 = B)")
     .AddFloat("proneHoldTime", &FirstPersonController::proneHoldTime).Range(0, 3600, 0.05).Tooltip("Seconds the gamepad button is held to go prone")
     .AddFloat("standEyeHeight", &FirstPersonController::standEyeHeight).Range(0, kBig, 0.05).Tooltip("Child-camera local Y while standing")
     .AddFloat("crouchEyeHeight", &FirstPersonController::crouchEyeHeight).Range(0, kBig, 0.05).Tooltip("Minecraft-style: a modest dip, not a deep squat")
     .AddFloat("proneEyeHeight", &FirstPersonController::proneEyeHeight).Range(0, kBig, 0.05).Tooltip("Camera height while prone")
     .AddFloat("stanceLerp", &FirstPersonController::stanceLerp).Range(0, 1000, 0.1).Tooltip("How fast the eye follows the body down / up")
     .Section("Lean")
     .AddKey("leanLeftKey", &FirstPersonController::leanLeftKey)
     .AddKey("leanRightKey", &FirstPersonController::leanRightKey)
     .AddFloat("leanAngle", &FirstPersonController::leanAngle).Range(0, 360, 1).Tooltip("Camera roll while leaning (degrees)")
     .AddFloat("leanOffset", &FirstPersonController::leanOffset).Range(-kBig, kBig, 0.05).Tooltip("Sideways camera shift while leaning (units)")
     .AddFloat("leanSpeed", &FirstPersonController::leanSpeed).Range(0, 10000, 0.1).Tooltip("How fast the lean eases in/out")
     .Section("Feel")
     .AddFloat("lookSmoothing", &FirstPersonController::lookSmoothing).Range(0, 1000, 0.1).Tooltip("Look smoothing time (s): the view eases toward the look angles; 0 = raw")
     .AddFloat("sprintFovBoost", &FirstPersonController::sprintFovBoost).Range(0, 90, 0.5).Tooltip("FOV widens this much while sprinting (0 = off)")
     .AddFloat("fovLerp", &FirstPersonController::fovLerp).Range(0, 90, 0.5).Tooltip("How fast the FOV eases")
     .AddBool("zoomWithRightMouse", &FirstPersonController::zoomWithRightMouse).Tooltip("Hold right mouse to zoom / aim down sights")
     .AddFloat("zoomFov", &FirstPersonController::zoomFov).Range(1, 179, 0.5).Tooltip("Field of view while zoomed, degrees")
     .AddFloat("zoomSensitivity", &FirstPersonController::zoomSensitivity).Range(0, 1000, 0.01).Tooltip("Look sensitivity multiplier while zoomed")
     .AddFloat("landDip", &FirstPersonController::landDip).Range(0, kBig, 0.05).Tooltip("Max camera dip on a hard landing (world units)")
     .Section("Slide")
     .AddBool("canSlide", &FirstPersonController::canSlide).Tooltip("Slide when crouching while running")
     .AddFloat("slideSpeed", &FirstPersonController::slideSpeed).Range(0, 10000, 0.1).Tooltip("Burst speed at the start of a slide")
     .AddFloat("slideTime", &FirstPersonController::slideTime).Range(0, 3600, 0.05).Tooltip("Seconds until it settles into a crouch")
     .AddFloat("slideEyeHeight", &FirstPersonController::slideEyeHeight).Range(0, kBig, 0.05).Tooltip("Camera height while sliding")
     .Section("Traversal")
     .AddBool("canVault", &FirstPersonController::canVault).Tooltip("Vault over low obstacles")
     .AddBool("canMantle", &FirstPersonController::canMantle).Tooltip("Mantle up onto ledges in reach")
     .AddBool("canLedgeGrab", &FirstPersonController::canLedgeGrab).Tooltip("Grab and hang from ledges")
     .Section("Dolphin dive")
     .AddBool("diveOnProneWhileSprinting", &FirstPersonController::diveOnProneWhileSprinting).Tooltip("Pressing prone while sprinting dives instead")
     .AddFloat("diveHop", &FirstPersonController::diveHop).Range(0, kBig, 0.05).Tooltip("Metres of upward launch")
     .AddFloat("diveSlideTime", &FirstPersonController::diveSlideTime).Range(0, 3600, 0.05).Tooltip("Glide after landing, then prone")
     .Section("Hooks")
     .AddBool("driveStateMachine", &FirstPersonController::driveStateMachine).Tooltip("Feed an AnimStateMachine speed/grounded/crouch/... + jump/land triggers")
     .AddBool("ikLook", &FirstPersonController::ikLook).Tooltip("A Full Body IK on the body looks where the camera aims");
    b.Section("Dodge")
     .AddBool("canDodge", &FirstPersonController::dodge, &DodgeMove::canDodge).Tooltip("Key dodge. Off by default so existing games do not change")
     .AddKey("dodgeKey", &FirstPersonController::dodge, &DodgeMove::key).Tooltip("Key code of the dodge (0 = no key; a script can still call dodge())")
     .AddFloat("dodgeImpulse", &FirstPersonController::dodge, &DodgeMove::impulse).Range(0, 1000, 0.1).Tooltip("Horizontal speed of the burst")
     .AddFloat("dodgeDuration", &FirstPersonController::dodge, &DodgeMove::duration).Range(0, 60, 0.05)
     .AddFloat("dodgeCooldown", &FirstPersonController::dodge, &DodgeMove::cooldown).Range(0, 3600, 0.05)
     .AddFloat("dodgeStamina", &FirstPersonController::dodge, &DodgeMove::stamina).Range(0, 100000, 0.5).Tooltip("Spent when a Stamina stat is on this body")
     .AddBool("dodgeIFrames", &FirstPersonController::dodge, &DodgeMove::iFrames).Tooltip("On: no damage during a dodge");
    b.Commit();
}

// ThirdPersonController
void RegisterThirdPersonController() {
    PropertyBuilder<ThirdPersonController> b("ThirdPersonController", "tpctrl", "Controllers");
    b.Section("Movement")
     .AddFloat("walkSpeed", &ThirdPersonController::walkSpeed).Range(0, 10000, 0.1).Tooltip("Walking speed, units per second")
     .AddFloat("runSpeed", &ThirdPersonController::runSpeed).Range(0, 10000, 0.1).Tooltip("Speed while the sprint key is held")
     .AddFloat("jumpForce", &ThirdPersonController::jumpForce).Range(0, kBig, 0.1).Tooltip("Upward speed of a jump")
     .AddKey("sprintKey", &ThirdPersonController::sprintKey).Tooltip("Hold (or tap, if toggleRun) to run; 0 = disabled")
     .AddBool("toggleRun", &ThirdPersonController::toggleRun).Tooltip("Tap sprint to keep running instead of holding")
     .AddBool("canJump", &ThirdPersonController::canJump).Tooltip("Allow jumping")
     .AddBool("driveAnimation", &ThirdPersonController::driveAnimation).Tooltip("Drive a sibling Character's idle, walk and run from the movement")
     .AddBool("footIK", &ThirdPersonController::footIK).Tooltip("Plant the Character's feet on the ground")
     .AddFloat("turnSpeed", &ThirdPersonController::turnSpeed).Range(0, 36000, 1).Tooltip("How fast the body turns toward movement (1/s)")
     .AddFloat("walkAcceleration", &ThirdPersonController::walkAcceleration).Range(0, kBig, 1).Tooltip("Accel while walking (m/s^2). Run uses `acceleration`")
     .AddFloat("acceleration", &ThirdPersonController::acceleration).Range(0, kBig, 1).Tooltip("Accel while running (m/s^2)")
     .AddFloat("deceleration", &ThirdPersonController::deceleration).Range(0, kBig, 1).Tooltip("How fast the speed drops with no input (units/s^2, 0 = instant)")
     .AddFloat("steerRate", &ThirdPersonController::steerRate).Range(0, 36000, 1).Tooltip("How fast a turn drops the old heading (m/s^2)")
     .AddFloat("airControl", &ThirdPersonController::airControl).Range(0, 1000, 0.01).Tooltip("Authority multiplier while airborne (0..1)")
     .AddFloat("coyoteTime", &ThirdPersonController::coyoteTime).Range(0, 3600, 0.05).Tooltip("A jump still works this long after leaving a ledge, seconds")
     .AddFloat("jumpBufferTime", &ThirdPersonController::jumpBufferTime).Range(0, 3600, 0.05).Tooltip("A jump pressed this long before landing still counts, seconds")
     .AddInt("maxJumps", &ThirdPersonController::maxJumps).Range(1, 16, 1).Tooltip("1 = single jump, 2 = double jump, etc")
     .Section("Stance")
     .AddKey("crouchKey", &ThirdPersonController::crouchKey)
     .AddKey("proneKey", &ThirdPersonController::proneKey)
     .AddBool("toggleStance", &ThirdPersonController::toggleStance).Tooltip("Tap to toggle vs hold")
     .AddFloat("crouchSpeed", &ThirdPersonController::crouchSpeed).Range(0, 10000, 0.1).Tooltip("Speed while crouched")
     .AddFloat("proneSpeed", &ThirdPersonController::proneSpeed).Range(0, 10000, 0.1).Tooltip("Speed while prone")
     .AddBool("allowProne", &ThirdPersonController::allowProne).Tooltip("False: no prone at all (key and gamepad)")
     .AddBool("resizeBody", &ThirdPersonController::resizeBody).Tooltip("Crouch / prone shrink the collider (crawl under things; no standing into a ceiling)")
     .AddFloat("crouchHeight", &ThirdPersonController::crouchHeight).Range(0, kBig, 0.05).Tooltip("Crouched body height, fraction of standing")
     .AddFloat("proneHeight", &ThirdPersonController::proneHeight).Range(0, kBig, 0.05).Tooltip("Prone body height, fraction of standing")
     .AddInt("stanceGamepadButton", &ThirdPersonController::stanceGamepadButton).Range(-1, 31, 1).Tooltip("Tap = crouch / stand, hold = prone (-1 = none; 1 = B)")
     .AddFloat("proneHoldTime", &ThirdPersonController::proneHoldTime).Range(0, 3600, 0.05).Tooltip("Seconds the gamepad button is held to go prone")
     .AddFloat("crouchHeightDrop", &ThirdPersonController::crouchHeightDrop).Range(0, kBig, 0.05).Tooltip("Minecraft-style modest dip when crouched")
     .AddFloat("proneHeightDrop", &ThirdPersonController::proneHeightDrop).Range(0, kBig, 0.05).Tooltip("...and when prone")
     .AddFloat("stanceLerp", &ThirdPersonController::stanceLerp).Range(0, 1000, 0.1).Tooltip("How fast the camera follows the body down / up")
     .Section("Lean")
     .AddKey("leanLeftKey", &ThirdPersonController::leanLeftKey)
     .AddKey("leanRightKey", &ThirdPersonController::leanRightKey)
     .AddFloat("leanAngle", &ThirdPersonController::leanAngle).Range(0, 360, 1).Tooltip("Camera roll while leaning (degrees)")
     .AddFloat("leanOffset", &ThirdPersonController::leanOffset).Range(-kBig, kBig, 0.05).Tooltip("Sideways camera shift while leaning (units)")
     .AddFloat("leanSpeed", &ThirdPersonController::leanSpeed).Range(0, 10000, 0.1).Tooltip("How fast the lean eases in/out")
     .Section("Camera orbit")
     .AddFloat("mouseSensitivity", &ThirdPersonController::mouseSensitivity).Range(0, kBig, 0.1).Tooltip("Degrees per pixel")
     .AddBool("invertY", &ThirdPersonController::invertY).Tooltip("Invert vertical mouse look")
     .AddBool("invertX", &ThirdPersonController::invertX).Tooltip("Invert horizontal mouse look")
     .AddFloat("distance", &ThirdPersonController::distance).Range(0, kBig, 0.05).Tooltip("Camera distance from the player")
     .AddFloat("minDistance", &ThirdPersonController::minDistance).Range(0, kBig, 0.05).Tooltip("Closest the camera may come")
     .AddFloat("maxDistance", &ThirdPersonController::maxDistance).Range(0, kBig, 0.05).Tooltip("Farthest the camera may go")
     .AddFloat("zoomSpeed", &ThirdPersonController::zoomSpeed).Range(0, 10000, 0.1).Tooltip("Wheel zoom step")
     .AddFloat("cameraHeight", &ThirdPersonController::cameraHeight).Range(0, kBig, 0.05).Tooltip("Look target height above the player origin")
     .AddFloat("minPitch", &ThirdPersonController::minPitch).Range(-90, 90, 1).Tooltip("Lowest look angle, degrees")
     .AddFloat("maxPitch", &ThirdPersonController::maxPitch).Range(-90, 90, 1).Tooltip("Highest look angle, degrees")
     .AddFloat("shoulderOffset", &ThirdPersonController::shoulderOffset).Range(-kBig, kBig, 0.05).Tooltip("Lateral camera offset (over-the-shoulder)")
     .AddFloat("pivotDampingXZ", &ThirdPersonController::pivotDampingXZ).Range(0, 1000, 0.1).Tooltip("Camera pivot smoothing on the ground plane (0 = rigid)")
     .AddFloat("pivotDampingY", &ThirdPersonController::pivotDampingY).Range(0, 1000, 0.1).Tooltip("Camera pivot smoothing in height (0 = rigid)")
     .AddFloat("maxPivotLag", &ThirdPersonController::maxPivotLag).Range(0, kBig, 0.05).Tooltip("The pivot never trails the player by more (m); 0 = no limit")
     .AddFloat("orbitSmoothing", &ThirdPersonController::orbitSmoothing).Range(0, 1000, 0.1).Tooltip("Smoothing of the orbit camera (0 = rigid)")
     .AddBool("cameraCollision", &ThirdPersonController::cameraCollision).Tooltip("Pull the camera in when something is in the way")
     .AddFloat("cameraCollisionSkin", &ThirdPersonController::cameraCollisionSkin).Range(0, kBig, 0.05).Tooltip("Gap kept between the camera and an obstacle")
     .AddFloat("cameraCollisionRadius", &ThirdPersonController::cameraCollisionRadius).Range(0, kBig, 0.05).Tooltip("Radius of the camera probe used to detect obstacles")
     .AddInt("cameraCollisionMask", &ThirdPersonController::cameraCollisionMask).Tooltip("Collision layers the camera avoids (bit n = layer n)")
     .AddFloat("cameraCollisionPullIn", &ThirdPersonController::cameraCollisionPullIn).Range(0, 1000, 0.1).Tooltip("How fast a hair-closer obstacle eases the camera in")
     .AddFloat("cameraCollisionRecover", &ThirdPersonController::cameraCollisionRecover).Range(0, 1000, 0.1).Tooltip("How fast the camera returns after an obstacle moves away")
     .AddEnum("faceMode", &ThirdPersonController::faceMode, {"Movement", "Camera"}).Tooltip("What the body turns toward")
     .Section("Feel")
     .AddFloat("lookSmoothing", &ThirdPersonController::lookSmoothing).Range(0, 1000, 0.1).Tooltip("Look smoothing time (s), when orbitSmoothing is 0; 0 = raw")
     .AddFloat("sprintFovBoost", &ThirdPersonController::sprintFovBoost).Range(0, 90, 0.5).Tooltip("FOV widens this much while sprinting (0 = off)")
     .AddFloat("fovLerp", &ThirdPersonController::fovLerp).Range(0, 90, 0.5).Tooltip("How fast the field of view eases")
     .AddBool("aimWithRightMouse", &ThirdPersonController::aimWithRightMouse).Tooltip("Hold the right mouse button to aim")
     .AddFloat("aimDistance", &ThirdPersonController::aimDistance).Range(0, kBig, 0.05).Tooltip("Camera distance while aiming")
     .AddFloat("aimShoulder", &ThirdPersonController::aimShoulder).Range(-kBig, kBig, 0.05).Tooltip("Over-the-shoulder shift while aiming")
     .AddFloat("aimFov", &ThirdPersonController::aimFov).Range(1, 179, 0.5).Tooltip("Field of view while aiming, degrees")
     .AddFloat("aimSensitivity", &ThirdPersonController::aimSensitivity).Range(0, kBig, 0.1).Tooltip("Look sensitivity multiplier while aiming")
     .AddFloat("aimLerp", &ThirdPersonController::aimLerp).Range(0, 1000, 0.1).Tooltip("How fast the aim camera blends in")
     .AddBool("autoRecenter", &ThirdPersonController::autoRecenter).Tooltip("Swing the camera back behind the player when idle")
     .AddFloat("recenterDelay", &ThirdPersonController::recenterDelay).Range(0, 3600, 0.05).Tooltip("Seconds without camera input before it recentres")
     .AddFloat("recenterSpeed", &ThirdPersonController::recenterSpeed).Range(0, 10000, 0.1).Tooltip("How fast the camera swings back behind the player")
     .AddFloat("landShake", &ThirdPersonController::landShake).Range(0, 1000, 0.01).Tooltip("Camera bump on hard landings (world units, max)")
     .AddFloat("strideLength", &ThirdPersonController::strideLength).Range(0, kBig, 0.05).Tooltip("Distance per footstep event at walk speed")
     .Section("Slide")
     .AddBool("canSlide", &ThirdPersonController::canSlide).Tooltip("Slide when crouching while running")
     .AddFloat("slideSpeed", &ThirdPersonController::slideSpeed).Range(0, 10000, 0.1).Tooltip("Burst speed at the start of a slide")
     .AddFloat("slideTime", &ThirdPersonController::slideTime).Range(0, 3600, 0.05).Tooltip("Seconds a slide lasts")
     .Section("Traversal")
     .AddBool("canVault", &ThirdPersonController::canVault).Tooltip("Vault over low obstacles")
     .AddBool("canMantle", &ThirdPersonController::canMantle).Tooltip("Mantle up onto ledges in reach")
     .AddBool("canLedgeGrab", &ThirdPersonController::canLedgeGrab).Tooltip("Grab and hang from ledges")
     .Section("Dolphin dive")
     .AddBool("diveOnProneWhileSprinting", &ThirdPersonController::diveOnProneWhileSprinting).Tooltip("Pressing prone while sprinting dives instead")
     .AddFloat("diveHop", &ThirdPersonController::diveHop).Range(0, kBig, 0.05).Tooltip("Height of the upward launch of a dive")
     .AddFloat("diveSlideTime", &ThirdPersonController::diveSlideTime).Range(0, 3600, 0.05).Tooltip("Seconds of glide after a dive lands")
     .Section("Hooks")
     .AddBool("driveStateMachine", &ThirdPersonController::driveStateMachine).Tooltip("Feed an AnimStateMachine speed/grounded/crouch/aim/... + jump/land triggers")
     .AddBool("ikLook", &ThirdPersonController::ikLook).Tooltip("A Full Body IK on the body looks where the camera aims");
    b.Section("Dodge")
     .AddBool("canDodge", &ThirdPersonController::dodge, &DodgeMove::canDodge).Tooltip("Key dodge. Off by default so existing games do not change")
     .AddKey("dodgeKey", &ThirdPersonController::dodge, &DodgeMove::key).Tooltip("Key code of the dodge (0 = no key; a script can still call dodge())")
     .AddFloat("dodgeImpulse", &ThirdPersonController::dodge, &DodgeMove::impulse).Range(0, 1000, 0.1).Tooltip("Horizontal speed of the burst")
     .AddFloat("dodgeDuration", &ThirdPersonController::dodge, &DodgeMove::duration).Range(0, 60, 0.05)
     .AddFloat("dodgeCooldown", &ThirdPersonController::dodge, &DodgeMove::cooldown).Range(0, 3600, 0.05)
     .AddFloat("dodgeStamina", &ThirdPersonController::dodge, &DodgeMove::stamina).Range(0, 100000, 0.5).Tooltip("Spent when a Stamina stat is on this body")
     .AddBool("dodgeIFrames", &ThirdPersonController::dodge, &DodgeMove::iFrames).Tooltip("On: no damage during a dodge");
    b.Commit();
}

// CharacterController2D
void RegisterCharacterController2D() {
    PropertyBuilder<CharacterController2D> b("CharacterController2D", "charctrl2d", "Controllers");
    b.AddEnum("mode", &CharacterController2D::mode, {"TopDown", "Platformer"}).Tooltip("Movement style")
     .Section("Movement")
     .AddFloat("speed", &CharacterController2D::speed).Range(0, 10000, 0.1).Tooltip("Walk / move speed")
     .AddFloat("runSpeed", &CharacterController2D::runSpeed).Range(0, 10000, 0.1).Tooltip("Speed while the sprint key is held")
     .AddKey("sprintKey", &CharacterController2D::sprintKey).Tooltip("Hold to run (0 = disabled)")
     .AddFloat("acceleration", &CharacterController2D::acceleration).Range(0, kBig, 1).Tooltip("Units/s^2 toward the target speed (0 = instant)")
     .AddFloat("deceleration", &CharacterController2D::deceleration).Range(0, kBig, 1).Tooltip("Units/s^2 back toward 0 with no input (0 = instant)")
     .AddBool("normalizeDiagonal", &CharacterController2D::normalizeDiagonal).Tooltip("Top-down: diagonals aren't faster")
     .AddBool("useGamepad", &CharacterController2D::useGamepad).Tooltip("Left stick also drives movement")
     .AddBool("flipSprite", &CharacterController2D::flipSprite).Tooltip("Flip a sibling SpriteRenderer to face travel")
     .Section("Platformer")
     .AddFloat("jumpForce", &CharacterController2D::jumpForce).Range(0, kBig, 0.1).Tooltip("Upward velocity of a jump")
     .AddInt("maxJumps", &CharacterController2D::maxJumps).Range(1, 16, 1).Tooltip("2 = double jump, etc. (ground counts as one)")
     .AddBool("variableJump", &CharacterController2D::variableJump).Tooltip("Release the jump key early to cut it short")
     .AddFloat("jumpCutMultiplier", &CharacterController2D::jumpCutMultiplier).Range(0, 1000, 0.01).Tooltip("How much upward speed remains on an early release")
     .AddFloat("coyoteTime", &CharacterController2D::coyoteTime).Range(0, 3600, 0.05).Tooltip("Can still jump this long after leaving a ledge (s)")
     .AddFloat("jumpBuffer", &CharacterController2D::jumpBuffer).Range(0, 3600, 0.05).Tooltip("A press this long before landing still jumps (s)")
     .AddFloat("airControl", &CharacterController2D::airControl).Range(0, 1000, 0.01).Tooltip("0..1 horizontal control while airborne")
     .AddFloat("maxFallSpeed", &CharacterController2D::maxFallSpeed).Range(0, 10000, 0.1).Tooltip("Clamp downward speed (0 = no clamp)")
     .AddFloat("extraFallGravity", &CharacterController2D::extraFallGravity).Range(-1000, 1000, 0.1).Tooltip("Extra downward accel while falling (snappier arc)")
     .Section("Wall slide + wall jump")
     .AddBool("wallJump", &CharacterController2D::wallJump).Tooltip("Enable: slow-slide down walls + jump off them")
     .AddFloat("wallSlideSpeed", &CharacterController2D::wallSlideSpeed).Range(0, 10000, 0.1).Tooltip("Max downward speed while sliding on a wall")
     .AddFloat("wallJumpForce", &CharacterController2D::wallJumpForce).Range(0, kBig, 0.1).Tooltip("Upward speed of a wall jump")
     .AddFloat("wallJumpPush", &CharacterController2D::wallJumpPush).Range(0, 10000, 0.1).Tooltip("Horizontal push away from the wall");
    b.Commit();
}

// CharacterController3D
void RegisterCharacterController3D() {
    PropertyBuilder<CharacterController3D> b("CharacterController3D", "charctrl3d", "Controllers");
    b.AddFloat("speed", &CharacterController3D::speed).Range(0, 10000, 0.1).Tooltip("Walk speed on the ground plane")
     .AddFloat("runSpeed", &CharacterController3D::runSpeed).Range(0, 10000, 0.1).Tooltip("Speed while the sprint key is held")
     .AddKey("sprintKey", &CharacterController3D::sprintKey).Tooltip("Hold to run (0 = disabled)")
     .AddFloat("jumpForce", &CharacterController3D::jumpForce).Range(0, kBig, 0.1).Tooltip("Upward velocity on jump")
     .AddBool("canJump", &CharacterController3D::canJump).Tooltip("Platformer-style jump on space")
     .AddFloat("acceleration", &CharacterController3D::acceleration).Range(0, kBig, 1).Tooltip("How fast velocity ramps toward the target (units/s^2)")
     .AddFloat("deceleration", &CharacterController3D::deceleration).Range(0, kBig, 1).Tooltip("How fast it ramps down when stopping")
     .AddFloat("airControl", &CharacterController3D::airControl).Range(0, 1000, 0.01).Tooltip("Accel multiplier while airborne (0..1)")
     .AddBool("driveAnimation", &CharacterController3D::driveAnimation).Tooltip("Set a Character's walk/run/idle anim from movement")
     .AddBool("footIK", &CharacterController3D::footIK).Tooltip("Plant the Character's feet on the ground");
    b.Commit();
}

// TopDownController
void RegisterTopDownController() {
    PropertyBuilder<TopDownController> b("TopDownController", "tdctrl", "Controllers");
    b.Section("Movement")
     .AddFloat("walkSpeed", &TopDownController::walkSpeed).Range(0, 10000, 0.1).Tooltip("Walking speed, units per second")
     .AddFloat("runSpeed", &TopDownController::runSpeed).Range(0, 10000, 0.1).Tooltip("Speed while the sprint key is held")
     .AddKey("sprintKey", &TopDownController::sprintKey).Tooltip("Hold to run (0 = disabled)")
     .AddBool("driveAnimation", &TopDownController::driveAnimation).Tooltip("Drive a sibling Character's idle, walk and run from the movement")
     .AddBool("footIK", &TopDownController::footIK).Tooltip("Plant the Character's feet on the ground")
     .AddBool("rotateToFace", &TopDownController::rotateToFace).Tooltip("Turn the body toward travel")
     .AddFloat("turnSpeed", &TopDownController::turnSpeed).Range(0, 36000, 1).Tooltip("How fast the body turns, degrees per second (0 = snap)")
     .AddFloat("acceleration", &TopDownController::acceleration).Range(0, kBig, 1).Tooltip("Momentum: ramp horizontal velocity toward the target (units/s^2)")
     .AddFloat("deceleration", &TopDownController::deceleration).Range(0, kBig, 1).Tooltip("How fast the speed drops with no input (units/s^2, 0 = instant)")
     .AddBool("cameraRelative", &TopDownController::cameraRelative).Tooltip("Movement axes: world-aligned (W = +world Z forward) or relative to the camera yaw")
     .Section("Camera")
     .AddFloat("cameraDistance", &TopDownController::cameraDistance).Range(0, kBig, 0.05).Tooltip("How far the camera sits from the player")
     .AddFloat("cameraPitch", &TopDownController::cameraPitch).Range(-90, 90, 1).Tooltip("Downward tilt (deg); higher = more overhead")
     .AddFloat("cameraYaw", &TopDownController::cameraYaw).Range(-360, 360, 1).Tooltip("Fixed orbit yaw (deg)")
     .AddFloat("lookHeight", &TopDownController::lookHeight).Range(0, kBig, 0.05).Tooltip("Look target height above the player origin")
     .AddFloat("cameraDamping", &TopDownController::cameraDamping).Range(0, 1000, 0.1).Tooltip("0 = instant follow; higher = snappier");
    b.Commit();
}

// TopDownController2D
void RegisterTopDownController2D() {
    PropertyBuilder<TopDownController2D> b("TopDownController2D", "tdctrl2d", "Controllers");
    b.Section("Movement")
     .AddFloat("speed", &TopDownController2D::speed).Range(0, 10000, 0.1).Tooltip("Walk speed")
     .AddFloat("runSpeed", &TopDownController2D::runSpeed).Range(0, 10000, 0.1).Tooltip("Speed while the sprint key is held")
     .AddKey("sprintKey", &TopDownController2D::sprintKey).Tooltip("Hold to run (0 = disabled)")
     .AddBool("normalizeDiagonal", &TopDownController2D::normalizeDiagonal).Tooltip("W+D isn't faster than W alone")
     .AddBool("useGamepad", &TopDownController2D::useGamepad).Tooltip("Left stick overrides the keys when pushed")
     .AddFloat("acceleration", &TopDownController2D::acceleration).Range(0, kBig, 1).Tooltip("Momentum: units/s^2 toward the target speed (0 = instant, arcade-snappy)")
     .AddFloat("deceleration", &TopDownController2D::deceleration).Range(0, kBig, 1).Tooltip("How fast the speed drops with no input (units/s^2, 0 = instant)")
     .Section("Dash")
     .AddKey("dashKey", &TopDownController2D::dashKey).Tooltip("Tap to dash (0 = disabled)")
     .AddFloat("dashSpeed", &TopDownController2D::dashSpeed).Range(0, 10000, 0.1).Tooltip("Speed during a dash")
     .AddFloat("dashDuration", &TopDownController2D::dashDuration).Range(0, 3600, 0.05).Tooltip("Seconds")
     .AddFloat("dashCooldown", &TopDownController2D::dashCooldown).Range(0, 3600, 0.05).Tooltip("Seconds between dashes")
     .Section("Facing")
     .AddEnum("faceMode", &TopDownController2D::faceMode, {"None", "Move", "Mouse"}).Tooltip("What the body turns toward")
     .AddEnum("spriteForward", &TopDownController2D::spriteForward, {"Up", "Right", "Down", "Left"}).Tooltip("Which way the sprite art points at rotation 0")
     .AddFloat("turnSpeed", &TopDownController2D::turnSpeed).Range(0, 36000, 1).Tooltip("Deg/s of turning (0 = snap instantly)")
     .Section("Animation")
     .AddBool("driveAnimation", &TopDownController2D::driveAnimation).Tooltip("Set a Character walk/run/idle from movement")
     .Section("World limits")
     .AddBool("clampBounds", &TopDownController2D::clampBounds).Tooltip("Keep the player inside [boundsMin, boundsMax]")
     .AddVec2("boundsMin", &TopDownController2D::boundsMin).Range(-kBig, kBig, 0.1).Tooltip("Lower-left corner of the allowed area")
     .AddVec2("boundsMax", &TopDownController2D::boundsMax).Range(-kBig, kBig, 0.1).Tooltip("Upper-right corner of the allowed area")
     .AddBool("screenWrap", &TopDownController2D::screenWrap).Tooltip("Wrap around the camera view edges instead")
     .Section("Combat")
     .AddKey("fireKey", &TopDownController2D::fireKey).Tooltip("Hold to shoot (0 = disabled)")
     .AddInt("fireButton", &TopDownController2D::fireButton).Range(-1, 4, 1).Tooltip("Mouse button to shoot (-1 = off; 0 = left)")
     .AddString("projectile", &TopDownController2D::projectile).Tooltip("Prefab spawned as a bullet (give it a Rigidbody2D + Lifetime)")
     .AddFloat("projectileSpeed", &TopDownController2D::projectileSpeed).Range(0, 10000, 0.1).Tooltip("Speed of a projectile, units per second")
     .AddFloat("fireRate", &TopDownController2D::fireRate).Range(0, 10000, 0.1).Tooltip("Shots per second")
     .AddFloat("knockbackTime", &TopDownController2D::knockbackTime).Range(0, 3600, 0.05).Tooltip("Control lockout after a Knockback()");
    b.Commit();
}

// FreeRoamController
void RegisterFreeRoamController() {
    PropertyBuilder<FreeRoamController> b("FreeRoamController", "frctrl", "Controllers");
    b.AddFloat("moveSpeed", &FreeRoamController::moveSpeed).Range(0, 10000, 0.1).Tooltip("Units/sec")
     .AddFloat("boostMultiplier", &FreeRoamController::boostMultiplier).Range(0, 1000, 0.01).Tooltip("Hold sprint to fly faster")
     .AddKey("sprintKey", &FreeRoamController::sprintKey)
     .AddKey("upKey", &FreeRoamController::upKey).Tooltip("Rise (Space)")
     .AddKey("downKey", &FreeRoamController::downKey).Tooltip("Descend ('c'; Ctrl also descends)")
     .AddFloat("mouseSensitivity", &FreeRoamController::mouseSensitivity).Range(0, kBig, 0.1).Tooltip("Degrees per pixel")
     .AddBool("invertY", &FreeRoamController::invertY).Tooltip("Invert vertical mouse look")
     .AddBool("lockCursor", &FreeRoamController::lockCursor).Tooltip("Capture the cursor and always look")
     .AddBool("lookRequiresRightMouse", &FreeRoamController::lookRequiresRightMouse).Tooltip("Otherwise hold RMB to look")
     .AddFloat("minPitch", &FreeRoamController::minPitch).Range(-90, 90, 1).Tooltip("Lowest look angle, degrees")
     .AddFloat("maxPitch", &FreeRoamController::maxPitch).Range(-90, 90, 1).Tooltip("Highest look angle, degrees")
     .AddFloat("acceleration", &FreeRoamController::acceleration).Range(0, kBig, 1).Tooltip("Smoothing toward the target velocity (0 = instant)")
     .AddFloat("yaw", &FreeRoamController::yaw).Range(-360, 360, 1).Tooltip("Look angles (degrees)")
     .AddFloat("pitch", &FreeRoamController::pitch).Range(-90, 90, 1).Tooltip("Look angles (degrees)");
    b.Commit();
}

// ClickToMoveController
void RegisterClickToMoveController() {
    PropertyBuilder<ClickToMoveController> b("ClickToMoveController", "ctmctrl", "Controllers");
    b.AddFloat("walkSpeed", &ClickToMoveController::walkSpeed).Range(0, 10000, 0.1).Tooltip("Walking speed, units per second")
     .AddFloat("runSpeed", &ClickToMoveController::runSpeed).Range(0, 10000, 0.1).Tooltip("Speed while the sprint key is held")
     .AddKey("runKey", &ClickToMoveController::runKey).Tooltip("Hold to run (0 = disabled)")
     .AddFloat("stopDistance", &ClickToMoveController::stopDistance).Range(0, kBig, 0.05).Tooltip("How close counts as \"arrived\"")
     .AddFloat("arriveRadius", &ClickToMoveController::arriveRadius).Range(0, kBig, 0.05).Tooltip("Ease to a stop over the last this-many metres")
     .AddFloat("turnSpeed", &ClickToMoveController::turnSpeed).Range(0, 36000, 1).Tooltip("How fast the body turns toward travel")
     .AddInt("mouseButton", &ClickToMoveController::mouseButton).Range(0, 4, 1).Tooltip("Which button sets the destination (0=left)")
     .AddBool("holdToMove", &ClickToMoveController::holdToMove).Tooltip("Hold the button to keep retargeting (else single clicks)")
     .AddBool("driveAnimation", &ClickToMoveController::driveAnimation).Tooltip("Animate a sibling Character from movement")
     .AddBool("footIK", &ClickToMoveController::footIK).Tooltip("Plant the Character's feet on the ground")
     .AddBool("showCursor", &ClickToMoveController::showCursor).Tooltip("Keep the mouse pointer visible (you click to move!)")
     .AddFloat("groundY", &ClickToMoveController::groundY).Range(-kBig, kBig, 0.05).Tooltip("Ground plane height when usePlayerHeight is off")
     .AddBool("usePlayerHeight", &ClickToMoveController::usePlayerHeight).Tooltip("Pick on the plane at the player's current Y")
     .AddBool("usePathfinding", &ClickToMoveController::usePathfinding).Tooltip("Route around obstacles with the scene navigation")
     .AddFloat("repathInterval", &ClickToMoveController::repathInterval).Range(0, 3600, 0.05).Tooltip("Seconds between path re-plans")
     .Section("Follow camera")
     .AddBool("followCamera", &ClickToMoveController::followCamera).Tooltip("Position the main Camera each frame")
     .AddFloat("cameraHeight", &ClickToMoveController::cameraHeight).Range(0, kBig, 0.05).Tooltip("Look target height above the player origin")
     .AddFloat("cameraDistance", &ClickToMoveController::cameraDistance).Range(0, kBig, 0.05).Tooltip("Distance from the player")
     .AddFloat("minDistance", &ClickToMoveController::minDistance).Range(0, kBig, 0.05).Tooltip("Closest the camera may come")
     .AddFloat("maxDistance", &ClickToMoveController::maxDistance).Range(0, kBig, 0.05).Tooltip("Farthest the camera may go")
     .AddFloat("cameraYaw", &ClickToMoveController::cameraYaw).Range(-360, 360, 1).Tooltip("Orbit angle (degrees); rotate with middle-drag / Q,E")
     .AddFloat("cameraPitch", &ClickToMoveController::cameraPitch).Range(-90, 90, 1).Tooltip("Downward tilt (degrees)")
     .AddFloat("minPitch", &ClickToMoveController::minPitch).Range(-90, 90, 1).Tooltip("Lowest look angle, degrees")
     .AddFloat("maxPitch", &ClickToMoveController::maxPitch).Range(-90, 90, 1).Tooltip("Highest look angle, degrees")
     .AddFloat("rotateSpeed", &ClickToMoveController::rotateSpeed).Range(0, 36000, 1).Tooltip("Degrees per pixel of middle-drag")
     .AddFloat("keyRotateSpeed", &ClickToMoveController::keyRotateSpeed).Range(0, 36000, 1).Tooltip("Degrees/sec for the rotate keys")
     .AddKey("rotateLeftKey", &ClickToMoveController::rotateLeftKey).Tooltip("E.g. 'q' (0 = disabled)")
     .AddKey("rotateRightKey", &ClickToMoveController::rotateRightKey).Tooltip("E.g. 'e'")
     .AddFloat("cameraDamping", &ClickToMoveController::cameraDamping).Range(0, 1000, 0.1).Tooltip("0 = snap; higher = smoother follow");
    b.Commit();
}

// ThirdPersonShooterController
void RegisterThirdPersonShooterController() {
    PropertyBuilder<ThirdPersonShooterController> b("ThirdPersonShooterController", "tpsctrl", "Controllers");
    b.Section("Movement")
     .AddFloat("walkSpeed", &ThirdPersonShooterController::walkSpeed).Range(0, 10000, 0.1).Tooltip("Walking speed, units per second")
     .AddFloat("runSpeed", &ThirdPersonShooterController::runSpeed).Range(0, 10000, 0.1).Tooltip("Speed while the sprint key is held")
     .AddFloat("jumpForce", &ThirdPersonShooterController::jumpForce).Range(0, kBig, 0.1).Tooltip("Upward speed of a jump")
     .AddKey("sprintKey", &ThirdPersonShooterController::sprintKey).Tooltip("Hold to run (matches the other controllers)")
     .AddBool("canJump", &ThirdPersonShooterController::canJump).Tooltip("Allow jumping")
     .AddInt("maxJumps", &ThirdPersonShooterController::maxJumps).Range(1, 16, 1).Tooltip("1 = single jump, 2 = double jump, etc")
     .AddBool("driveAnimation", &ThirdPersonShooterController::driveAnimation).Tooltip("Drive a sibling Character's idle, walk and run from the movement")
     .AddBool("footIK", &ThirdPersonShooterController::footIK).Tooltip("Plant the Character's feet on the ground")
     .AddFloat("walkAcceleration", &ThirdPersonShooterController::walkAcceleration).Range(0, kBig, 1).Tooltip("Acceleration while walking (units/s^2)")
     .AddFloat("acceleration", &ThirdPersonShooterController::acceleration).Range(0, kBig, 1).Tooltip("How fast the speed rises toward the target (units/s^2, 0 = instant)")
     .AddFloat("deceleration", &ThirdPersonShooterController::deceleration).Range(0, kBig, 1).Tooltip("How fast the speed drops with no input (units/s^2, 0 = instant)")
     .AddFloat("steerRate", &ThirdPersonShooterController::steerRate).Range(0, 36000, 1).Tooltip("How fast a change of direction drops the old heading (units/s^2)")
     .AddFloat("airControl", &ThirdPersonShooterController::airControl).Range(0, 1000, 0.01).Tooltip("Control while airborne (0 = none, 1 = full)")
     .AddFloat("stepOffset", &ThirdPersonShooterController::stepOffset).Range(-kBig, kBig, 0.05).Tooltip("Max stair / curb height (0 = no step-up)")
     .AddFloat("coyoteTime", &ThirdPersonShooterController::coyoteTime).Range(0, 3600, 0.05).Tooltip("A jump still works this long after leaving a ledge, seconds")
     .AddFloat("jumpBufferTime", &ThirdPersonShooterController::jumpBufferTime).Range(0, 3600, 0.05).Tooltip("A jump pressed this long before landing still counts, seconds")
     .AddFloat("turnSpeed", &ThirdPersonShooterController::turnSpeed).Range(0, 36000, 1).Tooltip("Body snaps to the aim direction (1/s)")
     .Section("Look / camera")
     .AddFloat("mouseSensitivity", &ThirdPersonShooterController::mouseSensitivity).Range(0, kBig, 0.1).Tooltip("Degrees of turn per pixel of mouse movement")
     .AddBool("invertY", &ThirdPersonShooterController::invertY).Tooltip("Invert vertical mouse look")
     .AddFloat("minPitch", &ThirdPersonShooterController::minPitch).Range(-90, 90, 1).Tooltip("Lowest look angle, degrees")
     .AddFloat("maxPitch", &ThirdPersonShooterController::maxPitch).Range(-90, 90, 1).Tooltip("Highest look angle, degrees")
     .AddFloat("distance", &ThirdPersonShooterController::distance).Range(0, kBig, 0.05).Tooltip("Hip-fire camera distance")
     .AddFloat("cameraHeight", &ThirdPersonShooterController::cameraHeight).Range(0, kBig, 0.05).Tooltip("Shoulder/look height above the player origin")
     .AddFloat("shoulderOffset", &ThirdPersonShooterController::shoulderOffset).Range(-kBig, kBig, 0.05).Tooltip("Over-the-shoulder lateral offset")
     .AddFloat("pivotDampingXZ", &ThirdPersonShooterController::pivotDampingXZ).Range(0, 1000, 0.1).Tooltip("Camera pivot smoothing on the ground plane (0 = rigid)")
     .AddFloat("pivotDampingY", &ThirdPersonShooterController::pivotDampingY).Range(0, 1000, 0.1).Tooltip("Camera pivot smoothing in height (0 = rigid)")
     .AddFloat("maxPivotLag", &ThirdPersonShooterController::maxPivotLag).Range(0, kBig, 0.05).Tooltip("The pivot never trails the player by more (m); 0 = no limit")
     .AddBool("cameraCollision", &ThirdPersonShooterController::cameraCollision).Tooltip("Pull in so the view never clips walls")
     .AddFloat("cameraCollisionSkin", &ThirdPersonShooterController::cameraCollisionSkin).Range(0, kBig, 0.05).Tooltip("Gap kept between the camera and an obstacle")
     .AddFloat("cameraCollisionRadius", &ThirdPersonShooterController::cameraCollisionRadius).Range(0, kBig, 0.05).Tooltip("Radius of the camera probe used to detect obstacles")
     .AddInt("cameraCollisionMask", &ThirdPersonShooterController::cameraCollisionMask).Tooltip("Collision layers the camera avoids (bit n = layer n)")
     .AddFloat("cameraCollisionPullIn", &ThirdPersonShooterController::cameraCollisionPullIn).Range(0, 1000, 0.1).Tooltip("How fast a hair-closer obstacle eases the camera in")
     .AddFloat("cameraCollisionRecover", &ThirdPersonShooterController::cameraCollisionRecover).Range(0, 1000, 0.1).Tooltip("Ease the camera back out once an obstacle clears (spring arm) instead of snapping. Higher = quicker recovery")
     .Section("Aim")
     .AddInt("aimButton", &ThirdPersonShooterController::aimButton).Range(0, 4, 1).Tooltip("SDL right button")
     .AddFloat("aimDistance", &ThirdPersonShooterController::aimDistance).Range(0, kBig, 0.05).Tooltip("Closer when aiming (zoom over the shoulder)")
     .AddFloat("aimShoulder", &ThirdPersonShooterController::aimShoulder).Range(-kBig, kBig, 0.05).Tooltip("Over-the-shoulder shift while aiming")
     .AddFloat("aimSpeed", &ThirdPersonShooterController::aimSpeed).Range(0, 10000, 0.1).Tooltip("How fast it eases between hip and aim")
     .Section("Fire")
     .AddInt("fireButton", &ThirdPersonShooterController::fireButton).Range(0, 4, 1).Tooltip("Mouse button that fires")
     .AddBool("autoFire", &ThirdPersonShooterController::autoFire).Tooltip("Hold to keep firing vs one shot per click")
     .AddFloat("fireRate", &ThirdPersonShooterController::fireRate).Range(0, 10000, 0.1).Tooltip("Shots/sec when autoFire")
     .Section("Cursor")
     .AddBool("lockCursor", &ThirdPersonShooterController::lockCursor).Tooltip("Hide + lock the cursor while playing")
     .Section("Lean")
     .AddKey("leanLeftKey", &ThirdPersonShooterController::leanLeftKey)
     .AddKey("leanRightKey", &ThirdPersonShooterController::leanRightKey)
     .AddFloat("leanAngle", &ThirdPersonShooterController::leanAngle).Range(0, 360, 1).Tooltip("Camera roll + body-lean degrees")
     .AddFloat("leanOffset", &ThirdPersonShooterController::leanOffset).Range(-kBig, kBig, 0.05).Tooltip("Sideways camera shift while leaning")
     .AddFloat("leanSpeed", &ThirdPersonShooterController::leanSpeed).Range(0, 10000, 0.1).Tooltip("How fast the lean eases in and out");
    b.Commit();
}

// TopDownShooterController
void RegisterTopDownShooterController() {
    PropertyBuilder<TopDownShooterController> b("TopDownShooterController", "tdsctrl", "Controllers");
    b.Section("Movement")
     .AddFloat("walkSpeed", &TopDownShooterController::walkSpeed).Range(0, 10000, 0.1).Tooltip("Walking speed, units per second")
     .AddFloat("runSpeed", &TopDownShooterController::runSpeed).Range(0, 10000, 0.1).Tooltip("Speed while the sprint key is held")
     .AddKey("sprintKey", &TopDownShooterController::sprintKey).Tooltip("Hold to run (0 = off: shooters usually don't)")
     .AddFloat("acceleration", &TopDownShooterController::acceleration).Range(0, kBig, 1).Tooltip("How fast the speed rises toward the target (units/s^2, 0 = instant)")
     .AddFloat("deceleration", &TopDownShooterController::deceleration).Range(0, kBig, 1).Tooltip("How fast the speed drops with no input (units/s^2, 0 = instant)")
     .AddFloat("backpedalMultiplier", &TopDownShooterController::backpedalMultiplier).Range(0, 1000, 0.01).Tooltip("Speed when moving away from the aim")
     .AddBool("cameraRelative", &TopDownShooterController::cameraRelative).Tooltip("WASD follows the camera yaw instead of world axes")
     .AddBool("driveAnimation", &TopDownShooterController::driveAnimation).Tooltip("Drive a sibling Character's idle, walk and run from the movement")
     .Section("Aim")
     .AddEnum("aimMode", &TopDownShooterController::aimMode, {"Mouse", "MoveDirection", "Auto"}).Tooltip("Where the player aims: mouse, movement direction, or automatic")
     .AddFloat("turnSpeed", &TopDownShooterController::turnSpeed).Range(0, 36000, 1).Tooltip("How fast the body turns to the aim (0 = snap)")
     .Section("Weapon")
     .AddEnum("fireMode", &TopDownShooterController::fireMode, {"Bullets", "Prefab", "Hitscan"}).Tooltip("How a shot is made")
     .AddInt("fireButton", &TopDownShooterController::fireButton).Range(0, 4, 1).Tooltip("Mouse button (0 = left)")
     .AddKey("fireKey", &TopDownShooterController::fireKey).Tooltip("Optional keyboard fire")
     .AddInt("fireGamepadButton", &TopDownShooterController::fireGamepadButton).Range(-1, 31, 1).Tooltip("Gamepad button that fires (-1 = none)")
     .AddBool("autoFire", &TopDownShooterController::autoFire).Tooltip("Hold to keep firing")
     .AddFloat("fireRate", &TopDownShooterController::fireRate).Range(0, 10000, 0.1).Tooltip("Shots per second")
     .AddFloat("damage", &TopDownShooterController::damage).Range(0, kBig, 0.1).Tooltip("Damage dealt per hit")
     .AddFloat("projectileSpeed", &TopDownShooterController::projectileSpeed).Range(0, 10000, 0.1).Tooltip("Speed of a projectile, units per second")
     .AddFloat("projectileLifetime", &TopDownShooterController::projectileLifetime).Range(0, 3600, 0.05).Tooltip("Seconds before a projectile disappears")
     .AddFloat("range", &TopDownShooterController::range).Range(0, kBig, 0.05).Tooltip("Hitscan reach")
     .AddInt("pellets", &TopDownShooterController::pellets).Range(1, 1000, 1).Tooltip("Bullets per shot (shotgun > 1)")
     .AddFloat("spread", &TopDownShooterController::spread).Range(0, kBig, 0.1).Tooltip("Random cone (degrees)")
     .AddFloat("impactForce", &TopDownShooterController::impactForce).Range(0, kBig, 0.1).Tooltip("Impulse given to what it hits")
     .AddFloat("bulletSize", &TopDownShooterController::bulletSize).Range(0, kBig, 0.05).Tooltip("Size of a bullet")
     .AddColor("bulletColor", &TopDownShooterController::bulletColor).Range(0, 1, 0.01).Tooltip("Colour of a bullet")
     .AddString("projectile", &TopDownShooterController::projectile).Tooltip("Prefab for FireMode::Prefab")
     .AddFloat("muzzleHeight", &TopDownShooterController::muzzleHeight).Range(-kBig, kBig, 0.05).Tooltip("Bullets start this high above the player origin")
     .AddFloat("muzzleForward", &TopDownShooterController::muzzleForward).Range(-kBig, kBig, 0.05).Tooltip("... and this far in front")
     .Section("Ammo")
     .AddInt("magazineSize", &TopDownShooterController::magazineSize).Range(0, kBig, 1).Tooltip("0 = never reload")
     .AddKey("reloadKey", &TopDownShooterController::reloadKey)
     .AddFloat("reloadTime", &TopDownShooterController::reloadTime).Range(0, 3600, 0.05).Tooltip("Seconds a reload takes")
     .AddBool("autoReload", &TopDownShooterController::autoReload).Tooltip("Reload when you fire an empty magazine")
     .Section("Dash")
     .AddKey("dashKey", &TopDownShooterController::dashKey).Tooltip("Space")
     .AddInt("dashGamepadButton", &TopDownShooterController::dashGamepadButton).Range(-1, 31, 1).Tooltip("Pad A (-1 = none)")
     .AddFloat("dashSpeed", &TopDownShooterController::dashSpeed).Range(0, 10000, 0.1).Tooltip("Peak speed at the start of the dash")
     .AddFloat("dashTime", &TopDownShooterController::dashTime).Range(0, 3600, 0.05).Tooltip("Seconds a dash lasts")
     .AddFloat("dashCooldown", &TopDownShooterController::dashCooldown).Range(0, 3600, 0.05).Tooltip("After a dash ends")
     .AddBool("dashInvulnerable", &TopDownShooterController::dashInvulnerable).Tooltip("No damage while dashing (sibling HealthStat)")
     .Section("Camera")
     .AddFloat("cameraDistance", &TopDownShooterController::cameraDistance).Range(0, kBig, 0.05).Tooltip("How far the camera sits from the player")
     .AddFloat("cameraPitch", &TopDownShooterController::cameraPitch).Range(-90, 90, 1).Tooltip("Downward tilt (deg)")
     .AddFloat("cameraYaw", &TopDownShooterController::cameraYaw).Range(-360, 360, 1).Tooltip("Fixed horizontal angle of the camera, degrees")
     .AddFloat("cameraDamping", &TopDownShooterController::cameraDamping).Range(0, 1000, 0.1).Tooltip("Camera follow smoothing (0 = rigid)")
     .AddFloat("aimLookAhead", &TopDownShooterController::aimLookAhead).Range(-kBig, kBig, 0.05).Tooltip("Share of the way to the cursor the camera leans")
     .AddFloat("maxLookAhead", &TopDownShooterController::maxLookAhead).Range(-kBig, kBig, 0.05).Tooltip("Farthest the camera leans toward the cursor")
     .AddBool("wheelZoom", &TopDownShooterController::wheelZoom).Tooltip("The mouse wheel changes the camera distance")
     .AddFloat("minDistance", &TopDownShooterController::minDistance).Range(0, kBig, 0.05).Tooltip("Closest the camera may come")
     .AddFloat("maxDistance", &TopDownShooterController::maxDistance).Range(0, kBig, 0.05).Tooltip("Farthest the camera may go")
     .AddFloat("fireShake", &TopDownShooterController::fireShake).Range(0, 1000, 0.01).Tooltip("Camera kick per shot");
    b.Commit();
}

// Platformer25DController
void RegisterPlatformer25DController() {
    PropertyBuilder<Platformer25DController> b("Platformer25DController", "p25ctrl", "Controllers");
    b.Section("Run")
     .AddFloat("walkSpeed", &Platformer25DController::walkSpeed).Range(0, 10000, 0.1).Tooltip("Walking speed, units per second")
     .AddFloat("runSpeed", &Platformer25DController::runSpeed).Range(0, 10000, 0.1).Tooltip("Speed while the sprint key is held")
     .AddKey("sprintKey", &Platformer25DController::sprintKey)
     .AddFloat("acceleration", &Platformer25DController::acceleration).Range(0, kBig, 1).Tooltip("How fast the speed rises toward the target (units/s^2, 0 = instant)")
     .AddFloat("deceleration", &Platformer25DController::deceleration).Range(0, kBig, 1).Tooltip("How fast the speed drops with no input (units/s^2, 0 = instant)")
     .AddFloat("airControl", &Platformer25DController::airControl).Range(0, 1000, 0.01).Tooltip("Control while airborne (0 = none, 1 = full)")
     .AddFloat("stepHeight", &Platformer25DController::stepHeight).Range(0, kBig, 0.05).Tooltip("Walk up ledges this high without jumping")
     .AddFloat("stickRunThreshold", &Platformer25DController::stickRunThreshold).Range(0, 3600, 0.05).Tooltip("Gamepad: push the stick this far to run")
     .AddBool("driveAnimation", &Platformer25DController::driveAnimation).Tooltip("Drive a sibling Character's idle, walk and run from the movement")
     .Section("Jump")
     .AddBool("canJump", &Platformer25DController::canJump).Tooltip("Allow jumping")
     .AddKey("jumpKey", &Platformer25DController::jumpKey)
     .AddInt("jumpGamepadButton", &Platformer25DController::jumpGamepadButton).Range(-1, 31, 1).Tooltip("A (-1 = none)")
     .AddFloat("jumpForce", &Platformer25DController::jumpForce).Range(0, kBig, 0.1).Tooltip("Upward speed of a jump")
     .AddInt("maxJumps", &Platformer25DController::maxJumps).Range(1, 16, 1).Tooltip("2 = double jump")
     .AddFloat("jumpCut", &Platformer25DController::jumpCut).Range(0, 1000, 0.01).Tooltip("Upward speed kept when you let go early (1 = fixed height)")
     .AddFloat("fallGravity", &Platformer25DController::fallGravity).Range(-1000, 1000, 0.1).Tooltip("Extra gravity while falling (snappier landings)")
     .AddFloat("maxFallSpeed", &Platformer25DController::maxFallSpeed).Range(0, 10000, 0.1).Tooltip("Terminal fall speed (0 = no limit)")
     .AddFloat("coyoteTime", &Platformer25DController::coyoteTime).Range(0, 3600, 0.05).Tooltip("A jump still works this long after leaving a ledge, seconds")
     .AddFloat("jumpBufferTime", &Platformer25DController::jumpBufferTime).Range(0, 3600, 0.05).Tooltip("A jump pressed this long before landing still counts, seconds")
     .Section("Stance")
     .AddKey("crouchKey", &Platformer25DController::crouchKey)
     .AddKey("proneKey", &Platformer25DController::proneKey)
     .AddBool("toggleStance", &Platformer25DController::toggleStance).Tooltip("Tap to toggle vs hold")
     .AddFloat("crouchSpeed", &Platformer25DController::crouchSpeed).Range(0, 10000, 0.1).Tooltip("Speed while crouched")
     .AddFloat("proneSpeed", &Platformer25DController::proneSpeed).Range(0, 10000, 0.1).Tooltip("Speed while prone")
     .AddBool("allowProne", &Platformer25DController::allowProne).Tooltip("False: no prone at all (key and gamepad)")
     .AddBool("resizeBody", &Platformer25DController::resizeBody).Tooltip("Crouch / prone shrink the collider (fit under low things; no standing into a ceiling)")
     .AddFloat("crouchHeight", &Platformer25DController::crouchHeight).Range(0, kBig, 0.05).Tooltip("Crouched body height, fraction of standing")
     .AddFloat("proneHeight", &Platformer25DController::proneHeight).Range(0, kBig, 0.05).Tooltip("Prone body height, fraction of standing")
     .AddInt("stanceGamepadButton", &Platformer25DController::stanceGamepadButton).Range(-1, 31, 1).Tooltip("B: tap = crouch / stand, hold = prone (-1 = none)")
     .AddFloat("proneHoldTime", &Platformer25DController::proneHoldTime).Range(0, 3600, 0.05).Tooltip("Seconds the gamepad button is held to go prone")
     .Section("Depth layers")
     .AddInt("layerCount", &Platformer25DController::layerCount).Range(1, 16, 1).Tooltip("LittleBigPlanet uses 3")
     .AddFloat("layerSpacing", &Platformer25DController::layerSpacing).Range(0, kBig, 0.05).Tooltip("Distance between layers (world Z)")
     .AddFloat("frontLayerZ", &Platformer25DController::frontLayerZ).Range(-kBig, kBig, 0.05).Tooltip("World Z of layer 0; deeper layers are further from the camera")
     .AddInt("layer", &Platformer25DController::layer).Range(0, 15, 1).Tooltip("Current layer (the starting layer when the scene starts)")
     .AddKey("layerBackKey", &Platformer25DController::layerBackKey)
     .AddKey("layerFrontKey", &Platformer25DController::layerFrontKey)
     .AddFloat("layerSwitchSpeed", &Platformer25DController::layerSwitchSpeed).Range(0, 10000, 0.1).Tooltip("How quickly you slide between layers")
     .AddBool("checkLayerFree", &Platformer25DController::checkLayerFree).Tooltip("Only switch when the other layer has room")
     .AddFloat("bodyRadius", &Platformer25DController::bodyRadius).Range(0, kBig, 0.05).Tooltip("For the room check")
     .Section("Facing")
     .AddFloat("turnSpeed", &Platformer25DController::turnSpeed).Range(0, 36000, 1).Tooltip("How fast the body turns, degrees per second (0 = snap)")
     .AddFloat("faceCameraAngle", &Platformer25DController::faceCameraAngle).Range(0, 360, 1).Tooltip("Turn this far toward the camera so the face shows")
     .Section("Grab")
     .AddBool("canGrab", &Platformer25DController::canGrab).Tooltip("Allow grabbing and dragging objects")
     .AddKey("grabKey", &Platformer25DController::grabKey)
     .AddInt("grabGamepadButton", &Platformer25DController::grabGamepadButton).Range(-1, 31, 1).Tooltip("Gamepad button that grabs (-1 = none)")
     .AddFloat("grabReach", &Platformer25DController::grabReach).Range(0, kBig, 0.05).Tooltip("How far the player can reach to grab")
     .AddFloat("grabPullSpeed", &Platformer25DController::grabPullSpeed).Range(0, 10000, 0.1).Tooltip("Speed multiplier while dragging something")
     .AddBool("grabHang", &Platformer25DController::grabHang).Tooltip("Catch a ledge (the top edge of something solid) in the air and hang")
     .AddBool("ledgeClimb", &Platformer25DController::ledgeClimb).Tooltip("W / up while hanging climbs onto the ledge")
     .AddFloat("climbTime", &Platformer25DController::climbTime).Range(0, 3600, 0.05).Tooltip("Seconds to pull yourself up")
     .Section("Camera")
     .AddFloat("cameraDistance", &Platformer25DController::cameraDistance).Range(0, kBig, 0.05).Tooltip("How far the camera sits from the player")
     .AddFloat("cameraHeight", &Platformer25DController::cameraHeight).Range(0, kBig, 0.05).Tooltip("Height of the point the camera looks at")
     .AddFloat("cameraPitch", &Platformer25DController::cameraPitch).Range(-90, 90, 1).Tooltip("Look down a little (deg)")
     .AddFloat("lookAhead", &Platformer25DController::lookAhead).Range(-kBig, kBig, 0.05).Tooltip("Lead in the direction you're running")
     .AddFloat("cameraDamping", &Platformer25DController::cameraDamping).Range(0, 1000, 0.1).Tooltip("Camera follow smoothing (0 = rigid)")
     .AddFloat("verticalDamping", &Platformer25DController::verticalDamping).Range(0, 1000, 0.1).Tooltip("Slower up / down follow, so jumps don't jerk the view")
     .AddFloat("speedZoom", &Platformer25DController::speedZoom).Range(0, 1000, 0.01).Tooltip("Pull back this much per unit of speed")
     .AddBool("cameraFollowsLayer", &Platformer25DController::cameraFollowsLayer).Tooltip("Off: the camera stays on the middle of the layers");
    b.Commit();
}

// ---- Vehicles, projectiles and spawners ------------------------------------

// VehicleController
void RegisterVehicleController() {
    PropertyBuilder<VehicleController> b("VehicleController", "vehicle", "Vehicles");
    b.AddFloat("acceleration", &VehicleController::acceleration).Range(0, kBig, 1).Tooltip("How quickly it reaches top speed (units/s^2)")
     .AddFloat("maxSpeed", &VehicleController::maxSpeed).Range(0, 10000, 0.1).Tooltip("Forward top speed (units/s)")
     .AddFloat("reverseSpeed", &VehicleController::reverseSpeed).Range(0, 10000, 0.1).Tooltip("Top reverse speed")
     .AddFloat("brakeForce", &VehicleController::brakeForce).Range(0, kBig, 0.1).Tooltip("Deceleration when braking (units/s^2)")
     .AddFloat("drag", &VehicleController::drag).Range(0, kBig, 0.1).Tooltip("Rolling resistance while coasting (no throttle)")
     .AddFloat("turnSpeed", &VehicleController::turnSpeed).Range(0, 36000, 1).Tooltip("Steering rate at speed (degrees/s)")
     .AddFloat("grip", &VehicleController::grip).Range(0, 1000, 0.01).Tooltip("How fast sideways slide is killed (higher = sticky)")
     .AddFloat("handbrakeGrip", &VehicleController::handbrakeGrip).Range(0, 1000, 0.01).Tooltip("Grip while the handbrake is held (low = drifty)")
     .AddFloat("groundCheckDistance", &VehicleController::groundCheckDistance).Range(0, kBig, 0.05).Tooltip("Down-ray length for \"on the ground\"")
     .Section("Per-wheel raycast suspension")
     .AddBool("suspension", &VehicleController::suspension).Tooltip("Enable spring suspension + body tilt")
     .AddFloat("rideHeight", &VehicleController::rideHeight).Range(0, kBig, 0.05).Tooltip("Chassis height the spring rests at (units)")
     .AddFloat("springStrength", &VehicleController::springStrength).Range(0, 1000, 0.01).Tooltip("Vertical spring stiffness")
     .AddFloat("springDamping", &VehicleController::springDamping).Range(0, 1000, 0.1).Tooltip("Vertical spring damper")
     .AddFloat("suspensionTravel", &VehicleController::suspensionTravel).Range(0, kBig, 0.05).Tooltip("Droop below rideHeight before airborne")
     .AddFloat("wheelBase", &VehicleController::wheelBase).Range(0, kBig, 0.05).Tooltip("Front-to-rear wheel spacing")
     .AddFloat("trackWidth", &VehicleController::trackWidth).Range(0, kBig, 0.05).Tooltip("Left-to-right wheel spacing")
     .AddFloat("bodyLean", &VehicleController::bodyLean).Range(0, 360, 1).Tooltip("Chassis lean from accel/cornering (0 = off)")
     .AddFloat("maxTilt", &VehicleController::maxTilt).Range(0, 360, 1).Tooltip("Clamp body roll/pitch (degrees)")
     .AddFloat("tiltSmooth", &VehicleController::tiltSmooth).Range(0, 1000, 0.1).Tooltip("How fast tilt eases toward target")
     .AddBool("followCamera", &VehicleController::followCamera).Tooltip("Chase the scene's main camera behind the car")
     .AddFloat("camDistance", &VehicleController::camDistance).Range(0, kBig, 0.05).Tooltip("Distance of the chase camera behind the car")
     .AddFloat("camHeight", &VehicleController::camHeight).Range(0, kBig, 0.05).Tooltip("Height of the chase camera above the car")
     .AddFloat("camLerp", &VehicleController::camLerp).Range(0, 1000, 0.1).Tooltip("How fast the chase camera follows")
     .AddKey("handbrakeKey", &VehicleController::handbrakeKey);
    b.Commit();
}

// VehicleController2D
void RegisterVehicleController2D() {
    PropertyBuilder<VehicleController2D> b("VehicleController2D", "vehicle2d", "Vehicles");
    b.AddFloat("acceleration", &VehicleController2D::acceleration).Range(0, kBig, 1).Tooltip("How fast the speed rises toward the target (units/s^2, 0 = instant)")
     .AddFloat("maxSpeed", &VehicleController2D::maxSpeed).Range(0, 10000, 0.1).Tooltip("Top speed, units per second")
     .AddFloat("reverseSpeed", &VehicleController2D::reverseSpeed).Range(0, 10000, 0.1).Tooltip("Top reverse speed")
     .AddFloat("brakeForce", &VehicleController2D::brakeForce).Range(0, kBig, 0.1).Tooltip("Deceleration while braking (units/s^2)")
     .AddFloat("drag", &VehicleController2D::drag).Range(0, kBig, 0.1).Tooltip("Slowdown while coasting")
     .AddFloat("turnSpeed", &VehicleController2D::turnSpeed).Range(0, 36000, 1).Tooltip("Deg/s steering (top-down only)")
     .AddFloat("grip", &VehicleController2D::grip).Range(0, 1000, 0.01).Tooltip("How fast sideways slide is killed (high = sticky)")
     .AddFloat("handbrakeGrip", &VehicleController2D::handbrakeGrip).Range(0, 1000, 0.01).Tooltip("Sideways grip while the handbrake is held (low = drifty)")
     .AddBool("sideView", &VehicleController2D::sideView).Tooltip("True = platformer car (drive on X, gravity on)")
     .AddKey("handbrakeKey", &VehicleController2D::handbrakeKey);
    b.Commit();
}

// Projectile3D
void RegisterProjectile3D() {
    PropertyBuilder<Projectile3D> b("Projectile3D", "proj3d", "Gameplay");
    b.AddVec3("velocity", &Projectile3D::velocity).Range(-kBig, kBig, 0.1).Tooltip("World units per second")
     .AddFloat("damage", &Projectile3D::damage).Range(0, kBig, 0.1).Tooltip("Damage dealt per hit")
     .AddFloat("lifetime", &Projectile3D::lifetime).Range(0, 3600, 0.05).Tooltip("Seconds before it disappears")
     .AddFloat("gravityScale", &Projectile3D::gravityScale).Range(0, 1000, 0.01).Tooltip("0 = straight line; 1 = drops like a grenade")
     .AddFloat("impactForce", &Projectile3D::impactForce).Range(0, kBig, 0.1).Tooltip("Impulse given to a Rigidbody3D it hits")
     .AddInt("pierce", &Projectile3D::pierce).Range(0, 1000, 1).Tooltip("Extra targets it passes through")
     .AddBool("bounce", &Projectile3D::bounce).Tooltip("Ricochet off walls instead of stopping")
     .AddInt("maxBounces", &Projectile3D::maxBounces).Range(0, 1000, 1).Tooltip("How many ricochets before it stops");
    b.Commit();
}

// Volume
void RegisterVolume() {
    PropertyBuilder<Volume> b("Volume", "volume", "Gameplay");
    b.AddEnum("kind", &Volume::kind, {"Checkpoint", "Kill", "Force", "Atmosphere", "Ambience"}).Tooltip("What the region does")
     .AddEnum("shape", &Volume::shape, {"Box", "Sphere", "Cylinder"}).Tooltip("Box, the ellipsoid inside the box, or the cylinder standing on the Y axis")
     .AddVec3("size", &Volume::size).Range(0.05, kBig, 0.1).Tooltip("The box the shape fills, in local units (scaled by the Transform)")
     .AddVec3("offset", &Volume::offset).Range(-kBig, kBig, 0.1).Tooltip("Centre of the region, in local units")
     .AddString("activatorTag", &Volume::activatorTag).Tooltip("Who counts: a tag or name text. Blank = any body (Force)")
     .AddString("eventName", &Volume::eventName).Tooltip("Checkpoint / Kill: sent to scripts as an On Logic Signal when it fires")
     .AddEnum("killAction", &Volume::killAction, {"Respawn", "Destroy"}).Tooltip("Kill: send the body back to its checkpoint, or remove it")
     .AddFloat("killDamage", &Volume::killDamage).Range(0, kBig, 1).Tooltip("Kill: health taken before the respawn")
     .AddEnum("forceShape", &Volume::forceShape, {"Direction", "Radial"}).Tooltip("Force: one steady direction, or away from / toward the centre")
     .AddVec3("forceDirection", &Volume::forceDirection).Range(-1, 1, 0.05).Tooltip("Force (Direction): which way it pushes")
     .AddBool("forceLocal", &Volume::forceLocal).Tooltip("Force (Direction): the direction turns with the object")
     .AddFloat("forceStrength", &Volume::forceStrength).Range(-kBig, kBig, 0.5).Tooltip("Force: acceleration in m/s^2 (Radial: negative pulls in)")
     .AddColor("fogColor", &Volume::fogColor).Tooltip("Atmosphere: fog colour inside")
     .AddFloat("fogStart", &Volume::fogStart).Range(0, kBig, 0.5).Tooltip("Atmosphere: where the fog starts")
     .AddFloat("fogEnd", &Volume::fogEnd).Range(0, kBig, 0.5).Tooltip("Atmosphere: where the fog is solid")
     .AddFloat("vignette", &Volume::vignette).Range(0, 1, 0.01).Tooltip("Atmosphere: edge darkening inside")
     .AddFloat("blend", &Volume::blend).Range(0, 1, 0.01).Tooltip("Atmosphere: how far in from the edge it takes to reach full strength")
     .AddFloat("fadeTime", &Volume::fadeTime).Range(0, 60, 0.1).Tooltip("Ambience: seconds to fade the sound in and out");
    b.Commit();
}

// Spawner
void RegisterSpawner() {
    PropertyBuilder<Spawner> b("Spawner", "spawner", "Gameplay");
    b.AddString("templateName", &Spawner::templateName).Tooltip("Object to clone (takes priority)")
     .AddAssetPath("prefabPath", &Spawner::prefabPath, "prefab").Tooltip("Or a .okayprefab file when no template is set")
     .AddFloat("interval", &Spawner::interval).Range(0, 3600, 0.05).Tooltip("Seconds between spawns")
     .AddInt("maxAlive", &Spawner::maxAlive).Range(0, kBig, 1).Tooltip("Cap on live spawns from this spawner (0 = unlimited)")
     .AddInt("totalToSpawn", &Spawner::totalToSpawn).Range(0, 1000000, 1).Tooltip("Lifetime cap (0 = endless)")
     .AddFloat("spawnRadius", &Spawner::spawnRadius).Range(0, kBig, 0.05).Tooltip("Spawns land within this radius of the spawner")
     .AddFloat("startDelay", &Spawner::startDelay).Range(0, 3600, 0.05).Tooltip("Wait before the first spawn")
     .AddBool("deactivateTemplate", &Spawner::deactivateTemplate).Tooltip("Hide the blueprint object on start")
     .AddInt("count", &Spawner::count).Range(1, 100000, 1).Tooltip("Spawns per wave")
     .AddInt("waves", &Spawner::waves).Range(0, kBig, 1).Tooltip("Waves to run (0 = endless)")
     .AddFloat("waveDelay", &Spawner::waveDelay).Range(0, 3600, 0.05).Tooltip("Pause between waves (0 = just `interval`)")
     .AddBool("autoStart", &Spawner::autoStart).Tooltip("Begin at scene start (off = spawner_start / StartWaves)")
     .AddBool("usePool", &Spawner::usePool).Tooltip("Make the copies from an ObjectPool (reused, see Scene::Despawn)");
    b.Commit();
}

// ---- Audio -----------------------------------------------------------------

// MusicPlayer
void RegisterMusicPlayer() {
    PropertyBuilder<MusicPlayer> b("MusicPlayer", "music", "Audio");
    b.AddBool("playOnStart", &MusicPlayer::playOnStart).Tooltip("Start playing when the scene starts")
     .AddBool("shuffle", &MusicPlayer::shuffle).Tooltip("Play the tracks in random order")
     .AddBool("loopPlaylist", &MusicPlayer::loopPlaylist).Tooltip("After the last track, start over")
     .AddFloat("crossfade", &MusicPlayer::crossfade).Range(0, 3600, 0.05).Tooltip("Seconds two tracks overlap")
     .AddFloat("volume", &MusicPlayer::volume).Range(0, 1, 0.01).Tooltip("Playback volume (0 to 1)")
     .AddBool("stream", &MusicPlayer::stream).Tooltip("Read each track from its file as it plays (small fixed memory) instead of loading it whole");
    b.Commit();
}

// ---- No-code gameplay ------------------------------------------------------

// Collectible
void RegisterCollectible() {
    PropertyBuilder<Collectible> b("Collectible", "collectible", "Gameplay");
    b.AddString("collectorTag", &Collectible::collectorTag).Tooltip("Who can collect (tag or name text); empty = anyone")
     .AddString("scoreVar", &Collectible::scoreVar).Tooltip("Variable to add to (drives the HUD)")
     .AddFloat("points", &Collectible::points).Range(-kBig, kBig, 0.1).Tooltip("Added to scoreVar per pickup")
     .AddFloat("heal", &Collectible::heal).Range(-kBig, kBig, 0.1).Tooltip("Also heal the collector this much (needs a HealthStat)")
     .AddBool("respawn", &Collectible::respawn).Tooltip("Reappear instead of vanishing for good")
     .AddFloat("respawnDelay", &Collectible::respawnDelay).Range(0, 3600, 0.05);
    b.Commit();
}

// DamageOnTouch
void RegisterDamageOnTouch() {
    PropertyBuilder<DamageOnTouch> b("DamageOnTouch", "damagetouch", "Gameplay");
    b.AddString("targetTag", &DamageOnTouch::targetTag).Tooltip("Who takes damage (tag or name text); empty = anyone")
     .AddFloat("damage", &DamageOnTouch::damage).Range(0, kBig, 0.1).Tooltip("Damage dealt per hit")
     .AddFloat("interval", &DamageOnTouch::interval).Range(0, 3600, 0.05).Tooltip("Seconds between repeat hits while overlapping (0 = every frame)")
     .AddBool("once", &DamageOnTouch::once).Tooltip("Hit only on first contact, then never again")
     .AddFloat("knockback", &DamageOnTouch::knockback).Range(-kBig, kBig, 0.1).Tooltip("Impulse away from this object (Rigidbody toucher)")
     .AddBool("destroySelf", &DamageOnTouch::destroySelf).Tooltip("Remove this object after it lands a hit (projectile)");
    b.Commit();
}

// Teleporter
void RegisterTeleporter() {
    PropertyBuilder<Teleporter> b("Teleporter", "teleporter", "Gameplay");
    b.AddString("triggerTag", &Teleporter::triggerTag).Tooltip("Who teleports (tag or name text); empty = anyone")
     .AddString("targetName", &Teleporter::targetName).Tooltip("Teleport to this object's position (empty = use destination)")
     .AddVec3("destination", &Teleporter::destination).Range(-kBig, kBig, 0.1).Tooltip("Fixed target when targetName is empty")
     .AddFloat("cooldown", &Teleporter::cooldown).Range(0, 3600, 0.05).Tooltip("Seconds a just-teleported body is ignored");
    b.Commit();
}

// JumpPad
void RegisterJumpPad() {
    PropertyBuilder<JumpPad> b("JumpPad", "jumppad", "Gameplay");
    b.AddString("triggerTag", &JumpPad::triggerTag).Tooltip("Who launches (tag or name text); empty = anyone")
     .AddFloat("force", &JumpPad::force).Range(0, kBig, 0.1).Tooltip("Launch speed (world units/s)")
     .AddBool("useObjectUp", &JumpPad::useObjectUp).Tooltip("Launch along this object's local +Y (tilt the pad to aim); off = straight up")
     .AddFloat("forwardBoost", &JumpPad::forwardBoost).Range(0, 1000, 0.01).Tooltip("Extra speed added along the body's CURRENT travel direction")
     .AddFloat("cooldown", &JumpPad::cooldown).Range(0, 3600, 0.05).Tooltip("Seconds a just-launched body is ignored (no double-fire)");
    b.Commit();
}

// TriggerZone
void RegisterTriggerZone() {
    PropertyBuilder<TriggerZone> b("TriggerZone", "triggerzone", "Gameplay");
    b.AddEnum("action", &TriggerZone::action, {"SetVar", "AddVar", "ActivateTarget", "DeactivateTarget", "Win", "Lose", "Quit"})
     .AddString("triggerTag", &TriggerZone::triggerTag).Tooltip("Who fires it (tag or name text); empty = anyone")
     .AddString("varName", &TriggerZone::varName).Tooltip("Variable for SetVar/AddVar")
     .AddFloat("amount", &TriggerZone::amount).Range(0, 1000, 0.01).Tooltip("Value to set / add")
     .AddString("targetName", &TriggerZone::targetName).Tooltip("Object to activate/deactivate")
     .AddBool("once", &TriggerZone::once).Tooltip("Fire a single time (default) or on every entry");
    b.Commit();
}

// FollowTarget2D
void RegisterFollowTarget2D() {
    PropertyBuilder<FollowTarget2D> b("FollowTarget2D", "follow2d", "Gameplay");
    b.AddString("target", &FollowTarget2D::target).Tooltip("Name of the object to follow")
     .AddFloat("speed", &FollowTarget2D::speed).Range(0, 10000, 0.1)
     .AddFloat("stopDistance", &FollowTarget2D::stopDistance).Range(0, kBig, 0.05).Tooltip("Keep this far from the target (0 = touch)");
    b.Commit();
}

// Destructible
void RegisterDestructible() {
    PropertyBuilder<Destructible> b("Destructible", "destructible", "Gameplay");
    b.AddString("blockTag", &Destructible::blockTag).Tooltip("Only objects with this tag fracture/collapse")
     .AddFloat("voxelSize", &Destructible::voxelSize).Range(0.01, kBig, 0.05).Tooltip("Grid cell size (matches BlockBuilder.blockSize)")
     .AddInt("fractureChunks", &Destructible::fractureChunks).Range(1, 8, 1).Tooltip("Debris cubes per axis (2 => 8 shards, 3 => 27)")
     .AddFloat("debrisLifetime", &Destructible::debrisLifetime).Range(0, 3600, 0.05).Tooltip("Seconds before a shard despawns")
     .AddFloat("explosionForce", &Destructible::explosionForce).Range(0, kBig, 0.1).Tooltip("Outward speed imparted to shards")
     .AddFloat("upwardBias", &Destructible::upwardBias).Range(-kBig, kBig, 0.1).Tooltip("Extra \"pop\" up so debris arcs, not just sprays out")
     .AddFloat("debrisGravityScale", &Destructible::debrisGravityScale).Range(0, 1000, 0.01).Tooltip("Gravity multiplier on shards and collapsing blocks")
     .AddFloat("debrisDrag", &Destructible::debrisDrag).Range(0, kBig, 0.1).Tooltip("Air drag on shards (settles them)")
     .AddBool("collapseEnabled", &Destructible::collapseEnabled).Tooltip("Run structural-support collapse after each break")
     .AddFloat("anchorY", &Destructible::anchorY).Range(-kBig, kBig, 0.1).Tooltip("Blocks centred at/below this Y are ground-anchored")
     .AddInt("maxDebris", &Destructible::maxDebris).Range(0, kBig, 1).Tooltip("Safety cap on live shards (skip fracturing past it)")
     .AddInt("breakButton", &Destructible::breakButton).Range(-1, 4, 1).Tooltip("Mouse button to fire (0=left,1=right,2=middle)")
     .AddFloat("breakRadius", &Destructible::breakRadius).Range(0, kBig, 0.05).Tooltip("Blocks within this radius of the hit shatter")
     .AddFloat("reach", &Destructible::reach).Range(0, kBig, 0.05).Tooltip("Max ray distance for the gun");
    b.Commit();
}

// Draggable
void RegisterDraggable() {
    PropertyBuilder<Draggable> b("Draggable", "draggable", "Gameplay");
    b.AddBool("returnToStart", &Draggable::returnToStart).Tooltip("Snap back if not dropped on a target")
     .AddBool("anyTarget", &Draggable::anyTarget).Tooltip("Any sprite is a target (not just DropZones)")
     .AddBool("snapToZone", &Draggable::snapToZone).Tooltip("Center the item on the zone it lands on")
     .AddEnum("axis", &Draggable::axis, {"Both", "Horizontal", "Vertical"})
     .AddFloat("dragThreshold", &Draggable::dragThreshold).Range(0, 3600, 0.05).Tooltip("World units the pointer must move before a drag begins")
     .AddBool("bringToFront", &Draggable::bringToFront).Tooltip("Raise sortOrder while dragging so the item draws above the rest")
     .AddFloat("gridX", &Draggable::gridX).Range(-kBig, kBig, 0.1)
     .AddFloat("gridY", &Draggable::gridY).Range(-kBig, kBig, 0.1)
     .AddFloat("dragScale", &Draggable::dragScale).Range(0, 1000, 0.01).Tooltip("Multiply the sprite's scale while dragging for a \"lifted\" look (1 = off)");
    b.Commit();
}

// DropZone
void RegisterDropZone() {
    PropertyBuilder<DropZone> b("DropZone", "dropzone", "Gameplay");
    b.AddString("acceptTag", &DropZone::acceptTag);
    b.Commit();
}

// Flashlight
void RegisterFlashlight() {
    PropertyBuilder<Flashlight> b("Flashlight", "flashlight", "Gameplay");
    b.AddKey("toggleKey", &Flashlight::toggleKey).Tooltip("Press to toggle (0 = no key; still starts in `on`)")
     .AddBool("on", &Flashlight::on).Tooltip("Current state")
     .AddFloat("range", &Flashlight::range).Range(0, kBig, 0.05).Tooltip("How far the beam reaches (world units)")
     .AddFloat("angle", &Flashlight::angle).Range(1, 179, 1).Tooltip("Cone width (full angle, degrees)")
     .AddFloat("intensity", &Flashlight::intensity).Range(0, 1000, 0.05).Tooltip("Brightness")
     .AddColor("color", &Flashlight::color).Range(0, 1, 0.01).Tooltip("Warm torch white");
    b.Commit();
}

// ---- Physics joints and environment ----------------------------------------

// Joint2D
void RegisterJoint2D() {
    PropertyBuilder<Joint2D> b("Joint2D", "joint2d", "Physics 2D");
    b.AddEnum("mode", &Joint2D::mode, {"Distance", "Spring", "Pin", "Hinge"}).Tooltip("Movement style")
     .AddString("connectedBody", &Joint2D::connectedBody).Tooltip("Name of the other body (empty = world anchor)")
     .AddVec2("anchor", &Joint2D::anchor).Range(-kBig, kBig, 0.1).Tooltip("World anchor point when there's no connected body")
     .AddFloat("distance", &Joint2D::distance).Range(0, kBig, 0.05).Tooltip("Rest length (Distance / Spring)")
     .AddBool("autoConfigure", &Joint2D::autoConfigure).Tooltip("Set `distance`/offset from the start separation")
     .AddFloat("spring", &Joint2D::spring).Range(0, kBig, 0.1).Tooltip("Spring stiffness")
     .AddFloat("damper", &Joint2D::damper).Range(0, 1000, 0.1).Tooltip("Spring damping")
     .AddBool("breakable", &Joint2D::breakable).Tooltip("Snap the joint if stretched past breakForce")
     .AddFloat("breakForce", &Joint2D::breakForce).Range(0, kBig, 0.1).Tooltip("Stretch (length error) at which it breaks")
     .Section("Hinge")
     .AddBool("useMotor", &Joint2D::useMotor).Tooltip("Drive the spin toward `motorSpeed`")
     .AddFloat("motorSpeed", &Joint2D::motorSpeed).Range(-10000, 10000, 0.1).Tooltip("Target angular speed (deg/s)")
     .AddFloat("maxMotorTorque", &Joint2D::maxMotorTorque).Range(0, kBig, 0.1).Tooltip("Torque the motor can apply to reach it")
     .AddBool("useLimits", &Joint2D::useLimits).Tooltip("Clamp the hinge angle to [minAngle, maxAngle]")
     .AddFloat("minAngle", &Joint2D::minAngle).Range(-360, 360, 1).Tooltip("Lower limit (deg, relative to the start angle)")
     .AddFloat("maxAngle", &Joint2D::maxAngle).Range(-360, 360, 1).Tooltip("Upper limit (deg, relative to the start angle)");
    b.Commit();
}

// Joint3D
void RegisterJoint3D() {
    PropertyBuilder<Joint3D> b("Joint3D", "joint3d", "Physics 3D");
    b.AddEnum("mode", &Joint3D::mode, {"Distance", "Spring", "Pin", "Hinge"}).Tooltip("Movement style")
     .AddString("connectedBody", &Joint3D::connectedBody).Tooltip("Name of the other body (empty = world anchor)")
     .AddVec3("anchor", &Joint3D::anchor).Range(-kBig, kBig, 0.1).Tooltip("World anchor point when there's no connected body")
     .AddFloat("distance", &Joint3D::distance).Range(0, kBig, 0.05).Tooltip("Rest length (Distance / Spring)")
     .AddBool("autoConfigure", &Joint3D::autoConfigure).Tooltip("Set `distance`/offset from the start separation")
     .AddFloat("spring", &Joint3D::spring).Range(0, kBig, 0.1).Tooltip("Spring stiffness")
     .AddFloat("damper", &Joint3D::damper).Range(0, 1000, 0.1).Tooltip("Spring damping")
     .AddBool("breakable", &Joint3D::breakable).Tooltip("Snap the joint if stretched past breakForce")
     .AddFloat("breakForce", &Joint3D::breakForce).Range(0, kBig, 0.1).Tooltip("Stretch (length error) at which it breaks")
     .Section("Hinge")
     .AddVec3("axis", &Joint3D::axis).Range(-kBig, kBig, 0.1).Tooltip("World hinge axis the body may spin about")
     .AddBool("useMotor", &Joint3D::useMotor).Tooltip("Drive the spin about the axis toward motorSpeed")
     .AddFloat("motorSpeed", &Joint3D::motorSpeed).Range(-10000, 10000, 0.1).Tooltip("Target angular speed about the axis (deg/s)")
     .AddFloat("maxMotorTorque", &Joint3D::maxMotorTorque).Range(0, kBig, 0.1).Tooltip("Torque the motor can apply to reach it");
    b.Commit();
}

// DayNightCycle
void RegisterDayNightCycle() {
    PropertyBuilder<DayNightCycle> b("DayNightCycle", "daynight", "Environment");
    b.AddFloat("dayLengthSeconds", &DayNightCycle::dayLengthSeconds).Range(0, kBig, 1).Tooltip("Real seconds for a full 24h day")
     .AddFloat("time", &DayNightCycle::time).Range(0, 24, 0.1).Tooltip("Current time of day, hours [0,24)")
     .AddBool("paused", &DayNightCycle::paused)
     .AddBool("controlSun", &DayNightCycle::controlSun).Tooltip("Drive the sun Light (intensity/color/ambient)")
     .AddBool("rotateSun", &DayNightCycle::rotateSun).Tooltip("Rotate the sun Light's transform across the sky")
     .AddBool("controlSky", &DayNightCycle::controlSky).Tooltip("Drive the main camera's background color")
     .AddFloat("dayIntensity", &DayNightCycle::dayIntensity).Range(0, 1000, 0.05)
     .AddFloat("nightIntensity", &DayNightCycle::nightIntensity).Range(0, 1000, 0.05)
     .AddFloat("dayAmbient", &DayNightCycle::dayAmbient).Range(0, 1, 0.01)
     .AddFloat("nightAmbient", &DayNightCycle::nightAmbient).Range(0, 1, 0.01)
     .AddColor("dayLight", &DayNightCycle::dayLight).Range(0, 1, 0.01).Tooltip("Midday white")
     .AddColor("nightLight", &DayNightCycle::nightLight).Range(0, 1, 0.01).Tooltip("Cool moonlight")
     .AddColor("skyDay", &DayNightCycle::skyDay).Range(0, 1, 0.01).Tooltip("Clear blue")
     .AddColor("skyHorizon", &DayNightCycle::skyHorizon).Range(0, 1, 0.01).Tooltip("Sunrise/sunset orange")
     .AddColor("skyNight", &DayNightCycle::skyNight).Range(0, 1, 0.01).Tooltip("Deep night");
    b.Commit();
}

// Water
void RegisterWater() {
    PropertyBuilder<Water> b("Water", "water", "Environment");
    b.AddFloat("size", &Water::size).Range(0, kBig, 0.05).Tooltip("Plane edge length (world units), centred on the object")
     .AddInt("resolution", &Water::resolution).Range(2, 256, 1).Tooltip("Grid subdivisions per side (more = smoother waves)")
     .AddFloat("waveHeight", &Water::waveHeight).Range(0, kBig, 0.05).Tooltip("Crest-to-trough amplitude")
     .AddFloat("waveLength", &Water::waveLength).Range(0.01, kBig, 0.05).Tooltip("Distance between crests (world units)")
     .AddFloat("waveSpeed", &Water::waveSpeed).Range(0, 10000, 0.1).Tooltip("How fast the waves travel")
     .Custom("color", PropertyType::Color,
             [](const Water& w) { return PropertyValue::MakeColor(Color{w.color.r, w.color.g, w.color.b, 1.0f}); },
             // the file keeps the red, green and blue; the alpha of `color` always follows `opacity`
             [](Water& w, const PropertyValue& v) { const Color c = v.AsColor(); w.color = Color{c.r, c.g, c.b, w.opacity}; return true; })
        .Range(0, 1, 0.01).Tooltip("Tint (red, green, blue). The transparency is the separate opacity property")
     .AddFloat("opacity", &Water::opacity).Range(0, 1, 0.01).Tooltip("0 clear .. 1 opaque (GPU renderers blend by this)")
     .AddFloat("reflectivity", &Water::reflectivity).Range(0, 1, 0.01).Tooltip("How much sky reflects in the surface")
     .AddFloat("specular", &Water::specular).Range(0, 100, 0.05).Tooltip("Sun-glint strength")
     .AddFloat("shininess", &Water::shininess).Range(1, 1024, 1).Tooltip("Glint tightness")
     .AddVec2("flow", &Water::flow).Range(-kBig, kBig, 0.1).Tooltip("Surface UV scroll (a current)")
     .AddAssetPath("texture", &Water::texture, "image").Tooltip("Optional water/normal-ripple texture")
     .AddFloat("depth", &Water::depth).Range(0, kBig, 0.1)
     .AddFloat("fluidDensity", &Water::fluidDensity).Range(0, kBig, 0.1)
     .AddFloat("linearDrag", &Water::linearDrag).Range(0, kBig, 0.1).Tooltip("How fast a submerged prop's velocity fades (1/s, scaled by how much of it is under)");
    b.Commit();
}

// ---- Navigation ------------------------------------------------------------

// NavMeshAgent
void RegisterNavMeshAgent() {
    PropertyBuilder<NavMeshAgent> b("NavMeshAgent", "navagent", "Navigation");
    b.AddFloat("radius", &NavMeshAgent::radius).Range(0, kBig, 0.05).Tooltip("Body radius for local avoidance")
     .AddFloat("height", &NavMeshAgent::height).Range(0, kBig, 0.05).Tooltip("Body height (informational; the bake sets the clearance)")
     .AddFloat("speed", &NavMeshAgent::speed).Range(0, 10000, 0.1).Tooltip("Top speed, units per second")
     .AddFloat("acceleration", &NavMeshAgent::acceleration).Range(0, kBig, 1).Tooltip("How quickly the velocity eases to the target (per sec; <= 0 = instant)")
     .AddFloat("angularSpeed", &NavMeshAgent::angularSpeed).Range(0, 36000, 1).Tooltip("Turn rate toward travel, degrees per second (<= 0 = snap)")
     .AddFloat("stoppingDistance", &NavMeshAgent::stoppingDistance).Range(0, kBig, 0.05).Tooltip("Done when this close to the destination")
     .AddBool("autoRepath", &NavMeshAgent::autoRepath).Tooltip("Replan every `repathInterval` while moving")
     .AddFloat("repathInterval", &NavMeshAgent::repathInterval).Range(0, 3600, 0.05).Tooltip("Seconds between path re-plans")
     .AddInt("avoidancePriority", &NavMeshAgent::avoidancePriority).Range(0, 99, 1).Tooltip("0 = others get out of its way .. 99 = it yields to everyone")
     .AddFloat("avoidanceRange", &NavMeshAgent::avoidanceRange).Range(0, kBig, 0.05).Tooltip("How far it looks for agents and obstacles to avoid (0 = off)")
     .AddBool("autoBraking", &NavMeshAgent::autoBraking).Tooltip("Slow down over the last stretch instead of stopping dead")
     .AddBool("faceMovement", &NavMeshAgent::faceMovement).Tooltip("Turn to face the direction of travel")
     .AddBool("driveAnimation", &NavMeshAgent::driveAnimation).Tooltip("Drive a sibling Character's idle / walk / run")
     .AddFloat("baseOffset", &NavMeshAgent::baseOffset).Range(-kBig, kBig, 0.05).Tooltip("The object's origin above its feet (a cube's centre: half its height)")
     .AddInt("areaMask", &NavMeshAgent::areaMask).Tooltip("Areas this agent may enter (bit n = area n). Default: all but Not Walkable");
    b.Commit();
}

// NavMeshModifier
void RegisterNavMeshModifier() {
    PropertyBuilder<NavMeshModifier> b("NavMeshModifier", "navmod", "Navigation");
    b.AddInt("area", &NavMeshModifier::area).Range(0, kNavAreaCount - 1, 1).Tooltip("Navigation area (0 Walkable, 1 Not Walkable, 2 Water, 3 Road, 4..31 custom) the colliders on this object bake as")
     .AddBool("applyToChildren", &NavMeshModifier::applyToChildren).Tooltip("Also colliders on child objects that have no modifier of their own");
    b.Commit();
}

// NavMeshLink
void RegisterNavMeshLink() {
    PropertyBuilder<NavMeshLink> b("NavMeshLink", "navlink", "Navigation");
    b.AddEnum("type", &NavMeshLink::type, {"Jump", "Drop", "Ladder"}).Tooltip("How an agent crosses: an arc, a fall, or a straight climb")
     .AddVec3("startPoint", &NavMeshLink::startPoint).Range(-kBig, kBig, 0.1).Tooltip("Where the crossing starts, in local space (must be on the baked mesh)")
     .AddVec3("endPoint", &NavMeshLink::endPoint).Range(-kBig, kBig, 0.1).Tooltip("Where the crossing ends, in local space (must be on the baked mesh)")
     .AddBool("bidirectional", &NavMeshLink::bidirectional).Tooltip("Off: only from the start point to the end point")
     .AddFloat("cost", &NavMeshLink::cost).Range(0, kBig, 0.1).Tooltip("Multiplier on the crossing distance in the path search")
     .AddInt("area", &NavMeshLink::area).Range(0, kNavAreaCount - 1, 1).Tooltip("Navigation area, checked against each agent's area mask")
     .AddFloat("speed", &NavMeshLink::speed).Range(0, kBig, 0.1).Tooltip("Crossing speed in units per second (0 = the agent's own speed)")
     .AddFloat("arcHeight", &NavMeshLink::arcHeight).Range(0, kBig, 0.1).Tooltip("Jump: how high the arc peaks");
    b.Commit();
}

// NavMeshObstacle
void RegisterNavMeshObstacle() {
    PropertyBuilder<NavMeshObstacle> b("NavMeshObstacle", "navobstacle", "Navigation");
    b.AddVec3("size", &NavMeshObstacle::size).Range(-kBig, kBig, 0.1).Tooltip("The box, in local units (scaled by the Transform)")
     .AddVec3("center", &NavMeshObstacle::center).Range(-kBig, kBig, 0.1).Tooltip("Offset from the Transform, in local units");
    b.Commit();
}

// NavMeshSurface
void RegisterNavMeshSurface() {
    PropertyBuilder<NavMeshSurface> b("NavMeshSurface", "navsurface", "Navigation");
    b.AddAssetPath("navPath", &NavMeshSurface::navPath, "file").Tooltip("The .okaynav file (written by Bake). Absolute, or relative to the scene file / project")
     .AddBool("autoBounds", &NavMeshSurface::autoBounds).Tooltip("Bake the union of the static geometry (padded), or the box below")
     .AddVec3("boundsCenter", &NavMeshSurface::boundsCenter).Range(-kBig, kBig, 0.1).Tooltip("Centre of the bake box")
     .AddVec3("boundsSize", &NavMeshSurface::boundsSize).Range(0, kBig, 0.1).Tooltip("Size of the bake box")
     .AddBool("includeDynamic", &NavMeshSurface::includeDynamic).Tooltip("Also bake bodies with a dynamic Rigidbody3D (off: only static / kinematic ones)")
     .AddInt("layerMask", &NavMeshSurface::layerMask).Tooltip("Collider layers that take part (bit n = layer n)")
     .AddBool("bakeBeforePlay", &NavMeshSurface::bakeBeforePlay).Tooltip("Editor: bake again every time Play starts");
    b.Section("Bake settings")
     .AddFloat("agentRadius", &NavMeshSurface::settings, &NavMeshBuildSettings::agentRadius).Range(0, 1000, 0.05).Tooltip("The walkable area is shrunk by this much from every wall and ledge")
     .AddFloat("agentHeight", &NavMeshSurface::settings, &NavMeshBuildSettings::agentHeight).Range(0.1, 1000, 0.05).Tooltip("Head room a floor needs to count as walkable")
     .AddFloat("maxSlope", &NavMeshSurface::settings, &NavMeshBuildSettings::maxSlope).Range(0, 89, 1).Tooltip("Steepest walkable surface, in degrees")
     .AddFloat("maxStep", &NavMeshSurface::settings, &NavMeshBuildSettings::maxStep).Range(0, 1000, 0.05).Tooltip("Tallest step or ledge an agent walks over")
     .AddFloat("cellSize", &NavMeshSurface::settings, &NavMeshBuildSettings::cellSize).Range(0.05, 100, 0.01).Tooltip("Horizontal resolution of the bake (smaller = more detail, slower)")
     .AddFloat("cellHeight", &NavMeshSurface::settings, &NavMeshBuildSettings::cellHeight).Range(0.01, 100, 0.01).Tooltip("Height tolerance: how far a polygon may bend before it is split");
    b.Commit();
}

// ---- Networking ------------------------------------------------------------

// NetworkObject
void RegisterNetworkObject() {
    PropertyBuilder<NetworkObject> b("NetworkObject", "netobject", "Networking");
    b.Custom("netId", PropertyType::String,
             [](const NetworkObject& n) { return PropertyValue::MakeString(n.netId); },
             [](NetworkObject& n, const PropertyValue& v) {
                 if (v.AsString().find('/') != std::string::npos) return false;   // the first '/' splits an id from an RPC name
                 n.netId = v.AsString();
                 return true;
             })
        .Tooltip("Shared id; blank = the object's name. It may not contain '/'")
     .AddEnum("authority", &NetworkObject::authority, {"Host", "Mine", "Manual"}).Tooltip("Who may drive this object: the host, the owner, or set by hand")
     .AddBool("owned", &NetworkObject::owned).Tooltip("Used when authority == Manual");
    b.Commit();
}

// NetworkSync
void RegisterNetworkSync() {
    PropertyBuilder<NetworkSync> b("NetworkSync", "netsync", "Networking");
    b.AddString("netId", &NetworkSync::netId).Tooltip("Shared id (blank = object name)")
     .AddEnum("authority", &NetworkSync::authority, {"Host", "Mine", "Manual"}).Tooltip("Who broadcasts this object")
     .AddBool("owned", &NetworkSync::owned).Tooltip("Used when authority == Manual")
     .AddBool("syncAnimation", &NetworkSync::syncAnimation).Tooltip("If a Character is found, replicate its anim + punches")
     .AddBool("clientClaimable", &NetworkSync::clientClaimable).Tooltip("A client may take over this object");
    b.Commit();
}

// SyncVar
void RegisterSyncVar() {
    PropertyBuilder<SyncVar> b("SyncVar", "syncvar", "Networking");
    b.AddString("name", &SyncVar::name).Tooltip("Name of the variable")
     .AddString("value", &SyncVar::value).Tooltip("Current value");
    b.Commit();
}

// NetworkPlayerSpawner
void RegisterNetworkPlayerSpawner() {
    PropertyBuilder<NetworkPlayerSpawner> b("NetworkPlayerSpawner", "npspawner", "Networking");
    b.AddString("playerTemplate", &NetworkPlayerSpawner::playerTemplate).Tooltip("Name of an inactive scene object to clone")
     .AddAssetPath("prefabFile", &NetworkPlayerSpawner::prefabFile, "prefab").Tooltip("OR a .okayprefab path (takes priority if set)")
     .AddVec3("spawnPoint", &NetworkPlayerSpawner::spawnPoint).Range(-kBig, kBig, 0.1).Tooltip("Where players appear")
     .AddFloat("spawnSpread", &NetworkPlayerSpawner::spawnSpread).Range(0, kBig, 0.1).Tooltip("Stagger remotes so they don't stack")
     .AddBool("spawnLocalPlayer", &NetworkPlayerSpawner::spawnLocalPlayer).Tooltip("Also spawn + own the local player")
     .AddKey("glyph", &NetworkPlayerSpawner::glyph).Tooltip("Avatar glyph for the manager roster");
    b.Commit();
}

// A transform-only component: it has no fields of its own, only `enabled`.
void RegisterNetworkTransform() {
    PropertyBuilder<NetworkTransform> b("NetworkTransform", "nettransform", "Networking");
    b.Commit();
}

} // namespace

void RegisterGameplayProperties() {
    RegisterPlayerController();
    RegisterPlayerView();
    RegisterFirstPersonController();
    RegisterThirdPersonController();
    RegisterCharacterController2D();
    RegisterCharacterController3D();
    RegisterTopDownController();
    RegisterTopDownController2D();
    RegisterFreeRoamController();
    RegisterClickToMoveController();
    RegisterThirdPersonShooterController();
    RegisterTopDownShooterController();
    RegisterPlatformer25DController();
    RegisterVehicleController();
    RegisterVehicleController2D();
    RegisterProjectile3D();
    RegisterSpawner();
    RegisterVolume();
    RegisterMusicPlayer();
    RegisterCollectible();
    RegisterDamageOnTouch();
    RegisterTeleporter();
    RegisterJumpPad();
    RegisterTriggerZone();
    RegisterFollowTarget2D();
    RegisterDestructible();
    RegisterDraggable();
    RegisterDropZone();
    RegisterFlashlight();
    RegisterJoint2D();
    RegisterJoint3D();
    RegisterDayNightCycle();
    RegisterWater();
    RegisterNavMeshAgent();
    RegisterNavMeshObstacle();
    RegisterNavMeshModifier();
    RegisterNavMeshLink();
    RegisterNavMeshSurface();
    RegisterNetworkObject();
    RegisterNetworkSync();
    RegisterSyncVar();
    RegisterNetworkPlayerSpawner();
    RegisterNetworkTransform();
}

} // namespace detail
} // namespace okay
