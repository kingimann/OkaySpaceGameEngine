// Typed script API, Time, Debug, Audio, SaveGame, Localization and Sequence: the Bind<Topic> functions of the binding table (see EngineApiInternal.hpp).
#include "EngineApiInternal.hpp"

namespace okay::script::engine_api {

void Ctx::BindRuntime() {
    // ---- Time ----
    {
        ApiType t; t.name = "Time"; t.isStatic = true; t.doc = "Frame timing.";
        t.staticProps.push_back(ro("deltaTime", tFloat, [](const Value&, CallContext& c) { return Value{c.deltaTime}; },
            "Seconds since the last frame (inside FixedUpdate: the fixed step)."));
        t.staticProps.push_back(ro("fixedDeltaTime", tFloat, [](const Value&, CallContext& c) { return Value{c.fixedDeltaTime}; },
            "The FixedUpdate step in seconds."));
        t.staticProps.push_back(prop("physicsSteps", tFloat,
            [](const Value&, CallContext& c) {
                Scene* s = c.self ? c.self->scene() : nullptr;
                return Value{s ? s->PhysicsStepsPerSecond() : 0.0f};
            },
            [](const Value&, const Value& v, CallContext& c) {
                if (Scene* s = c.self ? c.self->scene() : nullptr) s->SetPhysicsStepsPerSecond(F(v));
            },
            "How many times a second physics runs. 50 is the usual rate. 0 runs physics once per frame."));
        t.staticProps.push_back(prop("smoothPhysics", tBool,
            [](const Value&, CallContext& c) {
                Scene* s = c.self ? c.self->scene() : nullptr;
                return Value{s ? s->SmoothPhysics() : false};
            },
            [](const Value&, const Value& v, CallContext& c) {
                if (Scene* s = c.self ? c.self->scene() : nullptr) s->SetSmoothPhysics(std::get<bool>(v));
            },
            "Draw bodies between physics steps so motion stays smooth at any frame rate."));
        // time and frameCount come from the shared table (Scripting/ScriptApiCore.cpp).
        t.staticProps.push_back(prop("timeScale", tFloat,
            [](const Value&, CallContext&) { return Value{Time::TimeScale()}; },
            [](const Value&, const Value& v, CallContext& c) {
                if (F(v) < 0.0f) throw RuntimeError("timeScale can't be negative", c.loc);
                Time::SetTimeScale(F(v));
            }, "How fast time passes: 1 normal, 0.5 slow motion, 0 paused."));
        // Since this Play began. Time.time and Time.frameCount are the process's own clocks and keep counting
        // across Stop and Play; these start again from 0 with each Play (or scene load).
        t.staticProps.push_back(ro("sessionTime", tFloat, [](const Value&, CallContext&) { return Value{(float)Time::SessionTime()}; },
            "Seconds of game time since this Play began (Time.time keeps counting across Plays; this starts from 0 each time)."));
        t.staticProps.push_back(ro("sessionUnscaledTime", tFloat, [](const Value&, CallContext&) { return Value{(float)Time::SessionUnscaledTime()}; },
            "Real seconds since this Play began: a pause, slow motion and hit stops do not change it."));
        t.staticProps.push_back(ro("sessionFrameCount", tInt, [](const Value&, CallContext&) {
                const std::uint64_t n = Time::SessionFrameCount();
                return Value{(std::int32_t)(n > 2147483647ull ? 2147483647ull : n)};
            }, "Frames since this Play began."));
        t.staticProps.push_back(ro("unscaledDeltaTime", tFloat, [](const Value&, CallContext&) { return Value{Time::UnscaledDeltaTime()}; },
            "Real seconds since the last frame: not slowed by timeScale, a hit stop or a pause (animate a menu with it)."));
        t.staticProps.push_back(ro("unscaledTime", tFloat, [](const Value&, CallContext&) { return Value{(float)Time::UnscaledTime()}; },
            "Real seconds since the game started: a pause, slow motion and hit stops do not change it."));
        t.staticProps.push_back(ro("fps", tFloat, [](const Value&, CallContext&) {
                const GameLoop* loop = GameLoop::Current();
                return Value{loop ? (float)loop->Stats().AverageFps(30) : 0.0f};
            }, "Frames per second over the last half second of real frame times (0 before the first frame)."));
        t.staticProps.push_back(ro("hitchCount", tInt, [](const Value&, CallContext&) {
                const GameLoop* loop = GameLoop::Current();
                return Value{(std::int32_t)(loop ? loop->HitchCount() : 0)};
            }, "How many frames took over a tenth of a second since this Play began."));
        t.staticProps.push_back(ro("paused", tBool, [](const Value&, CallContext&) { return Value{Game::Paused()}; },
            "True while the game is paused (a menu, a cutscene or the pause button holds it)."));
        t.staticMethods.push_back(method("PushPause", {tString}, {"reason"}, tVoid,
            [](const Value&, std::vector<Value>& a, CallContext&) { Game::PushPause(S(a[0])); return Value{}; },
            "Hold the game paused for a reason of your own (\"dialogue\", \"shop\"). The game stays paused until every reason is let go, so two scripts that pause never resume each other."));
        t.staticMethods.push_back(method("PopPause", {tString}, {"reason"}, tBool,
            [](const Value&, std::vector<Value>& a, CallContext&) { return Value{Game::PopPause(S(a[0]))}; },
            "Let go of one hold of that reason. False when nothing held it."));
        t.staticMethods.push_back(method("IsPausedBy", {tString}, {"reason"}, tBool,
            [](const Value&, std::vector<Value>& a, CallContext&) { return Value{Game::IsPausedBy(S(a[0]))}; },
            "True while that reason is holding the pause."));
        t.staticMethods.back().pure = true;   // a value node in a visual script
        t.staticMethods.push_back(method("HitStop", {tFloat, tFloat}, {"seconds", "scale"}, tVoid,
            [](const Value&, std::vector<Value>& a, CallContext&) { Time::HitStop(F(a[0]), F(a[1])); return Value{}; },
            "A freeze frame or a brief slow-down for an impact: for some real seconds the game runs at this scale (0 = frozen), then goes back by itself."));
        t.staticMethods.push_back(method("SlowMotion", {tFloat, tFloat}, {"scale", "seconds"}, tVoid,
            [](const Value&, std::vector<Value>& a, CallContext&) { Time::HitStop(F(a[1]), F(a[0])); return Value{}; },
            "Bullet time: run at this speed (0.2 = a fifth) for some real seconds, then back to normal by itself. Composes with Time.timeScale."));
        t.staticMethods.push_back(method("RampTimeScale", {tFloat, tFloat}, {"target", "seconds"}, tVoid,
            [](const Value&, std::vector<Value>& a, CallContext&) { Time::RampTimeScale(F(a[0]), F(a[1])); return Value{}; },
            "Ease Time.timeScale to a new value over some real seconds, in a straight line."));
        api.types.push_back(std::move(t));
    }
    // ---- Debug ----
    {
        ApiType d; d.name = "Debug"; d.isStatic = true; d.doc = "Messages to the Console.";
        d.staticMethods.push_back(method("Log", {tAny}, {"message"}, tVoid,
            [](const Value&, std::vector<Value>& a, CallContext&) { Log::Info("[script] ", LogText(a[0])); return Value{}; },
            "Print a message to the Console."));
        d.staticMethods.push_back(method("LogWarning", {tAny}, {"message"}, tVoid,
            [](const Value&, std::vector<Value>& a, CallContext&) { Log::Warning("[script] ", LogText(a[0])); return Value{}; },
            "Print a warning to the Console."));
        d.staticMethods.push_back(method("LogError", {tAny}, {"message"}, tVoid,
            [](const Value&, std::vector<Value>& a, CallContext&) { Log::Error("[script] ", LogText(a[0])); return Value{}; },
            "Print an error to the Console."));
        api.types.push_back(std::move(d));
    }
    // ---- Audio ----
    {
        ApiType au; au.name = "Audio"; au.isStatic = true;
        au.doc = "The mixer: overall volume and each group's (Music, SFX, UI, Voice), as a settings menu sets them.";
        au.staticProps.push_back(prop("masterVolume", tFloat,
            [](const Value&, CallContext&) { return Value{AudioMixer::masterVolume}; },
            [](const Value&, const Value& v, CallContext&) { AudioMixer::masterVolume = Mathf::Clamp(F(v), 0.0f, 1.0f); },
            "Overall volume, 0 to 1."));
        au.staticProps.push_back(prop("muted", tBool,
            [](const Value&, CallContext&) { return Value{AudioMixer::muted}; },
            [](const Value&, const Value& v, CallContext&) { AudioMixer::muted = std::get<bool>(v); },
            "Silence everything."));
        auto group = [](const std::string& name, CallContext& c) {
            AudioGroup g;
            if (!AudioMixer::GroupFromName(name.c_str(), g))
                throw RuntimeError("There's no audio group called \"" + name + "\" (use Music, SFX, UI or Voice)", c.loc);
            return (int)g;
        };
        // SetGroupVolume and GetGroupVolume come from the shared table (Scripting/ScriptApiCore.cpp).
        au.staticMethods.push_back(method("MuteGroup", {tString, tBool}, {"group", "mute"}, tVoid,
            [group](const Value&, std::vector<Value>& a, CallContext& c) {
                AudioMixer::groupMuted[group(S(a[0]), c)] = std::get<bool>(a[1]); return Value{}; },
            "Mute or unmute one group."));
        au.staticMethods.push_back(method("SaveSettings", {}, {}, tVoid,
            [](const Value&, std::vector<Value>&, CallContext&) { AudioMixer::SaveSettings(); return Value{}; },
            "Store the volumes in the player's prefs now (they are also saved when the game closes)."));
        api.types.push_back(std::move(au));
    }
    // ---- SaveGame ----
    {
        ApiType sg; sg.name = "SaveGame"; sg.isStatic = true;
        sg.doc = "Whole-game saves in named slots: every object, the player, inventories, script variables.";
        // Save, Load, Exists, Delete, Label and playTime come from the shared table (Scripting/ScriptApiCore.cpp).
        api.types.push_back(std::move(sg));
    }
    // ---- Localization ----
    {
        ApiType lo; lo.name = "Localization"; lo.isStatic = true;
        lo.doc = "Text in the player's language from the project's string table (docs/engine/localization.md). "
                 "A script hears a change of language in OnLanguageChanged().";
        auto args = [](const std::vector<Value>& a) {
            std::vector<LocArg> out;
            for (std::size_t i = 1; i < a.size(); ++i) {
                if (auto* f = std::get_if<float>(&a[i])) out.push_back(LocArg((double)*f));
                else if (auto* n = std::get_if<std::int32_t>(&a[i])) out.push_back(LocArg((double)*n));
                else out.push_back(LocArg(ValueToString(a[i])));
            }
            return out;
        };
        auto get = [args](const Value&, std::vector<Value>& a, CallContext&) {
            return Value{Localization::LocalizeArgs(S(a[0]), args(a))};
        };
        lo.staticMethods.push_back(method("Get", {tString}, {"key"}, tString, get,
            "The key's text in the player's language (the default language's when it has none, else the key)."));
        lo.staticMethods.push_back(method("Get", {tString, tAny}, {"key", "arg0"}, tString, get,
            "The key's text with {0} filled in; a number also picks the plural form."));
        lo.staticMethods.push_back(method("Get", {tString, tAny, tAny}, {"key", "arg0", "arg1"}, tString, get,
            "The key's text with {0} and {1} filled in."));
        lo.staticMethods.push_back(method("Get", {tString, tAny, tAny, tAny}, {"key", "arg0", "arg1", "arg2"}, tString, get,
            "The key's text with {0}, {1} and {2} filled in."));
        for (auto& m : lo.staticMethods) m.pure = true;
        lo.staticMethods.push_back(method("SetLanguage", {tString}, {"code"}, tBool,
            [](const Value&, std::vector<Value>& a, CallContext&) {
                const bool ok = Localization::SetLanguage(S(a[0]));
                if (ok) Localization::SaveSettings();
                else Log::Warning("[script] Localization.SetLanguage: the string table has no language \"", S(a[0]), "\"");
                return Value{ok};
            }, "Show every localized text in a language of the string table (\"fr\") and remember it for the next run. "
               "False when the table doesn't have it."));
        ApiMethod name = method("LanguageName", {tString}, {"code"}, tString,
            [](const Value&, std::vector<Value>& a, CallContext&) { return Value{Localization::LanguageName(S(a[0]))}; },
            "A language's name for a menu (\"Français\" for \"fr\").");
        name.pure = true; lo.staticMethods.push_back(std::move(name));
        lo.staticProps.push_back(ro("Language", tString,
            [](const Value&, CallContext&) { return Value{Localization::Language()}; },
            "The current language's code (\"en\", \"fr\"...)."));
        lo.staticProps.push_back(ro("Languages", Type::ApiStrings(),
            [](const Value&, CallContext&) {
                auto list = std::make_shared<ArrayObj>();
                list->typeName = "string[]";
                for (const std::string& c : Localization::Languages()) list->items.push_back(Value{c});
                return Value{list};
            }, "The codes of the string table's languages, in the table's order."));
        api.types.push_back(std::move(lo));
    }
    // ---- Sequence (cutscenes) ----
    {
        ApiType sq; sq.name = "Sequence"; sq.isStatic = true;
        sq.doc = "Cutscenes: play a .okayseq timeline through a Sequence Player in the scene.";
        auto sceneOf = [](CallContext& c) -> Scene* { return c.self ? c.self->scene() : nullptr; };
        sq.staticMethods.push_back(method("Play", {tString}, {"name"}, tBool,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                Scene* s = sceneOf(c);
                return Value{s && SequencePlayer::PlayByName(*s, c.self, S(a[0]))};
            }, "Play the sequence named like this (a Sequence Player's file, or Assets/<name>.okayseq). False if there is none."));
        sq.staticMethods.push_back(method("Stop", {}, {}, tVoid,
            [sceneOf](const Value&, std::vector<Value>&, CallContext& c) { if (Scene* s = sceneOf(c)) SequencePlayer::StopAll(*s); return Value{}; },
            "Stop every playing sequence; the camera goes back to the game."));
        sq.staticProps.push_back(ro("isPlaying", tBool,
            [sceneOf](const Value&, CallContext& c) { Scene* s = sceneOf(c); return Value{s && SequencePlayer::Playing(*s) != nullptr}; },
            "True while a sequence plays."));
        sq.staticProps.push_back(ro("time", tFloat,
            [sceneOf](const Value&, CallContext& c) {
                Scene* s = sceneOf(c);
                SequencePlayer* sp = s ? SequencePlayer::Playing(*s) : nullptr;
                return Value{sp ? sp->Time() : 0.0f};
            }, "Seconds into the playing sequence (0 when none plays)."));
        sq.staticProps.push_back(ro("lastEvent", tString,
            [](const Value&, CallContext&) { return Value{SequencePlayer::LastEvent()}; },
            "The event being delivered: read it in OnSequenceEvent()."));
        api.types.push_back(std::move(sq));
    }
}

} // namespace okay::script::engine_api
