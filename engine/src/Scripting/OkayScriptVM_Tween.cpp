#include "OkayScriptVMInternal.hpp"

namespace okay {

void OkayScriptVM::Impl::RegisterTweenBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto go = c.go;
        // ---- Tweening (DOTween-style) ----------------------------------
        // Every tween_* call starts a tween on this object and returns its id, so it
        // can be configured and controlled afterwards (tween_loops(id, -1, "yoyo"),
        // tween_kill(id) ...). Tweens die with their object. Ease names are forgiving:
        // "out_quad", "OutQuad", "in_out_sine"...
        auto easeFromArg = [](std::vector<Value>& a, std::size_t i) -> Ease {
            if (i >= a.size() || !a[i].IsString()) return Ease::Linear;
            return Easing::FromName(a[i].AsString(), Ease::Linear);
        };
        // Apply the ease named by arg `i` to a tween: a preset name as above, or a curve
        // as text ("cubic-bezier(0.4,0,0.2,1)", "steps(4)"). Unknown text is linear.
        auto applyEase = [easeFromArg](Tweener& t, std::vector<Value>& a, std::size_t i) {
            EaseCurve c;
            if (i < a.size() && a[i].IsString() && EaseCurve::Parse(a[i].AsString(), c) && c.kind != EaseCurve::Kind::Preset)
                t.SetEaseSpec(a[i].AsString());
            else t.SetEase(easeFromArg(a, i));
        };
        auto tweens = [this]() -> TweenManager* {
            if (rt.host && rt.host->gameObject && rt.host->gameObject->scene())
                return &rt.host->gameObject->scene()->tweens();
            return nullptr;
        };
        // Optional on-complete: if arg `i` names a script function, a callback that
        // calls it (DOTween's OnComplete). Anything else yields an empty callback.
        auto fnFromArg = [this](std::vector<Value>& a, std::size_t i) -> std::function<void()> {
            if (i >= a.size() || !a[i].IsString()) return {};
            std::string fn = a[i].AsString();
            if (!rt.functions.count(fn)) return {};
            return [this, fn, alive = std::weak_ptr<int>(life)]() {
                if (alive.expired()) return;
                std::vector<Value> none; rt.Call(fn, none);
            };
        };
        // Script-facing ids for tweens (also those moved into a sequence).
        struct TweenRefs { std::unordered_map<int, std::weak_ptr<Tweener>> map; int next = 1; };
        auto refs = std::make_shared<TweenRefs>();
        auto reg = [refs](const std::shared_ptr<Tweener>& t) -> Value {
            if (!t) return Value{0.0f};
            if (refs->map.size() > 256 && (refs->next % 64) == 0)   // forget finished ones now and then
                for (auto it = refs->map.begin(); it != refs->map.end();) it = it->second.expired() ? refs->map.erase(it) : std::next(it);
            const int id = refs->next++;
            refs->map[id] = t;
            return Value{(float)id};
        };
        auto lookup = [refs](std::vector<Value>& a, std::size_t i = 0) -> std::shared_ptr<Tweener> {
            if (i >= a.size()) return nullptr;
            auto it = refs->map.find((int)std::lround(a[i].AsFloat()));
            if (it == refs->map.end()) return nullptr;
            auto t = it->second.lock();
            return t && t->IsActive() ? t : nullptr;
        };
        // Finish a new tween: ease from arg `ei` (else keep its own), on-complete from `di`.
        auto finish = [applyEase, easeFromArg, fnFromArg, reg](std::vector<Value>& a, std::shared_ptr<Tweener> t, std::size_t ei, std::size_t di) -> Value {
            if (!t) return Value{0.0f};
            if (ei < a.size() && a[ei].IsString()) applyEase(*t, a, ei);
            else if (ei != (std::size_t)-1) t->SetEase(Ease::Linear);
            if (auto f = fnFromArg(a, di)) t->OnComplete(f);
            return reg(t);
        };
        const std::size_t kNone = (std::size_t)-1;
        auto self = go;
        auto num = [](std::vector<Value>& a, std::size_t i, float def) { return i < a.size() && !a[i].IsString() ? a[i].AsFloat() : def; };
        // A script number as a count: NaN is `whenNaN`, and a huge value stops at about two billion (casting a float that
        // is out of range to int is undefined, and gave a negative count: a loop count of 1e12 became "forever").
        auto toCount = [](float v, int whenNaN) { return v != v ? whenNaN : v >= 2.0e9f ? 2000000000 : v <= -2.0e9f ? -2000000000 : (int)v; };

        b["tween_move"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 3) return Value{0.0f};
            const float z = g->transform->localPosition.z;
            return finish(a, DOTween::LocalMove(g, {a[0].AsFloat(), a[1].AsFloat(), z}, a[2].AsFloat()), 3, 4);
        };
        b["tween_move3"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 4) return Value{0.0f};
            return finish(a, DOTween::LocalMove(g, {a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()}, a[3].AsFloat()), 4, 5);
        };
        // Glide by an offset (DOTween's DOBlendableMoveBy).
        b["tween_move_by"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 3) return Value{0.0f};
            auto t = DOTween::LocalMove(g, {a[0].AsFloat(), a[1].AsFloat(), 0.0f}, a[2].AsFloat());
            t->SetRelative();
            return finish(a, t, 3, 4);
        };
        // World-space move (ignores the parent).
        b["tween_move_world"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 4) return Value{0.0f};
            return finish(a, DOTween::Move(g, {a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()}, a[3].AsFloat()), 4, 5);
        };
        b["tween_scale"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            return finish(a, DOTween::Scale(g, a[0].AsFloat(), a[1].AsFloat()), 2, 3);
        };
        b["tween_scale_xy"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 3) return Value{0.0f};
            return finish(a, DOTween::Scale(g, {a[0].AsFloat(), a[1].AsFloat(), g->transform->localScale.z}, a[2].AsFloat()), 3, 4);
        };
        // Spin by `deg` degrees about Z (full turns work).
        b["tween_rotate"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            auto t = DOTween::Rotate(g, {0, 0, a[0].AsFloat()}, a[1].AsFloat());
            t->SetRelative();
            return finish(a, t, 2, 3);
        };
        // Rotate to an absolute Z angle (DOTween's DORotate).
        b["tween_rotate_to"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            return finish(a, DOTween::Rotate(g, {0, 0, a[0].AsFloat()}, a[1].AsFloat()), 2, 3);
        };
        // Rotate to x, y, z degrees (3D).
        b["tween_rotate3"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 4) return Value{0.0f};
            return finish(a, DOTween::Rotate(g, {a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()}, a[3].AsFloat()), 4, 5);
        };
        // Ping-pong forever between here and (x, y) — floating pickups, patrols.
        b["tween_loop_move"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 3) return Value{0.0f};
            auto t = DOTween::LocalMove(g, {a[0].AsFloat(), a[1].AsFloat(), g->transform->localPosition.z}, a[2].AsFloat());
            t->SetLoops(-1, LoopType::Yoyo);
            return finish(a, t, 3, kNone);
        };
        b["tween_loop_scale"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            auto t = DOTween::Scale(g, a[0].AsFloat(), a[1].AsFloat());
            t->SetLoops(-1, LoopType::Yoyo);
            return finish(a, t, 2, kNone);
        };
        // Spin forever: a full turn every `dur` seconds (dir +1 CCW, -1 CW).
        b["tween_loop_rotate"] = [self, reg](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.empty() || a[0].AsFloat() < 1e-3f) return Value{0.0f};
            const float dir = a.size() > 1 ? (a[1].AsFloat() < 0 ? -1.0f : 1.0f) : 1.0f;
            auto t = DOTween::Rotate(g, {0, 0, 360.0f * dir}, a[0].AsFloat());
            t->SetRelative().SetEase(Ease::Linear).SetLoops(-1, LoopType::Incremental);
            return reg(t);
        };
        // Punch the scale out and settle back (DOPunchScale) — button presses, pickups.
        b["tween_punch_scale"] = [self, reg, num, toCount](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            const float p = a[0].AsFloat();
            return reg(DOTween::PunchScale(g, {p, p, p}, a[1].AsFloat(), toCount(num(a, 2, 6.0f), 6)));
        };
        b["tween_punch_pos"] = [self, reg, num, toCount](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 3) return Value{0.0f};
            return reg(DOTween::PunchPosition(g, {a[0].AsFloat(), a[1].AsFloat(), 0}, a[2].AsFloat(), toCount(num(a, 3, 6.0f), 6)));
        };
        b["tween_punch_rotate"] = [self, reg, num, toCount](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            return reg(DOTween::PunchRotation(g, {0, 0, a[0].AsFloat()}, a[1].AsFloat(), toCount(num(a, 2, 6.0f), 6)));
        };
        // Shake, fading out (camera / impact shake).
        b["tween_shake"] = [self, reg, num, toCount](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            const float s = a[0].AsFloat();
            return reg(DOTween::ShakePosition(g, a[1].AsFloat(), {s, s, 0}, toCount(num(a, 2, 20.0f), 20)));
        };
        b["tween_shake_rotate"] = [self, reg, num, toCount](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            return reg(DOTween::ShakeRotation(g, a[1].AsFloat(), {0, 0, a[0].AsFloat()}, toCount(num(a, 2, 14.0f), 14)));
        };
        b["tween_shake_scale"] = [self, reg, num, toCount](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            const float s = a[0].AsFloat();
            return reg(DOTween::ShakeScale(g, a[1].AsFloat(), {s, s, s}, toCount(num(a, 2, 14.0f), 14)));
        };
        // Jump to (x, y) in an arc `height` high: tween_jump(x, y, height, dur[, jumps][, "done"]).
        b["tween_jump"] = [self, finish, num, toCount](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 4) return Value{0.0f};
            const int jumps = toCount(num(a, 4, 1.0f), 1);
            const std::size_t di = a.size() > 4 && a[4].IsString() ? 4 : 5;
            auto t = DOTween::Jump(g, {a[0].AsFloat(), a[1].AsFloat(), g->transform->localPosition.z}, a[2].AsFloat(), jumps, a[3].AsFloat());
            return finish(a, t, kNone, di);
        };
        // Through waypoints: tween_path(dur, x1, y1, x2, y2, ...). tween_path_smooth curves through them.
        auto pathFn = [self, reg](std::vector<Value>& a, DOTween::PathType type) -> Value {
            GameObject* g = self(); if (!g || a.size() < 3) return Value{0.0f};
            std::vector<Vec3> pts;
            for (std::size_t i = 1; i + 1 < a.size(); i += 2) pts.push_back({a[i].AsFloat(), a[i + 1].AsFloat(), g->transform->localPosition.z});
            auto t = DOTween::Path(g, pts, a[0].AsFloat(), type);
            t->SetEase(Ease::Linear);
            return reg(t);
        };
        b["tween_path"] = [pathFn](std::vector<Value>& a) { return pathFn(a, DOTween::PathType::Linear); };
        b["tween_path_smooth"] = [pathFn](std::vector<Value>& a) { return pathFn(a, DOTween::PathType::CatmullRom); };
        // Count this object's text from -> to (score ticks); optional prefix "Score: ".
        b["tween_number"] = [self, reg](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 3) return Value{0.0f};
            const std::string prefix = (a.size() > 3 && a[3].IsString()) ? a[3].AsString() : std::string{};
            auto t = DOTween::Counter(g, a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat(), prefix);
            t->SetEase(Ease::Linear);
            return reg(t);
        };
        // Type this object's text out letter by letter (dialogue): tween_text("Hello!", dur[, "done"]).
        b["tween_text"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            return finish(a, DOTween::Text(g, a[0].AsString(), a[1].AsFloat()), kNone, 2);
        };
        // A UI widget's anchored position / size — slide menus and panels in and out.
        b["tween_ui_move"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 3) return Value{0.0f};
            return finish(a, DOTween::AnchorPos(g, {a[0].AsFloat(), a[1].AsFloat()}, a[2].AsFloat()), 3, 4);
        };
        b["tween_ui_size"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 3) return Value{0.0f};
            return finish(a, DOTween::SizeDelta(g, {a[0].AsFloat(), a[1].AsFloat()}, a[2].AsFloat()), 3, 4);
        };
        // This object's colour (sprite, mesh, UI or text) / its alpha.
        b["tween_color"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); Color* c = DOTween::ColorOf(g);
            if (!c || a.size() < 4) return Value{0.0f};
            return finish(a, DOTween::ColorTo(g, Color{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat(), c->a}, a[3].AsFloat()), 4, 5);
        };
        b["tween_fade"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self(); if (!g || a.size() < 2) return Value{0.0f};
            return finish(a, DOTween::Fade(g, a[0].AsFloat(), a[1].AsFloat()), 2, 3);
        };
        // flash(r, g, b[, dur]): snap to a colour, then fade back (hit feedback).
        b["flash"] = [self, reg](std::vector<Value>& a) {
            GameObject* g = self(); Color* c = DOTween::ColorOf(g);
            if (!c || a.size() < 3) return Value{0.0f};
            const Color orig = *c;
            auto t = DOTween::ColorTo(g, orig, a.size() > 3 ? a[3].AsFloat() : 0.2f);
            *c = Color{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat(), orig.a};
            t->SetEase(Ease::Linear);
            return reg(t);
        };
        // A script variable: tween_var("speed", 10, 2[, ease][, "done"]) (DOTween.To).
        b["tween_var"] = [this, tweens, finish](std::vector<Value>& a) {
            TweenManager* m = tweens(); if (!m || !rt.host || a.size() < 3) return Value{0.0f};
            const std::string name = a[0].AsString();
            std::weak_ptr<int> alive = life;
            auto t = m->To([this, alive, name]() {
                               if (alive.expired()) return 0.0f;
                               auto& g = rt.Global().vars; auto it = g.find(name);
                               return it != g.end() ? it->second.AsFloat() : 0.0f; },
                           [this, alive, name](float v) { if (!alive.expired()) rt.Global().vars[name] = Value{v}; },
                           a[1].AsFloat(), a[2].AsFloat());
            t->SetTarget(rt.host->gameObject);
            return finish(a, t, 3, 4);
        };
        // Any property of this object by path: tween_prop("Light.intensity", 3, 0.5[, ease][, "done"]).
        // One vector component is its own path ("Transform.scale.y"); an integer property rounds.
        b["tween_prop"] = [self, finish](std::vector<Value>& a) {
            GameObject* g = self();
            if (!g || a.size() < 3 || !a[0].IsString()) return Value{0.0f};
            const std::string path = a[0].AsString();
            const PropertyValue cur = PropertyRegistry::GetProperty(*g, path);
            const PropertyValue to = cur.Type() == PropertyType::Int ? PropertyValue::MakeInt(std::llround((double)a[1].AsFloat()))
                                                                      : PropertyValue::MakeFloat(a[1].AsFloat());
            return finish(a, DOTween::Property(g, path, to, a[2].AsFloat()), 3, 4);
        };
        // Call a function after a delay: tween_call(seconds, "fn") (DOVirtual.DelayedCall).
        b["tween_call"] = [this, tweens, fnFromArg, reg](std::vector<Value>& a) {
            TweenManager* m = tweens(); if (!m || a.size() < 2) return Value{0.0f};
            auto t = m->DelayedCall(a[0].AsFloat(), fnFromArg(a, 1));
            if (rt.host) t->SetTarget(rt.host->gameObject);
            return reg(t);
        };

        // ---- Settings: each returns the id, so they nest ----
        auto setting = [lookup](std::function<void(Tweener&, std::vector<Value>&)> fn) {
            return [lookup, fn](std::vector<Value>& a) -> Value {
                auto t = lookup(a, 0);
                if (t) fn(*t, a);
                return a.empty() ? Value{0.0f} : a[0];
            };
        };
        b["tween_delay"]    = setting([](Tweener& t, std::vector<Value>& a) { t.SetDelay(a.size() > 1 ? a[1].AsFloat() : 0.0f); });
        b["tween_ease"]     = setting([applyEase](Tweener& t, std::vector<Value>& a) { applyEase(t, a, 1); });
        b["tween_speed"]    = setting([](Tweener& t, std::vector<Value>& a) { t.SetSpeed(a.size() > 1 ? a[1].AsFloat() : 1.0f); });
        b["tween_unscaled"] = setting([](Tweener& t, std::vector<Value>& a) { t.SetUpdate(a.size() > 1 ? a[1].AsBool() : true); });
        // tween_clock(id, "ui"): follow a named clock channel ("game", "ui", "cinematic", your own); "" = scene time.
        b["tween_clock"]    = setting([](Tweener& t, std::vector<Value>& a) { t.SetClock(a.size() > 1 ? a[1].AsString() : std::string{}); });
        // tween_tag(id, "menu"): a group label for tween_kill_tag / tween_pause_tag / tween_play_tag.
        b["tween_tag"]      = setting([](Tweener& t, std::vector<Value>& a) { t.SetTag(a.size() > 1 ? a[1].AsString() : std::string{}); });
        // tween_speed_based(id): the duration you gave is a speed (units a second); the tween lasts distance / speed.
        b["tween_speed_based"] = setting([](Tweener& t, std::vector<Value>& a) { t.SetSpeedBased(a.size() > 1 ? a[1].AsBool() : true); });
        // tween_decay(id, rate): how a punch or shake dies away (0 = straight fade, 6 = snappy).
        b["tween_decay"]    = setting([](Tweener& t, std::vector<Value>& a) { t.SetDecay(a.size() > 1 ? a[1].AsFloat() : 0.0f); });
        b["tween_set_id"]   = setting([](Tweener& t, std::vector<Value>& a) { t.SetId(a.size() > 1 ? a[1].AsString() : std::string{}); });
        b["tween_from"]     = setting([](Tweener& t, std::vector<Value>&) { t.From(); });
        b["tween_relative"] = setting([](Tweener& t, std::vector<Value>&) { t.SetRelative(); });
        b["tween_keep"]     = setting([](Tweener& t, std::vector<Value>&) { t.SetAutoKill(false); });
        // tween_loops(id, count[, "restart" | "yoyo" | "incremental"]); count -1 = forever.
        b["tween_loops"] = setting([toCount](Tweener& t, std::vector<Value>& a) {
            std::string ty = a.size() > 2 && a[2].IsString() ? a[2].AsString() : std::string("restart");
            for (auto& ch : ty) ch = (char)std::tolower((unsigned char)ch);
            const LoopType lt = ty == "yoyo" ? LoopType::Yoyo : ty == "incremental" ? LoopType::Incremental : LoopType::Restart;
            t.SetLoops(a.size() > 1 ? toCount(a[1].AsFloat(), 1) : 1, lt);   // NaN plays once; a negative count loops forever
        });
        b["tween_on_complete"] = setting([fnFromArg](Tweener& t, std::vector<Value>& a) { t.OnComplete(fnFromArg(a, 1)); });
        b["tween_on_step"]     = setting([fnFromArg](Tweener& t, std::vector<Value>& a) { t.OnStepComplete(fnFromArg(a, 1)); });
        b["tween_on_start"]    = setting([fnFromArg](Tweener& t, std::vector<Value>& a) { t.OnStart(fnFromArg(a, 1)); });
        b["tween_on_update"]   = setting([fnFromArg](Tweener& t, std::vector<Value>& a) { t.OnUpdate(fnFromArg(a, 1)); });

        // ---- Control ----
        b["tween_kill"]      = setting([](Tweener& t, std::vector<Value>& a) { t.Kill(a.size() > 1 && a[1].AsBool()); });
        b["tween_pause"]     = setting([](Tweener& t, std::vector<Value>&) { t.Pause(); });
        b["tween_play"]      = setting([](Tweener& t, std::vector<Value>&) { t.Play(); });
        b["tween_toggle"]    = setting([](Tweener& t, std::vector<Value>&) { t.TogglePause(); });
        b["tween_restart"]   = setting([](Tweener& t, std::vector<Value>&) { t.Restart(); });
        b["tween_rewind"]    = setting([](Tweener& t, std::vector<Value>&) { t.Rewind(); });
        b["tween_complete"]  = setting([](Tweener& t, std::vector<Value>&) { t.Complete(); });
        b["tween_backwards"] = setting([](Tweener& t, std::vector<Value>&) { t.PlayBackwards(); });
        b["tween_goto"]      = setting([](Tweener& t, std::vector<Value>& a) { t.Goto(a.size() > 1 ? a[1].AsFloat() : 0.0f, a.size() > 2 && a[2].AsBool()); });
        b["tween_playing"] = [lookup](std::vector<Value>& a) { auto t = lookup(a, 0); return Value{t && t->IsPlaying()}; };
        b["tween_active"]  = [lookup](std::vector<Value>& a) { auto t = lookup(a, 0); return Value{t != nullptr}; };
        b["tween_progress"] = [lookup](std::vector<Value>& a) { auto t = lookup(a, 0); return Value{t ? t->ElapsedPercentage() : 1.0f}; };
        // Everything on this object.
        b["tween_kill_all"]  = [self](std::vector<Value>& a) { return Value{(float)DOTween::Kill(self(), !a.empty() && a[0].AsBool())}; };
        b["tween_pause_all"] = [self](std::vector<Value>&) { return Value{(float)DOTween::Pause(self())}; };
        b["tween_play_all"]  = [self](std::vector<Value>&) { return Value{(float)DOTween::Play(self())}; };
        b["is_tweening"] = [self](std::vector<Value>&) {
            GameObject* g = self();
            return Value{g && g->scene() && g->scene()->tweens().IsTweening(g)};
        };
        // Everything with an id set by tween_set_id (or a Tween Animation's Id), scene-wide.
        auto byId = [this, tweens](int op) {
            return [this, tweens, op](std::vector<Value>& a) -> Value {
                TweenManager* m = tweens(); if (!m || a.empty()) return Value{0.0f};
                const std::string id = a[0].AsString();
                int n = op == 0 ? m->Kill(id) : op == 1 ? m->Pause(id) : op == 2 ? m->Play(id) : m->Restart(id);
                if (op == 2 || op == 3)   // Tween Animations that haven't started yet
                    n += TweenAnimation::PlayAll(*rt.host->gameObject->scene(), id, op == 3);
                return Value{(float)n};
            };
        };
        // Everything with a tag set by tween_tag, scene-wide: tween_kill_tag("menu"[, complete]), pause, play, complete.
        auto byTag = [tweens](int op) {
            return [tweens, op](std::vector<Value>& a) -> Value {
                TweenManager* m = tweens(); if (!m || a.empty()) return Value{0.0f};
                const std::string tag = a[0].AsString();
                const int n = op == 0 ? m->KillByTag(tag, a.size() > 1 && a[1].AsBool())
                            : op == 1 ? m->PauseByTag(tag) : op == 2 ? m->PlayByTag(tag) : m->CompleteByTag(tag);
                return Value{(float)n};
            };
        };
        b["tween_kill_tag"]     = byTag(0);
        b["tween_pause_tag"]    = byTag(1);
        b["tween_play_tag"]     = byTag(2);
        b["tween_complete_tag"] = byTag(3);
        b["tween_rewind_id"] = [tweens](std::vector<Value>& a) -> Value {
            TweenManager* m = tweens(); if (!m || a.empty()) return Value{0.0f};
            return Value{(float)m->Rewind(a[0].AsString())};
        };
        b["tween_kill_id"]    = byId(0);
        b["tween_pause_id"]   = byId(1);
        b["tween_play_id"]    = byId(2);
        b["tween_restart_id"] = byId(3);

        // ---- Sequences: tweens one after another / together on one timeline ----
        b["seq_new"] = [this, tweens, reg](std::vector<Value>&) {
            TweenManager* m = tweens(); if (!m) return Value{0.0f};
            auto s = m->Sequence();
            if (rt.host) s->SetTarget(rt.host->gameObject);
            return reg(s);
        };
        auto seqOp = [lookup](std::function<void(TweenSequence&, std::vector<Value>&)> fn) {
            return [lookup, fn](std::vector<Value>& a) -> Value {
                auto s = std::dynamic_pointer_cast<TweenSequence>(lookup(a, 0));
                if (s) fn(*s, a);
                return a.empty() ? Value{0.0f} : a[0];
            };
        };
        b["seq_append"] = seqOp([lookup](TweenSequence& s, std::vector<Value>& a) { if (auto t = lookup(a, 1)) s.Append(t); });
        b["seq_join"]   = seqOp([lookup](TweenSequence& s, std::vector<Value>& a) { if (auto t = lookup(a, 1)) s.Join(t); });
        b["seq_prepend"] = seqOp([lookup](TweenSequence& s, std::vector<Value>& a) { if (auto t = lookup(a, 1)) s.Prepend(t); });
        b["seq_join_callback"] = seqOp([fnFromArg](TweenSequence& s, std::vector<Value>& a) { if (auto f = fnFromArg(a, 1)) s.JoinCallback(f); });
        b["seq_insert"] = seqOp([lookup](TweenSequence& s, std::vector<Value>& a) { if (auto t = lookup(a, 2)) s.Insert(a[1].AsFloat(), t); });
        b["seq_interval"] = seqOp([](TweenSequence& s, std::vector<Value>& a) { s.AppendInterval(a.size() > 1 ? a[1].AsFloat() : 0.0f); });
        b["seq_prepend_interval"] = seqOp([](TweenSequence& s, std::vector<Value>& a) { s.PrependInterval(a.size() > 1 ? a[1].AsFloat() : 0.0f); });
        b["seq_callback"] = seqOp([fnFromArg](TweenSequence& s, std::vector<Value>& a) { if (auto f = fnFromArg(a, 1)) s.AppendCallback(f); });
        b["seq_insert_callback"] = seqOp([fnFromArg](TweenSequence& s, std::vector<Value>& a) {
            if (auto f = fnFromArg(a, 2)) s.InsertCallback(a.size() > 1 ? a[1].AsFloat() : 0.0f, f);
        });

}

} // namespace okay
