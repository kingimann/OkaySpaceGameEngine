#include "okay/VisualScript/VsTypes.hpp"
#include "okay/VisualScript/NodeGraph.hpp"
#include "okay/VisualScript/VsApi.hpp"
#include "okay/VisualScript/VsBuiltins.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"

#include <cctype>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

namespace okay::vs {

namespace {
struct Row { const char* type; bool execIn; const char* ins; const char* outs; };
// One row per node type: exec input?, input pin types, output pin types
// (letters: see PinType). Kept in step with the editor's node list (it checks
// this table at startup) and the runtime (tests/test_vs_types.cpp).
const Row kRows[] = {
        {"OnStart", false, "", ""},
        {"OnUpdate", false, "", ""},
        {"Branch", true, "B", ""},
        {"Sequence", true, "", ""},
        {"Once", true, "B", ""},
        {"Timer", true, "", ""},
        {"GetVar", false, "", "A"},
        {"SetVar", true, "A", ""},
        {"AddVar", true, "N", ""},
        {"Const", false, "", "A"},
        {"Add", false, "NN", "N"},
        {"Sub", false, "NN", "N"},
        {"Mul", false, "NN", "N"},
        {"Div", false, "NN", "N"},
        {"Min", false, "NN", "N"},
        {"Max", false, "NN", "N"},
        {"Compare", false, "NN", "B"},
        {"And", false, "BB", "B"},
        {"Or", false, "BB", "B"},
        {"Not", false, "B", "B"},
        {"Key", false, "", "B"},
        {"KeyDown", false, "", "B"},
        {"AxisX", false, "", "N"},
        {"AxisY", false, "", "N"},
        {"Time", false, "", "N"},
        {"DeltaTime", false, "", "N"},
        {"GetX", false, "", "N"},
        {"GetY", false, "", "N"},
        {"Translate", true, "NNN", ""},
        {"SetPosition", true, "NNN", ""},
        {"Rotate", true, "N", ""},
        {"Print", true, "T", ""},
        {"PlaySound", true, "", ""},
        {"Spawn", true, "NNN", "O"},
        {"Destroy", true, "O", ""},
        {"Mod", false, "NN", "N"},
        {"Pow", false, "NN", "N"},
        {"Abs", false, "N", "N"},
        {"Neg", false, "N", "N"},
        {"Sqrt", false, "N", "N"},
        {"Sin", false, "N", "N"},
        {"Cos", false, "N", "N"},
        {"Floor", false, "N", "N"},
        {"Round", false, "N", "N"},
        {"Sign", false, "N", "N"},
        {"Clamp", false, "NNN", "N"},
        {"Lerp", false, "NNN", "N"},
        {"Random", false, "", "N"},
        {"RandomRange", false, "NN", "N"},
        {"Xor", false, "BB", "B"},
        {"Equals", false, "AA", "B"},
        {"NotEquals", false, "AA", "B"},
        {"KeyUp", false, "", "B"},
        {"MouseX", false, "", "N"},
        {"MouseY", false, "", "N"},
        {"MakeVec3", false, "NNN", "V"},
        {"GetPosition", false, "", "V"},
        {"GetZ", false, "", "N"},
        {"SetScale", true, "NNN", ""},
        {"SetRotation", true, "N", ""},
        {"Wait", true, "", ""},
        {"LogWarn", true, "T", ""},
        {"LogError", true, "T", ""},
        {"AddForce", true, "NNN", ""},
        {"SetVelocity", true, "NNN", ""},
        {"AddImpulse", true, "NNN", ""},
        {"Approx", false, "NN", "B"},
        {"Between", false, "NNN", "B"},
        {"Chance", false, "", "B"},
        {"HasVar", false, "", "B"},
        {"Select", false, "BAA", "A"},
        {"Concat", false, "TT", "T"},
        {"VecX", false, "V", "N"},
        {"VecY", false, "V", "N"},
        {"VecZ", false, "V", "N"},
        {"VelX", false, "", "N"},
        {"VelY", false, "", "N"},
        {"VelZ", false, "", "N"},
        {"EaseIn", false, "N", "N"},
        {"EaseOut", false, "N", "N"},
        {"EaseInOut", false, "N", "N"},
        {"OnCollision", false, "", ""},
        {"OnTrigger", false, "", ""},
        {"OnClick", false, "", ""},
        {"OnAnimEvent", false, "", ""},
        {"AnimEventName", false, "", "T"},
        {"OtherName", false, "", "T"},
        {"OtherTag", false, "", "T"},
        {"Tan", false, "N", "N"},
        {"Atan2", false, "NN", "N"},
        {"Exp", false, "N", "N"},
        {"Log", false, "N", "N"},
        {"InverseLerp", false, "NNN", "N"},
        {"Remap", false, "NNNNN", "N"},
        {"SmoothStep", false, "NNN", "N"},
        {"PingPong", false, "NN", "N"},
        {"Repeat01", false, "NN", "N"},
        {"MoveTowardsValue", false, "NNN", "N"},
        {"DeltaAngle", false, "NN", "N"},
        {"Vec3Add", false, "VV", "V"},
        {"Vec3Sub", false, "VV", "V"},
        {"Vec3Scale", false, "VN", "V"},
        {"Vec3Length", false, "V", "N"},
        {"Vec3Normalize", false, "V", "V"},
        {"Vec3Dot", false, "VV", "N"},
        {"Vec3Distance", false, "VV", "N"},
        {"ToString", false, "A", "T"},
        {"ToFixed", false, "N", "T"},
        {"TextLength", false, "T", "N"},
        {"Contains", false, "TT", "B"},
        {"Upper", false, "T", "T"},
        {"Lower", false, "T", "T"},
        {"ObjectExists", false, "O", "B"},
        {"ObjectPosition", false, "O", "V"},
        {"DistanceTo", false, "O", "N"},
        {"SelfName", false, "", "T"},
        {"LookAt", true, "O", ""},
        {"MoveTowards", true, "NO", ""},
        {"SetActive", true, "BO", ""},
        {"SetText", true, "TO", ""},
        {"LoadScene", true, "", ""},
        {"MouseButton", false, "", "B"},
        {"MouseButtonDown", false, "", "B"},
        {"MouseGround", false, "", "NNNB"},
        {"Cooldown", true, "", ""},
        {"AnimTrigger", true, "", ""},
        {"AnimSetFloat", true, "N", ""},
        {"AnimSetBool", true, "B", ""},
        {"AnimState", false, "", "T"},
        {"IkLook", true, "VN", ""},
        {"IkHand", true, "VN", ""},
        {"SetWeather", true, "N", ""},
        {"SetFog", true, "BNN", ""},
        {"SetPostEffect", true, "N", ""},
        {"NavMoveTo", true, "V", ""},
        {"NavStop", true, "", ""},
        {"NavArrived", false, "", "B"},
        // Story: dialogues, story variables, quests.
        {"StartDialogue", true, "", ""},
        {"SetStoryVar", true, "A", ""},
        {"GetStoryVar", false, "", "A"},
        {"StartQuest", true, "", ""},
        {"CompleteObjective", true, "N", ""},
        {"QuestState", false, "", "T"},
        {"OnDialogueEvent", false, "", ""},
        {"OnDialogueEnd", false, "", ""},
        {"OnQuestStarted", false, "", ""},
        {"OnQuestCompleted", false, "", ""},
        {"OnQuestFailed", false, "", ""},
        {"OnObjectiveCompleted", false, "", ""},
        {"OnQuestEvent", false, "", ""},
        {"BtSet", true, "A", ""},
        {"BtGet", false, "", "A"},
        {"BtRun", true, "", ""},
        {"ForLoop", true, "NN", "N"},
        {"Repeat", true, "", ""},
        {"FlipFlop", true, "", ""},
        {"Gate", true, "B", ""},
        {"DoN", true, "B", "N"},
        {"SwitchInt", true, "N", ""},
        {"Toggle", true, "", ""},
        {"RandomInt", false, "NN", "N"},
        {"Ceil", false, "N", "N"},
        {"Frac", false, "N", "N"},
        {"OneMinus", false, "N", "N"},
        {"Saturate", false, "N", "N"},
        {"Asin", false, "N", "N"},
        {"Acos", false, "N", "N"},
        {"Deg2Rad", false, "N", "N"},
        {"Rad2Deg", false, "N", "N"},
        {"Vec3Cross", false, "VV", "V"},
        {"Vec3Lerp", false, "VVN", "V"},
        {"Vec3Negate", false, "V", "V"},
        {"ForwardVector", false, "", "V"},
        {"RightVector", false, "", "V"},
        {"UpVector", false, "", "V"},
        {"GetScale", false, "", "V"},
        {"GetRotation", false, "", "V"},
        {"SetPositionVec", true, "V", ""},
        {"MoveBy", true, "V", ""},
        {"Rotate3", true, "NNN", ""},
        {"SetRotation3", true, "NNN", ""},
        {"SetColor", true, "NNNN", ""},
        {"PlayClip", true, "", ""},
        {"PlayAction", true, "N", ""},
        {"IsInAction", false, "", "B"},
        {"Die", true, "", ""},
        {"Revive", true, "", ""},
        {"Dive", true, "", ""},
        {"Sit", true, "", ""},
        {"StandUp", true, "", ""},
        {"IsSitting", false, "", "B"},
        {"Interact", true, "", ""},
        {"SeatPrompt", false, "", "T"},
        {"HitReact", true, "B", ""},
        {"Roll", true, "", ""},
        {"Dodge", true, "", ""},
        {"Vault", true, "", ""},
        {"Mantle", true, "", ""},
        {"LedgeGrab", true, "", ""},
        {"ClimbUp", true, "", ""},
        {"LetGo", true, "", ""},
        {"IsHanging", false, "", "B"},
        {"Shimmy", true, "N", ""},
        {"SetTimeScale", true, "N", ""},
        {"SetBlendShape", true, "N", ""},
        {"GetBlendShape", false, "", "N"},
        {"Raycast", false, "NNNNN", "B"},
        {"MouseWheel", false, "", "N"},
        {"TimeScale", false, "", "N"},
        {"FrameCount", false, "", "N"},
        {"NetHost", true, "", ""},
        {"NetJoin", true, "", ""},
        {"NetDisconnect", true, "", ""},
        {"NetConnected", false, "", "B"},
        {"NetIsServer", false, "", "B"},
        {"NetIsClient", false, "", "B"},
        {"NetPeers", false, "", "N"},
        {"NetId", false, "", "N"},
        {"NetSend", true, "A", ""},
        {"NetSetVar", true, "A", ""},
        {"NetGetVar", false, "", "A"},
        {"NetChat", true, "T", ""},
        {"NetRpc", true, "A", ""},
        {"NetSpawnOwned", true, "", ""},
        {"NetDespawn", true, "T", ""},
        {"NetReady", true, "B", ""},
        {"NetStartMatch", true, "", ""},
        {"NetMatchStarted", false, "", "B"},
        {"NetAllReady", false, "", "B"},
        {"NetObjRpc", true, "A", ""},
        {"SyncVarSet", true, "A", ""},
        {"SyncVarGet", false, "", "A"},
        {"OnNetRpc", false, "", "A"},
        {"SteamName", false, "", "T"},
        {"SteamIsUnlocked", false, "", "B"},
        {"SteamUnlock", true, "", ""},
        {"SteamGetStat", false, "", "N"},
        {"SteamSetStat", true, "N", ""},
        {"SteamStore", true, "", ""},
        {"SteamLeaderboard", true, "N", ""},
        {"OnKey", false, "", ""},
        {"OnKeyUp", false, "", ""},
        {"OnTimer", false, "", ""},
        {"OnEvent", false, "", "A"},
        {"OnCollisionExit", false, "", ""},
        {"OnTriggerExit", false, "", ""},
        {"OnMouseEnter", false, "", ""},
        {"OnMouseDown", false, "", ""},
        {"OnMouseUp", false, "", ""},
        {"OnMouseOver", false, "", ""},
        {"OnCollisionStay", false, "", ""},
        {"OnLateUpdate", false, "", ""},
        {"OnFixedUpdate", false, "", ""},
        {"OnEnable", false, "", ""},
        {"OnDisable", false, "", ""},
        {"OnDestroy", false, "", ""},
        {"Action", true, "", ""},
        {"Actions", true, "", ""},
        {"Condition", true, "", ""},
        {"Delay", true, "", ""},
        {"OnMouseExit", false, "", ""},
        {"SendEvent", true, "OA", ""},
        {"BroadcastEvent", true, "A", ""},
        {"EventValue", false, "", "A"},
        {"EventName", false, "", "T"},
        {"SetObjectVar", true, "OA", ""},
        {"GetObjectVar", false, "O", "A"},
        {"SaveValue", true, "A", ""},
        {"LoadValue", false, "A", "A"},
        {"Format", false, "AAA", "T"},
        {"Localize", false, "AAA", "T"},
        {"SetLanguage", true, "T", ""},
        {"GetLanguage", false, "", "T"},
        {"OnLanguageChanged", false, "", ""},
        {"OnObjectPlaced", false, "", ""},
        {"OnItemAdded", false, "", ""},
        {"OnItemRemoved", false, "", ""},
        {"OnInventoryFull", false, "", ""},
        {"OnItemUsed", false, "", ""},
        {"OnEquip", false, "", ""},
        {"OnCraft", false, "", ""},
        {"OnObjectRemoved", false, "", ""},
        {"OnLogicSignal", false, "", ""},
        {"OnCheatDetected", false, "", ""},
        {"OnValidateRpc", false, "", ""},
        {"RejectRpc", true, "", ""},
        {"ServerBudget", true, "NN", "N"},
        {"OnValidateHit", false, "", ""},
        {"RejectHit", true, "", ""},
        {"EnterBuildMode", true, "", ""},
        {"ExitBuildMode", true, "", ""},
        {"InBuildMode", false, "", "B"},
        {"SetBuildPalette", true, "", ""},
        {"SaveCreation", true, "", "B"},
        {"LoadCreation", true, "", "B"},
        {"AddItem", true, "N", "N"},
        {"RemoveItem", true, "N", "B"},
        {"HasItem", false, "N", "B"},
        {"ItemCount", false, "", "N"},
        {"EquipItem", true, "T", "B"},
        {"CraftItem", true, "N", "B"},
        {"LootRoll", true, "", "N"},
        {"SpawnPickup", true, "NNNN", "T"},
        {"FunctionStart", false, "", ""},
        {"OnCustomEvent", false, "", ""},
        {"RaiseEvent", true, "O", "N"},
        {"CallDispatcher", true, "O", "N"},
        {"BindEvent", true, "O", ""},
        {"UnbindEvent", true, "O", ""},
        {"Return", true, "", ""},
        {"CallFunction", true, "", ""},
        {"SetLocal", true, "A", ""},
        {"GetLocal", false, "", "A"},
        // Loops, lists, scene / global variables, Variable Changed events.
        {"While", true, "B", ""},
        {"ForEach", true, "L", "AN"},
        {"ForEachMap", true, "M", "TA"},
        {"NewMap", false, "", "M"},
        {"MapSet", true, "MTA", ""},
        {"MapRemove", true, "MT", ""},
        {"MapGet", false, "MT", "AB"},
        {"MapHas", false, "MT", "B"},
        {"MapKeys", false, "M", "L"},
        {"MapValues", false, "M", "L"},
        {"SwitchString", true, "A", ""},
        {"SwitchCases", true, "N", ""},
        {"LatchGate", true, "BBB", "B"},
        {"IsValid", false, "A", "B"},
        {"CallScript", true, "A", "A"},
        {"RunGraph", true, "", ""},
        {"Break", true, "", ""},
        {"Continue", true, "", ""},
        {"NewList", false, "", "L"},
        {"ListAdd", true, "LA", ""},
        {"ListRemoveAt", true, "LN", ""},
        {"ListGet", false, "LN", "A"},
        {"ListLength", false, "L", "N"},
        {"ListContains", false, "LA", "B"},
        {"ListInsert", true, "LNA", ""},
        {"ListSet", true, "LNA", ""},
        {"ListFind", false, "LA", "NB"},
        {"ListRemoveItem", true, "LA", "B"},
        {"ListSort", true, "LB", ""},
        {"ListReverse", true, "L", ""},
        {"ListClear", true, "L", ""},
        {"ListCopy", false, "LB", "L"},
        {"MapClear", true, "M", ""},
        {"MapCount", false, "M", "N"},
        {"MapCopy", false, "MB", "M"},
        {"StructNew", false, "", "M"},
        {"StructGet", false, "M", "A"},
        {"StructSet", true, "MA", ""},
        {"MakeColor", false, "NNNN", "C"},
        {"SplitColor", false, "C", "NNNN"},
        {"AllWithTag", false, "", "L"},
        {"GetSceneVar", false, "", "A"},
        {"SetSceneVar", true, "A", ""},
        {"AddSceneVar", true, "N", ""},
        {"GetGlobalVar", false, "", "A"},
        {"SetGlobalVar", true, "A", ""},
        {"AddGlobalVar", true, "N", ""},
        {"OnVarChanged", false, "", "AA"},
        {"OnSceneVarChanged", false, "", "AA"},
        {"OnGlobalVarChanged", false, "", "AA"},
        {"Substring", false, "TNN", "T"},
        {"Replace", false, "TTT", "T"},
        {"ToNumber", false, "T", "N"},
        {"StartsWith", false, "TT", "B"},
        {"EndsWith", false, "TT", "B"},
        {"Trim", false, "T", "T"},
        {"IndexOf", false, "TT", "N"},
        {"Noise", false, "NN", "N"},
        {"Snap", false, "NN", "N"},
        {"Wrap", false, "NNN", "N"},
        {"Average", false, "NN", "N"},
        {"Distance2D", false, "NNNN", "N"},
        {"FindWithTag", false, "", "O"},
        {"CountWithTag", false, "", "N"},
        {"NearestWithTag", false, "", "O"},
        {"Teleport", true, "NNNO", ""},
        {"SetVisible", true, "B", ""},
        {"PauseGame", true, "B", ""},
        {"QuitGame", true, "", ""},
        {"ShowCursor", true, "B", ""},
        {"ChanceBranch", true, "N", ""},
        {"Counter", true, "B", "N"},
        {"EveryN", true, "N", ""},
        {"ShowUI", true, "", ""},
        {"HideUI", true, "", ""},
        {"OnTriggerStay", false, "", ""},
        {"Raycast3D", false, "VVNB", "BVVNO"},
        {"SphereCast3D", false, "VVNN", "BVVNO"},
        {"RaycastAll3D", false, "VVN", "NLO"},
        {"OverlapSphere3D", false, "VN", "NL"},
        {"OverlapBox3D", false, "VV", "NL"},
        {"ContactPoint", false, "", "V"},
        {"ContactNormal", false, "", "V"},
        {"ImpactSpeed", false, "", "N"},
        {"Self", false, "", "O"},
        {"Other", false, "", "O"},
        {"FindObject", false, "", "O"},
        {"GetName", false, "", "T"},
        {"GetTag", false, "", "T"},
        // Cutscenes (SequencePlayer).
        {"OnSequenceEvent", false, "", ""},
        {"PlaySequence", true, "", ""},
        {"StopSequence", true, "", ""},
        // Splines, lines and trails.
        {"GetSplinePoint", false, "N", "V"},
        {"SplineLength", false, "", "N"},
        {"SetLinePoints", true, "L", ""},
        {"ClearTrail", true, "", ""},
        // Music, camera, particles.
        {"MusicPlay", true, "", ""},
        {"MusicStop", true, "N", ""},
        {"MusicPause", true, "", ""},
        {"MusicResume", true, "", ""},
        {"MusicNext", true, "", ""},
        {"CameraShake", true, "N", ""},
        {"SetFov", true, "N", ""},
        {"CameraFollow", true, "TN", ""},
        {"ParticlePlay", true, "", ""},
        {"ParticleStop", true, "", ""},
        {"ParticleBurst", true, "N", ""},
        // High-level gameplay. These sit on top of the nodes above; they do not replace them.
        {"RotateTowards", true, "NO", ""},
        {"OrbitAround", true, "ONN", ""},
        {"FollowPath", true, "NL", ""},
        {"Bob", true, "NN", ""},
        {"Spin", true, "N", ""},
        {"Knockback", true, "ON", ""},
        {"SmoothFollow", true, "ON", ""},
        {"TweenPosition", true, "VN", ""},
        {"TweenRotation", true, "NN", ""},
        {"TweenScale", true, "VN", ""},
        {"TweenColor", true, "NNNNN", ""},
        {"CrossfadeClip", true, "N", ""},
        {"StopAnimation", true, "", ""},
        {"WaitAnimEnd", true, "", ""},
        {"OnInteract", false, "", ""},
        {"PickUp", true, "O", ""},
        {"Drop", true, "O", ""},
        {"DoorOpen", true, "NN", ""},
        {"DoorClose", true, "N", ""},
        {"Lever", true, "", ""},
        {"Collect", true, "N", ""},
        {"OnTagEnter", false, "", ""},
        {"OnTagStay", false, "", ""},
        {"OnTagExit", false, "", ""},
        {"Proximity", false, "ON", "B"},
        {"Damage", true, "NO", ""},
        {"Heal", true, "NO", ""},
        {"IsDead", false, "O", "B"},
        {"OnDeath", false, "", ""},
        {"OnHealthChanged", false, "", ""},
        {"Respawn", true, "O", ""},
        {"SpawnProjectile", true, "N", "O"},
        {"Shoot", true, "NN", "BO"},
        {"Ammo", true, "", "N"},
        {"Patrol", true, "NL", ""},
        {"Chase", true, "ON", ""},
        {"Flee", true, "ON", ""},
        {"Wander", true, "NN", ""},
        {"CanSee", false, "ONN", "B"},
        {"SpawnEvery", true, "N", "O"},
        {"DestroyAfter", true, "NO", ""},
        {"PoolGet", true, "", "O"},
        {"PoolRelease", true, "O", ""},
        {"ScoreAdd", true, "N", ""},
        {"ScoreSet", true, "N", ""},
        {"ScoreGet", false, "", "N"},
        {"Countdown", true, "N", ""},
        {"Win", true, "", ""},
        {"Lose", true, "", ""},
        {"RestartScene", true, "", ""},
        {"CheckpointSave", true, "", ""},
        {"CheckpointLoad", true, "", ""},
        {"SetProgress", true, "NO", ""},
        {"ShowFloatingText", true, "TO", ""},
        {"FadeScreen", true, "NO", ""},
        {"SetCameraTarget", true, "ON", ""},
        {"Zoom", true, "N", ""},
        {"SwitchCamera", true, "", ""},
        {"PlaySoundAt", true, "O", ""},
        {"StopSound", true, "O", ""},
        {"OnAction", false, "", ""},
        {"OnActionUp", false, "", ""},
        {"OnActionHeld", false, "", ""},
        {"GetMoveAxis", false, "", "V"},
        {"RandomPick", false, "L", "A"},
        {"RandomPoint", false, "VV", "V"},
        {"ShuffleList", false, "L", "L"},
        {"ListMin", false, "L", "N"},
        {"ListMax", false, "L", "N"},
        {"OnCollisionTag", false, "", ""},
        {"NotAnd", false, "BB", "B"},
        {"NotOr", false, "BB", "B"},
        {"AllTrue", false, "L", "B"},
        {"AnyTrue", false, "L", "B"},
        {"IsEven", false, "N", "B"},
        {"IsOdd", false, "N", "B"},
        {"PickByIndex", false, "NL", "A"},
        {"FirstValid", false, "AAAA", "A"},
        {"BranchInOrder", true, "BBB", ""},
        {"DoWhile", true, "B", ""},
        {"ForStep", true, "NNN", "N"},
        {"WaitUntil", true, "B", ""},
        {"WaitWhile", true, "B", ""},
        {"BecameTrue", true, "B", ""},
        {"BecameFalse", true, "B", ""},
        {"CountUntil", true, "NB", "N"},
        {"WeightedBranch", true, "NN", ""},
        {"WaitSteady", true, "BN", ""},
        {"StepThrough", true, "", ""},
        {"WaitLimit", true, "BN", ""},
        {"SetState", true, "T", ""},
        {"GetState", false, "", "T"},
        {"IsInState", false, "T", "B"},
        {"WhenStateBecomes", true, "T", ""},
        {"WhenStateLeaves", true, "T", ""},
        {"HeldLong", false, "BN", "B"},
        {"HappenedNear", false, "BBN", "B"},
        {"EveryWhile", true, "BN", ""},
        {"EaseToward", false, "NNN", "N"},
        {"BlendTime", true, "NNN", "N"},
        {"Stopwatch", true, "B", "N"},
        {"IsSlightly", false, "N", "B"},
        {"IsMostly", false, "N", "B"},
        {"IsVery", false, "N", "B"},
        {"PickBest", false, "NNNN", "N"},
        {"ShuffleBag", false, "L", "A"},
        {"ChanceEachSecond", false, "N", "B"},
        {"TryFirst", true, "", ""},
        {"AllSucceed", true, "", ""},
        {"FlipOutcome", true, "", ""},
        {"AlwaysSucceed", true, "", ""},
        {"RepeatUntilFail", true, "", ""},
        {"WaitBetween", true, "N", ""},
        {"SucceedNow", true, "", ""},
        {"FailNow", true, "", ""},
        {"CheckOutcome", true, "B", ""},
        {"InCone", false, "ON", "B"},
        {"LineOfSight", false, "O", "B"},
        {"InsideArea", false, "VV", "B"},
        {"CountNearby", false, "NT", "N"},
        {"NearestObject", false, "NT", "O"},
        {"FarthestObject", false, "NT", "O"},
        {"SortByDistance", false, "L", "L"},
        {"WhenChanged", true, "A", ""},
        {"DoUnless", true, "BB", ""},
        {"RememberValue", true, "TA", ""},
        {"RecallValue", false, "T", "A"},
        {"HealthEmpty", true, "", ""},
        {"HealthFull", true, "", ""},
        {"AddBuff", true, "NNNN", "N"},
        {"AdvanceObjective", true, "NB", "N"},
        {"AssertThis", true, "BT", ""},
        {"LogOnce", true, "AB", ""},
        {"DrawDebugLine", true, "VV", ""},
#include "okay/VisualScript/library_rows.inc"
#include "okay/VisualScript/gplib_rows.inc"
        // Tweens, springs and eases.
        {"EaseCurve", false, "N", "N"},
        {"SpringStep", true, "NNN", "NN"},
        {"TweenMove", true, "NNNNT", ""},
        {"TweenMoveSpeed", true, "NNNNT", ""},
        {"TweenKillTag", true, "B", "N"},
        {"TweenPauseTag", true, "", "N"},
        {"TweenPlayTag", true, "", "N"},
};

const std::unordered_map<std::string, NodeSignature>& Table() {
    static const std::unordered_map<std::string, NodeSignature> t = [] {
        std::unordered_map<std::string, NodeSignature> m;
        for (const Row& r : kRows) {
            NodeSignature s{r.execIn, r.ins, r.outs};
            const int tp = TargetPinFor(r.type);
            if (tp >= 0 && tp == (int)s.ins.size()) s.ins += 'O';
            m.emplace(r.type, s);
        }
        return m;
    }();
    return t;
}
} // namespace

std::vector<std::string> AllNodeTypes() {
    std::vector<std::string> v;
    for (const Row& r : kRows) v.push_back(r.type);
    for (const VsApiEntry& e : VsApiEntries()) v.push_back(e.type);
    for (const BuiltinNodeInfo& b : BuiltinNodes()) v.push_back(b.type);
    for (const char* t : {"GetProperty","SetProperty","CallMethod","GetComponent","AddComponent",
                          "FindByTag","SendMessage","Broadcast","RunSnippet"})
        v.push_back(t);
    return v;
}

const NodeSignature* SignatureFor(const std::string& type) {
    auto it = Table().find(type);
    if (it != Table().end()) return &it->second;
    // "OnKey:space" spelling of an event node.
    const auto c = type.find(':');
    if (c != std::string::npos) {
        it = Table().find(type.substr(0, c));
        if (it != Table().end()) return &it->second;
    }
    if (const VsApiEntry* e = VsApiFind(type)) return &e->sig;
    if (IsBuiltinNode(type)) return &BuiltinSignature();
    if (type == "GetProperty") { static const NodeSignature s{true, "TO", "A"}; return &s; }
    if (type == "SetProperty") { static const NodeSignature s{true, "ATO", ""}; return &s; }
    if (type == "CallMethod")  { static const NodeSignature s{true, "AAAAT", "A"}; return &s; }
    if (type == "GetComponent"){ static const NodeSignature s{true, "OT", "OB"}; return &s; }
    if (type == "AddComponent"){ static const NodeSignature s{true, "OT", ""}; return &s; }
    if (type == "FindByTag")   { static const NodeSignature s{false, "T", "O"}; return &s; }
    if (type == "SendMessage") { static const NodeSignature s{true, "TO", ""}; return &s; }
    if (type == "Broadcast")   { static const NodeSignature s{true, "T", ""}; return &s; }
    if (type == "RunSnippet")  { static const NodeSignature s{true, "T", "A"}; return &s; }
    return nullptr;
}

PinType ConstType(const std::string& argIn) {
    std::string a = argIn;
    if (a.size() >= 2 && a.front() == '"' && a.back() == '"') return PinType::Text;
    if (a == "true" || a == "false") return PinType::Bool;
    if (a.empty()) return PinType::Number;   // a blank Const is 0
    char* end = nullptr;
    std::strtod(a.c_str(), &end);
    return (end && *end == '\0') ? PinType::Number : PinType::Text;
}

PinType OutputType(const std::string& type, int pin, const std::string& arg) {
    if (type == "Const") return ConstType(arg);
    const NodeSignature* s = SignatureFor(type);
    if (!s || pin < 0 || pin >= (int)s->outs.size()) return PinType::Any;
    return (PinType)s->outs[(std::size_t)pin];
}

PinType InputType(const std::string& type, int pin) {
    const NodeSignature* s = SignatureFor(type);
    if (!s || pin < 0 || pin >= (int)s->ins.size()) return PinType::Any;
    return (PinType)s->ins[(std::size_t)pin];
}

namespace {
bool VarNodeScope(const std::string& type, const char* verb, VarScope& scope) {
    const std::string v = verb;
    if (type == v + "Var") { scope = VarScope::Object; return true; }
    if (type == v + "SceneVar") { scope = VarScope::Scene; return true; }
    if (type == v + "GlobalVar") { scope = VarScope::Global; return true; }
    return false;
}
const VarDecl* FindDeclIn(const std::vector<VarDecl>& decls, const std::string& name, VarScope scope) {
    for (const auto& d : decls) if (d.name == name && d.scope == scope && !d.legacy) return &d;
    return nullptr;
}
} // namespace

PinType DeclOutType(const std::string& type, const std::string& arg, int pin, const std::vector<VarDecl>& decls, PinType* elem) {
    VarScope sc;
    if (pin != 0 || !VarNodeScope(type, "Get", sc)) return PinType::Any;
    const VarDecl* d = FindDeclIn(decls, arg, sc);
    if (!d) return PinType::Any;
    if (elem) *elem = d->type.elem;
    return d->type.base;
}

PinType DeclInType(const std::string& type, const std::string& arg, int pin, const std::vector<VarDecl>& decls) {
    VarScope sc;
    if (pin != 0 || !VarNodeScope(type, "Set", sc)) return PinType::Any;
    const VarDecl* d = FindDeclIn(decls, arg, sc);
    return d ? d->type.base : PinType::Any;
}

bool PinCompatible(PinType from, PinType to) {
    if (from == to || from == PinType::Any || to == PinType::Any) return true;
    if (to == PinType::Text) return true;                                   // everything prints
    if ((from == PinType::Number && to == PinType::Bool) || (from == PinType::Bool && to == PinType::Number)) return true;
    if ((from == PinType::Vec3 && to == PinType::Color) || (from == PinType::Color && to == PinType::Vec3)) return true;   // RGB
    if (from == PinType::Text && to == PinType::Object) return true;        // a name
    if (from == PinType::Text && to == PinType::Color) return true;         // "#rrggbb"
    if (from == PinType::Object && to == PinType::Bool) return true;        // still exists?
    return false;
}

const char* PinTypeName(PinType t) {
    switch (t) {
        case PinType::Any:    return "Any";
        case PinType::Number: return "Number";
        case PinType::Bool:   return "Bool";
        case PinType::Text:   return "Text";
        case PinType::Vec3:   return "Vector3";
        case PinType::Object: return "Object";
        case PinType::List:   return "List";
        case PinType::Map:    return "Map";
        case PinType::Color:  return "Color";
    }
    return "?";
}

std::string ConversionHint(PinType from, PinType to) {
    if (from == PinType::Color && to == PinType::Number) return "use Split Color to get a channel";
    if (from == PinType::Number && to == PinType::Color) return "use Make Color";
    if (from == PinType::Vec3 && to == PinType::Number) return "use Vec X / Vec Y / Vec Z or Vec3 Length";
    if (from == PinType::Number && to == PinType::Vec3) return "use Make Vec3";
    if (from == PinType::Text && to == PinType::Number) return "use To Number";
    if (from == PinType::Text && to == PinType::Bool) return "compare it with Equals, or check Text Length";
    if (from == PinType::Object && (to == PinType::Vec3)) return "use Object Position (or Get Position with this object as Target)";
    if (from == PinType::Object && to == PinType::Number) return "use Distance To, or Get X / Y / Z with this object as Target";
    if (from == PinType::Map && to == PinType::List) return "use Map Keys or Map Values";
    if (from == PinType::List && to == PinType::Map) return "build a map with New Map and Map Set";
    if (from == PinType::List && to == PinType::Number) return "use the Count output";
    if (from == PinType::List && to == PinType::Object) return "use the Nearest output";
    if (to == PinType::Object) return "wire an object (Self, Other Object, Find Object, Spawn...) or type a name";
    return "";
}

// ---- Function signatures ------------------------------------------------------
namespace {
std::string Trim(std::string s) {
    while (!s.empty() && std::isspace((unsigned char)s.front())) s.erase(s.begin());
    while (!s.empty() && std::isspace((unsigned char)s.back())) s.pop_back();
    return s;
}
bool ValidName(const std::string& s) {
    if (s.empty() || !(std::isalpha((unsigned char)s[0]) || s[0] == '_')) return false;
    for (char c : s) if (!(std::isalnum((unsigned char)c) || c == '_')) return false;
    return true;
}
bool TypeFromName(std::string t, PinType& out) {
    for (char& c : t) c = (char)std::tolower((unsigned char)c);
    if (t.empty() || t == "any") { out = PinType::Any; return true; }
    if (t == "number" || t == "float" || t == "int" || t == "n") { out = PinType::Number; return true; }
    if (t == "bool" || t == "boolean" || t == "b") { out = PinType::Bool; return true; }
    if (t == "text" || t == "string" || t == "t") { out = PinType::Text; return true; }
    if (t == "vector3" || t == "vec3" || t == "v") { out = PinType::Vec3; return true; }
    if (t == "color" || t == "colour" || t == "c") { out = PinType::Color; return true; }
    if (t == "object" || t == "gameobject" || t == "o") { out = PinType::Object; return true; }
    if (t == "list" || t == "l") { out = PinType::List; return true; }
    if (t == "map" || t == "dictionary" || t == "m") { out = PinType::Map; return true; }
    return false;
}
bool ParsePins(const std::string& list, std::vector<FunctionSig::Pin>& out, std::string* err, const char* what) {
    std::string cur;
    auto flush = [&]() -> bool {
        std::string item = Trim(cur); cur.clear();
        if (item.empty()) return true;
        FunctionSig::Pin p;
        const auto colon = item.find(':');
        p.name = Trim(colon == std::string::npos ? item : item.substr(0, colon));
        const std::string type = colon == std::string::npos ? "" : Trim(item.substr(colon + 1));
        if (!ValidName(p.name)) { if (err) *err = std::string(what) + " name '" + p.name + "' isn't a valid name"; return false; }
        if (!TypeFromName(type, p.type)) {
            if (err) *err = "unknown type '" + type + "' (use Number, Bool, Text, Vector3, Color, Object, List, Map or Any)";
            return false;
        }
        for (const auto& q : out) if (q.name == p.name) { if (err) *err = std::string(what) + " '" + p.name + "' is listed twice"; return false; }
        out.push_back(p);
        return true;
    };
    for (char c : list) { if (c == ',') { if (!flush()) return false; } else cur += c; }
    return flush();
}
} // namespace

std::string FunctionNameOf(const std::string& text) {
    const auto open = text.find('(');
    return Trim(open == std::string::npos ? text : text.substr(0, open));
}

bool ParseFunctionSignature(const std::string& textIn, FunctionSig& out, std::string* err) {
    out = FunctionSig{};
    const std::string text = Trim(textIn);
    const auto open = text.find('('), close = text.find(')');
    out.name = FunctionNameOf(text);
    if (!ValidName(out.name)) {
        if (err) *err = out.name.empty() ? "the function has no name (write e.g. Damage(amount: Number))"
                                         : "'" + out.name + "' isn't a valid function name (letters, digits and _)";
        return false;
    }
    if (open == std::string::npos) return true;   // "Reset" = Reset()
    if (close == std::string::npos || close < open) { if (err) *err = "missing ')' after the parameters"; return false; }
    if (!ParsePins(text.substr(open + 1, close - open - 1), out.params, err, "parameter")) return false;
    std::string rest = Trim(text.substr(close + 1));
    if (rest.empty()) return true;
    if (rest.rfind("->", 0) != 0) { if (err) *err = "write the return values after '->'"; return false; }
    return ParsePins(rest.substr(2), out.returns, err, "return value");
}

// ---- The checker -------------------------------------------------------------
namespace {
// The runtime's tokenizer, so the checker reads quotes, escapes and comments the same way.
std::vector<std::string> Tokens(const std::string& line) { return TokenizeLine(line); }
std::string Unquote(std::string s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    return s;
}
bool IsEventType(const std::string& t) {
    const NodeSignature* s = SignatureFor(t);
    return s && !s->execIn && t.rfind("On", 0) == 0;
}
} // namespace

std::vector<VsIssue> ValidateGraphText(const std::string& text, const std::string& baseDir) {
    std::vector<VsIssue> issues;
    std::unique_ptr<NodeGraph> parsedGraph;
    {   // Anything the runtime itself refuses comes first.
        std::string err;
        parsedGraph = NodeGraph::Parse(text, &err);
        if (!parsedGraph) { issues.push_back({0, -1, "the graph can't be loaded: " + err}); return issues; }
    }
    const std::vector<VarDecl>& decls = parsedGraph->Decls();
    const std::vector<StructDef>& structDefs = parsedGraph->Structs();
    struct N { std::string type, arg; };
    std::map<int, N> nodes;
    struct Data { int consumer, inPin, producer, outPin; };
    std::vector<Data> datas;
    std::multimap<int, int> execOut;   // from -> to
    struct Exec { int from, pin, to; };
    std::vector<Exec> execList;
    std::set<int> entries, hasExecOut;
    std::set<std::string> exposed;   // `var name value` lines: a default the Inspector can edit
    std::map<std::pair<int, int>, std::string> typed;   // (node, pin) -> value typed into the pin
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        auto t = Tokens(line);   // drops '#' comments (outside quotes)
        if (t.empty()) continue;
        try {
            if (t[0] == "node" && t.size() >= 3) {
                N n{t[2], ""};
                for (std::size_t i = 3; i < t.size(); ++i) n.arg += (i > 3 ? " " : "") + t[i];
                if (n.type != "Const") n.arg = Unquote(n.arg);
                nodes[std::stoi(t[1])] = n;
            } else if (t[0] == "data" && t.size() >= 5) {
                datas.push_back({std::stoi(t[1]), std::stoi(t[2]), std::stoi(t[3]), std::stoi(t[4])});
            } else if (t[0] == "exec" && t.size() >= 4) {
                execOut.emplace(std::stoi(t[1]), std::stoi(t[3]));
                execList.push_back({std::stoi(t[1]), std::stoi(t[2]), std::stoi(t[3])});
                hasExecOut.insert(std::stoi(t[1]));
            } else if (t[0] == "entry" && t.size() >= 3) {
                entries.insert(std::stoi(t[2]));
            } else if (t[0] == "default" && t.size() >= 4) {
                std::string v;
                for (std::size_t i = 3; i < t.size(); ++i) v += (i > 3 ? " " : "") + t[i];
                typed[{std::stoi(t[1]), std::stoi(t[2])}] = Unquote(v);
            } else if ((t[0] == "var" || t[0] == "decl") && t.size() >= 2) {
                exposed.insert(Unquote(t[1]));
            }
        } catch (...) {}
    }
    for (const auto& kv : nodes) if (kv.second.type == "OnCustomEvent") entries.insert(kv.first);
    auto title = [&](int id) {
        auto it = nodes.find(id);
        return it == nodes.end() ? std::string("?") : "'" + it->second.type + "' (#" + std::to_string(id) + ")";
    };
    const std::vector<EventDecl>& evDecls = parsedGraph->Events();
    auto isEventNode = [](const std::string& t) { return t == "OnCustomEvent" || t == "RaiseEvent" || t == "CallDispatcher"; };
    auto evNameOf = [&](const N& n) { return FunctionNameOf(n.arg); };
    auto evOf = [&](const N& n) -> const EventDecl* { return isEventNode(n.type) ? FindEventDecl(evDecls, evNameOf(n)) : nullptr; };

    // 0) Functions: each Function node's signature (its pins come from it).
    std::map<std::string, FunctionSig> fns;
    std::map<std::string, int> fnNode;
    for (const auto& kv : nodes) {
        if (kv.second.type != "FunctionStart") continue;
        FunctionSig sig; std::string err;
        if (!ParseFunctionSignature(kv.second.arg, sig, &err)) { issues.push_back({0, kv.first, "bad function signature: " + err}); continue; }
        if (fns.count(sig.name)) { issues.push_back({0, kv.first, "there's already a function named '" + sig.name + "'"}); continue; }
        fns[sig.name] = sig; fnNode[sig.name] = kv.first;
    }
    auto fnOf = [&](const N& n) -> const FunctionSig* {
        auto it = fns.find(FunctionNameOf(n.arg));
        return it != fns.end() ? &it->second : nullptr;
    };
    std::map<std::pair<int, int>, std::pair<int, int>> srcOf;   // (consumer, in pin) -> (producer, out pin)
    for (const Data& d : datas) srcOf[{d.consumer, d.inPin}] = {d.producer, d.outPin};
    // The element type of the list / map a node's output pin holds, when the graph says (a typed
    // variable, a typed New List, a copy of one...). Any = unknown.
    std::function<PinType(int, int, int)> elemOf = [&](int id, int pin, int depth) -> PinType {
        auto it = nodes.find(id);
        if (it == nodes.end() || depth > 8) return PinType::Any;
        const N& n = it->second;
        PinType e = PinType::Any;
        if (DeclOutType(n.type, n.arg, pin, decls, &e) != PinType::Any) return e;
        auto in0 = srcOf.find({id, 0});
        if (n.type == "NewList" || n.type == "NewMap") {
            VarType vt;
            if (!n.arg.empty() && ParseVarType(n.arg, structDefs, vt) && !vt.IsStruct()) return vt.base;
            return PinType::Any;
        }
        if ((n.type == "ListCopy" || n.type == "MapCopy" || n.type == "MapValues" || n.type == "ShuffleList") && in0 != srcOf.end())
            return elemOf(in0->second.first, in0->second.second, depth + 1);
        if (n.type == "MapKeys") return PinType::Text;
        return PinType::Any;
    };
    auto srcElem = [&](int id, int inPin) {
        auto s2 = srcOf.find({id, inPin});
        return s2 == srcOf.end() ? PinType::Any : elemOf(s2->second.first, s2->second.second, 0);
    };
    // The struct (declared type) a node's output holds, for Struct Get / Set field types.
    std::function<const StructDef*(int, int, int)> structOfOut = [&](int id, int pin, int depth) -> const StructDef* {
        auto it = nodes.find(id);
        if (it == nodes.end() || depth > 8) return nullptr;
        const N& n = it->second;
        VarScope sc;
        for (const char* verb : {"Get"}) {
            if (VarNodeScope(n.type, verb, sc)) {
                for (const auto& d : decls) if (d.name == n.arg && d.scope == sc && d.type.IsStruct()) return FindStruct(structDefs, d.type.structName);
            }
        }
        if (n.type == "StructNew") return FindStruct(structDefs, n.arg);
        (void)pin;
        return nullptr;
    };
    auto fieldType = [&](int id, int inPin0) -> PinType {   // the struct on `id`'s input 0: its field `arg`
        auto s2 = srcOf.find({id, inPin0});
        if (s2 == srcOf.end()) return PinType::Any;
        const StructDef* sd = structOfOut(s2->second.first, s2->second.second, 0);
        if (!sd) return PinType::Any;
        for (const auto& f : sd->fields) if (f.name == nodes[id].arg) return f.type.base;
        return PinType::Any;
    };
    auto outType = [&](int id, int pin) {
        const N& n = nodes[id];
        if (const PinType dt = DeclOutType(n.type, n.arg, pin, decls); dt != PinType::Any) return dt;
        if (n.type == "StructNew") return PinType::Map;
        if (n.type == "StructGet" && pin == 0) return fieldType(id, 0);
        if (pin == 0 && (n.type == "ListGet" || n.type == "ForEach")) {
            const PinType e = srcElem(id, 0);
            if (e != PinType::Any) return e;
        }
        if (n.type == "ForEachMap" && pin == 1) { const PinType e = srcElem(id, 0); if (e != PinType::Any) return e; }
        if (n.type == "MapGet" && pin == 0) { const PinType e = srcElem(id, 0); if (e != PinType::Any) return e; }
        if (n.type == "FunctionStart" || n.type == "CallFunction") {
            const FunctionSig* f = fnOf(n);
            const auto& pins = !f ? std::vector<FunctionSig::Pin>{} : n.type == "FunctionStart" ? f->params : f->returns;
            return pin >= 0 && pin < (int)pins.size() ? pins[(std::size_t)pin].type : PinType::Any;
        }
        if (n.type == "OnCustomEvent") {
            const EventDecl* e = evOf(n);
            return e && pin >= 0 && pin < (int)e->params.size() ? e->params[(std::size_t)pin].type.base : PinType::Any;
        }
        return OutputType(n.type, pin, n.arg);
    };
    auto inType = [&](int id, int pin) {
        const N& n = nodes[id];
        if (const PinType dt = DeclInType(n.type, n.arg, pin, decls); dt != PinType::Any) return dt;
        if (n.type == "StructSet" && pin == 1) { const PinType f = fieldType(id, 0); if (f != PinType::Any) return f; }
        {   // an item going into a typed list / map
            int listPin = -1;
            if (n.type == "ListAdd" || n.type == "ListContains" || n.type == "ListFind" || n.type == "ListRemoveItem") listPin = 1;
            else if (n.type == "ListInsert" || n.type == "ListSet") listPin = 2;
            else if (n.type == "MapSet") listPin = 2;
            if (pin == listPin) { const PinType e = srcElem(id, 0); if (e != PinType::Any) return e; }
        }
        if (n.type == "Return" || n.type == "CallFunction") {
            const FunctionSig* f = fnOf(n);
            const auto& pins = !f ? std::vector<FunctionSig::Pin>{} : n.type == "Return" ? f->returns : f->params;
            return pin >= 0 && pin < (int)pins.size() ? pins[(std::size_t)pin].type : PinType::Any;
        }
        if ((n.type == "RaiseEvent" || n.type == "CallDispatcher") && pin >= 1) {
            const EventDecl* e = evOf(n);
            return e && pin - 1 < (int)e->params.size() ? e->params[(std::size_t)pin - 1].type.base : PinType::Any;
        }
        return InputType(n.type, pin);
    };
    // Wires to parameters / return values a function doesn't have (any more).
    for (const Data& d : datas) {
        auto c = nodes.find(d.consumer), p = nodes.find(d.producer);
        if (c == nodes.end() || p == nodes.end()) continue;
        if (p->second.type == "FunctionStart" || p->second.type == "CallFunction") {
            const FunctionSig* f = fnOf(p->second);
            const std::size_t n = !f ? 0 : p->second.type == "FunctionStart" ? f->params.size() : f->returns.size();
            if (f && d.outPin >= (int)n)
                issues.push_back({0, d.consumer, "it's wired to " + std::string(p->second.type == "FunctionStart" ? "parameter" : "return value") +
                                                 " #" + std::to_string(d.outPin + 1) + " of '" + f->name + "', which it doesn't have (any more)"});
        }
        if (c->second.type == "Return" || c->second.type == "CallFunction") {
            const FunctionSig* f = fnOf(c->second);
            const std::size_t n = !f ? 0 : c->second.type == "Return" ? f->returns.size() : f->params.size();
            if (f && d.inPin >= (int)n)
                issues.push_back({0, d.consumer, "a wire goes into " + std::string(c->second.type == "Return" ? "return value" : "parameter") +
                                                 " #" + std::to_string(d.inPin + 1) + " of '" + f->name + "', which it doesn't have (any more)"});
        }
    }
    for (const auto& kv : nodes)
        if (kv.second.type == "CallFunction" || kv.second.type == "Return") {
            const std::string f = FunctionNameOf(kv.second.arg);
            if (f.empty()) issues.push_back({0, kv.first, "no function is picked"});
            else if (!fns.count(f)) issues.push_back({0, kv.first, "there's no function named '" + f + "' in this graph"});
        }

    // 0b) Custom events and dispatchers: declared names, parameter pins, bind targets.
    auto wired = [&](int id, int pin) {
        for (const Data& d : datas) if (d.consumer == id && d.inPin == pin) return true;
        return typed.count({id, pin}) != 0;
    };
    {
        auto plural = [](std::size_t n) { return std::to_string(n) + " parameter" + (n == 1 ? "" : "s"); };
        for (const Data& d : datas) {   // wires to parameters an event doesn't have (any more)
            auto c = nodes.find(d.consumer), p = nodes.find(d.producer);
            if (c == nodes.end() || p == nodes.end()) continue;
            if (p->second.type == "OnCustomEvent")
                if (const EventDecl* e = evOf(p->second); e && d.outPin >= (int)e->params.size())
                    issues.push_back({0, d.consumer, "it's wired to parameter #" + std::to_string(d.outPin + 1) + " of event '" + e->name + "', which it doesn't have (any more)"});
            if (c->second.type == "RaiseEvent" || c->second.type == "CallDispatcher")
                if (const EventDecl* e = evOf(c->second); e && d.inPin >= 1 && d.inPin - 1 >= (int)e->params.size())
                    issues.push_back({0, d.consumer, "a wire goes into parameter #" + std::to_string(d.inPin) + " of '" + e->name + "', which it doesn't have (any more)", d.inPin, 0});
        }
        std::map<std::string, std::set<int>> handlerNodes;   // event name -> its On Event nodes
        for (const auto& kv : nodes) if (kv.second.type == "OnCustomEvent") handlerNodes[evNameOf(kv.second)].insert(kv.first);
        for (const auto& kv : nodes) {
            const N& n = kv.second;
            if (isEventNode(n.type)) {
                const std::string nm = evNameOf(n);
                const EventDecl* e = FindEventDecl(evDecls, nm);
                const bool wantDisp = n.type == "CallDispatcher";
                if (nm.empty()) issues.push_back({0, kv.first, std::string("no ") + (wantDisp ? "dispatcher" : "event") + " is picked"});
                else if (!e) issues.push_back({0, kv.first, std::string("there's no ") + (wantDisp ? "dispatcher" : "event") + " named '" + nm +
                                                            "' declared in this graph (add it in the Events list)"});
                else if (e->dispatcher && n.type == "OnCustomEvent")
                    issues.push_back({0, kv.first, "'" + nm + "' is a dispatcher: other objects bind to it, they don't handle it with On Event. Declare an event, or Bind a handler to the dispatcher"});
                else if (e->dispatcher && n.type == "RaiseEvent")
                    issues.push_back({0, kv.first, "'" + nm + "' is a dispatcher: use Call Dispatcher"});
                else if (!e->dispatcher && n.type == "CallDispatcher")
                    issues.push_back({0, kv.first, "'" + nm + "' is an event: use Raise Event"});
                else if (n.type == "RaiseEvent" && !wired(kv.first, 0) && !handlerNodes.count(nm))
                    issues.push_back({1, kv.first, "this raises '" + nm + "' on its own object, but nothing in this graph has an On Event node for it"});
            } else if (n.type == "BindEvent" || n.type == "UnbindEvent") {
                std::string disp, handler;
                ParseBindText(n.arg, disp, handler);
                const EventDecl* de = FindEventDecl(evDecls, disp);
                if (disp.empty()) { issues.push_back({0, kv.first, "no dispatcher is picked"}); continue; }
                if (!de) { issues.push_back({0, kv.first, "there's no dispatcher named '" + disp + "' declared in this graph (add it in the Events list)"}); continue; }
                if (!de->dispatcher) { issues.push_back({0, kv.first, "'" + disp + "' is an event, not a dispatcher: Raise Event runs handlers directly, only a dispatcher can be bound"}); continue; }
                if (handler.empty()) { issues.push_back({0, kv.first, "no handler is picked: choose a custom event or a function of this graph"}); continue; }
                if (n.type == "UnbindEvent") continue;
                if (const EventDecl* he = FindEventDecl(evDecls, handler)) {
                    std::string why;
                    if (he->dispatcher) issues.push_back({0, kv.first, "'" + handler + "' is a dispatcher, not a handler: pick a custom event or a function"});
                    else if (!EventSigCompatible(*de, *he, &why))
                        issues.push_back({0, kv.first, "handler event '" + handler + "' doesn't fit dispatcher '" + disp + "': " + why});
                    else if (!handlerNodes.count(handler))
                        issues.push_back({1, kv.first, "event '" + handler + "' has no On Event node in this graph, so the bound handler does nothing"});
                } else if (auto f = fns.find(handler); f != fns.end()) {
                    if (f->second.params.size() != de->params.size())
                        issues.push_back({0, kv.first, "function '" + handler + "' takes " + plural(f->second.params.size()) + " but dispatcher '" + disp + "' sends " + plural(de->params.size())});
                    else for (std::size_t i = 0; i < de->params.size(); ++i) {
                        const PinType want = f->second.params[i].type;
                        if (want != PinType::Any && !PinCompatible(de->params[i].type.base, want))
                            issues.push_back({0, kv.first, "parameter '" + f->second.params[i].name + "' of function '" + handler + "' is a " + PinTypeName(want) +
                                                           " but dispatcher '" + disp + "' sends a " + VarTypeText(de->params[i].type)});
                    }
                } else issues.push_back({0, kv.first, "'" + handler + "' is neither an event nor a function declared in this graph"});
            }
        }
    }

    // 1) Wires whose value can't mean what the input expects.
    for (const Data& d : datas) {
        auto c = nodes.find(d.consumer), p = nodes.find(d.producer);
        if (c == nodes.end() || p == nodes.end()) continue;
        const PinType from = outType(d.producer, d.outPin);
        const PinType to = inType(d.consumer, d.inPin);
        if (PinCompatible(from, to)) continue;
        std::string m = "a " + std::string(PinTypeName(from)) + " from " + title(d.producer) +
                        " is wired into an input that needs a " + PinTypeName(to);
        const std::string hint = ConversionHint(from, to);
        if (!hint.empty()) m += " - " + hint;
        issues.push_back({0, d.consumer, m, d.inPin, 0});
    }

    // 2) Exec nodes no event (or function) reaches never run.
    auto reach = [&](std::vector<int> stack) {
        std::set<int> seen;
        while (!stack.empty()) {
            int id = stack.back(); stack.pop_back();
            if (!seen.insert(id).second) continue;
            auto range = execOut.equal_range(id);
            for (auto it = range.first; it != range.second; ++it) stack.push_back(it->second);
        }
        return seen;
    };
    const std::set<int> fromEvents = reach(std::vector<int>(entries.begin(), entries.end()));
    std::map<std::string, std::set<int>> inFn;   // function name -> exec nodes of its body
    for (const auto& kv : fnNode) inFn[kv.first] = reach({kv.second});
    std::set<int> reached = fromEvents;
    for (const auto& kv : inFn) reached.insert(kv.second.begin(), kv.second.end());
    for (const auto& kv : nodes) {
        const NodeSignature* s = SignatureFor(kv.second.type);
        if (s && s->execIn && !reached.count(kv.first))
            issues.push_back({1, kv.first, "nothing runs this node: no event's chain leads to it"});
        if (IsEventType(kv.second.type) && !hasExecOut.count(kv.first))
            issues.push_back({1, kv.first, "this event does nothing: nothing is wired to it"});
    }

    // 2b) Function rules: where Return / locals / pauses may be, and whether it's called.
    std::set<std::string> called;
    for (const auto& kv : nodes) {
        if (kv.second.type == "CallFunction") called.insert(FunctionNameOf(kv.second.arg));
        if (kv.second.type == "BindEvent") { std::string d, h; ParseBindText(kv.second.arg, d, h); called.insert(h); }   // a bound function is called by its dispatcher
    }
    for (const auto& kv : fnNode) {
        if (!called.count(kv.first)) issues.push_back({1, kv.second, "function '" + kv.first + "' is never called"});
        if (!hasExecOut.count(kv.second)) issues.push_back({1, kv.second, "function '" + kv.first + "' does nothing: nothing is wired to it"});
    }
    for (const auto& kv : nodes) {
        const std::string& t = kv.second.type;
        std::string owner;   // the function whose body runs this node
        for (const auto& f : inFn) if (f.second.count(kv.first)) { owner = f.first; break; }
        if (t == "Return") {
            if (owner.empty() && fromEvents.count(kv.first))
                issues.push_back({0, kv.first, "Return is outside a function (an event's chain reaches it)"});
            else if (!owner.empty() && FunctionNameOf(kv.second.arg) != owner)
                issues.push_back({0, kv.first, "this Return is in function '" + owner + "' but returns from '" + FunctionNameOf(kv.second.arg) + "'"});
        }
        if (t == "SetLocal" && owner.empty() && fromEvents.count(kv.first))
            issues.push_back({0, kv.first, "Set Local only works inside a function - use Set Var for graph variables"});
    }
    {   // 2c) Break / Continue act on the loop whose Body runs them.
        auto isLoop = [&](int id) {
            auto it = nodes.find(id);
            if (it == nodes.end()) return false;
            const std::string& t = it->second.type;
            return t == "ForLoop" || t == "Repeat" || t == "While" || t == "ForEach" || t == "ForEachMap";
        };
        std::vector<int> bodies;
        std::set<int> hasBody;
        for (const Exec& e : execList)
            if (e.pin == 0 && isLoop(e.from)) { bodies.push_back(e.to); hasBody.insert(e.from); }
        const std::set<int> inLoop = reach(bodies);
        for (const auto& kv : nodes) {
            const std::string& t = kv.second.type;
            if ((t == "Break" || t == "Continue") && reached.count(kv.first) && !inLoop.count(kv.first))
                issues.push_back({0, kv.first, t + " isn't inside the Body of a loop (For Loop, Repeat, While, For Each, For Each Map), "
                                                   "so it does nothing"});
            if ((t == "While" || t == "ForEach" || t == "ForEachMap") && reached.count(kv.first) && !hasBody.count(kv.first))
                issues.push_back({1, kv.first, "nothing is wired to Body, so the loop does nothing" +
                                               std::string(t == "While" ? " (or never ends, while Cond is true)" : "")});
        }
    }
    {   // Data nodes take the context of the exec nodes that read them.
        std::multimap<int, int> producers;   // consumer -> producer
        for (const Data& d : datas) producers.emplace(d.consumer, d.producer);
        auto readers = [&](const std::set<int>& execNodes) {
            std::set<int> seen; std::vector<int> st(execNodes.begin(), execNodes.end());
            while (!st.empty()) {
                int id = st.back(); st.pop_back();
                auto r = producers.equal_range(id);
                for (auto it = r.first; it != r.second; ++it) if (seen.insert(it->second).second) st.push_back(it->second);
            }
            return seen;
        };
        const std::set<int> eventData = readers(fromEvents);
        for (const auto& kv : nodes) {
            if (kv.second.type == "GetLocal" && eventData.count(kv.first))
                issues.push_back({0, kv.first, "Get Local only works inside a function (an event's chain reads it) - use Get Var"});
            if (kv.second.type == "FunctionStart" && eventData.count(kv.first)) {
                const FunctionSig* f = fnOf(kv.second);
                issues.push_back({0, kv.first, "a parameter of function '" + (f ? f->name : std::string("?")) +
                                               "' is read by an event's chain, outside the function (it reads 0)"});
            }
        }
        {   // An event's parameters only have values inside that event's handlers.
            std::map<std::string, std::set<int>> handlerReach;
            for (const auto& kv : nodes)
                if (kv.second.type == "OnCustomEvent") {
                    const std::set<int> r = reach({kv.first});
                    handlerReach[evNameOf(kv.second)].insert(r.begin(), r.end());
                }
            std::set<int> flagged;
            for (const auto& kv : handlerReach) {
                for (int e : fromEvents) {
                    if (kv.second.count(e)) continue;
                    for (int rd : readers({e})) {
                        auto it = nodes.find(rd);
                        if (it != nodes.end() && it->second.type == "OnCustomEvent" && evNameOf(it->second) == kv.first && flagged.insert(rd).second)
                            issues.push_back({0, rd, "a parameter of event '" + kv.first + "' is read by a chain that isn't one of its handlers (it reads 0)"});
                    }
                }
            }
        }
        for (const auto& f : inFn) {
            const std::set<int> fd = readers(f.second);
            for (const auto& g2 : fnNode)
                if (g2.first != f.first && fd.count(g2.second))
                    issues.push_back({0, g2.second, "a parameter of function '" + g2.first + "' is read inside function '" +
                                                    f.first + "', where it has no value"});
        }
    }

    // 3) Variables read but never written by this graph. A `var` line is a
    // default (shown in the Inspector), so reading it is not a missing write.
    std::set<std::string> written = exposed;
    for (const auto& kv : nodes)
        if (kv.second.type == "SetVar" || kv.second.type == "AddVar" || kv.second.type == "Toggle")
            written.insert(kv.second.arg.empty() ? "x" : kv.second.arg);
    for (const auto& kv : nodes)
        if (kv.second.type == "GetVar") {
            const std::string v = kv.second.arg.empty() ? "x" : kv.second.arg;
            if (!written.count(v))
                issues.push_back({1, kv.first, "variable '" + v + "' is read but this graph never sets it "
                                               "(it reads 0 unless another graph or a script sets it)"});
        }

    // 4) Key names that would silently mean a different key.
    for (const auto& kv : nodes) {
        const std::string& t = kv.second.type;
        if (t == "OnKey" || t == "OnKeyUp" || t == "Key" || t == "KeyDown" || t == "KeyUp") {
            const std::string k = kv.second.arg.empty() ? "space" : kv.second.arg;
            if (!Input::IsKnownKeyName(k))
                issues.push_back({0, kv.first, "'" + k + "' isn't a key name OkaySpace knows - it would act as '" +
                                               std::string(1, (char)std::tolower((unsigned char)k[0])) +
                                               "'. Use one letter or digit, space, enter, escape, tab, backspace, "
                                               "shift, ctrl or an arrow (up / down / left / right)"});
        }
    }

    // 4b) Engine-API inputs the binding itself checks (key names, axis names),
    // when the value is typed into the pin or comes straight from a Const.
    for (const auto& kv : nodes) {
        const VsApiEntry* e = VsApiFind(kv.second.type);
        if (!e) continue;
        for (int pin = 0; pin < (int)e->ins.size(); ++pin) {
            std::string lit;
            bool known = true;
            const Data* wire = nullptr;
            for (const Data& d : datas) if (d.consumer == kv.first && d.inPin == pin) { wire = &d; break; }
            if (wire) {
                auto p = nodes.find(wire->producer);
                if (p == nodes.end() || p->second.type != "Const") known = false;
                else lit = Unquote(p->second.arg);
            } else if (auto it = typed.find({kv.first, pin}); it != typed.end()) {
                lit = it->second;
            }
            if (!known) continue;
            const std::string why = VsApiCheckLiteral(kv.second.type, pin, lit);
            if (!why.empty()) issues.push_back({0, kv.first, "input '" + e->ins[(std::size_t)pin].name + "': " + why});
        }
    }

    // 4c) Required inputs left empty (the node says so in the editor too).
    for (const auto& kv : nodes) {
        const std::string& t = kv.second.type;
        if (t == "PlayClip" && kv.second.arg.empty())
            issues.push_back({1, kv.first, "Required: clip name. Pick one from the list, or drop a clip file."});
        if (t == "Branch" && !wired(kv.first, 0))
            issues.push_back({1, kv.first, "Required: condition. Wire a true/false value into Condition."});
        if (t == "MoveTowards" && kv.second.arg.empty() && !wired(kv.first, 1))
            issues.push_back({1, kv.first, "Required: object to move toward. Drop an object on the slot or wire one in."});
        if ((t == "LoadScene" || t == "GetVar" || t == "SetVar") && kv.second.arg.empty())
            issues.push_back({1, kv.first, "Required: " + std::string(t == "LoadScene" ? "scene" : "variable name") + "."});
    }

    // 5) Prefabs that aren't there.
    if (!baseDir.empty())
        for (const auto& kv : nodes)
            if (kv.second.type == "Spawn" || kv.second.type == "NetSpawnOwned") {
                namespace fs = std::filesystem;
                std::error_code ec;
                fs::path p(kv.second.arg);
                if (kv.second.arg.empty())
                    issues.push_back({0, kv.first, "no prefab file is set"});
                else if (!fs::exists(p, ec) && !fs::exists(fs::path(baseDir) / p, ec))
                    issues.push_back({0, kv.first, "prefab file '" + kv.second.arg + "' doesn't exist"});
            }
    return issues;
}

std::string SceneGraphIssue::Describe() const {
    std::string d = "Visual Script on '" + object + "'";
    if (!graph.empty()) d += " (" + std::filesystem::path(graph).filename().string() + ")";
    if (issue.node >= 0) d += ", node '" + nodeType + "' (#" + std::to_string(issue.node) + ")";
    return d + ": " + issue.message;
}

namespace {
/// One graph of a scene, parsed for the cross-graph event rules.
struct SceneGraphInfo {
    std::string object, path;
    std::vector<EventDecl> events;
    struct Use { int id; std::string type, arg, targetName; bool targetWired; };
    std::vector<Use> uses;   // Raise Event / Call Dispatcher / Bind / Unbind nodes
};

void CheckSceneEvents(const Scene& scene, std::vector<SceneGraphIssue>& out) {
    std::vector<SceneGraphInfo> infos;
    for (const auto& up : scene.Objects()) {
        if (!up) continue;
        for (VisualScriptComponent* vc : up->GetComponents<VisualScriptComponent>()) {
            const std::string& src = vc->Source();
            if (src.empty()) continue;
            auto g = NodeGraph::Parse(src);
            if (!g) continue;
            SceneGraphInfo info; info.object = up->name; info.path = vc->Path(); info.events = g->Events();
            std::map<int, std::pair<std::string, std::string>> nodes;
            std::map<int, std::string> pin0;
            std::set<int> wired0;
            std::istringstream in(src); std::string line;
            while (std::getline(in, line)) {
                const auto t = Tokens(line);
                try {
                    if (t.size() >= 3 && t[0] == "node") {
                        std::string a; for (std::size_t i = 3; i < t.size(); ++i) a += (i > 3 ? " " : "") + t[i];
                        nodes[std::stoi(t[1])] = {t[2], Unquote(a)};
                    } else if (t.size() >= 4 && t[0] == "default" && t[2] == "0") pin0[std::stoi(t[1])] = Unquote(t[3]);
                    else if (t.size() >= 5 && t[0] == "data" && t[2] == "0") wired0.insert(std::stoi(t[1]));
                } catch (...) {}
            }
            for (const auto& kv : nodes) {
                const std::string& ty = kv.second.first;
                if (ty != "RaiseEvent" && ty != "CallDispatcher" && ty != "BindEvent" && ty != "UnbindEvent") continue;
                const bool w = wired0.count(kv.first) != 0;
                auto d = pin0.find(kv.first);
                info.uses.push_back({kv.first, ty, kv.second.second, !w && d != pin0.end() ? d->second : std::string(), w});
            }
            if (!info.events.empty() || !info.uses.empty()) infos.push_back(std::move(info));
        }
    }
    auto graphsOf = [&](const std::string& objName) {
        std::vector<const SceneGraphInfo*> v;
        for (const auto& i : infos) if (i.object == objName) v.push_back(&i);
        return v;
    };
    auto add = [&](const SceneGraphInfo& at, int node, const std::string& type, int severity, const std::string& msg) {
        SceneGraphIssue si; si.object = at.object; si.graph = at.path; si.nodeType = type; si.issue = {severity, node, msg};
        out.push_back(std::move(si));
    };
    // A node that names its target object: that object's graphs must declare the event.
    for (const auto& gi : infos) {
        for (const auto& u : gi.uses) {
            if (u.targetName.empty() || u.targetName == "self") continue;
            const bool isBind = u.type == "BindEvent" || u.type == "UnbindEvent";
            std::string nm = FunctionNameOf(u.arg), handler;
            if (isBind) ParseBindText(u.arg, nm, handler);
            const bool wantDisp = u.type != "RaiseEvent";
            const EventDecl* mine = FindEventDecl(gi.events, nm);
            if (!mine) continue;   // the graph-level check already says so
            const auto targets = graphsOf(u.targetName);
            bool exists = false;
            for (const auto& o : scene.Objects()) if (o && o->name == u.targetName) { exists = true; break; }
            if (!exists) { add(gi, u.id, u.type, 1, "its Target '" + u.targetName + "' isn't an object in this scene (it may be spawned later)"); continue; }
            bool found = false; std::string why;
            for (const SceneGraphInfo* t : targets) {
                const EventDecl* te = FindEventDecl(t->events, nm);
                if (!te || te->dispatcher != wantDisp) continue;
                if (EventSigCompatible(*mine, *te, &why)) { found = true; break; }
            }
            if (!found)
                add(gi, u.id, u.type, 0, "'" + u.targetName + "' " + (why.empty() ? std::string("has no Visual Script that declares ") + (wantDisp ? "dispatcher" : "event") + " '" + nm + "'"
                                                                                  : "declares " + std::string(wantDisp ? "dispatcher" : "event") + " '" + nm + "' differently than this graph (" + why + ")"));
        }
    }
    // Two graphs declaring the same name with different parameters: values are converted or refused at run time.
    std::map<std::string, const SceneGraphInfo*> firstOf;
    for (const auto& gi : infos)
        for (const auto& e : gi.events) {
            auto it = firstOf.find(e.name);
            if (it == firstOf.end()) { firstOf[e.name] = &gi; continue; }
            const EventDecl* other = FindEventDecl(it->second->events, e.name);
            std::string why;
            if (other && it->second != &gi && (other->dispatcher != e.dispatcher || !EventSigCompatible(e, *other, &why)))
                add(gi, -1, "", 1, std::string(e.dispatcher ? "dispatcher" : "event") + " '" + e.name + "' " + EventParamsText(e) + " is declared differently on '" +
                                   it->second->object + "' " + EventParamsText(*other) + (why.empty() ? " (one is an event, the other a dispatcher)" : " (" + why + ")") +
                                   ": a raise between them fits what it can and reports the rest");
        }
}
} // namespace

std::vector<SceneGraphIssue> ValidateSceneGraphs(const Scene& scene, const std::string& baseDir) {
    std::vector<SceneGraphIssue> out;
    for (const auto& up : scene.Objects()) {
        if (!up) continue;
        for (VisualScriptComponent* vc : up->GetComponents<VisualScriptComponent>()) {
            const std::string& src = vc->Source();
            if (src.empty()) continue;
            std::map<int, std::string> types;   // node id -> type, for the message
            {
                std::istringstream in(src); std::string line;
                while (std::getline(in, line)) {
                    auto t = Tokens(line);
                    if (t.size() >= 3 && t[0] == "node") { try { types[std::stoi(t[1])] = t[2]; } catch (...) {} }
                }
            }
            for (const VsIssue& is : ValidateGraphText(src, baseDir)) {
                SceneGraphIssue si;
                si.object = up->name; si.graph = vc->Path(); si.issue = is;
                auto it = types.find(is.node);
                if (it != types.end()) si.nodeType = it->second;
                out.push_back(std::move(si));
            }
        }
    }
    CheckSceneEvents(scene, out);
    return out;
}

} // namespace okay::vs
