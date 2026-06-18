#pragma once

namespace okay {

class GameObject;
class Transform;
class Scene;
class IRenderer;

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

    Scene* GetScene() const;

private:
    friend class Scene;
    bool m_started = false;
};

/// Convenience alias for user scripts, echoing Unity's MonoBehaviour.
using Behaviour = Component;

} // namespace okay
