#pragma once
// "Only one per object" in the editor: the ONE answer that the Add Component browser, Duplicate Component and Paste As New
// Component all use.
//
// The Add Component browser (editor/main.cpp and the *Tools / *Window files that add rows to it) offers most kinds of component
// only while the object has none of them: once an object has a Rigidbody 2D the row is gone. The kinds are the ones in the table
// below. A row is written as `item(oked::CanAddOne<Rigidbody2D>(*go), "Rigidbody2D")`, and CanAddOne refuses to compile for a
// kind that is not in the table, so a new row cannot make the browser hide a kind that Duplicate and Paste As New still copy.
// The same table answers the run-time question (IsOnePerObject / HasOnePerObjectKind) for the Inspector's menus, which only know a
// component by its registry entry. tests/test_add_component_rules.cpp reads the browser's sources and checks they agree with the
// table (every kind in the table has a row, every row's kind is in the table, no row still spells the test out by hand).
//
// Not here: kinds the browser offers again and again (scripts, Tween Animation, Net Validator, Volume ...) and the project's own
// DisallowMultipleComponent rules (okay::ComponentRules), which the menus ask as well.
// Written for the Inspector menus review of 2026-11-15; the list is the browser's, as it stood on that day.
#include <Okay.hpp>
#include <string>
#include <type_traits>
#include <typeindex>
#include <typeinfo>
#include <vector>

// Every kind the Add Component browser hides once the object has one. Keep it sorted; add a kind here when a new row uses CanAddOne.
#define OKED_ONE_PER_OBJECT_KINDS(X) \
    X(AimIK) X(AimOffset) X(AnimStateMachine) X(Animator) X(AntiCheatConfig) X(AudioSource) X(AutoSave) \
    X(BehaviorTreeRunner) X(BleedingStat) X(BlockBuilder) X(BoxCollider2D) X(BoxCollider3D) X(BuildMode) X(Buildable) \
    X(Cable) X(Camera) X(CameraFollow) X(Canvas) X(CapsuleCollider2D) X(CapsuleCollider3D) X(CarryWeightStat) \
    X(ChainIK) X(Character) X(CharacterController2D) X(CharacterController3D) X(ChestInventory) X(CinemachineBrain) \
    X(CircleCollider2D) X(ClickToMoveController) X(Collectible) X(Consumables) X(Crafting) X(CraftingMenu) \
    X(CraftingStation) X(Crosshair) X(CylinderCollider3D) X(DamageOnTouch) X(DayNightCycle) X(Decal) X(Destructible) \
    X(DialogueRunner) X(DollyCart) X(DollyPath) X(Draggable) X(DropZone) X(EdgeCollider2D) X(Equipment) \
    X(EventSystem) X(FirstPersonController) X(FirstPersonHand) X(FirstPersonHands) X(Flashlight) X(Foliage) \
    X(FollowTarget2D) X(FootIK) X(FreeRoamController) X(FullBodyIK) X(GridCostLayer) X(GridObstacle2D) \
    X(GridPathAgent2D) X(HealthStat) X(HoldPrompt) X(HoldableObject) X(HumanoidRetarget) X(HungerStat) X(IKTarget) \
    X(Inventory) X(ItemPickup) X(Joint2D) X(Joint3D) X(JumpPad) X(LODGroup) X(Lifetime) X(Light) X(LimbIK) \
    X(LineRenderer) X(LoadingScreen) X(LogicSource) X(LogicTarget) X(LookAtIK) X(LootSource) X(MeleeAttacker) \
    X(MeshCollider3D) X(MeshModifiers) X(MeshRenderer) X(Minimap) X(MinimapBlip) X(Mover) X(MusicPlayer) \
    X(NPCController) X(NavMeshAgent) X(NavMeshLink) X(NavMeshModifier) X(NavMeshObstacle) X(NavMeshSurface) \
    X(NetInventory) X(NetworkManager) X(NetworkPlayerSpawner) X(NetworkSync) X(Occluder) X(OxygenStat) \
    X(ParticleSystem) X(PauseMenu) X(Platformer25DController) X(PlayerController) X(PoisonStat) X(PolygonCollider2D) \
    X(PoseCapture) X(ProceduralObjectHolding) X(Projectile3D) X(QuestLog) X(QuestTrigger) X(RadiationStat) X(Ragdoll) \
    X(RangedWeapon) X(Rigidbody2D) X(Rigidbody3D) X(SanityStat) X(SaveConfig) X(Saveable) X(Seat) X(SequencePlayer) \
    X(SleepStat) X(Spawner) X(SphereCollider3D) X(Spinner) X(Spline) X(SplineFollower) X(SplineInstancer) \
    X(SplineMesh) X(SpriteAnimator) X(SpriteRenderer) X(StaminaStat) X(Stats) X(StatusEffectStat) X(StoryState) \
    X(StructureBuilder) X(SurvivalSave) X(SurvivalStats) X(SurvivalZone) X(Teleporter) X(TemperatureStat) \
    X(TerrainDigger) X(TextRenderer) X(ThirdPersonController) X(ThirdPersonShooterController) X(ThirstStat) \
    X(TilemapCollider2D) X(TopDownController) X(TopDownController2D) X(TopDownShooterController) X(TouchButton) \
    X(TouchDPad) X(TrailRenderer) X(TriggerZone) X(TurnManager) X(UIAccordion) X(UIAnimation) X(UIBarBind) \
    X(UIBottomSheet) X(UIBreadcrumbs) X(UIButton) X(UIColorPicker) X(UIContextMenu) X(UIDamageNumbers) \
    X(UIDateTimePicker) X(UIDialogueBox) X(UIDocument) X(UIDraggable) X(UIDropTarget) X(UIFilePicker) X(UIFlexItem) \
    X(UIFrame) X(UIImage) X(UIInventoryBind) X(UIKeybindField) X(UIListView) X(UIMask) X(UIModal) X(UIPagination) \
    X(UIPanel) X(UIProgressBar) X(UIRadialProgress) X(UIRating) X(UIRawImage) X(UISafeArea) X(UISearchBar) \
    X(UISkeleton) X(UISlider) X(UISlotGrid) X(UIStepper) X(UIStyleVariant) X(UITabs) X(UITextBind) X(UITheme) \
    X(UIToast) X(UIToggle) X(VehicleController) X(VehicleController2D) X(VehicleSeat) X(VirtualCamera) \
    X(VirtualJoystick) X(VoxelDigger) X(VoxelTerrain) X(Water) X(WetnessStat) X(WorldStreamer) X(WorldUI)

namespace oked {

namespace detail {
template <class T> struct OnePerObjectKind : std::false_type {};
#define OKED_DECLARE_ONE_PER_OBJECT(K) template <> struct OnePerObjectKind<okay::K> : std::true_type {};
OKED_ONE_PER_OBJECT_KINDS(OKED_DECLARE_ONE_PER_OBJECT)
#undef OKED_DECLARE_ONE_PER_OBJECT
}   // namespace detail

/// The Add Component browser's test for a kind it offers once: true while `go` has no component of kind T (or of a kind derived
/// from it, as GameObject::GetComponent counts). Fails to compile for a kind that is not in OKED_ONE_PER_OBJECT_KINDS.
template <class T>
inline bool CanAddOne(const okay::GameObject& go) {
    static_assert(detail::OnePerObjectKind<T>::value,
                  "this kind is not in OKED_ONE_PER_OBJECT_KINDS (editor/AddComponentRules.hpp): add it there, so Duplicate and Paste As New refuse a second one too");
    return go.GetComponent<T>() == nullptr;
}

/// One row of the table, for code that only has the run-time type of a component.
struct OnePerObjectEntry {
    std::type_index type;
    const char* name;                              ///< the C++ type name, as okay::ComponentSerializerRegistry::TypeKey gives it
    bool (*present)(const okay::GameObject&);      ///< what CanAddOne<T> tests, negated
};

/// Every kind in the table, in alphabetical order.
inline const std::vector<OnePerObjectEntry>& OnePerObjectKinds() {
    static const std::vector<OnePerObjectEntry> table = {
#define OKED_ONE_PER_OBJECT_ENTRY(K) \
        {std::type_index(typeid(okay::K)), #K, [](const okay::GameObject& g) { return g.GetComponent<okay::K>() != nullptr; }},
        OKED_ONE_PER_OBJECT_KINDS(OKED_ONE_PER_OBJECT_ENTRY)
#undef OKED_ONE_PER_OBJECT_ENTRY
    };
    return table;
}

/// True when the browser offers only one component of this exact run-time type to an object.
inline bool IsOnePerObject(std::type_index type) {
    for (const OnePerObjectEntry& e : OnePerObjectKinds()) if (e.type == type) return true;
    return false;
}

/// True when `go` already has what the browser would refuse to add a second time. False for a kind that is not in the table.
inline bool HasOnePerObjectKind(const okay::GameObject& go, std::type_index type) {
    for (const OnePerObjectEntry& e : OnePerObjectKinds()) if (e.type == type) return e.present(go);
    return false;
}

}   // namespace oked
