#pragma once

namespace okay {

class GameObject;
class Transform;
class Scene;
class IRenderer;
class Collider2D;
struct Collision2D;
class Collider3D;
struct Collision3D;

/// Base class for everything attachable to a GameObject. Scripts derive from
/// this (see `Behaviour`), as do built-in pieces like Transform and Camera.
/// The lifecycle callbacks mirror Unity's MonoBehaviour message order.
class Component {
public:
    virtual ~Component() = default;

    /// The GameObject this component is attached to (set on AddComponent).
    GameObject* gameObject = nullptr;
    /// Shortcut to the owning GameObject's Transform.
    Transform*  transform  = nullptr;
    /// When false the component is skipped by Update/Render.
    bool enabled = true;

    // ---- Lifecycle (called by the Scene) -------------------------------
    /// Called once, immediately after the component is created.
    virtual void Awake() {}
    /// Called once, before the first Update, if the component is enabled.
    virtual void Start() {}
    /// Called every frame.
    virtual void Update(float /*deltaTime*/) {}
    /// Called every frame after all Update calls have run.
    virtual void LateUpdate(float /*deltaTime*/) {}
    /// Called every frame during the render pass.
    virtual void OnRender(IRenderer& /*renderer*/) {}
    /// Called when the component or its GameObject is destroyed.
    virtual void OnDestroy() {}

    // ---- 2D physics messages (dispatched by Physics2D) ----------------
    virtual void OnCollisionEnter2D(const Collision2D& /*collision*/) {}
    virtual void OnCollisionStay2D(const Collision2D& /*collision*/) {}
    virtual void OnCollisionExit2D(const Collision2D& /*collision*/) {}
    virtual void OnTriggerEnter2D(Collider2D* /*other*/) {}
    virtual void OnTriggerStay2D(Collider2D* /*other*/) {}
    virtual void OnTriggerExit2D(Collider2D* /*other*/) {}

    // ---- 3D physics messages (dispatched by Physics3D) ----------------
    virtual void OnCollisionEnter3D(const Collision3D& /*collision*/) {}
    virtual void OnCollisionStay3D(const Collision3D& /*collision*/) {}
    virtual void OnCollisionExit3D(const Collision3D& /*collision*/) {}
    virtual void OnTriggerEnter3D(Collider3D* /*other*/) {}
    virtual void OnTriggerStay3D(Collider3D* /*other*/) {}
    virtual void OnTriggerExit3D(Collider3D* /*other*/) {}

    Scene* GetScene() const;

private:
    friend class Scene;
    bool m_started = false;
};

/// Convenience aliases for user scripts. OkaySpace's scripts subclass
/// `OkaySource` the way Unity scripts subclass MonoBehaviour.
using Behaviour = Component;
using OkaySource = Component;

} // namespace okay
