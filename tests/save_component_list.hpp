// The list of every concrete component type the engine has, as an X-macro: OKAY_SAVE_COMPONENTS(X) calls
// X(Type) once per type. test_save_coverage.cpp checks each one is saved and loaded back;
// test_save_helper_leaks.cpp checks that no type adds objects on every save-game load.
// A new component goes here (test_save_coverage fails by name when the property registry has a type this
// list does not).
#pragma once
#include <Okay.hpp>
#include "okay/Components/InventoryPlayer.hpp"
#include "okay/Components/Seat.hpp"
#include "okay/Components/VehicleSeat.hpp"
#include "okay/Components/SettingsScreen.hpp"
#include "okay/Components/UIAnimation.hpp"
#include "okay/Components/WorldSpaceUI.hpp"
#include "okay/Testing/PlayTest.hpp"

#define OKAY_SAVE_COMPONENTS(X) \
    X(ActionList) X(AimIK) X(AimOffset) X(AnimStateMachine) X(Animator) X(AntiCheatConfig) X(AudioSource) \
    X(AutoSave) X(BehaviorTreeRunner) X(BleedingStat) X(BlendShapes) X(BlockBuilder) X(BoxCollider2D) \
    X(BoxCollider3D) X(BuildMode) X(BuildPiece) X(Buildable) X(BuilderMode) X(Cable) X(Camera) X(CameraFollow) X(Canvas) \
    X(CapsuleCollider2D) X(CapsuleCollider3D) X(CarryWeightStat) X(ChainIK) X(Character) X(CharacterController2D) \
    X(CharacterController3D) X(CheckpointTrigger) X(ChestInventory) X(CinemachineBrain) X(CircleCollider2D) \
    X(ClickToMoveController) X(Collectible) X(Consumables) X(Crafting) X(CraftingMenu) X(CraftingStation) \
    X(Crosshair) X(CylinderCollider3D) X(DamageOnTouch) X(DayNightCycle) X(Decal) X(Destructible) X(DialogueRunner) \
    X(DollyCart) X(DollyPath) X(Draggable) X(DropZone) X(EdgeCollider2D) X(Equipment) X(testing::EventProbe) \
    X(EventSystem) X(FirstPersonController) X(FirstPersonHand) X(FirstPersonHands) X(Flashlight) \
    X(FollowTarget2D) X(Foliage) X(FootIK) X(FreeRoamController) X(FullBodyIK) X(GridCostLayer) X(GridInventory) X(GridInventoryUI) \
    X(GridObstacle2D) X(GridPathAgent2D) \
    X(HealthStat) X(HoldPrompt) X(HoldableObject) X(HumanoidRetarget) X(HungerStat) X(IKTarget) X(Inventory) \
    X(InventoryUI) X(ItemPickup) X(Joint2D) X(Joint3D) X(JumpPad) X(Lifetime) X(Light) X(LimbIK) X(LineRenderer) \
    X(LoadingScreen) X(LogicSource) X(LogicTarget) X(LookAtIK) X(LootSource) X(MeleeAttacker) X(MeshCollider3D) \
    X(MeshModifiers) X(MeshRenderer) X(Minimap) X(MinimapBlip) X(ModelAnimator) X(Mover) X(MusicPlayer) \
    X(NPCController) X(NavMeshAgent) X(NavMeshLink) X(NavMeshModifier) X(NavMeshObstacle) X(NavMeshSurface) X(NetInventory) X(NetValidator) \
    X(NetworkAvatarMotion) X(NetworkManager) X(NetworkObject) X(NetworkPlayerSpawner) X(NetworkSync) \
    X(NetworkTransform) X(OxygenStat) X(ParticleSystem) X(PauseMenu) X(Platformer25DController) \
    X(PlayerController) X(PlayerView) X(PoisonStat) X(PolygonCollider2D) X(PoseCapture) \
    X(ProceduralObjectHolding) X(Projectile3D) X(QuestLog) X(QuestTrigger) X(RadiationStat) X(Ragdoll) X(RangedWeapon) \
    X(Rigidbody2D) X(Rigidbody3D) X(RootMotion) X(SanityStat) X(SaveConfig) X(Saveable) X(ScriptComponent) X(Seat) \
    X(SequencePlayer) X(SettingsScreen) X(SkinnedMesh) X(SleepStat) X(Spawner) X(DebrisEmitter) X(SphereCollider3D) X(Spinner) \
    X(Spline) X(SplineFollower) X(SplineInstancer) X(SplineMesh) X(SpriteAnimator) X(SpriteRenderer) \
    X(StaminaStat) X(StatComponent) X(Stats) X(StatusEffectStat) X(SteamManager) X(StoryState) \
    X(StructureBuilder) X(SurvivalSave) X(SurvivalStats) X(SurvivalZone) X(SyncVar) X(Teleporter) \
    X(TemperatureStat) X(Terrain) X(TerrainDigger) X(TextRenderer) X(ThirdPersonController) \
    X(ThirdPersonShooterController) X(ThirstStat) X(Tilemap) X(TilemapCollider2D) X(TimedDestroy) \
    X(TopDownController) X(TopDownController2D) X(TopDownShooterController) X(TrailRenderer) X(TriggerZone) \
    X(TurnManager) X(TweenAnimation) X(UIAccordion) X(UIAnimation) X(UIBarBind) X(UIBottomSheet) X(UIBreadcrumbs) X(UIButton) X(UIColorPicker) X(UIContextMenu) \
    X(UIDamageNumbers) X(UIDateTimePicker) X(UIInventoryBind) X(UIDialogueBox) X(UIStyleVariant) X(UIDocument) X(UIDraggable) X(UIDropTarget) X(UIDropdown) X(UIFilePicker) X(UIFlexItem) X(UIFrame) X(UIImage) \
    X(UIInputField) X(UIKeybindField) X(UILayoutGroup) X(UIListView) X(UIMask) X(UIModal) X(UIPanel) \
    X(UIPagination) X(UIProgressBar) X(UIRadialProgress) X(UIRating) X(UIRawImage) X(UISafeArea) X(UIScrollView) X(UISearchBar) X(UISkeleton) X(UISlider) \
    X(UISlotGrid) X(UIStepper) X(UITabs) X(UITextBind) X(UITheme) X(UIToast) X(UIToastItem) X(UIToggle) \
    X(TouchButton) X(TouchDPad) X(VirtualJoystick) \
    X(UITooltip) X(VehicleController) X(VehicleController2D) X(VehicleSeat) X(VirtualCamera) X(VisualScriptComponent) \
    X(VoxelDigger) X(VoxelTerrain) X(Volume) X(Water) X(WeatherRig) X(WetnessStat) X(WorldSpaceUI) X(WorldStreamer) \
    X(WorldUI)
