// Private to engine/src/Script/EngineApi*.cpp: the helpers every binding topic uses, and the builder that the
// per-topic Bind<Topic> functions fill in. Each topic lives in its own EngineApi_<Topic>.cpp.
#pragma once
#include "ScriptInternal.hpp"
#include "okay/Scripting/ScriptApiRegistry.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/InputActions.hpp"
#include "okay/Input/Gestures.hpp"
#include "okay/Input/InputRuntime.hpp"
#include "okay/Components/LocalPlayerInput.hpp"
#include "okay/Core/Time.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Core/GameLoop.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Core/Localization.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Components/PlayerView.hpp"
#include "okay/Components/PlayerController.hpp"
#include "okay/Components/UIColorPicker.hpp"   // grok/ui-color
#include "okay/Components/Crosshair.hpp"          // grok/ui-hud
#include "okay/Components/UIRadialProgress.hpp"   // grok/ui-hud
#include "okay/Components/UISlotGrid.hpp"         // grok/ui-hud
#include "okay/Components/UIDropdown.hpp"      // grok/ui-controls
#include "okay/Components/UIScrollView.hpp"
#include "okay/Components/UITabs.hpp"
#include "okay/Components/UITooltip.hpp"
#include "okay/Components/UIInputField.hpp"
#include "okay/Components/UIToast.hpp"    // grok/ui-toast
#include "okay/Components/UIModal.hpp"    // grok/ui-toast
#include "okay/Components/UITheme.hpp"          // grok/ui-theme
#include "okay/Components/UISlider.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIWidgetsMore.hpp"
#include "okay/Components/UINavigation.hpp"
#include "okay/Audio/AudioMixer.hpp"
#include "okay/Components/SequencePlayer.hpp"
#include "okay/Render/Atmosphere.hpp"   // PostProcess / Fog / Weather
#include "okay/Components/NavMeshAgent.hpp"
#include "okay/Components/GridPathAgent2D.hpp"
#include "okay/Components/GridCostLayer.hpp"
#include "okay/Components/GridObstacle2D.hpp"
#include "okay/Components/DialogueRunner.hpp"   // Dialogue / Story / Quest statics
#include "okay/Components/QuestLog.hpp"
#include "okay/Components/StoryState.hpp"
#include "okay/Components/BehaviorTreeRunner.hpp"
#include "okay/Components/Spline.hpp"
#include "okay/Components/LineRenderer.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/LookAtIK.hpp"
#include "okay/Components/FootIK.hpp"
#include "okay/Components/FullBodyIK.hpp"
#include "okay/Components/AimIK.hpp"
#include "okay/Script/AnimIkTools.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Components/WindZone.hpp"   // Wind (docs/gameplay/wind.md)
#include <algorithm>
#include <cmath>
#include <set>
#include <algorithm>
#include <cmath>
#include <set>

namespace okay::script::engine_api {

inline float F(const Value& v) {
    if (auto* f = std::get_if<float>(&v)) return *f;
    if (auto* i = std::get_if<std::int32_t>(&v)) return (float)*i;
    return 0.0f;
}
inline std::int32_t I(const Value& v) { if (auto* i = std::get_if<std::int32_t>(&v)) return *i; return (std::int32_t)F(v); }
inline const std::string& S(const Value& v) { static const std::string e; auto* s = std::get_if<std::string>(&v); return s ? *s : e; }
inline Vec3 V(const Value& v) { if (auto* x = std::get_if<Vec3>(&v)) return *x; return Vec3::Zero; }

// The component of type T on the object a script call is made on, or a plain-English error (slice 2 UI parts).
template <class T> T* UiComp(const Value& self, CallContext& c, const char* nice) {
    GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
    auto* x = g->GetComponent<T>();
    if (!x) throw RuntimeError("'" + g->name + "' has no " + std::string(nice), c.loc);
    return x;
}

inline Transform* SelfTransform(const Value& self, CallContext& c) {
    return Resolve(std::get<ObjRef>(self), c.loc)->transform;
}

inline std::string CheckAxisLiteral(int, const std::string& lit) {
    return (lit == "Horizontal" || lit == "Vertical") ? "" : "\"" + lit + "\" isn't an axis. Use \"Horizontal\" or \"Vertical\"";
}
inline std::string CheckMouseButtonLiteral(int, const std::string&) { return ""; }

inline Vec2 Axes() {
    Vec2 a = Input::AxisWASD();
    Vec2 pad = Input::GamepadAxis();
    if (std::fabs(pad.x) > std::fabs(a.x)) a.x = pad.x;
    if (std::fabs(pad.y) > std::fabs(a.y)) a.y = pad.y;
    return a;
}

inline std::string LogText(const Value& v) { return ValueToString(v); }

inline Rigidbody3D* Body(const Value& self, CallContext& c) {
    GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
    auto* rb = g->GetComponent<Rigidbody3D>();
    if (!rb) throw RuntimeError("'" + g->name + "' has no Rigidbody (add one in the Inspector)", c.loc);
    return rb;
}

/// The state shared by the topics while the table is built: the table itself, the common types and the
/// helpers that make a property or a method. Topics are called in a fixed order (it fixes the type order).
struct Ctx {
    EngineApi api;
    const Type tFloat = Type::Of(TK::Float), tInt = Type::Of(TK::Int), tBool = Type::Of(TK::Bool),
               tString = Type::Of(TK::String), tVec = Type::Of(TK::Vector3), tVoid = Type::Of(TK::Void),
               tAny = Type::Of(TK::Any);
    Type tGO, tTr;
    int goSlot = -1, trSlot = -1;   // the GameObject and Transform type slots (topics copy them into local goIdx / trIdx)

    template <class G, class S2>
    static ApiProperty prop(std::string n, Type t, G get, S2 set, std::string doc) {
        ApiProperty p; p.name = std::move(n); p.type = t; p.get = get; p.set = set; p.doc = std::move(doc); return p;
    }
    template <class G>
    static ApiProperty ro(std::string n, Type t, G get, std::string doc) {
        ApiProperty p; p.name = std::move(n); p.type = t; p.get = get; p.doc = std::move(doc); return p;
    }
    template <class Fn>
    static ApiMethod method(std::string n, std::vector<Type> params, std::vector<std::string> names, Type ret, Fn fn, std::string doc) {
        ApiMethod m; m.name = std::move(n); m.params = std::move(params); m.paramNames = std::move(names);
        m.ret = ret; m.fn = fn; m.doc = std::move(doc); return m;
    }

    void Init();   // the three reserved type slots (GameObject, Transform, Vector3)
    void BindCore();
    void BindGameplay();
    void BindUiControls();
    void BindUiApp();
    void BindUiLibrary();
    void BindInput();
    void BindRuntime();
    void BindRendering();
    void BindNavigation();
    void BindStory();
    void BindSplines();
    void BindAnimation();
    void BindTrail();
    void BindMathf();
};

} // namespace okay::script::engine_api
