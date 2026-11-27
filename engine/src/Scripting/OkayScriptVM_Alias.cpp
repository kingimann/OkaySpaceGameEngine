#include "OkayScriptVMInternal.hpp"

namespace okay {

void OkayScriptVM::Impl::RegisterFriendlyAliasBuiltins(BuiltinCtx& c) {
        auto alias = c.alias;
        // ---- Friendly aliases: intuitive names for common builtins so games
        //      read naturally. Each points at an already-registered function. ----
        alias("delta_time", "dt");
        alias("get_key", "key");
        alias("get_key_down", "key_down");
        alias("get_key_up", "key_up");
        alias("is_key_down", "key");
        alias("random", "rand");
        alias("random_int", "randi");
        alias("random_range", "rand");
        alias("pick", "choose");
        alias("distance", "dist");
        alias("distance_to", "dist_to");
        alias("instantiate", "spawn");
        alias("instantiate3", "spawn3");
        alias("destroy_self", "destroy");
        alias("translate", "move");
        alias("rotate_z", "rotate");
        alias("set_position", "set_pos");
        alias("set_rotation", "set_rot3");
        alias("play_audio", "play_sound");
        alias("to_string", "to_str");
        alias("to_number", "to_num");
        alias("str", "to_str");
        alias("num", "to_num");
        alias("string_length", "str_len");
        alias("get_x", "pos_x");
        alias("get_y", "pos_y");
        alias("get_z", "pos_z");
        alias("velocity", "set_velocity");
        alias("debug", "debug_log");

}

void OkayScriptVM::Impl::RegisterCsApiBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto alias = c.alias;
        // ---- API: write scripts that look like C#. -----
        //      Dotted names are single tokens (see the lexer), so these read as
        //      Input.GetKeyDown("space"), Mathf.Sin(t), Debug.Log(x), etc.
        b["Vector3"] = [](std::vector<Value>& a) -> Value {
            return Value{Vec3{a.size() > 0 ? a[0].AsFloat() : 0.0f,
                              a.size() > 1 ? a[1].AsFloat() : 0.0f,
                              a.size() > 2 ? a[2].AsFloat() : 0.0f}};
        };
        b["Vector2"] = [](std::vector<Value>& a) -> Value {
            return Value{Vec3{a.size() > 0 ? a[0].AsFloat() : 0.0f,
                              a.size() > 1 ? a[1].AsFloat() : 0.0f, 0.0f}};
        };
        // Color(r, g, b) -> a Vec3 of RGB (pass to set_color / set_tint).
        b["Color"] = [](std::vector<Value>& a) -> Value {
            return Value{Vec3{a.size() > 0 ? a[0].AsFloat() : 1.0f,
                              a.size() > 1 ? a[1].AsFloat() : 1.0f,
                              a.size() > 2 ? a[2].AsFloat() : 1.0f}};
        };
        // Vector math on Vec3 values (from Vector3(...)/new Vector3(...)). Read
        // components with v.x/v.y/v.z (see property access) or vec_x/y/z(v).
        // Construct a vector value from components: vec3(x, y, z) / vec2(x, y).
        b["vec3"] = [](std::vector<Value>& a) -> Value {
            return Value{Vec3{a.size() > 0 ? a[0].AsFloat() : 0.0f,
                              a.size() > 1 ? a[1].AsFloat() : 0.0f,
                              a.size() > 2 ? a[2].AsFloat() : 0.0f}};
        };
        b["vec2"] = [](std::vector<Value>& a) -> Value {
            return Value{Vec3{a.size() > 0 ? a[0].AsFloat() : 0.0f, a.size() > 1 ? a[1].AsFloat() : 0.0f, 0.0f}};
        };
        b["vec_cross"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{Vec3::Zero};
            Vec3 u = a[0].AsVec3(), v = a[1].AsVec3();
            return Value{Vec3{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x}};
        };
        b["vec_reflect"] = [](std::vector<Value>& a) -> Value {
            // Reflect direction d about a normal n:  d - 2*(d·n)*n.
            if (a.size() < 2) return Value{Vec3::Zero};
            Vec3 d = a[0].AsVec3(), n = a[1].AsVec3();
            float dn = d.x * n.x + d.y * n.y + d.z * n.z;
            return Value{Vec3{d.x - 2.0f * dn * n.x, d.y - 2.0f * dn * n.y, d.z - 2.0f * dn * n.z}};
        };
        b["vec_clamp_len"] = [](std::vector<Value>& a) -> Value {
            // Clamp a vector's magnitude to at most maxLen (Vector3.ClampMagnitude).
            if (a.empty()) return Value{Vec3::Zero};
            Vec3 v = a[0].AsVec3(); float maxLen = a.size() > 1 ? a[1].AsFloat() : 1.0f;
            float m = v.Magnitude();
            if (m > maxLen && m > 0.0001f) { float s = maxLen / m; v = Vec3{v.x * s, v.y * s, v.z * s}; }
            return Value{v};
        };
        b["vec_add"]   = [](std::vector<Value>& a) -> Value { return Value{(a.size() > 0 ? a[0].AsVec3() : Vec3::Zero) + (a.size() > 1 ? a[1].AsVec3() : Vec3::Zero)}; };
        b["vec_sub"]   = [](std::vector<Value>& a) -> Value { return Value{(a.size() > 0 ? a[0].AsVec3() : Vec3::Zero) - (a.size() > 1 ? a[1].AsVec3() : Vec3::Zero)}; };
        b["vec_scale"] = [](std::vector<Value>& a) -> Value { return Value{(a.size() > 0 ? a[0].AsVec3() : Vec3::Zero) * (a.size() > 1 ? a[1].AsFloat() : 1.0f)}; };
        b["vec_length"]= [](std::vector<Value>& a) -> Value { return Value{a.empty() ? 0.0f : a[0].AsVec3().Magnitude()}; };
        b["vec_dot"]   = [](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{0.0f};
            Vec3 u = a[0].AsVec3(), v = a[1].AsVec3();
            return Value{u.x * v.x + u.y * v.y + u.z * v.z};
        };
        b["vec_distance"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{0.0f};
            return Value{(a[0].AsVec3() - a[1].AsVec3()).Magnitude()};
        };
        b["vec_normalize"] = [](std::vector<Value>& a) -> Value {
            if (a.empty()) return Value{Vec3::Zero};
            Vec3 v = a[0].AsVec3(); float m = v.Magnitude();
            return Value{m > 1e-6f ? v * (1.0f / m) : Vec3::Zero};
        };
        // Angle in degrees between two vectors (Vector3.Angle).
        b["vec_angle"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{0.0f};
            Vec3 u = a[0].AsVec3(), v = a[1].AsVec3();
            float mu = u.Magnitude(), mv = v.Magnitude();
            if (mu < 1e-6f || mv < 1e-6f) return Value{0.0f};
            float c = (u.x * v.x + u.y * v.y + u.z * v.z) / (mu * mv);
            return Value{std::acos(Mathf::Clamp(c, -1.0f, 1.0f)) * Mathf::Rad2Deg};
        };
        b["vec_lerp"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 3) return Value{Vec3::Zero};
            Vec3 u = a[0].AsVec3(), v = a[1].AsVec3(); float t = a[2].AsFloat();
            t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
            return Value{u + (v - u) * t};
        };
        b["vec_x"] = [](std::vector<Value>& a) -> Value { return Value{a.empty() ? 0.0f : a[0].AsVec3().x}; };
        b["vec_y"] = [](std::vector<Value>& a) -> Value { return Value{a.empty() ? 0.0f : a[0].AsVec3().y}; };
        b["vec_z"] = [](std::vector<Value>& a) -> Value { return Value{a.empty() ? 0.0f : a[0].AsVec3().z}; };
        b["vec_move_towards"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 3) return Value{Vec3::Zero};
            Vec3 c = a[0].AsVec3(), t = a[1].AsVec3(); float md = a[2].AsFloat();
            Vec3 d = t - c; float dist = d.Magnitude();
            if (dist <= md || dist < 1e-6f) return Value{t};
            return Value{c + d * (md / dist)};
        };
        // Smooth 2D value noise in [0,1].
        b["noise"] = [](std::vector<Value>& a) -> Value {
            float x = a.size() > 0 ? a[0].AsFloat() : 0.0f;
            float y = a.size() > 1 ? a[1].AsFloat() : 0.0f;
            // Unsigned arithmetic wraps instead of overflowing (same values as before).
            auto hash = [](unsigned xi, unsigned yi) {
                unsigned h = xi * 374761393u + yi * 668265263u;
                h = (h ^ (h >> 13)) * 1274126177u;
                return (float)((h ^ (h >> 16)) & 0xFFFFFFu) / (float)0xFFFFFFu;
            };
            // Lattice cell (floor of v, wrapped mod 2^32 like the hash) and the
            // fraction within it, safe for huge or non-finite coordinates.
            auto cell = [](float v, unsigned& c) -> float {
                if (!std::isfinite(v)) { c = 0; return 0.0f; }
                const double f = std::floor((double)v);
                c = (unsigned)(long long)std::fmod(f, 4294967296.0);
                return (float)((double)v - f);
            };
            unsigned x0, y0;
            float fx = cell(x, x0), fy = cell(y, y0);
            auto sm = [](float t) { return t * t * (3.0f - 2.0f * t); };
            float sx = sm(fx), sy = sm(fy);
            float n00 = hash(x0, y0), n10 = hash(x0 + 1, y0);
            float n01 = hash(x0, y0 + 1), n11 = hash(x0 + 1, y0 + 1);
            float nx0 = n00 + (n10 - n00) * sx, nx1 = n01 + (n11 - n01) * sx;
            return Value{nx0 + (nx1 - nx0) * sy};
        };
        alias("vec3", "Vector3");
        alias("vec2", "Vector2");
        alias("Vector3.Lerp", "vec_lerp");
        alias("Vector3.Dot", "vec_dot");
        alias("Vector3.Normalize", "vec_normalize");
        alias("Vector3.MoveTowards", "vec_move_towards");
        alias("Mathf.PerlinNoise", "noise");
        alias("Mathf.Clamp01", "clamp01");
        alias("Mathf.InverseLerp", "inverse_lerp");
        alias("Mathf.Repeat", "math_repeat");
        alias("Mathf.DeltaAngle", "delta_angle");
        alias("Mathf.LerpAngle", "lerp_angle");
        alias("Mathf.Approximately", "approximately");
        alias("Mathf.Log", "log");   alias("Mathf.Exp", "exp");
        alias("Mathf.Asin", "asin"); alias("Mathf.Acos", "acos"); alias("Mathf.Atan", "atan");
        alias("Color.Lerp", "lerp_color");
        alias("Vector3.LerpUnclamped", "vec_lerp");
        alias("Vector2.Lerp", "vec_lerp");
        alias("Vector3.Angle", "vec_angle");
        alias("Vector2.Angle", "vec_angle");
        alias("Input.GetButton", "key");
        alias("Input.GetButtonDown", "key_down");
        alias("Input.GetButtonUp", "key_up");
        alias("string.Format", "format");
        alias("String.Format", "format");
        // Quaternion.Euler(x, y, z) -> a Vec3 of euler angles (assignable to
        // transform.rotation, which uses the Z component for 2D).
}

void OkayScriptVM::Impl::RegisterPlainAliasBuiltins(BuiltinCtx& c) {
        auto alias = c.alias;
        // ---- Plain-English aliases: say what you mean. Every name below points
        //      at an existing builtin, so beginners can guess the right word. ----
        // Input
        alias("is_key_held", "key");
        alias("key_pressed", "key_down");
        alias("key_released", "key_up");
        alias("input_horizontal", "axis_x");
        alias("input_vertical", "axis_y");
        alias("move_input_x", "axis_x");
        alias("move_input_y", "axis_y");
        alias("is_mouse_held", "mouse");
        alias("mouse_pressed", "mouse_down");
        alias("mouse_released", "mouse_up");
        alias("mouse_position_x", "mouse_x");
        alias("mouse_position_y", "mouse_y");
        // Transform / movement
        alias("move_by", "move");
        alias("shake", "tween_shake");   // shake(intensity, dur) — shorter name
        alias("face", "look_at");        // face("Name") — turn to look at an object
        alias("place_at", "set_pos");
        alias("set_position_x", "set_x");
        alias("set_position_y", "set_y");
        alias("position_x", "pos_x");
        alias("position_y", "pos_y");
        alias("position_z", "pos_z");
        alias("turn", "rotate");
        alias("face", "look_at");
        alias("chase", "follow");
        alias("run_from", "flee");
        alias("wasd_move", "on_key_move");
        alias("wasd_move3", "on_key_move3");
        alias("face_3d", "look_at3");
        alias("set_size", "set_scale");
        alias("move_towards_object", "move_toward3");
        // Time & scheduling
        alias("frame_time", "dt");
        alias("time_elapsed", "time");
        alias("game_time", "time");
        alias("call_later", "after");
        alias("invoke_after", "after");
        alias("repeat_every", "every");
        alias("set_time_scale", "set_timescale");
        alias("time_scale", "timescale");
        alias("Time.PushPause", "push_pause");
        alias("Time.PopPause", "pop_pause");
        alias("Time.IsPausedBy", "is_paused_by");
        alias("Time.HitStop", "hit_stop");
        alias("Time.SlowMotion", "slow_motion");
        alias("Time.RampTimeScale", "ramp_timescale");
        alias("ramp_time_scale", "ramp_timescale");
        // Objects & scene
        alias("create_object", "spawn");
        alias("create_object_3d", "spawn3");
        alias("remove_object", "destroy");
        alias("show_object", "activate");
        alias("hide_object", "deactivate");
        alias("object_exists", "exists");
        alias("is_object_active", "is_active");
        alias("object_x", "obj_x");
        alias("object_y", "obj_y");
        alias("object_z", "obj_z");
        alias("nearest_with_tag", "nearest_tag");
        alias("count_with_tag", "count_tag");
        // Rendering & components
        alias("set_tint", "set_color");
        alias("set_sprite", "set_texture");
        alias("set_background_color", "set_bg");
        alias("set_background", "set_bg");
        alias("set_light_direction", "set_light");
        alias("set_ui_image", "set_image");
        alias("set_button_enabled", "set_interactable");
        alias("set_progress_bar", "set_progress");
        alias("emit_particles", "emit");
        alias("play_sfx", "play_sound");
        alias("set_flip_x", "flip_x");
        alias("set_flip_y", "flip_y");
        // Camera
        alias("camera_x", "cam_x");
        alias("camera_y", "cam_y");
        alias("set_camera", "set_cam");
        alias("move_camera", "move_cam");
        alias("camera_zoom", "cam_zoom");
        alias("set_camera_zoom", "set_cam_zoom");
        alias("set_zoom", "set_cam_zoom");
        alias("shake_camera", "camera_shake");
        alias("cam_shake", "camera_shake");
        alias("zoom_camera", "camera_zoom_to");
        // Physics
        alias("is_overlapping", "overlap");
        alias("point_overlaps", "overlap");
        alias("cast_ray", "raycast");
        alias("apply_force", "add_force");
        alias("apply_impulse", "add_impulse");
        alias("get_velocity_x", "velocity_x");
        alias("get_velocity_y", "velocity_y");
        // Saving / prefs
        alias("save_value", "prefs_set");
        alias("load_number", "prefs_get");
        alias("load_value", "prefs_get");
        alias("load_text", "prefs_get_str");
        alias("write_save", "prefs_save");
        alias("read_save", "prefs_load");
        alias("set_master_volume", "set_volume");
        // Math
        alias("random_float", "rand");
        alias("random_chance", "chance");
        alias("roll_chance", "chance");
        alias("blend", "lerp");
        // Logging
        alias("log", "print");
        alias("log_message", "print");
        // json_stringify / json_parse come with to_json / from_json (CoreBuiltins.hpp InstallJsonBuiltins).

}

void OkayScriptVM::Impl::RegisterCsCompatBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto tf = c.tf;
        auto alias = c.alias;
        // ---- C# compatibility -------------------------------------------
        // Collections created by `new List<T>()`, `new Dictionary<K,V>()`, `new T[n]`
        // (the parser lowers those to these calls). Lists/arrays share one type.
        b["__new_list"] = okayscript::detail::NewList<Value>;     // the language core's (okayscript/detail/Support.hpp)
        b["__new_array"] = okayscript::detail::NewArray<Value>;
        b["__new_map"] = okayscript::detail::NewMap<Value>;
        // Invoke("Fire", 2f) / InvokeRepeating("Spawn", 1f, 0.5f) / CancelInvoke().
        b["Invoke"] = [this](std::vector<Value>& a) {
            if (!a.empty()) rt.timers.push_back({a.size() > 1 ? a[1].AsFloat() : 0.0f, 0.0f, a[0].AsString(), false});
            return Value{};
        };
        b["InvokeRepeating"] = [this](std::vector<Value>& a) {
            if (a.empty()) return Value{};
            float first = a.size() > 1 ? a[1].AsFloat() : 0.0f, rate = a.size() > 2 ? a[2].AsFloat() : 1.0f;
            if (rate <= 0.0f) rate = 0.0001f;
            rt.timers.push_back({first, rate, a[0].AsString(), false});
            return Value{};
        };
        b["CancelInvoke"] = [this](std::vector<Value>& a) {
            if (a.empty()) rt.timers.clear();
            else { std::string fn = a[0].AsString(); for (auto& t : rt.timers) if (t.fn == fn) t.dead = true; }
            return Value{};
        };
        // transform.Translate(Vector3 / x, y, z) moves in the object's own space
        //; pass Space.World to move in world axes.
        // The two-number OkayScript form translate(x, y) stays a plain 2D move.
        b["transform.Translate"] = [tf](std::vector<Value>& a) {
            Transform* t = tf(); if (!t || a.empty()) return Value{};
            if (a.size() == 2 && !a[0].IsVec3()) { t->Translate({a[0].AsFloat(), a[1].AsFloat(), 0.0f}); return Value{}; }
            Vec3 d; std::size_t next;
            if (a[0].IsVec3()) { d = a[0].AsVec3(); next = 1; }
            else { d = {a[0].AsFloat(), a.size() > 1 ? a[1].AsFloat() : 0.0f, a.size() > 2 ? a[2].AsFloat() : 0.0f}; next = 3; }
            const bool world = a.size() > next && a[next].AsFloat() != 0.0f;
            t->Translate(world ? d : t->localRotation * d);
            return Value{};
        };
        // transform.Rotate(x, y, z) / Rotate(Vector3) / Rotate(axis, angle); one number = 2D spin about Z.
        b["transform.Rotate"] = [tf](std::vector<Value>& a) {
            Transform* t = tf(); if (!t || a.empty()) return Value{};
            if (a[0].IsVec3() && a.size() >= 2 && !a[1].IsVec3() && a[1].AsFloat() != 0.0f && a.size() == 2 &&
                std::fabs(a[0].AsVec3().Magnitude() - 1.0f) < 1e-3f) {
                t->localRotation = (t->localRotation * Quat::AngleAxis(a[1].AsFloat(), a[0].AsVec3())).Normalized();
                return Value{};
            }
            Vec3 e = a[0].IsVec3() ? a[0].AsVec3()
                   : (a.size() == 1 ? Vec3{0, 0, a[0].AsFloat()}
                                    : Vec3{a[0].AsFloat(), a.size() > 1 ? a[1].AsFloat() : 0.0f, a.size() > 2 ? a[2].AsFloat() : 0.0f});
            t->Rotate(e);
            return Value{};
        };
        alias("Debug.LogWarning", "log_warn");
        alias("Debug.LogError", "log_error");
        alias("Debug.LogFormat", "format");
        b["Input.GetAxisRaw"] = [](std::vector<Value>& a) -> Value {
            std::string ax = a.empty() ? std::string{} : a[0].AsString();
            float v = ax == "Vertical" ? Input::AxisWASD().y : Input::AxisWASD().x;
            return Value{v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f)};
        };
        b["string.IsNullOrEmpty"] = [](std::vector<Value>& a) { return Value{a.empty() || a[0].AsString().empty()}; };
        b["string.IsNullOrWhiteSpace"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{true};
            for (char c : a[0].AsString()) if (!std::isspace((unsigned char)c)) return Value{false};
            return Value{true};
        };
        b["string.Join"] = [](std::vector<Value>& a) {   // string.Join(", ", items)
            std::string sep = a.empty() ? "" : a[0].AsString(), out;
            if (a.size() > 1)
                if (auto arr = a[1].AsArray())
                    for (std::size_t i = 0; i < arr->size(); ++i) { if (i) out += sep; out += (*arr)[i].AsString(); }
            return Value{out};
        };
        alias("String.IsNullOrEmpty", "string.IsNullOrEmpty");
        alias("String.Join", "string.Join");
        auto parseNum = [](std::vector<Value>& a, bool asInt) {
            float f = 0.0f;
            if (!a.empty()) { try { f = std::stof(a[0].AsString()); } catch (...) { f = 0.0f; } }
            return Value{asInt ? (float)(long long)f : f};
        };
        b["int.Parse"]   = [parseNum](std::vector<Value>& a) { return parseNum(a, true); };
        b["float.Parse"] = [parseNum](std::vector<Value>& a) { return parseNum(a, false); };
        alias("double.Parse", "float.Parse");
        alias("Convert.ToInt32", "int.Parse");
        alias("Convert.ToSingle", "float.Parse");
        b["Mathf.RoundToInt"] = [](std::vector<Value>& a) { return Value{a.empty() ? 0.0f : std::round(a[0].AsFloat())}; };
        b["Mathf.FloorToInt"] = [](std::vector<Value>& a) { return Value{a.empty() ? 0.0f : std::floor(a[0].AsFloat())}; };
        b["Mathf.CeilToInt"]  = [](std::vector<Value>& a) { return Value{a.empty() ? 0.0f : std::ceil(a[0].AsFloat())}; };
        b["Mathf.Log10"]      = [](std::vector<Value>& a) { return Value{a.empty() ? 0.0f : std::log10(a[0].AsFloat())}; };
        alias("Vector3.Cross", "vec_cross");
        alias("Vector3.ClampMagnitude", "vec_clamp_len");
        alias("Vector3.Reflect", "vec_reflect");
        alias("Mathf.Clamp01", "clamp01");

}

} // namespace okay
