#pragma once
#include "okay/AI/BehaviorTree.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Scene/Component.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace okay {

class Scene;
class GameObject;
class NPCController;
class NavMeshAgent;
class Character;

/// Runs a behavior tree (`.okaybt`) on an NPC: ticks the tree every frame (or at
/// `tickRate`), keeps its blackboard, and moves the body through what the object
/// has: a NavMeshAgent, else an NPCController (which then takes its orders from
/// the tree instead of its own states), else the Navigation facade and the
/// Transform / Rigidbody3D. Scripts reach the blackboard with bt_set / bt_get and
/// hear Send Event tasks as on_bt_event(name).
class BehaviorTreeRunner : public Behaviour {
public:
    /// Play stays paused for this reason while a node breakpoint is holding it.
    /// The editor's pause button clears it when Play resumes.
    static constexpr const char* kBreakPause = "bt";
    /// Called when a breakpoint pauses Play (the editor turns on its pause button).
    using PauseHook = void (*)(BehaviorTreeRunner& runner, int nodeId);
    static void SetPauseHook(PauseHook hook);

    BehaviorTreeRunner();
    ~BehaviorTreeRunner() override;

    /// Before the NPC Controller and the Nav Mesh Agent (-10), so an order given
    /// this tick moves the body this frame.
    int ExecutionOrder() const override { return -20; }

    std::string treePath;        ///< the .okaybt file (project-relative, see SetAssetBaseDir)
    float tickRate = 0.0f;       ///< ticks per second (0 = every frame)
    bool  runOnStart = true;     ///< start the tree when the scene starts
    bool  debug = false;         ///< log every node that finishes
    bool  homeOverride = false;  ///< use `homePoint` instead of the start position
    Vec3  homePoint{0, 0, 0};
    /// Blackboard values that replace the tree's defaults on this NPC (key, value text).
    std::vector<std::pair<std::string, std::string>> overrides;

    /// The live values (declared from the tree when the tree starts).
    Blackboard blackboard;
    /// A Custom task without a script function: called with "enter" on its first
    /// tick, "tick" after, "abort" when stopped; returns the task's status.
    std::function<BtStatus(BehaviorTreeRunner&, const BtNode&, const char* phase)> onCustomTask;
    /// Every Send Event task also reaches this (tests, C++ games).
    std::function<void(const std::string& name)> onEvent;

    /// Use a tree built in code or loaded elsewhere.
    void SetTree(std::shared_ptr<const BehaviorTree> tree);
    /// Load `path` (or treePath when empty) through the shared cache. False with `error` when it can't be read.
    bool LoadTree(const std::string& path = std::string(), std::string* error = nullptr);
    const BehaviorTree* Tree() const { return m_tree.get(); }
    std::shared_ptr<const BehaviorTree> TreeRef() const { return m_tree; }
    const std::string& LastError() const { return m_error; }

    /// Start (or start over): the blackboard is rebuilt from the tree's defaults
    /// and the overrides, every node forgets its state.
    void Run();
    /// Stop ticking; running tasks are aborted and the body stops.
    void Stop();
    bool IsRunning() const { return m_running; }
    /// How the tree's last full run ended, and how many runs finished.
    BtStatus LastResult() const { return m_lastResult; }
    int  Completions() const { return m_completions; }
    /// Seconds the tree has been running.
    float Clock() const { return m_time; }
    Vec3 Home() const { return m_home; }
    void SetHome(const Vec3& p) { m_home = p; }

    /// What the editor's live view shows per node.
    struct NodeState {
        BtStatus status = BtStatus::Invalid;   ///< the last result (Running while on the active path)
        bool  running = false;                 ///< on the path being run right now
        unsigned ticks = 0;                    ///< ticks since it last started
        float lastTick = -1.0f;                ///< Clock() when it was last ticked (-1 = never)
        float started = 0.0f;                  ///< Clock() when it last started running
    };
    const NodeState* State(int nodeId) const;
    /// The running nodes of the root tree, root first.
    std::vector<int> ActivePath() const;
    /// Live state of a node in the root tree or in a sub-tree that is running.
    /// `treePath` is the file (resolved the same way as a runner's tree). Empty
    /// or the root file reads the root tree.
    const NodeState* StateFor(const std::string& treePath, int nodeId) const;
    /// The running nodes of that tree, root first. Empty when that tree is not the
    /// one running right now (the root, or a sub-tree a Run Sub-Tree node is in).
    std::vector<int> ActivePathFor(const std::string& treePath) const;
    /// The blackboard of the root tree, or of a running sub-tree. Null when
    /// `treePath` is a sub-tree that is not running.
    Blackboard* BoardFor(const std::string& treePath);
    const Blackboard* BoardFor(const std::string& treePath) const;
    /// The blackboard a task or script sees: the sub-tree's while one is ticking,
    /// otherwise this runner's own.
    Blackboard& ActiveBoard();
    const Blackboard& ActiveBoard() const;
    /// Resolved path of the tree this runner started (empty when it was built in code).
    const std::string& RootTreePath() const { return m_activePath; }

    /// Set while a breakpoint is holding Play. The node is on the active path.
    bool IsBreakPaused() const;
    int BreakNode() const { return m_breakNode; }
    const std::string& BreakTreePath() const { return m_breakPath; }
    /// Drop the breakpoint pause (Stop, and the editor's resume, call this).
    void ReleaseBreak();

    struct TraceStep {
        std::string path;      ///< resolved tree file (empty for a tree built in code)
        std::string treeName;
        int nodeId = -1;
        std::string title;
    };
    /// Running nodes from the root down through any sub-tree that is running.
    std::vector<TraceStep> ActiveTrace() const;

    /// Tell this object's scripts (on_bt_event) and visual scripts (On Custom Event)
    /// about `name`; `to` sends it to another object, `broadcast` to every object
    /// and the Action Lists.
    void SendEvent(const std::string& name, bool broadcast = false, GameObject* to = nullptr);

    void Start() override;
    void Update(float dt) override;
    void OnDisable() override;
    void OnDestroy() override;

    /// The project folder a relative tree path is looked for in.
    static void SetAssetBaseDir(const std::string& dir) { BehaviorTree::SetAssetBaseDir(dir); }

private:
    struct Rt : NodeState {
        int   cur = 0;                     // composite: the child being run
        std::vector<int> order;            // random selector: the children's order this run
        std::vector<BtStatus> results;     // parallel: each child's result
        int   count = 0;                   // repeat: runs done; patrol: waypoints visited this run
        float timer = 0.0f;                // wait: the duration; patrol / wander: pause end; attack: windup end
        float until = 0.0f;                // cooldown: available again at; attack: next hit at (kept across runs)
        bool  flag = false;                // patrol / wander: pausing; attack: winding up
        int   index = 0, dir = 1;          // patrol: the waypoint and the ping-pong direction (kept across runs)
        Vec3  goal{0, 0, 0};               // wander / flee: the point chosen
        bool  hasGoal = false;
        float stamp = -1.0f;               // service: last run; play animation: the time limit
        bool  entered = false;             // custom: "enter" was sent
        bool  bpSeen = false;              // this activation already paused on its breakpoint
    };
    /// A Run Sub-Tree node's own tree, blackboard and node state. Not swapped onto
    /// the runner until that node ticks, so the parent tree's state stays put.
    struct SubFrame {
        std::shared_ptr<const BehaviorTree> tree;
        std::vector<Rt> rt;
        Blackboard board;
        std::map<int, std::unique_ptr<SubFrame>> children;
        std::string path;
    };
    enum class Move { Moving, Arrived, Blocked };

    int  Index(int id) const;
    BtStatus Tick(int idx, float dt);
    BtStatus TickNode(int idx, float dt, bool first);
    BtStatus TickComposite(int idx, float dt);
    BtStatus TickDecorator(int idx, float dt);
    BtStatus TickTask(int idx, float dt, bool first);
    BtStatus TickSubtree(int nodeIndex, float dt, bool first);
    void WithFrame(SubFrame& frame, const std::function<void()>& fn);
    void ApplyKeyMap(const std::string& mapText, Blackboard& parent, Blackboard& child, bool intoChild);
    bool PathOnStack(const std::string& path) const;
    void NoteBreak(int nodeId);
    std::string TreeIdentity() const;
    const SubFrame* FindFrame(const std::map<int, std::unique_ptr<SubFrame>>& kids, const std::string& path) const;
    SubFrame* FindFrame(std::map<int, std::unique_ptr<SubFrame>>& kids, const std::string& path);
    void RunServices(int idx, float dt);
    void RunService(int idx, float elapsed);
    void Enter(int idx);
    void Finish(int idx, BtStatus s);
    void Abort(int idx);
    bool Test(const BtNode& n) const;
    bool ResolveTarget(const std::string& spec, Vec3& pos, GameObject** obj = nullptr) const;
    bool LineOfSight(const Vec3& from, GameObject* target, float eyeHeight) const;
    float HealthFraction() const;
    Character* Body() const;
    Scene* SceneOf() const;
    Vec3 Position() const;
    Vec3 Forward() const;
    void FaceToward(const Vec3& dir, float degPerSec, float dt);
    float Rand();
    BtStatus ScriptStatus(const vs::VsValue& v) const;
    BtStatus CallCustom(const BtNode& n, Rt& r, const char* phase);

    Move MoveToward(const Vec3& goal, float stop, float speed, bool run, float dt);
    void StopMoving();
    void StepMover(float dt);
    void DriveBody(bool moving, float speed, float dt);

    std::shared_ptr<const BehaviorTree> m_tree;
    std::vector<Rt> m_rt;
    std::map<int, std::unique_ptr<SubFrame>> m_children;
    std::string m_activePath;                 ///< resolved path of the tree currently ticking
    std::vector<std::string> m_runStack;      ///< trees already on this tick (recursion guard)
    int m_breakNode = -1;
    std::string m_breakPath;
    bool m_breakLatched = false;
    std::string m_error;
    bool  m_running = false;
    float m_time = 0.0f, m_acc = 0.0f;
    BtStatus m_lastResult = BtStatus::Invalid;
    int   m_completions = 0;
    Vec3  m_home{0, 0, 0};
    std::uint32_t m_seed = 1u;
    bool  m_started = false;

    // the body
    NPCController* m_npc = nullptr;
    NavMeshAgent*  m_agent = nullptr;
    float m_agentSpeed0 = 0.0f;
    bool  m_cmdActive = false;
    Vec3  m_cmdGoal{0, 0, 0};
    int   m_cmdTicks = 0;
    // the built-in mover (no agent, no NPC controller)
    bool  m_moving = false;
    Vec3  m_moveGoal{0, 0, 0};
    float m_moveStop = 0.5f, m_moveSpeed = 3.0f;
    std::vector<Vec3> m_path;
    int   m_pathIdx = 0;
    Vec3  m_pathGoal{0, 0, 0};
    float m_repath = 0.0f;
    Vec3  m_velocity{0, 0, 0};
};

} // namespace okay
