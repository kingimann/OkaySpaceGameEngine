#include "OkayScriptVMInternal.hpp"

namespace okay {

void OkayScriptVM::Impl::RegisterBody2DBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto tf = c.tf;
        auto go = c.go;
        // Sibling Rigidbody2D control (jump, dash, knockback, top-down movement).
        // (No Rigidbody2D? The same calls drive a Rigidbody3D, with an optional z.)
        auto arg3 = [](std::vector<Value>& a, std::size_t i) { return a.size() > i ? a[i].AsFloat() : 0.0f; };
        b["velocity_x"] = [go](std::vector<Value>&) -> Value {
            if (GameObject* g = go()) {
                if (auto* rb = g->GetComponent<Rigidbody2D>()) return Value{rb->velocity.x};
                if (auto* r3 = g->GetComponent<Rigidbody3D>()) return Value{r3->velocity.x};
            }
            return Value{0.0f};
        };
        b["velocity_y"] = [go](std::vector<Value>&) -> Value {
            if (GameObject* g = go()) {
                if (auto* rb = g->GetComponent<Rigidbody2D>()) return Value{rb->velocity.y};
                if (auto* r3 = g->GetComponent<Rigidbody3D>()) return Value{r3->velocity.y};
            }
            return Value{0.0f};
        };
        b["set_velocity"] = [go, arg3](std::vector<Value>& a) {
            if (GameObject* g = go()) {
                if (auto* rb = g->GetComponent<Rigidbody2D>()) rb->velocity = {arg3(a, 0), arg3(a, 1)};
                else if (auto* r3 = g->GetComponent<Rigidbody3D>()) { r3->velocity = {arg3(a, 0), arg3(a, 1), arg3(a, 2)}; r3->WakeUp(); }
            }
            return Value{};
        };
        b["set_vx"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) {
                if (auto* rb = g->GetComponent<Rigidbody2D>()) rb->velocity.x = a.empty() ? 0.0f : a[0].AsFloat();
                else if (auto* r3 = g->GetComponent<Rigidbody3D>()) { r3->velocity.x = a.empty() ? 0.0f : a[0].AsFloat(); r3->WakeUp(); }
            }
            return Value{};
        };
        b["set_vy"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) {
                if (auto* rb = g->GetComponent<Rigidbody2D>()) rb->velocity.y = a.empty() ? 0.0f : a[0].AsFloat();
                else if (auto* r3 = g->GetComponent<Rigidbody3D>()) { r3->velocity.y = a.empty() ? 0.0f : a[0].AsFloat(); r3->WakeUp(); }
            }
            return Value{};
        };
        // ---- Stay-in-bounds helpers (rectangle minX,minY,maxX,maxY) ----
        // keep_in_box: clamp inside. wrap_in_box: teleport to the opposite edge
        // (asteroids). bounce_in_box: reverse the Rigidbody2D velocity at each wall.
        b["keep_in_box"] = [tf](std::vector<Value>& a) {
            Transform* t = tf(); if (!t || a.size() < 4) return Value{};
            Vec3 p = t->localPosition;
            p.x = Mathf::Clamp(p.x, a[0].AsFloat(), a[2].AsFloat());
            p.y = Mathf::Clamp(p.y, a[1].AsFloat(), a[3].AsFloat());
            t->localPosition = p;
            return Value{};
        };
        b["wrap_in_box"] = [tf](std::vector<Value>& a) {
            Transform* t = tf(); if (!t || a.size() < 4) return Value{};
            float x0 = a[0].AsFloat(), y0 = a[1].AsFloat(), x1 = a[2].AsFloat(), y1 = a[3].AsFloat();
            Vec3 p = t->localPosition;
            if (p.x < x0) p.x = x1; else if (p.x > x1) p.x = x0;
            if (p.y < y0) p.y = y1; else if (p.y > y1) p.y = y0;
            t->localPosition = p;
            return Value{};
        };
        b["bounce_in_box"] = [go, tf](std::vector<Value>& a) {
            Transform* t = tf(); if (!t || a.size() < 4) return Value{};
            GameObject* g = go(); auto* rb = g ? g->GetComponent<Rigidbody2D>() : nullptr;
            if (!rb) return Value{};
            Vec3 p = t->localPosition;
            float x0 = a[0].AsFloat(), y0 = a[1].AsFloat(), x1 = a[2].AsFloat(), y1 = a[3].AsFloat();
            if ((p.x <= x0 && rb->velocity.x < 0) || (p.x >= x1 && rb->velocity.x > 0)) rb->velocity.x = -rb->velocity.x;
            if ((p.y <= y0 && rb->velocity.y < 0) || (p.y >= y1 && rb->velocity.y > 0)) rb->velocity.y = -rb->velocity.y;
            return Value{};
        };
        // add_force / add_impulse / add_torque are in the shared table (Scripting/ScriptApiCore.cpp).
        // add_explosion_force(cx, cy, force, radius [, up]) — shove this 2D body
        // away from (cx, cy). Linear falloff; nothing past the radius. up > 0 pops it.
        b["add_explosion_force"] = [go](std::vector<Value>& a) {
            if (a.size() < 4) return Value{};
            if (GameObject* g = go()) if (auto* rb = g->GetComponent<Rigidbody2D>())
                rb->AddExplosionForce(a[2].AsFloat(), {a[0].AsFloat(), a[1].AsFloat()},
                                      a[3].AsFloat(), a.size() > 4 ? a[4].AsFloat() : 0.0f);
            return Value{};
        };
}

void OkayScriptVM::Impl::RegisterSpriteEffectBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto go = c.go;
        // Set a sibling UIImage's texture from script (swap icons, states).
        b["set_image"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* im = g->GetComponent<UIImage>())
                im->texture = a.empty() ? std::string{} : a[0].AsString();
            return Value{};
        };
        // Enable/disable a sibling UIButton (grey out a "Continue" entry, etc.).
        b["set_interactable"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go()) if (auto* bt = g->GetComponent<UIButton>())
                bt->interactable = a.empty() || a[0].AsBool();
            return Value{};
        };
        // Particle FX on a sibling ParticleSystem (explosions, dust, pickups).
        b["emit"] = [go](std::vector<Value>& a) {       // one-off burst of N
            if (GameObject* g = go()) if (auto* ps = g->GetComponent<ParticleSystem>())
                ps->Emit(a.empty() ? 1 : SafeInt(a[0].AsFloat()));
            return Value{};
        };
        b["particles_on"] = [go](std::vector<Value>& a) {  // start/stop emission
            // Play() / Stop(), not the raw flag: Play() starts a new cycle on a stopped or finished
            // one-shot, so a script can fire the same effect again.
            if (GameObject* g = go()) if (auto* ps = g->GetComponent<ParticleSystem>()) {
                if (a.empty() || a[0].AsBool()) ps->Play(); else ps->Stop();
            }
            return Value{};
        };
        // explode([count]): burst particles AND remove this object — the classic death
        // effect in one call (needs a Particle System for the burst).
        b["explode"] = [this, go](std::vector<Value>& a) {
            int n = a.empty() ? 20 : SafeInt(a[0].AsFloat());
            if (GameObject* g = go()) if (auto* ps = g->GetComponent<ParticleSystem>()) ps->Emit(n);
            if (rt.host && rt.host->gameObject && rt.host->gameObject->scene())
                rt.host->gameObject->scene()->Destroy(rt.host->gameObject);
            return Value{};
        };
        // Show/hide this object's graphics (sprite/mesh/text) without deactivating it, so
        // its script keeps running. blink(rate) flashes visibility (invincibility/pickups).
        auto setVis = [go](bool v) {
            if (GameObject* g = go()) {
                if (auto* sr = g->GetComponent<SpriteRenderer>()) sr->enabled = v;
                if (auto* mr = g->GetComponent<MeshRenderer>())   mr->enabled = v;
                if (auto* tr = g->GetComponent<TextRenderer>())   tr->enabled = v;
            }
        };
        b["set_visible"] = [setVis](std::vector<Value>& a) { setVis(a.empty() || a[0].AsBool()); return Value{}; };
        b["show"] = [setVis](std::vector<Value>&) { setVis(true);  return Value{}; };
        b["hide"] = [setVis](std::vector<Value>&) { setVis(false); return Value{}; };
        b["blink"] = [setVis](std::vector<Value>& a) {
            float r = a.empty() ? 6.0f : a[0].AsFloat();
            setVis(Mathf::Sin(Time::ElapsedTime() * r) > 0.0f);
            return Value{};
        };
        b["particles_alive"] = [go](std::vector<Value>&) -> Value {
            if (GameObject* g = go()) if (auto* ps = g->GetComponent<ParticleSystem>())
                return Value{(float)ps->AliveCount()};
            return Value{0.0f};
        };
        // Sprite animation control on a sibling SpriteAnimator.
        b["play_anim"] = [go](std::vector<Value>&) {    // restart from frame 0
            if (GameObject* g = go()) if (auto* an = g->GetComponent<SpriteAnimator>())
                an->Restart();
            return Value{};
        };
        b["stop_anim"] = [go](std::vector<Value>&) {
            if (GameObject* g = go()) if (auto* an = g->GetComponent<SpriteAnimator>())
                an->playing = false;
            return Value{};
        };
        // Tilemap editing on a sibling Tilemap (procedural levels).
        b["tile_resize"] = [go](std::vector<Value>& a) {
            if (GameObject* g = go())
                if (auto* tm = g->GetComponent<Tilemap>())
                    tm->Resize(a.size() > 0 ? SafeInt(a[0].AsFloat()) : 0, a.size() > 1 ? SafeInt(a[1].AsFloat()) : 0);
            return Value{};
        };
        b["set_tile"] = [go](std::vector<Value>& a) {
            if (a.size() >= 3) if (GameObject* g = go())
                if (auto* tm = g->GetComponent<Tilemap>())
                    tm->SetTile(SafeInt(a[0].AsFloat()), SafeInt(a[1].AsFloat()), SafeInt(a[2].AsFloat()));
            return Value{};
        };
        b["get_tile"] = [go](std::vector<Value>& a) {
            if (a.size() >= 2) if (GameObject* g = go())
                if (auto* tm = g->GetComponent<Tilemap>())
                    return Value{(float)tm->GetTile(SafeInt(a[0].AsFloat()), SafeInt(a[1].AsFloat()))};
            return Value{0.0f};
        };
        b["tile_w"] = [go](std::vector<Value>&) { GameObject* g = go(); auto* tm = g ? g->GetComponent<Tilemap>() : nullptr; return Value{tm ? (float)tm->Width() : 0.0f}; };
        b["tile_h"] = [go](std::vector<Value>&) { GameObject* g = go(); auto* tm = g ? g->GetComponent<Tilemap>() : nullptr; return Value{tm ? (float)tm->Height() : 0.0f}; };
}

void OkayScriptVM::Impl::RegisterAudioSaveBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto sceneOf = c.sceneOf;
        // Global audio settings (for options menus).
        // 0..1 like Audio.masterVolume and set_group_volume; NaN is ignored (it passes a clamp and turns the mix into NaN, #888 F48).
        b["set_volume"] = [](std::vector<Value>& a) {
            const float v = a.empty() ? 1.0f : a[0].AsFloat();
            if (!std::isnan(v)) AudioMixer::masterVolume = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
            return Value{};
        };
        b["volume"]     = [](std::vector<Value>&) { return Value{AudioMixer::masterVolume}; };
        b["mute"]       = [](std::vector<Value>& a) { AudioMixer::muted = a.empty() ? true : a[0].AsBool(); return Value{}; };
        // Mixer groups: set_group_volume / group_volume are in the shared table (Scripting/ScriptApiCore.cpp).
        // Whole-game saves (SaveGame): save_game, load_game, has_save (save_exists), delete_save, save_label and
        // play_time are in the shared table too. The slot details below are still OkayScript's own.
        auto slotArg = [](std::vector<Value>& a) { return a.empty() ? std::string("save1") : a[0].AsString(); };
        // Load menus: save_slots() lists every slot (newest first, damaged ones included), save_valid
        // says whether a slot would load, and one slot's details come as text a menu can show.
        b["save_slots"]  = [](std::vector<Value>&) {
            Value list = Value::MakeArray();
            for (const SaveSlotInfo& i : SaveGame::List()) list.AsArray()->push_back(Value{i.slot});
            return list;
        };
        b["save_valid"]     = [slotArg](std::vector<Value>& a) { return Value{SaveGame::Check(slotArg(a)) ? 1.0f : 0.0f}; };
        b["save_scene"]     = [slotArg](std::vector<Value>& a) { return Value{SaveGame::Info(slotArg(a)).scene}; };
        b["save_play_time"] = [slotArg](std::vector<Value>& a) { return Value{SaveGame::Info(slotArg(a)).playTime}; };
        b["save_play_text"] = [slotArg](std::vector<Value>& a) {   // "1h 05m"
            const SaveSlotInfo i = SaveGame::Info(slotArg(a));
            return Value{i.exists ? SaveMenuModel::FormatPlayTime(i.playTime) : std::string()};
        };
        b["save_date"] = [slotArg](std::vector<Value>& a) {   // "2026-11-08 14:03" (UTC)
            const SaveSlotInfo i = SaveGame::Info(slotArg(a));
            return Value{i.exists && i.savedAt > 0 ? SaveMenuModel::FormatDateTime(i.savedAt, 0) : std::string()};
        };
        b["save_ago"] = [slotArg](std::vector<Value>& a) {   // "5 minutes ago"
            const SaveSlotInfo i = SaveGame::Info(slotArg(a));
            return Value{i.exists && i.savedAt > 0 ? SaveMenuModel::FormatAgo((long long)Time::UnixSeconds() - i.savedAt) : std::string()};
        };
        // Save slots with no code to write (docs/engine/save_games.md): objects marked with a Saveable
        // component are stored; game_save("slot_1" [, "label"]), game_load("slot_1") (at the start
        // of the next frame), game_delete_slot, game_slot_exists, game_slot_age (seconds since it was saved, -1 = none),
        // game_slot_text ("2026-11-08 14:03"), game_slot_label, game_continue, game_can_continue,
        // game_autosave. Every problem (missing, damaged or newer-version slot) is a Console error.
        auto gameSlot = [](std::vector<Value>& a) { return a.empty() ? std::string("slot_1") : a[0].AsString(); };
        b["game_save"] = [sceneOf, gameSlot](std::vector<Value>& a) {
            Scene* s = sceneOf();
            return Value{s && GameSaves::Save(*s, gameSlot(a), a.size() > 1 ? a[1].AsString() : "") ? 1.0f : 0.0f};
        };
        b["game_load"] = [sceneOf, gameSlot](std::vector<Value>& a) {
            Scene* s = sceneOf();
            return Value{s && GameSaves::Load(*s, gameSlot(a)) ? 1.0f : 0.0f};
        };
        b["game_continue"] = [sceneOf](std::vector<Value>&) {
            Scene* s = sceneOf();
            return Value{s && GameSaves::Continue(*s) ? 1.0f : 0.0f};
        };
        b["game_autosave"] = [sceneOf](std::vector<Value>&) {
            Scene* s = sceneOf();
            return Value{s && GameSaves::AutoSave(*s) ? 1.0f : 0.0f};
        };
        b["game_delete_slot"]  = [gameSlot](std::vector<Value>& a) { return Value{GameSaves::Delete(gameSlot(a)) ? 1.0f : 0.0f}; };
        b["game_slot_exists"]  = [gameSlot](std::vector<Value>& a) { return Value{GameSaves::Exists(gameSlot(a)) ? 1.0f : 0.0f}; };
        b["game_slot_age"]     = [gameSlot](std::vector<Value>& a) { return Value{(float)GameSaves::SecondsSinceSave(gameSlot(a))}; };
        b["game_slot_text"]    = [gameSlot](std::vector<Value>& a) { return Value{GameSaves::SavedText(gameSlot(a))}; };
        b["game_slot_label"]   = [gameSlot](std::vector<Value>& a) { return Value{GameSaves::Label(gameSlot(a))}; };
        b["game_can_continue"] = [](std::vector<Value>&) { return Value{GameSaves::CanContinue() ? 1.0f : 0.0f}; };
}

void OkayScriptVM::Impl::RegisterDisplayBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        // Display: the monitor, window mode, resolution and refresh the Settings screen edits.
        // set_* applies at once and is saved. begin_display_confirm remembers the current
        // choice and starts the 10-second "keep these settings?" clock; keep_display_settings
        // accepts it and revert_display_settings puts the previous choice back.
        b["get_monitor"] = [](std::vector<Value>&) { return Value{DisplaySettings::MonitorName()}; };
        b["get_monitors"] = [](std::vector<Value>&) {
            Value list = Value::MakeArray();
            for (const DisplayMonitor& m : DisplaySettings::Monitors()) list.AsArray()->push_back(Value{m.name});
            return list;
        };
        b["set_monitor"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{0.0f};
            if (a[0].IsNumber()) {
                DisplaySettings::SetMonitorIndex((int)a[0].AsFloat());
                return Value{1.0f};
            }
            const std::string name = a[0].AsString();
            bool digits = !name.empty();
            for (char c : name) if (c < '0' || c > '9') digits = false;
            if (digits) { DisplaySettings::SetMonitorIndex(std::atoi(name.c_str())); return Value{1.0f}; }
            const bool ok = DisplaySettings::SetMonitorName(name);
            if (!ok) Log::Warning("[script] set_monitor: no connected monitor named \"", name, "\"");
            return Value{ok ? 1.0f : 0.0f};
        };
        b["get_display_mode"] = [](std::vector<Value>&) { return Value{std::string(DisplaySettings::ModeName(DisplaySettings::Mode()))}; };
        b["set_display_mode"] = [](std::vector<Value>& a) {
            WindowMode mode = WindowMode::Windowed;
            const std::string text = a.empty() ? std::string() : a[0].AsString();
            if (!DisplaySettings::ParseMode(text, mode)) {
                Log::Warning("[script] set_display_mode: use windowed, borderless or fullscreen, not \"", text, "\"");
                return Value{0.0f};
            }
            DisplaySettings::SetMode(mode);
            return Value{1.0f};
        };
        b["get_display_width"] = [](std::vector<Value>&) { return Value{(float)DisplaySettings::Width()}; };
        b["get_display_height"] = [](std::vector<Value>&) { return Value{(float)DisplaySettings::Height()}; };
        b["set_display_resolution"] = [](std::vector<Value>& a) {
            if (a.size() < 2) { Log::Warning("[script] set_display_resolution: needs a width and a height"); return Value{0.0f}; }
            DisplaySettings::SetResolution((int)a[0].AsFloat(), (int)a[1].AsFloat());
            return Value{1.0f};
        };
        b["get_refresh_rate"] = [](std::vector<Value>&) { return Value{(float)DisplaySettings::RefreshHz()}; };
        b["set_refresh_rate"] = [](std::vector<Value>& a) {
            if (a.empty()) { Log::Warning("[script] set_refresh_rate: needs a rate in hertz"); return Value{0.0f}; }
            DisplaySettings::SetRefreshHz((int)a[0].AsFloat());
            return Value{1.0f};
        };
        b["get_display_resolutions"] = [](std::vector<Value>&) {
            Value list = Value::MakeArray();
            for (const DisplayResolution& r : DisplaySettings::Resolutions()) list.AsArray()->push_back(Value{r.Label()});
            return list;
        };
        b["get_refresh_rates"] = [](std::vector<Value>& a) {
            const int w = a.size() >= 2 ? (int)a[0].AsFloat() : DisplaySettings::Width();
            const int h = a.size() >= 2 ? (int)a[1].AsFloat() : DisplaySettings::Height();
            Value list = Value::MakeArray();
            for (int hz : DisplaySettings::RefreshRatesForResolution(w, h)) list.AsArray()->push_back(Value{(float)hz});
            return list;
        };
        b["begin_display_confirm"] = [](std::vector<Value>&) { DisplaySettings::BeginTrial(10.f); return Value{1.0f}; };
        b["keep_display_settings"] = [](std::vector<Value>&) {
            const bool on = DisplaySettings::TrialActive();
            DisplaySettings::KeepTrial();
            return Value{on ? 1.0f : 0.0f};
        };
        b["revert_display_settings"] = [](std::vector<Value>&) {
            const bool on = DisplaySettings::TrialActive();
            DisplaySettings::RevertTrial();
            return Value{on ? 1.0f : 0.0f};
        };
        b["get_display_confirm_seconds"] = [](std::vector<Value>&) { return Value{DisplaySettings::TrialSecondsLeft()}; };
}

void OkayScriptVM::Impl::RegisterStoryBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto go = c.go;
        auto sceneOf = c.sceneOf;
        // Localization (docs/engine/localization.md): loc("key", args...) is the key's text in
        // the player's language with {0}, {1}... filled from the arguments, or {name}
        // from a map (loc("greet", map_set(map(), "player", name))); set_language("fr") switches and
        // remembers the choice for the next run; get_language(), languages() (codes),
        // language_name("fr") -> "Français". Changes reach on_language_changed(code).
        b["loc"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string()};
            auto arg = [](const Value& v) { return v.IsNumber() ? LocArg((double)v.AsFloat()) : LocArg(v.AsString()); };
            std::vector<LocArg> args;
            for (std::size_t i = 1; i < a.size(); ++i) {
                if (auto m = a[i].AsMap()) { for (const auto& kv : *m) args.push_back(LocArg::Named(kv.first, arg(kv.second))); }
                else args.push_back(arg(a[i]));
            }
            return Value{Localization::LocalizeArgs(a[0].AsString(), args)};
        };
        b["set_language"] = [](std::vector<Value>& a) {
            const std::string code = a.empty() ? std::string() : a[0].AsString();
            const bool ok = Localization::SetLanguage(code);
            if (ok) Localization::SaveSettings();
            else Log::Warning("[script] set_language: the string table has no language \"", code, "\"");
            return Value{ok ? 1.0f : 0.0f};
        };
        b["get_language"]  = [](std::vector<Value>&) { return Value{Localization::Language()}; };
        b["languages"]     = [](std::vector<Value>&) {
            Value list = Value::MakeArray();
            for (const std::string& c : Localization::Languages()) list.AsArray()->push_back(Value{c});
            return list;
        };
        b["language_name"] = [](std::vector<Value>& a) {
            return Value{Localization::LanguageName(a.empty() ? Localization::Language() : a[0].AsString())};
        };
        // Cutscenes (SequencePlayer): play_sequence("Intro") plays the Sequence Player
        // whose file is Intro (or the first one, loading Assets/Intro.okayseq),
        // stop_sequence() stops every one, sequence_playing(), sequence_time().
        b["play_sequence"] = [sceneOf, this](std::vector<Value>& a) {
            Scene* s = sceneOf();
            const bool ok = s && !a.empty() && SequencePlayer::PlayByName(*s, rt.host ? rt.host->gameObject : nullptr, a[0].AsString());
            return Value{ok ? 1.0f : 0.0f};
        };
        b["stop_sequence"] = [sceneOf](std::vector<Value>&) { if (Scene* s = sceneOf()) SequencePlayer::StopAll(*s); return Value{}; };
        b["sequence_playing"] = [sceneOf](std::vector<Value>&) {
            Scene* s = sceneOf();
            return Value{(s && SequencePlayer::Playing(*s)) ? 1.0f : 0.0f};
        };
        b["sequence_time"] = [sceneOf](std::vector<Value>&) {
            Scene* s = sceneOf();
            SequencePlayer* sp = s ? SequencePlayer::Playing(*s) : nullptr;
            return Value{sp ? sp->Time() : 0.0f};
        };
        // Dialogue, story variables and quests (DialogueRunner / StoryState / QuestLog):
        //   start_dialogue("Smith") (an object with a Dialogue Runner, else a graph name;
        //   no name = this object's runner), dialogue_active(), set_story(name, value),
        //   get_story(name), story_flag(name) / set_story_flag(name, on), start_quest(id),
        //   complete_objective(id, index), fail_quest(id), quest_state(id), quest_active(id).
        b["start_dialogue"] = [sceneOf, go](std::vector<Value>& a) {
            DialogueRunner* r = nullptr;
            if (a.empty() || a[0].AsString().empty()) { if (GameObject* g = go()) r = g->GetComponent<DialogueRunner>(); }
            if (!r && !a.empty()) r = DialogueRunner::FindByName(sceneOf(), a[0].AsString());
            if (!r) { Log::Warning("[script] start_dialogue: no Dialogue Runner named ", a.empty() ? std::string() : a[0].AsString()); return Value{false}; }
            return Value{r->StartDialogue(a.size() > 1 ? SafeInt(a[1].AsFloat()) : -1)};
        };
        b["stop_dialogue"]   = [sceneOf](std::vector<Value>&) { if (DialogueRunner* r = DialogueRunner::ActiveIn(sceneOf())) r->Stop(); return Value{}; };
        b["dialogue_active"] = [sceneOf](std::vector<Value>&) { return Value{DialogueRunner::ActiveIn(sceneOf()) != nullptr}; };
        b["set_story"] = [sceneOf](std::vector<Value>& a) {
            if (a.size() >= 2) if (StoryState* st = StoryState::Get(sceneOf(), true)) st->Set(a[0].AsString(), a[1]);
            return Value{};
        };
        b["get_story"] = [sceneOf](std::vector<Value>& a) {
            StoryState* st = StoryState::Get(sceneOf(), false);
            return (st && !a.empty()) ? Value{st->Get(a[0].AsString())} : Value{};
        };
        b["has_story"] = [sceneOf](std::vector<Value>& a) {
            StoryState* st = StoryState::Get(sceneOf(), false);
            return Value{st && !a.empty() && st->Has(a[0].AsString())};
        };
        b["set_story_flag"] = [sceneOf](std::vector<Value>& a) {
            if (!a.empty()) if (StoryState* st = StoryState::Get(sceneOf(), true)) st->SetFlag(a[0].AsString(), a.size() < 2 || a[1].AsBool());
            return Value{};
        };
        b["story_flag"] = [sceneOf](std::vector<Value>& a) {
            StoryState* st = StoryState::Get(sceneOf(), false);
            return Value{st && !a.empty() && st->HasFlag(a[0].AsString())};
        };
        b["start_quest"] = [sceneOf](std::vector<Value>& a) {
            QuestLog* q = a.empty() ? nullptr : QuestLog::Get(sceneOf(), true);
            return Value{q && q->StartQuest(a[0].AsString())};
        };
        b["complete_objective"] = [sceneOf](std::vector<Value>& a) {
            QuestLog* q = a.empty() ? nullptr : QuestLog::Get(sceneOf(), true);
            return Value{q && q->CompleteObjective(a[0].AsString(), a.size() > 1 ? SafeInt(a[1].AsFloat()) : 0)};
        };
        b["add_quest_progress"] = [sceneOf](std::vector<Value>& a) {
            QuestLog* q = a.empty() ? nullptr : QuestLog::Get(sceneOf(), true);
            return Value{q && q->AddProgress(a[0].AsString(), a.size() > 1 ? SafeInt(a[1].AsFloat()) : 0, a.size() > 2 ? SafeInt(a[2].AsFloat()) : 1)};
        };
        b["complete_quest"] = [sceneOf](std::vector<Value>& a) {
            QuestLog* q = a.empty() ? nullptr : QuestLog::Get(sceneOf(), false);
            return Value{q && q->CompleteQuest(a[0].AsString())};
        };
        b["fail_quest"] = [sceneOf](std::vector<Value>& a) {
            QuestLog* q = a.empty() ? nullptr : QuestLog::Get(sceneOf(), false);
            return Value{q && q->FailQuest(a[0].AsString())};
        };
        b["quest_state"] = [sceneOf](std::vector<Value>& a) {
            QuestLog* q = a.empty() ? nullptr : QuestLog::Get(sceneOf(), false);
            return Value{std::string(q ? q->StatusName(a[0].AsString()) : "inactive")};
        };
        b["quest_active"] = [sceneOf](std::vector<Value>& a) {
            QuestLog* q = a.empty() ? nullptr : QuestLog::Get(sceneOf(), false);
            return Value{q && q->IsActive(a[0].AsString())};
        };
        b["quest_completed"] = [sceneOf](std::vector<Value>& a) {
            QuestLog* q = a.empty() ? nullptr : QuestLog::Get(sceneOf(), false);
            return Value{q && q->IsCompleted(a[0].AsString())};
        };
        b["toggle_journal"] = [sceneOf](std::vector<Value>&) { if (QuestLog* q = QuestLog::Get(sceneOf(), false)) q->ToggleJournal(); return Value{}; };
        // The scene's look (docs/rendering/rendering.md): post_set("bloom", 1) / post_set("tonemap", "filmic"),
        // fog_set(on [, "#rrggbb"] [, start, end] [, mode [, density]]), weather_set("rain" [, intensity]),
        // time_of_day([hours]) on the scene's DayNightCycle.
        b["post_set"] = [sceneOf](std::vector<Value>& a) {
            Scene* s = sceneOf();
            if (!s || a.size() < 2) return Value{false};
            const std::string name = a[0].AsString();
            const float v = a[1].IsString() ? (float)TonemapFromName(a[1].AsString()) : a[1].AsFloat();
            const bool ok = PostSet(s->renderSettings, name, v);
            if (!ok) Log::Warning("[script] post_set: unknown setting '", name, "'");
            return Value{ok};
        };
        b["fog_set"] = [sceneOf](std::vector<Value>& a) {
            Scene* s = sceneOf();
            if (!s || a.empty()) return Value{};
            auto& rs = s->renderSettings;
            rs.fog = a[0].AsBool();
            std::size_t i = 1;
            if (i < a.size() && a[i].IsString()) {   // an optional "#rrggbb" colour
                std::string hex = a[i].AsString(); ++i;
                if (!hex.empty() && hex[0] == '#') hex.erase(0, 1);
                if (hex.size() == 6) {
                    const unsigned long v = std::strtoul(hex.c_str(), nullptr, 16);
                    rs.fogColor = Color::FromBytes((int)((v >> 16) & 0xFF), (int)((v >> 8) & 0xFF), (int)(v & 0xFF));
                    rs.fogMatchSky = false;
                }
            }
            if (i + 1 < a.size()) { rs.fogStart = a[i].AsFloat(); rs.fogEnd = a[i + 1].AsFloat(); i += 2; }
            if (i < a.size()) { rs.fogMode = a[i].IsString() ? FogModeFromName(a[i].AsString()) : SafeInt(Mathf::Clamp(a[i].AsFloat(), 0.0f, 2.0f)); ++i; }
            if (i < a.size()) rs.fogDensity = Mathf::Clamp(a[i].AsFloat(), 0.0f, 10.0f);
            return Value{};
        };
        b["weather_set"] = [sceneOf](std::vector<Value>& a) {
            Scene* s = sceneOf();
            if (!s || a.empty()) return Value{};
            auto& w = s->renderSettings.weather;
            w.preset = a[0].IsString() ? WeatherPresetFromName(a[0].AsString()) : SafeInt(Mathf::Clamp(a[0].AsFloat(), 0.0f, 3.0f));
            if (a.size() > 1) w.intensity = Mathf::Clamp01(a[1].AsFloat());
            return Value{};
        };
        // Wind (docs/gameplay/wind.md): wind_speed([x, y, z]) in metres per second where it is asked (default: where the camera is),
        // wind_x / wind_y / wind_z(x, y, z) the direction parts there, wind_set(strength [, yawDegrees]) and
        // wind_gust(amount [, gustsPerSecond]) change the scene's own wind (made when the scene has none).
        auto windAt = [sceneOf](std::vector<Value>& a) -> Vec3 {
            Vec3 p{0, 0, 0};
            if (a.size() >= 3) p = {a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()};
            else if (Scene* s = sceneOf()) { if (s->mainCamera && s->mainCamera->transform) p = s->mainCamera->transform->Position(); }
            return WindField::At(p);
        };
        b["wind_speed"] = [windAt](std::vector<Value>& a) { return Value{windAt(a).Magnitude()}; };
        b["wind_x"] = [windAt](std::vector<Value>& a) { return Value{windAt(a).x}; };
        b["wind_y"] = [windAt](std::vector<Value>& a) { return Value{windAt(a).y}; };
        b["wind_z"] = [windAt](std::vector<Value>& a) { return Value{windAt(a).z}; };
        b["wind_set"] = [sceneOf](std::vector<Value>& a) {
            Scene* s = sceneOf();
            if (!s || a.empty()) return Value{};
            WindZone* z = WindZone::EnsureGlobal(*s);
            const float st = a[0].AsFloat();
            if (std::isfinite(st)) z->strength = Mathf::Clamp(st, -200.0f, 200.0f);
            if (a.size() > 1 && std::isfinite(a[1].AsFloat()))   // yaw: 0 = toward +Z, 90 = toward +X
                z->Aim({std::sin(a[1].AsFloat() * 0.0174532925f), 0.0f, std::cos(a[1].AsFloat() * 0.0174532925f)});
            return Value{};
        };
        b["wind_gust"] = [sceneOf](std::vector<Value>& a) {
            Scene* s = sceneOf();
            if (!s || a.empty()) return Value{};
            WindZone* z = WindZone::EnsureGlobal(*s);
            if (std::isfinite(a[0].AsFloat())) z->gustAmount = Mathf::Clamp01(a[0].AsFloat());
            if (a.size() > 1 && std::isfinite(a[1].AsFloat())) z->gustFrequency = Mathf::Clamp(a[1].AsFloat(), 0.01f, 10.0f);
            return Value{};
        };
        b["time_of_day"] = [sceneOf](std::vector<Value>& a) {
            Scene* s = sceneOf();
            DayNightCycle* dn = s ? s->FindObjectOfType<DayNightCycle>() : nullptr;
            if (!dn) return Value{-1.0f};
            if (!a.empty()) dn->SetTime(a[0].AsFloat());
            return Value{dn->Hour()};
        };
        // Keep the current volumes as the player's settings (also saved on quit).
        b["save_audio_settings"] = [](std::vector<Value>&) { AudioMixer::SaveSettings(); return Value{}; };
}

void OkayScriptVM::Impl::RegisterSplineBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto sceneOf = c.sceneOf;
        // ---- Splines, lines and trails: this object's, or a named object's -----------
        // spline_point("Rail", t): the world point a fraction t (0..1) of the way along
        // the Spline (by length, so even steps of t are even steps on the curve);
        // spline_length("Rail"); follower_set_speed([name,] speed);
        // line_set_points([name,] points) with a list of vectors or several vectors;
        // trail_clear([name]).
        auto objOf = [this, sceneOf](std::vector<Value>& a, std::size_t& first) -> GameObject* {
            first = 0;
            GameObject* g = rt.host ? rt.host->gameObject : nullptr;
            if (!a.empty() && a[0].IsString()) {
                first = 1;
                Scene* s = sceneOf();
                g = s ? ResolveObject(s, a[0]) : nullptr;
            }
            return g;
        };
        b["spline_point"] = [objOf](std::vector<Value>& a) {
            std::size_t i = 0; GameObject* g = objOf(a, i);
            const Spline* sp = g ? g->GetComponent<Spline>() : nullptr;
            if (!sp) return Value{Vec3::Zero};
            const float t = a.size() > i ? a[i].AsFloat() : 0.0f;
            return Value{sp->SampleAtDistance(t * sp->Length()).position};
        };
        b["spline_length"] = [objOf](std::vector<Value>& a) {
            std::size_t i = 0; GameObject* g = objOf(a, i);
            const Spline* sp = g ? g->GetComponent<Spline>() : nullptr;
            return Value{sp ? sp->Length() : 0.0f};
        };
        b["follower_set_speed"] = [objOf](std::vector<Value>& a) {
            std::size_t i = 0; GameObject* g = objOf(a, i);
            SplineFollower* f = g ? g->GetComponent<SplineFollower>() : nullptr;
            if (!f || a.size() <= i) return Value{0.0f};
            f->speed = a[i].AsFloat();
            return Value{1.0f};
        };
        b["line_set_points"] = [objOf](std::vector<Value>& a) {
            std::size_t i = 0; GameObject* g = objOf(a, i);
            LineRenderer* lr = g ? g->GetComponent<LineRenderer>() : nullptr;
            if (!lr) return Value{0.0f};
            std::vector<Vec3> pts;
            for (std::size_t k = i; k < a.size(); ++k) {
                if (auto arr = a[k].AsArray()) for (const Value& v : *arr) pts.push_back(v.AsVec3());
                else pts.push_back(a[k].AsVec3());
            }
            lr->positions = std::move(pts);
            return Value{(float)lr->positions.size()};
        };
        b["trail_clear"] = [objOf](std::vector<Value>& a) {
            std::size_t i = 0; GameObject* g = objOf(a, i);
            TrailRenderer* tr = g ? g->GetComponent<TrailRenderer>() : nullptr;
            if (tr) tr->Clear();
            return Value{tr ? 1.0f : 0.0f};
        };
}

void OkayScriptVM::Impl::RegisterMusicBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto go = c.go;
        // Music: the scene's MusicPlayer. music_play(index or "file"), music_next(),
        // music_previous(), music_stop([fade seconds]), music_pause(), music_resume().
        auto music = [go]() -> MusicPlayer* {
            GameObject* g = go();
            if (!g || !g->scene()) return nullptr;
            auto all = g->scene()->FindObjectsOfType<MusicPlayer>();
            return all.empty() ? nullptr : all.front();
        };
        b["music_play"] = [music](std::vector<Value>& a) {
            MusicPlayer* m = music();
            if (!m) return Value{false};
            if (a.empty()) return Value{m->Play(m->CurrentIndex() < 0 ? 0 : m->CurrentIndex())};
            if (a[0].IsString()) return Value{m->Play(a[0].AsString())};
            return Value{m->Play(SafeInt(a[0].AsFloat()))};
        };
        b["music_next"]     = [music](std::vector<Value>&) { if (MusicPlayer* m = music()) m->Next(); return Value{}; };
        b["music_previous"] = [music](std::vector<Value>&) { if (MusicPlayer* m = music()) m->Previous(); return Value{}; };
        b["music_stop"]     = [music](std::vector<Value>& a) { if (MusicPlayer* m = music()) m->Stop(a.empty() ? 1.0f : a[0].AsFloat()); return Value{}; };
        b["music_pause"]    = [music](std::vector<Value>&) { if (MusicPlayer* m = music()) m->Pause(); return Value{}; };
        b["music_resume"]   = [music](std::vector<Value>&) { if (MusicPlayer* m = music()) m->Resume(); return Value{}; };
        // Ducking: set_ducking(on [, level]) dips the Music group while Voice plays.
        b["set_ducking"] = [](std::vector<Value>& a) {
            AudioMixer::duckMusicUnderVoice = a.empty() || a[0].AsBool();
            if (a.size() >= 2) AudioMixer::duckLevel = Mathf::Clamp(a[1].AsFloat(), 0.0f, 1.0f);
            return Value{};
        };
        b["mute_group"] = [](std::vector<Value>& a) {
            AudioGroup g;
            if (!a.empty() && AudioMixer::GroupFromName(a[0].AsString().c_str(), g))
                AudioMixer::groupMuted[(int)g] = a.size() < 2 || a[1].AsBool();
            return Value{};
        };
}

void OkayScriptVM::Impl::RegisterRaycast2DBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        // Physics queries: raycast_hit(ox, oy, dx, dy [, maxDist]) returns 1 if
        // the ray hits a collider, and overlap(x, y) returns 1 if a collider
        // contains that point. Great for shooting, ground checks, line-of-sight.
        b["raycast_hit"] = [this](std::vector<Value>& a) {
            if (a.size() < 4 || !rt.host || !rt.host->gameObject) return Value{false};
            Scene* sc = rt.host->gameObject->scene();
            if (!sc) return Value{false};
            Vec2 origin{a[0].AsFloat(), a[1].AsFloat()};
            Vec2 dir{a[2].AsFloat(), a[3].AsFloat()};
            float maxDist = a.size() > 4 ? a[4].AsFloat() : 1e9f;
            return Value{sc->physics().Raycast(*sc, origin, dir, maxDist).hit};
        };
        // raycast(ox, oy, dx, dy [, maxDist]) casts a ray and returns the name of
        // the object hit (empty string = nothing). Hit details are then available
        // via ray_hit()/ray_object()/ray_x()/ray_y()/ray_dist()/ray_nx()/ray_ny().
        b["raycast"] = [this](std::vector<Value>& a) -> Value {
            rt.lastHit2D = Runtime::LastHit{};
            if (a.size() < 4 || !rt.host || !rt.host->gameObject) return Value{std::string{}};
            Scene* sc = rt.host->gameObject->scene();
            if (!sc) return Value{std::string{}};
            Vec2 origin{a[0].AsFloat(), a[1].AsFloat()};
            Vec2 dir{a[2].AsFloat(), a[3].AsFloat()};
            float maxDist = a.size() > 4 ? a[4].AsFloat() : 1e9f;
            // Optional filters: [, layers [, ignore_triggers]] (as for raycast3).
            std::uint32_t mask = Physics2D::kAllLayers;
            if (a.size() > 5) mask = a[5].IsString() ? Layers::MaskFromList(a[5].AsString())
                                   : (a[5].AsFloat() <= 0.0f ? Physics2D::kAllLayers : SafeUInt32(a[5].AsFloat()));
            bool trig = a.size() > 6 ? !a[6].AsBool() : true;
            RaycastHit2D h = sc->physics().Raycast(*sc, origin, dir, maxDist, nullptr, mask, trig);
            sc->physics().RecordQueryRay(origin, dir, maxDist, h);
            if (h.hit) {
                rt.lastHit2D.hit = true;
                rt.lastHit2D.object = h.gameObject ? h.gameObject->name : std::string{};
                rt.lastHit2D.px = h.point.x;   rt.lastHit2D.py = h.point.y;
                rt.lastHit2D.nx = h.normal.x;  rt.lastHit2D.ny = h.normal.y;
                rt.lastHit2D.dist = h.distance;
            }
            return Value{rt.lastHit2D.object};
        };
        b["ray_hit"]    = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit2D.hit}; };
        b["ray_object"] = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit2D.object}; };
        b["ray_x"]      = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit2D.px}; };
        b["ray_y"]      = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit2D.py}; };
        b["ray_nx"]     = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit2D.nx}; };
        b["ray_ny"]     = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit2D.ny}; };
        b["ray_dist"]   = [this](std::vector<Value>&) -> Value { return Value{rt.lastHit2D.dist}; };
        // raycast_all(ox, oy, dx, dy [, maxDist [, layers [, ignore_triggers]]]) -> names of
        // everything the 2D ray passes through, nearest first (details: ray_all_*).
        b["raycast_all"] = [this](std::vector<Value>& a) -> Value {
            rt.lastHitAll.clear();
            Value out = Value::MakeArray();
            if (a.size() < 4 || !rt.host || !rt.host->gameObject) return out;
            Scene* sc = rt.host->gameObject->scene();
            if (!sc) return out;
            Vec2 origin{a[0].AsFloat(), a[1].AsFloat()};
            Vec2 dir{a[2].AsFloat(), a[3].AsFloat()};
            float maxDist = a.size() > 4 ? a[4].AsFloat() : 1e9f;
            std::uint32_t mask = Physics2D::kAllLayers;
            if (a.size() > 5) mask = a[5].IsString() ? Layers::MaskFromList(a[5].AsString())
                                   : (a[5].AsFloat() <= 0.0f ? Physics2D::kAllLayers : SafeUInt32(a[5].AsFloat()));
            bool trig = a.size() > 6 ? !a[6].AsBool() : true;
            auto hits = sc->physics().RaycastAll(*sc, origin, dir, maxDist, nullptr, mask, trig);
            sc->physics().RecordQueryRay(origin, dir, maxDist, hits.empty() ? RaycastHit2D{} : hits.front());
            for (const RaycastHit2D& h : hits) {
                Runtime::LastHit l;
                l.hit = true; l.object = h.gameObject ? h.gameObject->name : std::string{};
                l.px = h.point.x; l.py = h.point.y; l.nx = h.normal.x; l.ny = h.normal.y;
                l.dist = h.distance;
                rt.lastHitAll.push_back(l);
                out.AsArray()->push_back(Value{l.object});
            }
            return out;
        };
        b["overlap"] = [this](std::vector<Value>& a) {
            if (a.size() < 2 || !rt.host || !rt.host->gameObject) return Value{false};
            Scene* sc = rt.host->gameObject->scene();
            if (!sc) return Value{false};
            return Value{sc->physics().OverlapPoint(*sc, Vec2{a[0].AsFloat(), a[1].AsFloat()}) != nullptr};
        };
        b["set_gravity"] = [this](std::vector<Value>& a) {
            if (rt.host && rt.host->gameObject && rt.host->gameObject->scene())
                rt.host->gameObject->scene()->physics().gravity =
                    Vec2{a.size() > 0 ? a[0].AsFloat() : 0.0f, a.size() > 1 ? a[1].AsFloat() : 0.0f};
            return Value{};
        };
}

void OkayScriptVM::Impl::RegisterSceneLoadBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        // Load another scene at end of frame (level transitions, restart).
        b["load_scene"] = [this](std::vector<Value>& a) {
            if (!a.empty() && rt.host && rt.host->gameObject && rt.host->gameObject->scene() &&
                ScriptPathOk(a[0].AsString(), "load_scene"))
                rt.host->gameObject->scene()->RequestLoad(a[0].AsString());
            return Value{};
        };
        // Scene Manager (build list) — load by index/name, next, reload, query.
        b["load_scene_index"] = [this](std::vector<Value>& a) {
            Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            if (s && !a.empty()) return Value{SceneManager::LoadScene(*s, SafeInt(a[0].AsFloat())) ? 1.0f : 0.0f};
            return Value{0.0f};
        };
        b["load_scene_name"] = [this](std::vector<Value>& a) {
            Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            if (s && !a.empty()) {
                // A build-list name is fine; a raw path must stay inside the game.
                const std::string name = a[0].AsString();
                if (SceneManager::IndexOf(name) < 0 && !ScriptPathOk(name, "load_scene_name")) return Value{0.0f};
                return Value{SceneManager::LoadSceneByName(*s, name) ? 1.0f : 0.0f};
            }
            return Value{0.0f};
        };
        b["load_next_scene"] = [this](std::vector<Value>&) {
            Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            return Value{(s && SceneManager::LoadNextScene(*s)) ? 1.0f : 0.0f};
        };
        // Additively MERGE another scene into the running one (seamless worlds): the
        // current scene is kept and the named scene's objects are added, offset by
        // (x,y[,z]). load_scene_additive("Town", 50, 0) drops the Town chunk 50 units
        // to the right of what's already loaded. Returns 1 if the merge was queued.
        b["load_scene_additive"] = [this](std::vector<Value>& a) {
            Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            if (s && !a.empty()) {
                const std::string name = a[0].AsString();
                if (SceneManager::IndexOf(name) < 0 && !ScriptPathOk(name, "load_scene_additive")) return Value{0.0f};
                std::string path = SceneManager::PathForName(name);
                if (path.empty()) return Value{0.0f};
                Vec3 off{0, 0, 0};
                if (a.size() >= 4)      off = {a[1].AsFloat(), a[2].AsFloat(), a[3].AsFloat()};
                else if (a.size() >= 3) off = {a[1].AsFloat(), a[2].AsFloat(), 0.0f};
                s->RequestMerge(path, off);
                return Value{1.0f};
            }
            return Value{0.0f};
        };
        b["reload_scene"] = [this](std::vector<Value>&) {
            Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            return Value{(s && SceneManager::ReloadScene(*s)) ? 1.0f : 0.0f};
        };
        b["scene_count"] = [](std::vector<Value>&) { return Value{(float)SceneManager::SceneCount()}; };
        b["scene_index"] = [](std::vector<Value>&) { return Value{(float)SceneManager::ActiveIndex()}; };
        b["scene_name"]  = [this](std::vector<Value>&) {
            Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            return Value{s ? s->Name() : std::string{}};
        };
}

} // namespace okay
