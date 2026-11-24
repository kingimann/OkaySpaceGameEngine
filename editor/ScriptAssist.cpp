// Script assist (ScriptAssist.hpp): the Scripting Reference window, and what the Script Editor's code widget is
// helped by: autocomplete words, signatures, hover docs, C# member completion, the help card's description and
// example, the code snippet painter, and the OkayScript palette and language definition. Moved out of main.cpp
// unchanged (docs/editor/main_split_plan.md, move 3): only the includes, the namespace lines and the linkage of the
// names the Script Editor still in main.cpp uses differ.
#include "EditorCommon.hpp"
#include "ScriptAssist.hpp"
#include "MainShared.hpp"          // g_showScriptDocs, g_codeFont, SectionHeader (main.cpp)
#include "ConsolePanel.hpp"        // ConsoleLog
#include "okay/Lua/Lua.hpp"         // okay::lua::kKeywords
#include "okay/Lua/LuaScriptVM.hpp" // LuaLanguageWords: the bound engine names and their docs for Lua scripts

namespace oked {

// In-editor OkayScript reference, so scripting is documented without leaving
// the app. Opened from Help > Scripting Reference or the Script Editor's Docs.
void DrawScriptDocs() {
    if (!g_showScriptDocs) return;
    ImGui::SetNextWindowSize(ImVec2(680, 720), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Scripting Reference", &g_showScriptDocs)) { ImGui::End(); return; }

    auto header = [](const char* s) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f), "%s", s);
        ImGui::Separator();
    };
    // Each API entry reads as a card: the signature in a code color, then its
    // description wrapped on the line below (instead of cramped into a fixed column),
    // with breathing room between entries — far easier to scan than one dense line.
    auto api = [](const char* sig, const char* desc) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.80f, 0.90f, 0.74f, 1.0f));
        ImGui::Bullet(); ImGui::SameLine(); ImGui::TextUnformatted(sig);
        ImGui::PopStyleColor();
        // Click a signature to copy it, ready to paste into a script.
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("Click to copy");
        }
        if (ImGui::IsItemClicked()) {
            ImGui::SetClipboardText(sig);
            ConsoleLog(std::string("Copied: ") + sig);
        }
        if (desc && desc[0]) {
            ImGui::Indent(24.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.66f, 0.69f, 0.75f, 1.0f));
            ImGui::TextWrapped("%s", desc);
            ImGui::PopStyleColor();
            ImGui::Unindent(24.0f);
        }
        ImGui::Spacing();
    };
    auto code = [](const char* c) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.74f, 0.88f, 0.70f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.25f));
        ImVec2 sz(-1.0f, ImGui::GetTextLineHeightWithSpacing() *
                  (float)(std::count(c, c + std::strlen(c), '\n') + 1) + 8);
        ImGui::BeginChild(ImGui::GetID(c), sz, true);
        ImGui::TextUnformatted(c);
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
    };

    // ---- Tutorial: teach the language from scratch ----
    if (ImGui::CollapsingHeader("Learn the basics", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::TextWrapped("OkayScript is a small, friendly language for game logic. "
            "You write two functions: start() runs once, update(dt) runs every frame "
            "(dt = seconds since the last frame). Everything below is just those two "
            "plus values, math, and the built-in commands.");

        SectionHeader("1. Variables");
        ImGui::TextWrapped("Make a value with 'var'. Values are numbers, text (in "
            "quotes), or true/false. End each statement with a semicolon ;");
        code("var speed = 5;\nvar name = \"Hero\";\nvar alive = true;");

        SectionHeader("2. Math & comparisons");
        ImGui::TextWrapped("%s", "Use + - * / % for math, and == != < > <= >= to compare. "
            "Combine conditions with && (and), || (or), ! (not).");
        code("var hp = 100 - 25;        // 75\nvar low = hp < 50;       // false");

        SectionHeader("3. Making decisions: if / else");
        code("if (key(\"space\")) {\n    move(0, 5 * dt);   // jump while held\n} else {\n    move(0, -1 * dt);  // gently fall\n}");

        SectionHeader("4. Repeating: while / for");
        code("for (var i = 0; i < 3; i = i + 1) {\n    spawn(\"Coin\", i, 0);\n}");

        SectionHeader("5. The two lifecycle functions");
        ImGui::TextWrapped("start() sets things up once; update(dt) drives behaviour "
            "every frame. The movement one-liners (walk, spin, follow, platformer, ...) "
            "are already dt-scaled, so you never write '* dt'.");
        code("start() {\n    set_pos(0, 0);\n    set(\"score\", 0);\n}\n\n"
             "update(dt) {\n    on_key_move(5);   // WASD / arrows, 5 units/sec\n}");

        SectionHeader("6. Your own functions");
        ImGui::TextWrapped("Group steps into a function and call it by name (or with "
            "call(\"name\") for state machines). No 'function' keyword needed, and a "
            "single-line if/for/while needs no braces.");
        code("hurt() {\n    set(\"score\", get(\"score\") - 1);\n}\n\n"
             "update(dt) {\n    if (key_down(\"x\")) hurt();\n}");

        ImGui::TextWrapped("That's the whole language. The rest is the built-in "
            "commands below (move, key, spawn, set/get, …). Hit Compile & Run in the "
            "Script Editor to try it live.");
        ImGui::Spacing();
    }

    // ---- Visual scripting: the no-code path (Actions / Flow Graph) ----
    if (ImGui::CollapsingHeader("Visual scripting (no code)")) {
        ImGui::TextWrapped("Prefer no code? Add a Visual Script (Add Component > Scripts > "
            "Visual Script) and build it in the graph: an event node, then Check and Do "
            "steps picked from menus (+ Add > Do something / Check something). Older Actions "
            "lists still run and open as a graph from the Inspector. Each chain reads:");
        ImGui::Spacing();
        ImGui::BulletText("Trigger");
        ImGui::Indent();
        ImGui::TextWrapped("WHEN it runs: On Start, On Update (every frame), On Key, On "
            "Click, On Collision, On Trigger Enter/Exit, or the On Mouse events: the event "
            "node at the start of the chain.");
        ImGui::Unindent();
        ImGui::BulletText("Conditions (optional gate)");
        ImGui::Indent();
        ImGui::TextWrapped("The instructions run only if EVERY condition passes (AND). "
            "e.g. Variable In Range, Fewer Tagged Than, Is Moving, Raycast Hits, Chance.");
        ImGui::Unindent();
        ImGui::BulletText("Instructions");
        ImGui::Indent();
        ImGui::TextWrapped("WHAT happens, run in order (a Wait pauses between steps): Move, "
            "Set Variable, Spawn, Play Sound, Win/Lose, Follow, Spawn Wave, and dozens more. "
            "Add them from the searchable + Instruction palette.");
        ImGui::Unindent();
        ImGui::Spacing();
        header("Tips");
        api("Name each script", "Type a name at the top of the Actions component so several on "
            "one object are tellable apart. It's saved in the project.");
        api("Variables are shared", "Set Variable / Add To Variable use the same named values "
            "your UI reads — a HUD Text Bind of \"Score: {score}\" follows Set Variable \"score\". "
            "The variable fields auto-suggest names you've already used.");
        api("Win / Lose / Quit", "Instructions that set the won/lost variable (and pause) or quit "
            "the game — wire them to a Trigger Zone or an On Collision.");
        api("Drop-in mechanics", "For the most common jobs there are add-and-go components under "
            "Add Component > No-Code Mechanics: Collectible, Damage On Touch, Teleporter, Trigger Zone.");
        api("Mouse events need bounds", "On Mouse* triggers fire when the cursor is over the "
            "object: a 3D object needs a Collider; a 2D object needs a Sprite or Box Collider 2D.");
        ImGui::Spacing();
    }

    ImGui::TextWrapped("OkaySpace scripts are written in OkayScript. Attach a Script component, "
        "then edit it in the Script Editor. A script's fields and functions ARE the component: "
        "no class or base type is needed. The engine calls these by name:");
    ImGui::Indent();
    ImGui::TextDisabled("Awake() / OnEnable() / Start()   once, in that order, when the scene starts");
    ImGui::TextDisabled("Update()                         every frame (Time.deltaTime = seconds since the last)");
    ImGui::TextDisabled("FixedUpdate()                    50 times a second, however fast the game draws");
    ImGui::TextDisabled("OnDisable() / OnDestroy()        when the object is destroyed");
    ImGui::Unindent();

    // Live filter so a long, complete reference stays searchable.
    static char s_docFilter[64] = "";
    ImGui::SetNextItemWidth(-70.0f);
    ImGui::InputTextWithHint("##docfilter", "Filter functions (e.g. 'net', 'tween', 'ui_')...",
                             s_docFilter, sizeof(s_docFilter));
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear##docfilter")) s_docFilter[0] = '\0';
    const bool filtering = s_docFilter[0] != '\0';

    // A section is a CollapsingHeader; when a filter is active every section is
    // force-open so matches anywhere are visible. `api`/`code` entries below check
    // the filter too, so only matching rows show. `secOpen` reports whether to draw
    // a section's body at all (skip it entirely if its title doesn't match and it has
    // no visible rows — but we always render bodies when filtering and let rows filter).
    auto lc = [](std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; };
    std::string needle = lc(s_docFilter);
    auto match = [&](const char* a, const char* b) {
        if (!filtering) return true;
        return lc(a).find(needle) != std::string::npos ||
               (b && lc(b).find(needle) != std::string::npos);
    };
    // Filtered api row.
    auto fapi = [&](const char* sig, const char* desc) { if (match(sig, desc)) api(sig, desc); };
    auto sec = [&](const char* title) {
        int flags = filtering ? ImGuiTreeNodeFlags_DefaultOpen : 0;
        return ImGui::CollapsingHeader(title, flags);
    };

    if (sec("Language & syntax")) {
        ImGui::TextWrapped("%s",
            "Blocks use braces { }, statements end with ;  (NOT Lua-style 'end').\n"
            "var x = 5;   if/else   while   for   return   break   continue\n"
            "Operators: + - * / %   == != < > <= >=   && || !\n"
            "Arrays: var a = [1,2,3]; a[0] = 10;   Maps: var m = map(); m[\"k\"] = 1;\n"
            "foreach (var item in a) { print(item); }");
        ImGui::TextWrapped("Types are real: int drops decimals, float / bool / string only hold their own "
            "kind, and function parameter and return types convert too. const and readonly can't "
            "change; static is shared by every object running the script; public fields show in "
            "the Inspector. Pasted C# with `using`, a class wrapper or a base type still runs; the "
            "editor marks those lines because they do nothing. See docs/scripting/scripting.md.");
    }

    if (sec("Movement & Transform (2D)")) {
        fapi("move(dx, dy)", "translate by (dx, dy)");
        fapi("set_pos(x, y)", "set local position");
        fapi("set_x(v) / set_y(v)", "set one position axis");
        fapi("pos_x() / pos_y()", "current local position");
        fapi("rotate(deg)", "rotate around Z by degrees");
        fapi("move_toward(x, y, step)", "step toward a point");
        fapi("look_at(\"name\")", "rotate to face another object");
    }

    if (sec("Transform & Movement (3D)")) {
        fapi("move3(dx, dy, dz)", "translate in 3D");
        fapi("set_pos3(x, y, z)", "set 3D position");
        fapi("set_z(v) / pos_z()", "Z position");
        fapi("rotate3(x, y, z)", "rotate by Euler degrees");
        fapi("set_rot3(x, y, z)", "set Euler rotation");
        fapi("set_scale(s) / set_scale3(x,y,z)", "uniform / per-axis scale");
        fapi("scale_x() / scale_y() / scale_z()", "current scale");
        fapi("move_forward(d) / move_right(d)", "move along this object's facing");
        fapi("forward_x/y/z() / right_x/y/z()", "facing direction vectors");
        fapi("look_at3(\"name\")", "face an object in 3D");
    }

    if (sec("Rigidbody (2D & 3D)")) {
        fapi("set_velocity(x,y) / set_velocity3(x,y,z)", "set velocity");
        fapi("set_vx/set_vy/set_vz(v)", "set one velocity axis");
        fapi("velocity_x() / velocity_y() / velocity_z()", "read velocity");
        fapi("add_force(x,y) / add_force3(x,y,z)", "accumulate a force");
        fapi("add_impulse(...) / add_impulse3(...)", "instant velocity change");
        fapi("jump(v)", "set upward velocity (2D or 3D body)");
        fapi("set_gravity(x,y) / set_gravity3(x,y,z)", "change world gravity");
        fapi("physics_steps() / set_physics_steps(n)", "read or set physics steps per second");
        fapi("physics_smooth() / set_physics_smooth(on)", "draw bodies between physics steps");
    }

    if (sec("Input")) {
        fapi("key(\"w\") / key_down(\"w\") / key_up(\"w\")", "keyboard held / pressed / released");
        fapi("axis_x() / axis_y()", "WASD / arrow-key axis, -1..1");
        fapi("mouse_x() / mouse_y()", "cursor position in pixels");
        fapi("mouse(btn) / mouse_down(btn) / mouse_up(btn)", "mouse buttons (0=L,1=R,2=M)");
        fapi("gamepad(btn) / gamepad_x() / gamepad_y()", "controller input");
    }

    if (sec("This object")) {
        fapi("name() / set_name(s)", "object name");
        fapi("tag() / set_tag(s) / has_tag(s)", "object tag");
        fapi("set_active(b) / self_active()", "enable / query this object");
        fapi("destroy()", "destroy this object");
        fapi("set_parent(\"name\") / detach()", "re-parent / unparent");
        fapi("set_text(s) / set_color(r,g,b,a) / set_texture(p)", "sibling renderers");
        fapi("set_mesh(\"Sphere\")", "swap a sibling MeshRenderer's primitive");
        fapi("emit(n) / play_anim() / play_sound()", "sibling FX, animation, audio");
    }

    if (sec("Other objects & scene")) {
        fapi("exists(\"n\") / is_active(\"n\")", "query an object by name");
        fapi("activate(\"n\") / deactivate(\"n\")", "show / hide an object");
        fapi("obj_x(\"n\") / obj_y(\"n\") / obj_z(\"n\")", "another object's position");
        fapi("dist_to(\"n\")", "distance to a named object");
        fapi("npc_goto(\"n\", x, y, z)", "send an NPC Controller to a point (npc_arrived on arrival)");
        fapi("npc_stop(\"n\") / npc_busy(\"n\") / npc_state(\"n\")", "cancel / query the NPC");
        fapi("nav_move_to(x, y, z) / nav_stop()", "walk this object's Nav Mesh Agent over the navmesh / stop it");
        fapi("nav_arrived() / nav_remaining()", "reached the point? / path length left");
        fapi("grid_move_to(x, y) / grid_move_cell(x, y)", "walk this object's Grid Path Agent to a world point or a tile cell");
        fapi("grid_stop() / grid_arrived()", "stop the grid walk / 1 once the cell was reached");
        fapi("grid_set_tile_cost(id, cost) / grid_set_cell_cost(x, y, cost)", "price a tile or one cell (mud, road). 0 blocks it");
        fapi("grid_block(x, y) / grid_clear()", "block a cell with this object's Grid Obstacle, or stop blocking");
        fapi("bt_set(\"key\", value) / bt_get(\"key\")", "write / read this object's Behavior Tree blackboard (bt_set(\"Name\", key, value) for another object)");
        fapi("bt_run(\"Assets/Guard.okaybt\") / bt_stop()", "load and start a behavior tree on this object / stop it; on_bt_event(name) hears Send Event tasks");
        fapi("spline_point(\"n\", t) / spline_length(\"n\")", "a point t (0-1) along a Spline / its length");
        fapi("follower_set_speed(speed) / line_set_points(points) / trail_clear()", "this object's Spline Follower, Line, Trail");
        fapi("spawner_start(\"n\") / spawner_stop(\"n\")", "run / pause a Spawner's waves");
        fapi("spawner_alive(\"n\")", "live objects a Spawner created (enemies left)");
        fapi("build_enter() / build_exit() / build_toggle()", "in-game build mode on / off (Build Mode component)");
        fapi("build_active() / build_link_mode(on)", "in build mode? / wiring (link) mode");
        fapi("build_palette_clear() / build_palette_add(name, prefab, icon)", "what players can place");
        fapi("build_palette_select(i) / build_selected()", "the selected hotbar slot (0-based)");
        fapi("build_save(\"name\") / build_load(\"name\")", "save / load what players built (save folder)");
        fapi("build_exists(\"name\") / build_clear() / build_count()", "a saved creation? / remove all / how many placed");
        fapi("build_undo() / build_redo()", "undo / redo the player's build actions");
        fapi("logic_press(\"n\") / logic_state(\"n\")", "press a logic button / read a source or target");
        fapi("on_object_placed(name) / on_object_removed(name)", "events: a player placed / deleted something");
        fapi("on_logic_signal(name)", "event: a wired target turned on / off (signal_on)");
        fapi("vel_toward(\"n\", speed)", "aim this body's velocity at a target");
        fapi("destroy_obj(\"n\")", "destroy a named object");
        fapi("count_tag(\"t\") / nearest_tag(\"t\")", "tag queries");
        fapi("set_cam(x,y) / move_cam(dx,dy) / set_cam_zoom(z)", "camera control");
        fapi("set_bg(r,g,b) / set_light(x,y,z) / set_ambient(v)", "background & lighting");
        fapi("load_scene(\"file\")", "switch to another scene");
        fapi("screen_w() / screen_h()", "viewport size in pixels");
    }

    if (sec("UI — drive scene widgets by name")) {
        fapi("ui_set_text(\"n\",\"s\") / ui_get_text(\"n\")", "text/button label/input text");
        fapi("get_clipboard_text() / set_clipboard_text(\"s\")", "read or set the text a player can copy and paste");
        fapi("ui_set_focus(\"n\")", "move keyboard/gamepad focus to a widget and show its ring");
        fapi("ui_clicked(\"n\")", "true the frame a named button was clicked");
        fapi("ui_set_interactable(\"n\", on)", "enable / grey-out a button");
        fapi("ui_slider_value(\"n\") / ui_set_slider(\"n\", v)", "read / set a slider (0..1)");
        fapi("ui_toggle_value(\"n\") / ui_set_toggle(\"n\", on)", "read / set a toggle");
        fapi("ui_dropdown_value(\"n\") / ui_dropdown_text(\"n\")", "selected index / option text");
        fapi("ui_set_dropdown(\"n\", i)", "select a dropdown option (fires on_change)");
        fapi("ui_tab_value(\"n\") / ui_tab_text(\"n\") / ui_set_tab(\"n\", i)", "selected tab index / label; select a tab (fires on_change)");
        fapi("ui_set_tab_badge(\"n\", i, count) / ui_set_tab_enabled(\"n\", i, on)", "badge a tab (0 none, -1 dot) / enable or disable it");
        fapi("ui_set_progress(\"n\", v)", "set a progress bar's fill (0..1)");
        fapi("ui_set_search(\"n\", text) / ui_search_query(\"n\") / ui_search_count(\"n\")", "filter a search bar; read the text; how many rows match (on_change, on_submit)");
        fapi("ui_set_date(\"n\", y, m, d) / ui_set_time(\"n\", h, min) / ui_date_text(\"n\")", "set a date (invalid days are pulled back) or a time; read it");
        fapi("ui_file_set_root(\"n\", folder) / ui_file_open(\"n\", name) / ui_file_selected(\"n\")", "sandboxed file picker: the only folder, open a row, the chosen file");
        fapi("ui_accordion_toggle(\"n\", i) / ui_accordion_open(\"n\", i)", "open or close a section; is it open");
        fapi("ui_breadcrumb_select(\"n\", i) / ui_breadcrumb_path(\"n\")", "keep that crumb and drop the ones after it; read the path");
        fapi("ui_set_page(\"n\", page) / ui_page(\"n\") / ui_page_count(\"n\")", "show a page (pulled back into range); read the page and the page count");
        fapi("ui_set_loading(\"n\", on)", "show or hide a skeleton placeholder");
        fapi("ui_sheet_open(\"n\") / ui_sheet_close(\"n\") / ui_sheet_open_state(\"n\")", "open or close a bottom sheet; is it open");
        fapi("ui_get_rotation(\"n\") / ui_set_rotation(\"n\", degrees)", "read / turn a UI frame, clockwise degrees around its pivot");
        fapi("ui_get_scale(\"n\") / ui_set_scale(\"n\", x, y)", "read / set a UI frame's width and height scale (one number sets both)");
        fapi("set_swatch(\"n\", i) / swatch_value(\"n\")", "pick a color swatch / read the index"); // grok/ui-color
        fapi("kick_spread(\"n\", px) / set_spread / set_spread_recover", "crosshair gap; recover eases it back"); // grok/ui-hud
        fapi("set_radial(\"n\", v) / set_radial_thickness / set_radial_start", "radial fill, ring width, start angle"); // grok/ui-hud
        fapi("set_radial_spin / set_radial_clockwise / set_radial_percent", "loader spin, direction, percent text"); // grok/ui-hud
        fapi("set_slot(\"n\", i) / slot_value(\"n\")", "select a slot grid cell / read it"); // grok/ui-hud
        // grok/ui-controls
        fapi("set_dropdown_search(\"n\", on) / set_dropdown_max(\"n\", rows) / set_dropdown_icon(\"n\", i, \"glyph\")", "dropdown search, visible rows, option glyph");
        fapi("set_scroll_inertia(\"n\", rate) / set_scroll_elastic(\"n\", on) / set_scroll_bar(\"n\", style)", "scroll glide, elastic edges, bar style 0/1/2");
        fapi("set_tabs_closable(\"n\", on) / close_tab(\"n\", i)", "closable tabs; on_tab_close fires when one is removed");
        fapi("set_tooltip_rich(\"n\", on) / set_tooltip_offset(\"n\", x, y) / set_tooltip_delay(\"n\", s)", "tooltip rich text, cursor offset, hover delay");
        fapi("set_input_multiline(\"n\", on) / set_input_limit(\"n\", n) / set_input_type(\"n\", t)", "multiline, character limit, 0 standard 1 integer 2 decimal 3 password");
        // grok/ui-toast
        fapi("toast_push(\"Title\", \"Body\", kind)", "show a toast (0 info, 1 success, 2 warning, 3 error)");
        fapi("set_toast_duration(seconds) / set_toast_corner(n) / set_toast_max(n)", "toast lifetime, corner and stack size");
        fapi("toast_count()", "how many toasts are on screen");
        fapi("modal_open() / modal_close()", "show or hide the first modal (close fires on_modal_close)");
        fapi("set_modal_style(on) / set_modal_title(\"s\") / set_modal_message(\"s\")", "style the dialog card and its text");
        fapi("set_modal_confirm(\"s\") / set_modal_cancel(\"s\")", "confirm and cancel button labels");
        // grok/ui-options
        fapi("ui_set_slider_step(\"n\", step) / ui_set_slider_format(\"n\", \"{v}\")", "snap spacing / value label");
        fapi("ui_set_bar_segments(\"n\", n) / ui_set_bar_trail(\"n\", seconds) / ui_set_bar_format(\"n\", \"{n}%\")", "chunks, damage trail, label");
        fapi("ui_set_button_ease(\"n\", ease, seconds)", "0 linear, 1 smooth, 2 ease-out, 3 ease-in");
        fapi("ui_set_fill(\"n\", v)", "set a filled UI Image's amount (cooldowns/health)");
    }

    if (sec("OkayUI — immediate-mode HUD from script")) {
        ImGui::TextWrapped("Call these every frame — the toolkit draws the HUD over the "
            "game (player AND editor Play mode). No Canvas objects: the script IS the UI. "
            "Each widget returns its live value, so you read+write state in one line.");
        fapi("ui_begin(\"Title\", x, y, w, h) ... ui_end()", "open/close a draggable window (coords optional)");
        fapi("ui_text(\"s\")", "a line of text");
        fapi("ui_button(\"Label\")", "returns 1 the frame it's clicked, else 0");
        fapi("ui_checkbox(\"Label\", on)", "returns the new on/off (pass current value back in)");
        fapi("ui_slider(\"Label\", v, lo, hi)", "returns the new value (pass current value back in)");
        fapi("ui_progress(t)", "a progress/health bar, t in 0..1");
        fapi("ui_sameline() / ui_separator()", "lay next widget beside / draw a divider");
        fapi("ui_heading(\"Lobby\") / ui_caption(\"NOW PLAYING\"[, r, g, b])", "a page title (accent underline) / small coloured label");
        fapi("ui_text_dim(\"s\") / ui_text_wrapped(\"s\")", "secondary text / text that wraps to the window");
        fapi("ui_primary_button(\"Play\")", "an accent call-to-action button (1 when clicked)");
        fapi("ui_card_begin(\"id\") ... ui_card_end()", "a raised rounded card around widgets");
        fapi("ui_list_item(title[, subtitle[, right[, selected]]])", "a row with an avatar, title and dim subtitle (1 when clicked)");
        fapi("ui_badge(\"3/4\"[, r, g, b]) / ui_avatar(\"Maya\")", "a pill tag / a coloured initial circle");
        fapi("ui_key_value(\"Ping\", \"23 ms\") / ui_spinner()", "label left, value right / a busy spinner");
        fapi("ui_tabs(\"Players|Match\", tab) / ui_input(\"Name\", text)", "pill tabs (returns the index) / a text field (returns the text)");
        fapi("ui_toast(\"Saved\"[, 0 info|1 ok|2 error[, secs]])", "a pop-up message in the corner");
        fapi("ui_style(\"launcher\" | \"classic\")", "the OkaySpace launcher look (default) or the old pixel look");
        if (!filtering)
            code("ui_begin(\"HUD\", 24, 24, 240, 150)\n"
                 "ui_text(\"Health\")\n"
                 "ui_progress(hp / 100)\n"
                 "if (ui_button(\"Heal\")) { hp = 100 }\n"
                 "ui_sameline()\n"
                 "if (ui_button(\"Hurt\")) { hp = hp - 10 }\n"
                 "ui_separator()\n"
                 "paused = ui_checkbox(\"Paused\", paused)\n"
                 "speed  = ui_slider(\"Speed\", speed, 0, 10)\n"
                 "ui_end()");
    }

    if (sec("Tweening (DOTween-style) & saves")) {
        ImGui::TextDisabled("Every tween accepts an optional easing and an optional on-complete \"fn\" as trailing args.");
        fapi("tween_move(x,y,dur[,ease][,\"done\"]) / tween_move3(...)", "animate position");
        fapi("tween_scale(s,dur[,ease][,\"done\"])", "animate uniform scale");
        fapi("tween_rotate(deg,dur ...) / tween_rotate_to(deg,dur ...)", "spin relative / to absolute Z angle");
        fapi("tween_scale_xy(sx,sy,dur ...)", "non-uniform scale");
        fapi("tween_ui_move(x,y,dur ...) / tween_ui_size(w,h,dur ...)", "move / resize a UI widget");
        fapi("tween_color(r,g,b,dur ...) / tween_fade(alpha,dur ...)", "animate color / opacity");
        fapi("tween_move_by(dx,dy,dur ...)", "move by a relative offset");
        fapi("tween_jump(x,y,height,dur[,\"done\"])", "arc-jump to a target (coins, hops)");
        fapi("tween_path(dur, x1,y1, x2,y2, ...)", "move through waypoints");
        fapi("tween_loop_move(x,y,dur[,ease]) / tween_loop_scale(s,dur ...)", "ping-pong forever");
        fapi("tween_loop_rotate(dur[,dir])", "spin continuously (loaders, coins)");
        fapi("tween_number(from,to,dur[,\"prefix\"])", "count a sibling text number (score ticks)");
        fapi("tween_punch_scale(amt,dur[,vib]) / tween_punch_pos(dx,dy,dur[,vib])", "punch & settle (juice)");
        fapi("tween_shake(i, dur) / tween_shake_rotate / tween_shake_scale", "shake, fading out");
        fapi("tween_punch_rotate(deg,dur) / tween_path_smooth(dur, x1,y1, ...)", "wobble / curved path");
        fapi("tween_text(\"Hi!\",dur) / tween_var(\"name\",to,dur) / tween_call(s,\"fn\")", "typewriter / a variable / delayed call");
        ImGui::TextDisabled("Every tween returns an id. Set it up (each returns the id, so they nest):");
        fapi("tween_loops(id, n[, \"yoyo\"|\"incremental\"]) / tween_delay(id, s)", "repeat (-1 forever) / wait first");
        fapi("tween_ease(id, e) / tween_speed(id, x) / tween_unscaled(id)", "ease / speed / ignore time scale");
        fapi("tween_clock(id, channel)", "follow a Clock channel (\"ui\", \"cinematic\", your own); \"\" = scene time");
        fapi("tween_tag(id, \"menu\")", "a group label for the _tag calls below (many tweens can share one)");
        fapi("tween_speed_based(id)", "the duration is a speed (units a second): lasts distance / speed");
        fapi("tween_decay(id, rate)", "how a punch or shake dies away: 0 = straight fade, 6 = snappy");
        fapi("tween_prop(path, to, dur[, ease][, \"done\"])", "tween any property by path (\"Light.intensity\")");
        fapi("tween_from(id) / tween_relative(id) / tween_set_id(id, \"name\")", "from the end value / offset / name it");
        fapi("tween_on_complete(id, \"fn\") / tween_on_step / tween_on_start", "callbacks");
        fapi("tween_pause / play / restart / rewind / complete / kill(id)", "control one tween");
        fapi("tween_kill_all() / tween_play_id(\"name\") / is_tweening()", "this object / by name");
        fapi("tween_kill_tag(\"tag\"[, complete])", "kill every tween with that tag, scene-wide");
        fapi("tween_pause_tag / tween_play_tag / tween_complete_tag(\"tag\")", "pause / play / finish every tween with that tag");
        fapi("tween_rewind_id(\"name\")", "put the tweens with that Id back at the start");
        fapi("seq_new() / seq_append(s, id) / seq_join(s, id)", "sequence: after / together");
        fapi("seq_interval(s, t) / seq_callback(s, \"fn\") / seq_insert(s, t, id)", "pauses, calls, timing");
        fapi("seq_prepend(s, id) / seq_join_callback(s, \"fn\")", "play before everything / call when the last tween starts");
        fapi("save_game([slot], [label]) / load_game([slot])", "save / load the whole game");
        fapi("game_save(slot[, label]) / game_load(slot) / game_continue()", "save slots for Saveable objects (docs/engine/save_games.md); load happens next frame");
        fapi("game_delete_slot(slot) / game_slot_exists(slot) / game_can_continue()", "manage and ask about save slots");
        fapi("game_slot_text(slot) / game_slot_age(slot) / game_slot_label(slot) / game_autosave()", "when a slot was saved (UTC text, seconds ago), its label, autosave ring");
        fapi("save_exists([slot]) / delete_save([slot])", "manage save slots");
        ImGui::TextDisabled("Eases: linear, in_/out_/in_out_ + quad cubic quart quint sine expo circ back elastic bounce,\nor a curve as text: \"cubic-bezier(0.4,0,0.2,1)\", \"steps(4)\".");
    }

    if (sec("Persistence, PlayerPrefs & Data Assets")) {
        fapi("set(\"k\", v) / get(\"k\")", "shared variables across scripts");
        fapi("prefs_set / prefs_get / prefs_save / prefs_load", "PlayerPrefs-style key/value");
        fapi("save(\"k\", v[, file]) / load(\"k\", def[, file])", "Easy-Save: typed values, many files");
        fapi("save_has / save_delete / save_clear", "manage keys in a save file");
        fapi("save_exists([file]) / save_delete_file([file])", "check / remove a save file");
        fapi("data_num / data_str / data_has(path, key)", "read Scriptable Object (.okaydata) fields");
        fapi("data_set(path, key, v) / data_save(path)", "write a Data Asset");
        fapi("ui_drop_source() / ui_drop_target()", "the dragged item / target (UI drag & drop)");
        fapi("drop_source() / drop_target()", "world drag & drop pair");
    }

    if (sec("Scene Manager (build list)")) {
        fapi("load_scene_index(i)", "load scene i from the build list");
        fapi("load_scene_name(\"n\")", "load by scene name (file stem) or path");
        fapi("load_next_scene()", "load the next scene (wraps to the first)");
        fapi("load_scene_additive(\"n\"[, x, y, z])", "MERGE a scene into the running one at an offset (seamless worlds)");
        fapi("reload_scene()", "reload the active scene");
        fapi("scene_count() / scene_index() / scene_name()", "query the build list");
    }

    if (sec("Multiplayer (networking)")) {
        fapi("net_host(port)", "start a server on this machine");
        fapi("net_join(\"ip\", port)", "connect to a host");
        fapi("net_host_relay(\"ip\", port, \"code\") / net_join_relay(...)", "host/join via a relay (NAT traversal)");
        fapi("net_relay_ready()", "1 once the relay has paired this peer");
        fapi("net_launch_join()", "1 if the launcher started the game to join a friend / invite / server");
        fapi("net_join_link(\"okay://join?...\")", "join from a join link");
        fapi("net_invite_friends()", "open the launcher's Invite friends window (also Shift+Tab)");
        fapi("net_browser_open()", "show the built-in \"Find a server\" window");
        fapi("net_browser_refresh([\"all\"|\"internet\"|\"lan\"]) / net_browser_busy()", "search for servers in the background");
        fapi("net_browser_count() / net_browser_name(i) / _players(i) / _max(i) / _ping(i) / _map(i) / _locked(i)", "the results, for your own list");
        fapi("net_browser_join(i[, \"password\"])", "join result i");
        fapi("net_set_score(id, n) / net_score(id)", "server-owned scores, seen by everyone");
        fapi("net_peer_name(id) / net_roster_count() / net_roster_id(i)", "who's in the room (any peer)");
        fapi("net_teleport(id, x, y[, z])", "server: move a player (respawn) past the speed check");
        fapi("net_queue_join(\"mode\", players[, min[, \"region\"]]) / net_queue_cancel()", "matchmaking: find a match (joins it when found)");
        fapi("net_queue_state() / net_queue_time() / net_queue_count() / net_queue_error()", "searching / waiting / found / failed, seconds, players in the queue");
        fapi("net_match_start() / net_match_end([winner ids])", "server: zero scores / end the match and save results (stats, leaderboards)");
        fapi("net_stats_top(\"wins\"|\"score\"|\"best\"|\"matches\"|\"time\", n) / net_stats_mine(stat)", "this game's leaderboard from match results");
        fapi("net_max_speed(units) / net_rate_limit(perSec)", "server: snap back too-fast moves / drop message spam");
        fapi("net_anticheat([on]) / net_anticheat_load(path) / net_violations(id)", "server: anti-cheat on/off / load an .okayac file / a player's refusals this minute");
        fapi("on_cheat_detected(kind, peer, detail) / on_validate_rpc(name, data, sender)", "server events (Anti-Cheat Config): return false from on_validate_rpc to refuse the RPC");
        fapi("net_hit(\"target\", \"weapon\", ox, oy, oz, dx, dy, dz)", "claim a hit on a synced object: the server checks it (position, range, walls, fire rate) and applies the damage");
        fapi("on_validate_hit(shooter, target, weapon, damage)", "server event (Anti-Cheat Config): return false to refuse a hit, or a positive number for its damage");
        fapi("net_budget(id, \"rpc\"[, add])", "server: a player's Net Validator budget for an RPC (ammo); add tops it up (reload, pickup)");
        fapi("net_pin_host(\"key\")", "client: join only the host with this identity key (encryption on; \"\" = no pin)");
        fapi("net_set_pawn(\"object\") / net_authoritative([on])", "client: the player object your inputs drive / host: server-authoritative movement");
        fapi("net_tick_rate([hz]) / net_tick()", "host: a fixed network tick (0 = off, as before) / the current tick");
        fapi("net_disconnect()", "leave / stop the session");
        fapi("net_connected() / net_is_server() / net_is_client()", "status");
        fapi("net_id() / net_peers() / net_ping()", "peer id / peer count / RTT (ms)");
        fapi("net_name(\"name\") / net_room(\"name\")", "display name / isolated lobby room");
        fapi("net_ready(1/0) / net_ready_count() / net_all_ready()", "lobby ready state");
        fapi("net_start_match() / net_match_started()", "(host) begin / has the match begun?");
        fapi("net_send(\"ch\",\"data\") / net_send_to(id,\"ch\",\"data\")", "broadcast / message one peer");
        fapi("net_send_reliable(\"ch\",\"data\")", "resent until acked + de-duped");
        fapi("net_kick(id[,\"reason\"]) / net_was_kicked()", "host kicks / client check");
        fapi("net_poll() / net_msg_channel() / net_msg_data() / net_msg_from()", "pop & read a message (in a while)");
        fapi("net_set(\"key\",\"value\") / net_get(\"key\")", "server-authoritative synced variable");
        fapi("net_spawn(\"prefab\", x, y[, z])", "replicated spawn on every peer");
    }

    if (sec("Character animation (Character on this object)")) {
        fapi("load_clips(\"text\")", "load keyframe clips from OkayVS-anim text; returns count");
        fapi("play_clip(\"name\") / stop_clip()", "play a loaded clip / return to built-in");
        fapi("play_action(\"Death\" [, blend]) / is_in_action() / action_time()", "go to a built-in action state (0 if it is missing)");
        fapi("die([\"forward\"|\"back\"|\"left\"|\"right\"]) / die(x, y, z) / revive() / is_dead()", "death anim; x,y,z is where the hit came from");
        fapi("dive() / can_dive()", "dolphin dive while sprinting on the ground; 1 if a dive can start");
        fapi("sit([\"Seat\"|\"ground\"|\"knees\"|\"chair\"]) / stand_up() / is_sitting()", "sit on a Seat or a flat floor; move or jump stands up");
        fapi("interact() / seat_prompt()", "nearest Seat (E in range does this too), or its prompt");
        fapi("hit_react(\"forward\"|\"back\"|\"left\"|\"right\" [, heavy]) / hit_react(x, y, z [, heavy])", "flinch from where the hit came from; heavy is a full-body stumble");
        fapi("roll([\"forward\"|\"back\"|\"left\"|\"right\"]) / dodge([...])", "roll is the clip; dodge also bursts a first- or third-person body");
        fapi("vault() / mantle() / ledge_grab() / climb_up() / let_go() / is_hanging() / shimmy(v)", "vault, mantle, or hang; flags on the controller default off");
        fapi("play_clip_once(\"name\")", "play a clip once (attack/jump), then return to the previous one");
        fapi("playing_clip() / is_playing_clip()", "active clip name / is one playing");
        fapi("set_anim(n) / get_anim()", "built-in animation index (1 idle, 2 walk, 3 run, ...)");
    }

    if (sec("Steam (achievements, stats, leaderboards, cloud)")) {
        fapi("steam_name()", "the player's Steam name");
        fapi("steam_unlock(\"ID\") / steam_is_unlocked(\"ID\") / steam_clear(\"ID\")", "achievements");
        fapi("steam_set_stat(\"n\",v) / steam_get_stat(\"n\") / steam_inc_stat(\"n\",by)", "stats");
        fapi("steam_store()", "flush stats/achievements to Steam");
        fapi("steam_progress(\"ID\", cur, max)", "show achievement progress (auto-unlocks at max)");
        fapi("steam_leaderboard(\"board\", score) / steam_leaderboard_top(\"board\", n)", "submit / top-N scores");
        fapi("steam_cloud_write(\"file\",\"data\") / steam_cloud_read(\"file\")", "Steam Cloud");
        fapi("steam_presence(\"key\",\"value\") / steam_friends() / steam_overlay(\"page\")", "rich presence / friends / overlay");
        fapi("steam_owns(appId) / steam_owns_dlc(appId)", "ownership / DLC checks");
        fapi("steam_achievement_count() / steam_language()", "achievement count / client language");
    }

    if (sec("PlayFab (cloud login, leaderboards, saves)")) {
        ImGui::TextDisabled("Blocking web requests over PlayFab's REST API — call from start(), not every frame.");
        fapi("pf_login(\"titleId\", \"customId\")", "sign in (creates the account on first use); 1 on success");
        fapi("pf_register(\"titleId\", \"user\", \"pass\") / pf_login_password(...)", "username + password accounts");
        fapi("pf_logged_in() / pf_id() / pf_error()", "session state / PlayFab id / last error text");
        fapi("pf_name(\"display name\")", "the name shown on leaderboards");
        fapi("pf_set_stat(\"stat\", value)", "publish a statistic (drives leaderboards)");
        fapi("pf_leaderboard_top(\"stat\", n)", "top-N entries as \"rank,name,score\" strings");
        fapi("pf_save(\"key\",\"value\") / pf_load(\"key\") / pf_delete(\"key\")", "per-player cloud save data");
    }

    if (sec("Debugging")) {
        ImGui::TextDisabled("All of these print into the editor's Console.");
        fapi("print(...) / debug_log(...) / log_info(...)", "log a line (args joined by spaces)");
        fapi("log_warn(...) / log_error(...) / trace(...)", "log at a level");
        fapi("watch(\"name\", value)", "log name = value for quick inspection");
        fapi("assert(cond[, \"msg\"])", "log an error when cond is false; returns the result");
        fapi("format(\"hp={} of {}\", a, b)", "fill each {} with the next argument");
        fapi("concat(...) / str_repeat(\"ab\", 3)", "join args / repeat a string");
    }

    if (sec("Math & numeric helpers")) {
        fapi("approach(cur, target, step)", "step toward a target without overshooting");
        fapi("remap(v, inLo, inHi, outLo, outHi)", "rescale a value between two ranges");
        fapi("frac(x) / mod(a, b) / snap(v, step)", "fraction / positive modulo / round to a step");
        fapi("is_nan(x) / is_finite(x) / avg(...) / min3 / max3", "numeric helpers");
        fapi("lerp_angle(a, b, t)", "interpolate degrees the short way round");
        fapi("rand(lo, hi) / randi(lo, hi) / chance(p)", "randomness");
        fapi("dist(x1,y1,x2,y2) / dist3(...) / angle_to(...)", "geometry");
        fapi("sin cos tan asin acos atan atan2", "trigonometry");
        fapi("sqrt pow exp log abs sign floor ceil round", "math");
        fapi("min max clamp clamp01 lerp smoothstep wrap ping_pong", "ranges & easing");
        fapi("time() / dt() / fps()", "timing (fps() is the real rate over the last half second)");
        fapi("session_time() / session_unscaled_time() / session_frame_count()", "game seconds / real seconds / frames since this Play began");
        fapi("unscaled_dt() / is_paused() / hitch_count()", "real frame time / is the game paused / frames over 0.1 s");
        fapi("push_pause(reason) / pop_pause(reason) / is_paused_by(reason)", "pause with a reason of your own: two scripts never resume each other");
        fapi("hit_stop(s[, scale]) / slow_motion(scale, s) / ramp_timescale(target, s)", "freeze frame / bullet time / eased time scale (real seconds)");
    }

    if (sec("Collections — arrays & maps")) {
        ImGui::TextDisabled("Arrays and maps are shared by reference and can be nested.");
        fapi("array(...) / count(a) / push(a,v) / pop(a)", "make a list, length, append, remove-last");
        fapi("first(a) / last(a)", "first / last item (null if empty)");
        fapi("insert_at(a,i,v) / remove_at(a,i)", "insert at / remove at an index");
        fapi("slice(a, start, end)", "sub-array; negative indices count from the end");
        fapi("range(n) / range(lo, hi[, step])", "build a numeric array");
        fapi("contains(a,v) / index_of(a,v)", "membership / first index (or -1)");
        fapi("sum(a) / min_of(a) / max_of(a)", "reduce a numeric array");
        fapi("reverse(a) / sort_num(a) / sort_str(a) / shuffle(a) / choose(a)", "reorder / random pick");
        fapi("clear(a_or_m)", "empty an array or map in place");
        fapi("map() / map_set(m,\"k\",v) / map_get(m,\"k\") / map_has(m,\"k\")", "dictionary basics");
        fapi("map_remove(m,\"k\") / map_keys(m) / map_values(m) / map_count(m)", "delete / list / size");
        fapi("map_clear(m) / map_merge(dst, src)", "empty / merge (src wins)");
        if (!filtering)
            code("var a = array(10, 20, 30);\n"
                 "a[1] += 5;   a[0]++;         // element compound assignment\n"
                 "var m = map();  m[\"hp\"] = 100;  m[\"hp\"] -= 30;\n"
                 "foreach (var key in m) { print(key + \" = \" + map_get(m, key)); }");
    }

    if (sec("Functional helpers (named callbacks)")) {
        ImGui::TextDisabled("Pass the NAME of a function to apply it across an array.");
        fapi("map_fn(a, \"fn\")", "new array of fn(item) for each item");
        fapi("filter_fn(a, \"fn\")", "keep items where fn(item) is true");
        fapi("reduce_fn(a, \"fn\", init)", "fold left: acc = fn(acc, item)");
        fapi("for_each(a, \"fn\")", "call fn(item) for every item");
        fapi("find_fn / any_fn / all_fn / count_fn (a, \"fn\")", "search / test / count");
        fapi("call(\"fn\"[, args...])", "invoke a function (user or builtin) by name");
    }

    if (sec("JSON & type introspection")) {
        fapi("to_json(value) / from_json(\"...\")", "serialize / parse (also json_stringify / json_parse)");
        fapi("typeof(v)", "\"number\",\"string\",\"bool\",\"array\",\"map\",\"vec3\" or \"null\"");
        fapi("is_num is_str is_bool is_array is_map", "type tests");
    }

    if (sec("Error handling")) {
        ImGui::TextWrapped("Wrap risky code in try/catch and raise your own errors with throw. "
            "The catch clause is optional. Control flow (return/break/continue) passes through a try.");
        if (!filtering)
            code("try {\n"
                 "    var data = from_json(load_prefs(\"save\", \"{}\"));\n"
                 "    if (map_count(data) == 0) { throw \"no save data\"; }\n"
                 "} catch (e) {\n"
                 "    print(\"Load failed: \" + e);\n"
                 "}");
    }

    if (sec("Easing curves")) {
        ImGui::TextWrapped("Each maps a normalized t (0..1) to an eased 0..1 — feed the result to lerp:");
        fapi("ease_in / ease_out / ease_in_out", "quad ease");
        fapi("ease_in_cubic / ease_out_cubic / ease_in_out_cubic", "cubic ease");
        fapi("ease_back / ease_elastic / ease_bounce", "overshoot / spring / bounce");
        fapi("ease_curve(t, \"OutBack\")", "any ease by name or curve text: \"cubic-bezier(0.4,0,0.2,1)\", \"steps(4)\"");
        fapi("fract(x)", "the fractional part of x");
    }

    if (sec("String helpers")) {
        fapi("upper lower trim trim_start trim_end", "case & whitespace");
        fapi("capitalize title_case str_reverse", "capitalization / reverse");
        fapi("substr char_at replace split join", "extract / edit / split / join");
        fapi("repeat pad_left pad_right format", "repeat / pad / format");
        fapi("to_str / str / to_num / num", "convert between text and numbers");
        fapi("loc(\"key\"[, args...])", "the key's text in the player's language ({0}, {name}, plurals)");
        fapi("set_language(\"fr\") / get_language() / languages()", "switch / read the language (on_language_changed(code) runs)");
    }

    if (sec("Timers & spawning")) {
        fapi("after(seconds, \"fn\")", "call fn once after a delay");
        fapi("every(seconds, \"fn\")", "call fn repeatedly on an interval");
        fapi("cancel_timers()", "clear all scheduled callbacks");
        fapi("spawn(\"prefab\", x, y) / spawn3(\"prefab\", x, y, z)", "instantiate prefabs");
        fapi("emit(n) / particles_on(b)", "particle FX");
        fapi("set_tile / get_tile / tile_resize", "tilemap editing");
    }

    if (sec("Physics queries & events")) {
        fapi("raycast_hit(ox,oy,dx,dy[, maxDist])", "2D ray hits a collider? (boolean)");
        fapi("raycast(ox,oy,dx,dy[, maxDist])", "2D ray; returns the name hit (\"\" = miss)");
        fapi("ray_hit()/ray_object()/ray_x()/ray_y()/ray_nx()/ray_ny()/ray_dist()", "details of the last raycast()");
        fapi("raycast_hit3(o.., d..[, maxDist]) / raycast3(...)", "3D ray test / named hit + ray3_* details");
        fapi("...[, maxDist, layers, ignore_triggers]", "every ray / cast / overlap takes these: layers \"0,3\" or \"Default,Enemy\"");
        fapi("raycast_all3(o.., d..) / raycast_all(ox,oy,dx,dy)", "names of EVERYTHING hit, nearest first (+ ray_all_*)");
        fapi("sphere_cast3(o.., d.., r) / box_cast3(c.., half.., d..)", "thick casts (won't slip through gaps)");
        fapi("capsule_cast3(a.., b.., r, d..)", "would a capsule fit along this path?");
        fapi("box_cast(cx, cy, hx, hy, dx, dy)", "sweep a flat box; name of the first thing hit (ray_* details)");
        fapi("capsule_cast(ax, ay, bx, by, r, dx, dy)", "sweep a flat capsule along a direction");
        fapi("overlap_box(cx, cy, hx, hy) / overlap_capsule(ax, ay, bx, by, r)", "names inside a flat box or capsule");
        fapi("add_explosion_force(cx, cy, force, radius [, up])", "shove this 2D body; fades to nothing at radius");
        fapi("overlap_sphere3 / overlap_box3 / overlap_capsule3", "names of the objects inside a shape");
        fapi("layer_mask(\"Default\", \"Enemy\")", "a layers number for the queries above");
        fapi("overlap(x, y)", "a 2D collider contains this point?");
        ImGui::TextWrapped("Define on_collision() / on_trigger() — they fire when this "
            "object's collider hits or overlaps another. on_trigger_stay() runs every frame "
            "something stays inside. Inside these, contact_point / contact_normal (vectors), "
            "contact_x/_y/_z, impact_speed and other_tag describe the touch.");
    }

    if (sec("Friendly aliases")) {
        ImGui::TextWrapped("Intuitive alternate names for common builtins: delta_time (dt), "
            "get_key/get_key_down/get_key_up (key...), random/random_int (rand/randi), "
            "pick (choose), distance (dist), instantiate (spawn), destroy_self (destroy), "
            "translate (move), set_position (set_pos), play_audio (play_sound), "
            "to_string/str (to_str), to_number/num (to_num), get_x/get_y/get_z (pos_*), "
            "screen_width/screen_height.");
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Complete reference — mirrors docs/scripting/scripting.md. Hover a name in the Script Editor for its doc.");
    if (ImGui::Button("Close")) g_showScriptDocs = false;
    ImGui::End();
}

// --- VS Code-style script editor helpers ---------------------------------

// Autocomplete popup state: item count shown this frame (0 = no popup), the
// keyboard-highlighted item, and Esc's hide-until-the-word-changes latch.
int g_acCount = 0;
int g_acIndex = 0;
bool g_acDismiss = false;


// Words the editor offers for autocomplete: keywords, types, and common builtins
// (a curated subset of the OkayScript API — see docs/scripting/scripting.md).
const std::vector<std::string>& ScriptCompletions() {
    static const std::vector<std::string> w = {
        // keywords
        "if","else","for","while","do","return","break","continue","switch","case",
        "var","function","new","public","true","false","foreach","in",
        "try","catch","throw",
        // types + modifiers that do something
        "void","int","float","bool","string","static","const","readonly",
        "enum","yield","null","this","default","??","?.","??=",
        "IEnumerator","WaitForSeconds","StartCoroutine","StopCoroutine","StopAllCoroutines",
        "GetComponent","Instantiate","Destroy","Invoke","InvokeRepeating","CancelInvoke","CompareTag",
        "List","Dictionary","KeyValuePair","Rigidbody2D","Rigidbody","SpriteRenderer","Animator",
        "AudioSource","Collider2D","Collision2D","Collider","Collision","KeyCode","GameObject","Transform",
        "Start","Update","Awake","LateUpdate","FixedUpdate","OnEnable","OnDisable","OnDestroy",
        "OnTriggerEnter2D","OnTriggerEnter","OnCollisionEnter2D",
        "OnCollisionEnter","OnMouseDown","SerializeField","Header","Range","Tooltip",
        // API
        "Vector3","Vector2","Color","Mathf","Input","Time","Debug","Random","Physics2D",
        "SceneManager","Quaternion","transform","gameObject",
        // transform / movement
        "move","set_pos","set_x","set_y","pos_x","pos_y","rotate","move_toward","look_at",
        "move3","set_pos3","set_z","pos_z","rotate3","set_scale","set_scale3","move_forward",
        // physics
        "set_velocity","set_velocity3","add_force","add_impulse","jump","set_gravity",
        // input
        "key","key_down","key_up","axis_x","axis_y","mouse_x","mouse_y","mouse","mouse_down",
        // object / scene
        "name","set_name","tag","set_tag","has_tag","set_active","self_active","destroy",
        "set_parent","exists","is_active","obj_x","obj_y","dist_to","destroy_obj","count_tag",
        "child_count","get_child","parent_of","hurt","heal","add_xp","get_hp","get_max_hp","is_dead",
        "set_light_intensity","set_light_range","set_light_angle","set_light_color",
        "npc_goto","npc_stop","npc_busy","npc_state",
        "nav_move_to","nav_stop","nav_arrived","nav_remaining","nav_path_exists","nav_path_length","nav_point_walkable","nav_nearest_point",
        "grid_move_to","grid_move_cell","grid_stop","grid_arrived","grid_set_tile_cost","grid_set_cell_cost","grid_block","grid_clear",
        "bt_set","bt_get","bt_run","bt_stop",
        "spline_point","spline_length","follower_set_speed","line_set_points","trail_clear",
        "spawner_start","spawner_stop","spawner_alive",
        "build_enter","build_exit","build_toggle","build_active","build_link_mode","build_palette_clear",
        "build_palette_add","build_palette_select","build_selected","build_save","build_load","build_exists",
        "build_clear","build_undo","build_redo","build_count","logic_press","logic_state",
        "nearest_tag","set_cam","move_cam","set_cam_zoom","set_bg","set_light","set_ambient",
        "load_scene","load_scene_index","load_next_scene","screen_w","screen_h",
        // components / fx
        "set_text","set_color","set_texture","set_mesh","emit","play_anim","play_sound",
        "set_progress","set_fill",
        // save / prefs / data
        "save","load","save_has","save_delete","save_exists","save_clear","prefs_set",
        "prefs_get","prefs_get_str","prefs_save","prefs_load","data_num","data_str","data_set",
        // math / util
        "abs","sin","cos","tan","sqrt","pow","floor","ceil","round","min","max","sign",
        "clamp","clamp01","lerp","distance","random","random_range","print","log","format",
        // tweens
        "tween_move","tween_move3","tween_scale","tween_rotate","tween_color","tween_fade",
        "tween_loop_move","tween_punch_scale","tween_shake",
        "tween_move_by","tween_move_world","tween_rotate_to","tween_rotate3","tween_scale_xy",
        "tween_ui_move","tween_ui_size","tween_jump","tween_path","tween_path_smooth",
        "tween_loop_scale","tween_loop_rotate","tween_number","tween_text","tween_var","tween_call",
        "tween_punch_pos","tween_punch_rotate","tween_shake_rotate","tween_shake_scale","flash",
        "tween_loops","tween_delay","tween_ease","tween_speed","tween_unscaled","tween_clock","tween_prop","tween_from",
        "tween_relative","tween_set_id","tween_keep","tween_on_complete","tween_on_step",
        "tween_on_start","tween_on_update","tween_kill","tween_pause","tween_play","tween_toggle",
        "tween_restart","tween_rewind","tween_complete","tween_backwards","tween_goto",
        "tween_playing","tween_active","tween_progress","tween_kill_all","tween_pause_all",
        "tween_play_all","is_tweening","tween_kill_id","tween_pause_id","tween_play_id",
        "tween_restart_id","seq_new","seq_append","seq_join","seq_insert","seq_interval",
        "seq_prepend_interval","seq_callback","seq_insert_callback",
        "tween_tag","tween_speed_based","tween_decay","tween_kill_tag","tween_pause_tag","tween_play_tag",
        "tween_complete_tag","tween_rewind_id","seq_prepend","seq_join_callback",
        // networking
        "net_host","net_join","net_send","net_poll","net_is_server",
        // ui
        "ui_set_text","ui_get_text","ui_set_focus","ui_clicked","ui_slider_value","ui_set_slider",
        "ui_toggle_value","ui_set_progress","ui_get_rotation","ui_set_rotation","ui_get_scale","ui_set_scale",
        "set_swatch","swatch_value", // grok/ui-color
        "kick_spread","set_spread","set_spread_recover","set_radial","set_slot","slot_value", // grok/ui-hud

        // grok/ui-toast
        "toast_push","set_toast_duration","set_toast_corner","set_toast_max","toast_count",
        "modal_open","modal_close","set_modal_style","set_modal_title","set_modal_message",
        "set_modal_confirm","set_modal_cancel",
        // raycast
        "raycast","raycast3","overlap_circle","raycast_all3","sphere_cast3","box_cast3","overlap_box3",
    };
    // Merge in EVERY builtin the OkayScript VM actually registers (hundreds), so
    // autocomplete covers the whole language, not just this hand-picked subset.
    // Built once, lazily, from a throwaway VM.
    static std::vector<std::string> merged = [] {
        std::vector<std::string> m = w;
        std::unordered_set<std::string> seen(m.begin(), m.end());
        if (auto vm = CreateScriptVM("okayscript"))
            for (auto& n : vm->BuiltinNames())
                if (seen.insert(n).second) m.push_back(n);
        return m;
    }();
    return merged;
}

// Append identifiers found in the current document to a completion pool (deduped),
// so the editor also suggests the script's own variables and function names — the
// "current document symbols" source real IDEs include alongside the API list.
void AddBufferIdentifiers(const char* src, std::vector<std::string>& pool) {
    if (!src) return;
    std::unordered_set<std::string> have(pool.begin(), pool.end());
    std::string id;
    auto flush = [&]() {
        if (id.size() >= 3 && !std::isdigit((unsigned char)id[0]) && !have.count(id)) {
            have.insert(id); pool.push_back(id);
        }
        id.clear();
    };
    for (const char* p = src; *p; ++p) {
        char ch = *p;
        if (std::isalnum((unsigned char)ch) || ch == '_') id += ch;
        else flush();
    }
    flush();
}

// Signature (parameter list) for a builtin, shown as a hint while typing a call —
// e.g. inside `move_toward(` the editor floats "move_toward(x, y, step)". Empty for
// unknown names. A curated subset of the most-used OkayScript API (see docs).
const std::unordered_map<std::string, std::string>& ScriptSignatureMap() {
    static const std::unordered_map<std::string, std::string> sig = {
        // transform / movement
        {"move","move(dx, dy)"}, {"set_pos","set_pos(x, y)"}, {"set_x","set_x(v)"}, {"set_y","set_y(v)"},
        {"rotate","rotate(deg)"}, {"move_toward","move_toward(x, y, step)"}, {"look_at","look_at(\"name\")"},
        // high-level one-liners (dt-scaled internally)
        {"walk","walk(dx, dy, speed)"}, {"spin","spin(degPerSec)"}, {"on_key_move","on_key_move(speed)"},
        {"bob","bob(amount, speed)"}, {"pulse","pulse(amount, speed)"}, {"wander","wander(speed)"},
        {"explode","explode([count])"}, {"flash","flash(r, g, b [, dur])"}, {"shake","shake(intensity, dur)"}, {"face","face(\"name\")"},
        {"move_to","move_to(x, y, speed)"}, {"score","score()"}, {"add_score","add_score(n)"}, {"set_score","set_score(n)"},
        {"timer","timer(\"name\", seconds)"}, {"set_visible","set_visible(on)"}, {"show","show()"}, {"hide","hide()"}, {"blink","blink(rate)"},
        {"keep_in_box","keep_in_box(minX, minY, maxX, maxY)"}, {"wrap_in_box","wrap_in_box(minX, minY, maxX, maxY)"}, {"bounce_in_box","bounce_in_box(minX, minY, maxX, maxY)"},
        {"follow","follow(\"name\", speed[, stopDist])"}, {"patrol","patrol(x1, y1, x2, y2, speed)"}, {"orbit","orbit(\"name\", radius, degPerSec)"},
        {"move3","move3(dx, dy, dz)"}, {"set_pos3","set_pos3(x, y, z)"}, {"set_z","set_z(v)"},
        {"rotate3","rotate3(x, y, z)"}, {"set_scale","set_scale(s)"}, {"set_scale3","set_scale3(x, y, z)"},
        {"move_forward","move_forward(d)"}, {"move_right","move_right(d)"}, {"look_at3","look_at3(\"name\")"},
        // physics
        {"set_velocity","set_velocity(x, y)"}, {"set_velocity3","set_velocity3(x, y, z)"},
        {"add_force","add_force(x, y)"}, {"add_force3","add_force3(x, y, z)"},
        {"add_impulse","add_impulse(x, y)"}, {"jump","jump(v)"}, {"set_gravity","set_gravity(x, y)"},
        // input
        {"key","key(\"x\")"}, {"key_down","key_down(\"x\")"}, {"key_up","key_up(\"x\")"},
        {"gamepad","gamepad(btn)"},
        // object / scene
        {"find","find(\"name\")"}, {"spawn","spawn(\"prefab\", x, y)"}, {"spawn3","spawn3(\"prefab\", x, y, z)"},
        {"destroy","destroy(\"name\")"}, {"destroy_obj","destroy_obj(\"name\")"}, {"dist_to","dist_to(\"name\")"},
        {"count_tag","count_tag(\"tag\")"}, {"nearest_tag","nearest_tag(\"tag\")"}, {"load_scene","load_scene(\"file\")"},
        {"npc_goto","npc_goto(\"name\", x, y, z)"}, {"npc_stop","npc_stop(\"name\")"},
        {"npc_busy","npc_busy(\"name\")"}, {"npc_state","npc_state(\"name\")"},
        {"nav_move_to","nav_move_to(x, y, z)"}, {"nav_path_exists","nav_path_exists(x, y, z)"}, {"nav_path_length","nav_path_length(x, y, z)"}, {"nav_point_walkable","nav_point_walkable(x, y, z, maxDist)"}, {"nav_nearest_point","nav_nearest_point(x, y, z, maxDist)"}, {"nav_stop","nav_stop()"}, {"nav_arrived","nav_arrived()"}, {"nav_remaining","nav_remaining()"},
        {"grid_move_to","grid_move_to(x, y)"}, {"grid_move_cell","grid_move_cell(x, y)"}, {"grid_stop","grid_stop()"}, {"grid_arrived","grid_arrived()"},
        {"grid_set_tile_cost","grid_set_tile_cost(id, cost)"}, {"grid_set_cell_cost","grid_set_cell_cost(x, y, cost)"}, {"grid_block","grid_block(x, y)"}, {"grid_clear","grid_clear()"},
        {"bt_set","bt_set(\"key\", value)"}, {"bt_get","bt_get(\"key\")"}, {"bt_run","bt_run(\"file.okaybt\")"}, {"bt_stop","bt_stop()"},
        {"spline_point","spline_point(\"name\", t)"}, {"spline_length","spline_length(\"name\")"},
        {"follower_set_speed","follower_set_speed(speed)"}, {"line_set_points","line_set_points(points)"}, {"trail_clear","trail_clear()"},
        {"spawner_start","spawner_start(\"name\")"}, {"spawner_stop","spawner_stop(\"name\")"},
        {"spawner_alive","spawner_alive(\"name\")"},
        {"build_enter","build_enter()"}, {"build_exit","build_exit()"}, {"build_toggle","build_toggle()"},
        {"build_active","build_active()"}, {"build_link_mode","build_link_mode(on)"},
        {"build_palette_clear","build_palette_clear()"}, {"build_palette_add","build_palette_add(\"name\", \"prefab\", \"icon\")"},
        {"build_palette_select","build_palette_select(slot)"}, {"build_selected","build_selected()"},
        {"build_save","build_save(\"name\")"}, {"build_load","build_load(\"name\")"}, {"build_exists","build_exists(\"name\")"},
        {"build_clear","build_clear()"}, {"build_undo","build_undo()"}, {"build_redo","build_redo()"},
        {"build_count","build_count()"}, {"logic_press","logic_press(\"name\")"}, {"logic_state","logic_state(\"name\")"},
        {"set_cam","set_cam(x, y)"}, {"move_cam","move_cam(dx, dy)"}, {"set_cam_zoom","set_cam_zoom(z)"},
        {"camera_shake","camera_shake(strength)"}, {"camera_zoom_to","camera_zoom_to(zoom, seconds)"},
        {"camera_dutch","camera_dutch(degrees)"}, {"camera_fov","camera_fov(degrees)"}, {"camera_follow","camera_follow(\"name\", smoothing)"},
        {"Camera.Shake","Camera.Shake(\"impact\", strength)"}, {"Camera.BlendTo","Camera.BlendTo(\"shot\", seconds)"},
        {"Camera.SetTarget","Camera.SetTarget(\"name\")"}, {"Camera.AddToGroup","Camera.AddToGroup(\"name\")"},
        {"Camera.RemoveFromGroup","Camera.RemoveFromGroup(\"name\")"}, {"Camera.FovKick","Camera.FovKick(degrees, seconds)"},
        {"Camera.SetFovOffset","Camera.SetFovOffset(degrees, seconds)"}, {"Camera.HitPulse","Camera.HitPulse(strength)"},
        {"Camera.SetHandheld","Camera.SetHandheld(amount)"},
        {"set_bg","set_bg(r, g, b)"}, {"set_light","set_light(x, y, z)"}, {"set_ambient","set_ambient(v)"},
        // variables / prefs
        {"set_var","set_var(\"name\", value)"}, {"get_var","get_var(\"name\")"}, {"add_var","add_var(\"name\", amount)"},
        {"save_prefs","save_prefs(\"key\", value)"}, {"load_prefs","load_prefs(\"key\", default)"},
        // math
        {"abs","abs(x)"}, {"sqrt","sqrt(x)"}, {"pow","pow(base, exp)"}, {"min","min(a, b)"}, {"max","max(a, b)"},
        {"clamp","clamp(v, lo, hi)"}, {"lerp","lerp(a, b, t)"}, {"distance","distance(x1, y1, x2, y2)"},
        {"random","random()"}, {"random_range","random_range(lo, hi)"}, {"sin","sin(x)"}, {"cos","cos(x)"},
        {"floor","floor(x)"}, {"ceil","ceil(x)"}, {"round","round(x)"}, {"print","print(value)"},
        // tweens
        {"tween_move","tween_move(x, y, dur)"}, {"tween_move3","tween_move3(x, y, z, dur)"},
        {"tween_scale","tween_scale(s, dur)"}, {"tween_rotate","tween_rotate(deg, dur)"},
        {"tween_fade","tween_fade(a, dur)"},
        {"tween_tag","tween_tag(id, \"menu\")"}, {"tween_speed_based","tween_speed_based(id)"}, {"tween_decay","tween_decay(id, rate)"},
        {"tween_kill_tag","tween_kill_tag(\"tag\"[, complete])"}, {"tween_pause_tag","tween_pause_tag(\"tag\")"},
        {"tween_play_tag","tween_play_tag(\"tag\")"}, {"tween_complete_tag","tween_complete_tag(\"tag\")"},
        {"tween_rewind_id","tween_rewind_id(\"name\")"},
        {"seq_prepend","seq_prepend(seq, id)"}, {"seq_join_callback","seq_join_callback(seq, \"fn\")"},
        // ui (immediate-mode + named)
        {"ui_begin","ui_begin(\"Title\", x, y, w, h)"}, {"ui_text","ui_text(\"s\")"}, {"ui_button","ui_button(\"Label\")"},
        {"ui_checkbox","ui_checkbox(\"Label\", on)"}, {"ui_slider","ui_slider(\"Label\", v, lo, hi)"},
        {"ui_progress","ui_progress(t)"}, {"ui_set_text","ui_set_text(\"n\", \"s\")"}, {"ui_get_text","ui_get_text(\"n\")"},
        {"ui_set_focus","ui_set_focus(\"n\")"},
        {"ui_clicked","ui_clicked(\"n\")"}, {"ui_set_slider","ui_set_slider(\"n\", v)"}, {"ui_set_progress","ui_set_progress(\"n\", v)"},
        {"ui_get_rotation","ui_get_rotation(\"n\")"}, {"ui_set_rotation","ui_set_rotation(\"n\", degrees)"},
        {"ui_get_scale","ui_get_scale(\"n\")"}, {"ui_set_scale","ui_set_scale(\"n\", x, y)"},
        {"set_swatch","set_swatch(\"n\", i)"}, {"swatch_value","swatch_value(\"n\")"}, // grok/ui-color
        {"kick_spread","kick_spread(\"n\", px)"}, {"set_spread","set_spread(\"n\", px)"}, {"set_radial","set_radial(\"n\", v)"}, {"set_slot","set_slot(\"n\", i)"}, // grok/ui-hud
        // grok/ui-controls
        {"set_dropdown_search","set_dropdown_search(\"n\", on)"}, {"set_dropdown_max","set_dropdown_max(\"n\", rows)"},
        {"set_scroll_inertia","set_scroll_inertia(\"n\", rate)"}, {"set_tabs_closable","set_tabs_closable(\"n\", on)"},
        {"set_tooltip_delay","set_tooltip_delay(\"n\", seconds)"}, {"set_input_multiline","set_input_multiline(\"n\", on)"},
        // grok/ui-toast
        {"toast_push","toast_push(\"Title\", \"Body\", kind)"},
        {"set_toast_duration","set_toast_duration(seconds)"}, {"set_toast_corner","set_toast_corner(n)"},
        {"set_toast_max","set_toast_max(n)"}, {"toast_count","toast_count()"},
        {"modal_open","modal_open()"}, {"modal_close","modal_close()"},
        {"set_modal_style","set_modal_style(on)"}, {"set_modal_title","set_modal_title(\"s\")"},
        {"set_modal_message","set_modal_message(\"s\")"}, {"set_modal_confirm","set_modal_confirm(\"s\")"},
        {"set_modal_cancel","set_modal_cancel(\"s\")"},
        // networking / raycast
        {"net_send","net_send(\"msg\")"}, {"raycast","raycast(x, y, dx, dy)"}, {"raycast3","raycast3(x, y, z, dx, dy, dz)"},
        {"overlap_circle","overlap_circle(x, y, r)"},
        {"raycast_all3","raycast_all3(x, y, z, dx, dy, dz, dist)"}, {"sphere_cast3","sphere_cast3(x, y, z, dx, dy, dz, r, dist)"},
        {"box_cast3","box_cast3(x, y, z, hx, hy, hz, dx, dy, dz, dist)"}, {"overlap_box3","overlap_box3(x, y, z, hx, hy, hz)"},
        {"box_cast","box_cast(x, y, hx, hy, dx, dy, dist)"}, {"capsule_cast","capsule_cast(ax, ay, bx, by, r, dx, dy, dist)"},
        {"overlap_box","overlap_box(x, y, hx, hy)"}, {"overlap_capsule","overlap_capsule(ax, ay, bx, by, r)"},
        {"add_explosion_force","add_explosion_force(x, y, force, radius, up)"},
        {"overlap_sphere3","overlap_sphere3(x, y, z, r)"},
        // timers
        {"after","after(seconds, \"func\")"}, {"every","every(seconds, \"func\")"}, {"cancel_timers","cancel_timers()"},
        // high-level one-liners (call every frame from update)
        {"follow","follow(\"name\", speed[, stopDist])"}, {"follow3","follow3(\"name\", speed[, stopDist])"},
        {"flee","flee(\"name\", speed)"}, {"patrol","patrol(x1, y1, x2, y2, speed)"},
        {"orbit","orbit(\"name\", radius, degPerSec)"}, {"on_key_move","on_key_move(speed)"},
        {"platformer","platformer(speed[, jump])"}, {"on_key","on_key(\"key\", \"fn\")"},
        {"smooth_follow","smooth_follow(\"name\", speed)"}, {"spring_to","spring_to(x, y[, speed])"},
        {"aim","aim(x, y)"}, {"grid_snap","grid_snap(size)"},
        {"stop","stop()"}, {"is_moving","is_moving()"}, {"face_velocity","face_velocity()"},
        {"cooldown","cooldown(\"name\", seconds)"}, {"once","once(\"name\")"},
        {"on_key_move3","on_key_move3(speed)"}, {"shoot_at","shoot_at(\"target\", \"prefab\", speed)"},
        {"spawn_wave","spawn_wave(\"prefab\", count[, radius])"}, {"chase","chase(\"name\", speed[, stopDist])"},
        // velocity axes
        {"set_vx","set_vx(v)"}, {"set_vy","set_vy(v)"}, {"set_vz","set_vz(v)"}, {"set_z","set_z(v)"},
        {"set_rot3","set_rot3(x, y, z)"},
        // object / scene
        {"obj_x","obj_x(\"name\")"}, {"obj_y","obj_y(\"name\")"}, {"obj_z","obj_z(\"name\")"},
        {"tag","tag()"}, {"set_tag","set_tag(\"tag\")"}, {"has_tag","has_tag(\"tag\")"},
        {"is_active","is_active()"}, {"set_active","set_active(on)"}, {"set_name","set_name(\"name\")"},
        {"has_parent","has_parent()"}, {"set_parent","set_parent(\"name\")"},
        {"child_count","child_count(\"name\")"}, {"get_child","get_child(\"name\", index)"}, {"parent_of","parent_of(\"name\")"},
        {"hurt","hurt(\"name\", amount)"}, {"heal","heal(\"name\", amount)"}, {"add_xp","add_xp(\"name\", amount)"},
        {"get_hp","get_hp(\"name\")"}, {"get_max_hp","get_max_hp(\"name\")"}, {"is_dead","is_dead([\"name\"])"},
        {"set_light_intensity","set_light_intensity(\"name\", v)"}, {"set_light_range","set_light_range(\"name\", v)"},
        {"set_light_angle","set_light_angle(\"name\", degrees)"}, {"set_light_color","set_light_color(\"name\", r, g, b)"},
        {"cam_x","cam_x()"}, {"cam_y","cam_y()"}, {"cam_zoom","cam_zoom()"},
        // render / look
        {"set_color","set_color(r, g, b)"}, {"set_texture","set_texture(\"path\")"}, {"set_mesh","set_mesh(\"name\")"},
        {"set_unlit","set_unlit(on)"}, {"set_emissive","set_emissive(r, g, b)"}, {"set_text","set_text(\"s\")"},
        // animation (Character clips + layers)
        {"play_anim","play_anim(\"clip\")"}, {"stop_anim","stop_anim()"}, {"set_anim","set_anim(\"clip\")"},
        {"play_clip","play_clip(\"clip\")"}, {"play_clip_once","play_clip_once(\"clip\")"},
        {"play_action","play_action(\"Death\" [, blend])"}, {"is_in_action","is_in_action()"},
        {"action_time","action_time()"}, {"die","die([\"back\"])"}, {"revive","revive()"}, {"is_dead","is_dead()"},
        {"dive","dive()"}, {"can_dive","can_dive()"},
        {"sit","sit([\"Chair\"])"}, {"stand_up","stand_up()"}, {"is_sitting","is_sitting()"},
        {"interact","interact()"}, {"seat_prompt","seat_prompt()"},
        {"hit_react","hit_react(\"back\" [, 1])"},
        {"roll","roll(\"forward\")"}, {"dodge","dodge(\"left\")"},
        {"vault","vault()"}, {"mantle","mantle()"}, {"ledge_grab","ledge_grab()"},
        {"climb_up","climb_up()"}, {"let_go","let_go()"}, {"is_hanging","is_hanging()"}, {"shimmy","shimmy(-1..1)"},
        {"stop_clip","stop_clip()"}, {"is_playing_clip","is_playing_clip()"},
        {"play_layer","play_layer(\"clip\", \"part\")"}, {"stop_layer","stop_layer()"}, {"anim_event","anim_event(\"name\")"},
        {"play_sound","play_sound(\"name\")"}, {"set_volume","set_volume(v)"},
        // particles
        {"emit","emit(n)"}, {"particles_on","particles_on(on)"}, {"particles_alive","particles_alive()"},
        // tilemap
        {"set_tile","set_tile(x, y, id)"}, {"get_tile","get_tile(x, y)"}, {"tile_w","tile_w()"}, {"tile_h","tile_h()"},
        // arrays / lists
        {"array","array(v1, v2, ...)"}, {"count","count(arr)"}, {"push","push(arr, value)"}, {"pop","pop(arr)"},
        {"contains","contains(arr, value)"}, {"len","len(x, y)"},
        {"first","first(arr)"}, {"last","last(arr)"}, {"clear","clear(arr_or_map)"},
        {"insert_at","insert_at(arr, index, value)"}, {"slice","slice(arr, start, end)"},
        {"range","range(n)  |  range(lo, hi[, step])"}, {"sort_str","sort_str(arr)"},
        {"remove_at","remove_at(arr, index)"}, {"index_of","index_of(arr, value)"},
        {"sum","sum(arr)"}, {"min_of","min_of(arr)"}, {"max_of","max_of(arr)"},
        {"reverse","reverse(arr)"}, {"sort_num","sort_num(arr)"}, {"shuffle","shuffle(arr)"}, {"choose","choose(arr)"},
        // maps / dictionaries
        {"map","map()"}, {"map_set","map_set(m, \"key\", value)"}, {"map_get","map_get(m, \"key\")"},
        {"map_has","map_has(m, \"key\")"}, {"map_remove","map_remove(m, \"key\")"}, {"map_keys","map_keys(m)"},
        {"map_values","map_values(m)"}, {"map_count","map_count(m)"}, {"map_clear","map_clear(m)"},
        {"map_merge","map_merge(dst, src)"},
        // vectors
        {"vec3","vec3(x, y, z)"}, {"vec2","vec2(x, y)"}, {"vec_add","vec_add(a, b)"}, {"vec_sub","vec_sub(a, b)"},
        {"vec_scale","vec_scale(v, s)"}, {"vec_length","vec_length(v)"}, {"vec_dot","vec_dot(a, b)"},
        {"vec_cross","vec_cross(a, b)"}, {"vec_normalize","vec_normalize(v)"}, {"vec_distance","vec_distance(a, b)"},
        {"vec_lerp","vec_lerp(a, b, t)"}, {"vec_reflect","vec_reflect(dir, normal)"},
        {"vec_clamp_len","vec_clamp_len(v, maxLen)"}, {"vec_x","vec_x(v)"}, {"vec_y","vec_y(v)"}, {"vec_z","vec_z(v)"},
        // easing curves (t in 0..1)
        {"ease_in","ease_in(t)"}, {"ease_out","ease_out(t)"}, {"ease_in_out","ease_in_out(t)"},
        {"ease_in_cubic","ease_in_cubic(t)"}, {"ease_out_cubic","ease_out_cubic(t)"},
        {"ease_in_out_cubic","ease_in_out_cubic(t)"}, {"ease_back","ease_back(t)"},
        {"ease_elastic","ease_elastic(t)"}, {"ease_bounce","ease_bounce(t)"}, {"ease_curve","ease_curve(t, \"OutBack\")"},
        // time (the C#-style Time.* methods are keyed by their member name: the hint shows after `Time.HitStop(`)
        {"session_time","session_time()"}, {"session_unscaled_time","session_unscaled_time()"}, {"session_frame_count","session_frame_count()"},
        {"unscaled_dt","unscaled_dt()"}, {"is_paused","is_paused()"}, {"hitch_count","hitch_count()"},
        {"push_pause","push_pause(\"reason\")"}, {"pop_pause","pop_pause(\"reason\")"}, {"is_paused_by","is_paused_by(\"reason\")"},
        {"hit_stop","hit_stop(seconds [, scale])"}, {"slow_motion","slow_motion(scale, seconds)"}, {"ramp_timescale","ramp_timescale(target, seconds)"},
        {"HitStop","Time.HitStop(seconds, scale)"}, {"SlowMotion","Time.SlowMotion(scale, seconds)"}, {"RampTimeScale","Time.RampTimeScale(target, seconds)"},
        {"PushPause","Time.PushPause(reason)"}, {"PopPause","Time.PopPause(reason)"}, {"IsPausedBy","Time.IsPausedBy(reason)"},
        // higher-order array ops (named-function callbacks)
        {"call","call(\"fn\"[, args...])"}, {"map_fn","map_fn(arr, \"fn\")"}, {"filter_fn","filter_fn(arr, \"fn\")"},
        {"reduce_fn","reduce_fn(arr, \"fn\", init)"}, {"for_each","for_each(arr, \"fn\")"}, {"find_fn","find_fn(arr, \"fn\")"},
        {"any_fn","any_fn(arr, \"fn\")"}, {"all_fn","all_fn(arr, \"fn\")"}, {"count_fn","count_fn(arr, \"fn\")"},
        // bitwise / integer
        {"bit_and","bit_and(a, b)"}, {"bit_or","bit_or(a, b)"}, {"bit_xor","bit_xor(a, b)"}, {"bit_not","bit_not(a)"},
        {"shl","shl(a, n)"}, {"shr","shr(a, n)"}, {"bit_set","bit_set(v, bit)"}, {"bit_clear","bit_clear(v, bit)"},
        {"bit_toggle","bit_toggle(v, bit)"}, {"bit_test","bit_test(v, bit)"}, {"hex","hex(n) -> \"0x..\""},
        {"parse_int","parse_int(s[, base])"},
        // JSON
        {"to_json","to_json(value) -> string"}, {"from_json","from_json(\"...\") -> value"},
        {"json_stringify","json_stringify(value)"}, {"json_parse","json_parse(\"...\")"},
        // type introspection
        {"typeof","typeof(v) -> \"number|string|bool|array|map|vec3|null\""},
        {"is_array","is_array(v)"}, {"is_map","is_map(v)"}, {"is_str","is_str(v)"},
        {"is_num","is_num(v)"}, {"is_bool","is_bool(v)"}, {"fract","fract(x)"},
        // strings
        {"str_len","str_len(s)"}, {"str_find","str_find(s, sub)"}, {"str_contains","str_contains(s, sub)"},
        {"str_repeat","str_repeat(s, n)"}, {"starts_with","starts_with(s, prefix)"}, {"ends_with","ends_with(s, suffix)"},
        {"upper","upper(s)"}, {"lower","lower(s)"}, {"substr","substr(s, start[, n])"}, {"char_at","char_at(s, i)"},
        {"replace","replace(s, find, repl)"}, {"trim","trim(s)"}, {"trim_start","trim_start(s)"}, {"trim_end","trim_end(s)"},
        {"capitalize","capitalize(s)"}, {"title_case","title_case(s)"}, {"str_reverse","str_reverse(s)"},
        {"repeat","repeat(s, n)"}, {"split","split(s, sep)"}, {"join","join(arr, sep)"},
        {"pad_left","pad_left(s, width[, fill])"}, {"pad_right","pad_right(s, width[, fill])"},
        {"to_num","to_num(s)"}, {"to_str","to_str(v)"}, {"format","format(\"{} {}\", a, b)"},
        // ui widgets
        {"set_slider","set_slider(v)"}, {"set_toggle","set_toggle(on)"}, {"set_progress","set_progress(t)"},
        // grok/ui-options
        {"set_slider_step","set_slider_step(step)"}, {"set_slider_format","set_slider_format(\"{v}\")"},
        {"set_bar_segments","set_bar_segments(n)"}, {"set_bar_trail","set_bar_trail(seconds)"},
        {"set_bar_format","set_bar_format(\"{n}%\")"}, {"set_button_ease","set_button_ease(ease, seconds)"},
        {"ui_set_slider_step","ui_set_slider_step(\"n\", step)"}, {"ui_set_button_ease","ui_set_button_ease(\"n\", ease, seconds)"},
        {"set_tab","set_tab(i)"}, {"set_tab_badge","set_tab_badge(i, count)"}, {"set_tab_enabled","set_tab_enabled(i, on)"},
        {"ui_set_tab","ui_set_tab(\"n\", i)"}, {"ui_set_tab_badge","ui_set_tab_badge(\"n\", i, count)"},
        {"ui_set_tab_enabled","ui_set_tab_enabled(\"n\", i, on)"}, {"ui_tab_value","ui_tab_value(\"n\")"},
        // math checks
        {"is_nan","is_nan(x)"}, {"is_finite","is_finite(x)"}, {"acos","acos(x)"}, {"asin","asin(x)"},
        {"atan","atan(x)"}, {"atan2","atan2(y, x)"}, {"sign","sign(x)"},
        {"approach","approach(current, target, step)"}, {"approximately","approximately(a, b)"}, {"angle_to","angle_to(\"name\")"},
        // PlayFab (cloud services; blocking web calls — use from start())
        {"pf_login","pf_login(\"titleId\", \"customId\")"},
        {"pf_login_password","pf_login_password(\"titleId\", \"user\", \"pass\")"},
        {"pf_register","pf_register(\"titleId\", \"user\", \"pass\")"},
        {"pf_name","pf_name(\"display name\")"}, {"pf_set_stat","pf_set_stat(\"stat\", value)"},
        {"pf_leaderboard_top","pf_leaderboard_top(\"stat\"[, n])"},
        {"pf_save","pf_save(\"key\", \"value\")"}, {"pf_load","pf_load(\"key\")"},
        {"pf_delete","pf_delete(\"key\")"},
    };
    return sig;
}

// Signature (parameter list) for a builtin, shown as a hint while typing a call —
// e.g. inside `move_toward(` the editor floats "move_toward(x, y, step)". Empty for
// unknown names. A curated subset of the most-used OkayScript API (see docs).
const std::string* ScriptSignature(const std::string& name) {
    const auto& sig = ScriptSignatureMap();
    auto it = sig.find(name);
    return it == sig.end() ? nullptr : &it->second;
}

// Completion "kind" — drives the little colored tag the popup shows before each item,
// so the list reads at a glance the way a real IDE's does (keyword / type / function /
// local). The tag letter + color are returned together.
AcKind ClassifyCompletion(const std::string& name) {
    static const std::unordered_set<std::string> kw = {
        "if","else","for","while","do","return","break","continue","switch","case",
        "var","function","class","new","public","private","true","false","foreach","in",
        "try","catch","throw","and","or","not","null","this","const","let","then","end",
        "void","int","float","double","bool","string","char","static","readonly","override",
        "enum","yield","default"
    };
    static const std::unordered_set<std::string> types = {
        "Vector3","Vector2","Color","Mathf","Input","Time","Debug","Random","Physics2D",
        "SceneManager","Quaternion","transform","gameObject",
        "IEnumerator","WaitForSeconds","List","Dictionary","KeyValuePair","Rigidbody2D",
        "Rigidbody","SpriteRenderer","Animator","AudioSource","Collider2D","Collision2D","Collider",
        "Collision","KeyCode","GameObject","Transform","SerializeField","Header","Range","Tooltip"
    };
    if (kw.count(name))    return AcKind::Keyword;
    if (types.count(name)) return AcKind::Type;
    if (ScriptSignature(name)) return AcKind::Function;
    return AcKind::Variable;
}
void AcKindTag(AcKind k, char& letter, ImU32& col) {
    switch (k) {
        case AcKind::Keyword:  letter = 'k'; col = IM_COL32(204, 120, 50, 255);  break; // orange
        case AcKind::Type:     letter = 'T'; col = IM_COL32(102, 197, 204, 255); break; // teal
        case AcKind::Function: letter = 'f'; col = IM_COL32(220, 200, 120, 255); break; // yellow
        default:               letter = 'v'; col = IM_COL32(140, 175, 220, 255); break; // blue
    }
}

// Fuzzy subsequence match: are all of `pat`'s chars found in `cand`, in order, case-
// insensitively? Returns a score (higher = better: contiguous runs, a match at a word
// start, and an early first-match all score higher) or -1 for no match. This is what
// lets "mvfwd" find "move_forward" the way Rider/IntelliJ do.
int FuzzyScore(const std::string& pat, const std::string& cand) {
    if (pat.empty()) return 0;
    int score = 0, ci = 0, run = 0, firstAt = -1;
    for (std::size_t pi = 0; pi < pat.size(); ) {
        char pc = (char)std::tolower((unsigned char)pat[pi]);
        bool found = false;
        for (; ci < (int)cand.size(); ++ci) {
            if ((char)std::tolower((unsigned char)cand[ci]) == pc) {
                if (firstAt < 0) firstAt = ci;
                bool wordStart = (ci == 0) || cand[ci-1] == '_' ||
                                 (std::islower((unsigned char)cand[ci-1]) && std::isupper((unsigned char)cand[ci]));
                score += 1 + run * 2 + (wordStart ? 4 : 0);
                ++run; ++ci; ++pi; found = true; break;
            } else run = 0;
        }
        if (!found) return -1;
    }
    if (firstAt == 0) score += 6;               // matched from the very start
    score -= firstAt > 0 ? firstAt : 0;         // prefer earlier first-hit
    return score;
}

// A one-line human description for a builtin, shown under its signature in the
// hover tooltip and autocomplete. Only the less-obvious / newer builtins are
// documented; anything missing simply shows no description.
const std::string* ScriptDoc(const std::string& name) {
    static const std::unordered_map<std::string, std::string> doc = {
        // high-level one-liners (call each frame from update)
        {"walk","Move in a direction (dx,dy) at `speed` — dt-scaled, so no `* dt` needed."},
        {"spin","Rotate `degPerSec` degrees each second — dt-scaled (smooth spinning)."},
        {"bob","Hover up and down around the start height by `amount` at `speed` (juice)."},
        {"pulse","Gently grow and shrink around normal size by `amount` at `speed` (juice)."},
        {"wander","Roam in a random direction, picking a new one about once a second."},
        {"explode","Burst `count` particles AND destroy this object — the classic death effect, one call."},
        {"flash","Snap to color (r,g,b), then fade back to the current color over `dur` (hit feedback)."},
        {"shake","Shake this object with `intensity`, decaying over `dur` seconds."},
        {"face","Turn to look at a named object (2D)."},
        {"move_to","Walk toward a world point (x,y) at `speed`, stopping on arrival — dt-scaled."},
        {"score","Read the shared score value."},
        {"add_score","Add `n` to the shared score (short for set/get) — returns the new total."},
        {"set_score","Set the shared score to `n`."},
        {"timer","Returns true about once every `seconds` — no manual accumulator. if (timer(\"spawn\", 2)) {...}"},
        {"set_visible","Show/hide this object's graphics without deactivating it (its script keeps running)."},
        {"show","Show this object's graphics."},
        {"hide","Hide this object's graphics (but keep the script running)."},
        {"blink","Flash this object's graphics on/off at `rate` (invincibility / pickups)."},
        {"keep_in_box","Clamp this object inside a rectangle (stay on screen)."},
        {"wrap_in_box","Teleport to the opposite edge when leaving a rectangle (asteroids-style)."},
        {"bounce_in_box","Reverse the Rigidbody2D velocity at each wall of a rectangle (ball/pong)."},
        {"on_key_move","WASD/arrow keys move this object at `speed` (uses a Rigidbody2D if present)."},
        {"follow","Chase a named object on the XY plane at `speed`, stopping `stopDist` away."},
        {"follow3","Chase a named object in full 3D at `speed`, stopping `stopDist` away."},
        {"chase","Chase a named object (alias of follow)."},
        {"flee","Run directly away from a named object at `speed`."},
        {"patrol","Walk back and forth between (x1,y1) and (x2,y2) at `speed`."},
        {"orbit","Circle a named target at `radius`, turning `degPerSec` each second."},
        {"platformer","A whole 2D side-scroller controller: A/D or Left/Right move at `speed`, W/Up jumps by `jump` (uses a Rigidbody2D). dt-scaled."},
        {"on_key","Call this script's function `fn` the frame `key` is pressed — event-style input, no if/edge bookkeeping."},
        {"smooth_follow","Chase a named object with easing (exponential smoothing) so it glides in at `speed` — dt-scaled."},
        {"spring_to","Ease toward a world point (x,y) at `speed`, slowing as it arrives (cameras, cursors, snapping) — dt-scaled."},
        {"aim","Rotate (Z) to face a world point (x,y) — turrets, arrows, look-where-you-move."},
        {"grid_snap","Snap this object's position to the nearest multiple of `size` on X and Y (tile / building games)."},
        {"stop","Zero this object's Rigidbody velocity (halt in place)."},
        {"is_moving","True while this object's Rigidbody2D is moving (drive walk/idle anim)."},
        {"face_velocity","Rotate (Z) to point the way the Rigidbody2D is moving."},
        {"cooldown","True (and restarts the timer) only once every `seconds` — shooting, dashes, abilities: if (cooldown(\"shoot\", 0.3)) {...}"},
        {"once","True exactly the first time it's reached (per name) — one-time events with no manual flag."},
        {"on_key_move","WASD/arrow keys move this object on the XY plane (uses a Rigidbody2D if present)."},
        {"on_key_move3","WASD/arrow keys move this object on the XZ ground plane (uses a Rigidbody3D if present)."},
        {"shoot_at","Spawn a prefab projectile at self, flying toward a named target at `speed`."},
        {"spawn_wave","Spawn `count` copies of a prefab in a ring of `radius` around self."},
        // arrays
        {"array","Make an array from the given values."},
        {"count","Number of items in an array."},
        {"push","Append a value to the end of an array."},
        {"pop","Remove and return the last item of an array."},
        {"first","First item of an array (or null if empty)."},
        {"last","Last item of an array (or null if empty)."},
        {"clear","Empty an array or map in place."},
        {"insert_at","Insert a value at an index, shifting the rest right."},
        {"remove_at","Remove the item at an index."},
        {"slice","A sub-array from start up to (not including) end. Negative indices count from the end."},
        {"range","Build a numeric array: range(n), range(lo,hi) or range(lo,hi,step)."},
        {"contains","True if the array holds the given value."},
        {"index_of","Index of the first matching value, or -1."},
        {"sum","Add up all numbers in an array."},
        {"min_of","Smallest number in an array."},
        {"max_of","Largest number in an array."},
        {"reverse","Reverse an array in place."},
        {"sort_num","Sort an array ascending by number."},
        {"sort_str","Sort an array ascending by text."},
        {"shuffle","Randomly reorder an array in place."},
        {"choose","Return a random item from an array."},
        // maps
        {"map","Make an empty dictionary (string keys -> values)."},
        {"map_set","Set a key to a value in a map."},
        {"map_get","Read the value at a key (or null)."},
        {"map_has","True if the map contains the key."},
        {"map_remove","Delete a key from a map."},
        {"map_keys","An array of the map's keys."},
        {"map_values","An array of the map's values."},
        {"map_count","Number of entries in a map."},
        {"map_clear","Remove all entries from a map."},
        {"map_merge","Copy src's entries into dst (src wins on conflict)."},
        // higher-order
        {"call","Call a function by name, passing any extra arguments."},
        {"map_fn","New array of fn(element) for each element."},
        {"filter_fn","Keep only elements where fn(element) is true."},
        {"reduce_fn","Fold an array left: acc = fn(acc, element)."},
        {"for_each","Call fn(element) for every element."},
        {"find_fn","First element where fn(element) is true (or null)."},
        {"any_fn","True if fn(element) is true for any element."},
        {"all_fn","True if fn(element) is true for every element."},
        {"count_fn","How many elements satisfy fn(element)."},
        // json
        {"to_json","Serialize any value (arrays/maps too) to a JSON string."},
        {"from_json","Parse a JSON string into values/arrays/maps."},
        {"json_stringify","Alias of to_json."},
        {"json_parse","Alias of from_json."},
        // type introspection
        {"typeof","Name the value's type: number, string, bool, array, map, vec3 or null."},
        {"is_array","True if the value is an array."},
        {"is_map","True if the value is a map."},
        {"is_str","True if the value is a string."},
        {"is_num","True if the value is a number."},
        {"is_bool","True if the value is a boolean."},
        // strings
        {"upper","Uppercase a string."},
        {"lower","Lowercase a string."},
        {"trim","Strip leading and trailing whitespace."},
        {"trim_start","Strip leading whitespace."},
        {"trim_end","Strip trailing whitespace."},
        {"capitalize","Uppercase the first letter."},
        {"title_case","Uppercase the first letter of each word."},
        {"str_reverse","Reverse the characters of a string."},
        {"substr","A substring from start for n characters."},
        {"replace","Replace every occurrence of a substring."},
        {"split","Split a string into an array on a separator."},
        {"join","Join an array into a string with a separator."},
        {"repeat","Repeat a string n times."},
        {"format","Fill each {} in the template with the next argument."},
        // math / easing
        {"fract","Fractional part of x (x - floor(x))."},
        {"clamp","Constrain v to the range [lo, hi]."},
        {"lerp","Linearly interpolate from a to b by t (0..1)."},
        {"smoothstep","Ease-in-out interpolate from a to b by t."},
        {"ease_in","Quadratic ease-in for t in 0..1."},
        {"ease_out","Quadratic ease-out for t in 0..1."},
        {"ease_in_out","Quadratic ease-in-out for t in 0..1."},
        {"ease_in_cubic","Cubic ease-in for t in 0..1."},
        {"ease_out_cubic","Cubic ease-out for t in 0..1."},
        {"ease_in_out_cubic","Cubic ease-in-out for t in 0..1."},
        {"ease_back","Ease with a slight backward overshoot."},
        {"ease_elastic","Springy elastic ease-out."},
        {"ease_bounce","Bouncing ease-out."},
        {"ease_curve","The eased value of t (0..1) for any ease the tween calls take: a name (\"OutBack\") or curve text (\"cubic-bezier(0.4,0,0.2,1)\", \"steps(4)\"). Text it does not know leaves t as it is."},
        {"session_time","Game seconds since this Play began (time() keeps counting across Plays)."},
        {"session_unscaled_time","Real seconds since this Play began: a pause, slow motion and hit stops do not change it."},
        {"session_frame_count","Frames since this Play began."},
        {"unscaled_dt","Real seconds of the last frame: not slowed by the time scale, a hit stop or a pause (animate a menu with it)."},
        {"is_paused","True while the game is paused (a menu, a cutscene, the pause button)."},
        {"hitch_count","Frames that took over a tenth of a second since this Play began."},
        {"push_pause","Pause the game for a reason of your own (\"dialogue\", \"shop\"): it stays paused until every reason is let go."},
        {"pop_pause","Let go of one hold of that reason. False when nothing held it."},
        {"is_paused_by","True while that reason is holding the pause."},
        {"hit_stop","A freeze frame (scale 0, the default) or a brief slow-down for an impact: real seconds, ends by itself."},
        {"slow_motion","Bullet time: slow_motion(0.2, 2) runs at a fifth speed for two real seconds, then back to normal."},
        {"ramp_timescale","Ease the time scale to `target` over `seconds` real seconds, in a straight line."},
        {"HitStop","A freeze frame or a brief slow-down for an impact: for `seconds` real seconds the game runs at `scale` (0 = frozen), then goes back by itself."},
        {"SlowMotion","Bullet time: run at `scale` (0.2 = a fifth) for `seconds` real seconds, then back to normal. Composes with Time.timeScale."},
        {"RampTimeScale","Ease Time.timeScale to `target` over `seconds` real seconds, in a straight line."},
        {"PushPause","Hold the game paused for a reason of your own (\"dialogue\", \"shop\"); it stays paused until every reason is let go."},
        {"PopPause","Let go of one hold of that reason. False when nothing held it."},
        {"IsPausedBy","True while that reason is holding the pause."},
        // grok/ui-color
        {"set_swatch","Select a swatch on a named color picker. Fires on_change when it changes."},
        {"swatch_value","The selected swatch index on a named color picker, or -1."},
        // grok/ui-hud
        {"kick_spread","Push a named crosshair's ticks apart. They ease back while its recover rate is above 0."},
        {"set_spread","Set a named crosshair's extra gap, in pixels."},
        {"set_spread_recover","Pixels per second a named crosshair's extra gap eases back to 0. 0 keeps it."},
        {"set_radial","Set a named radial progress fill, from 0 to 1."},
        {"set_radial_thickness","Ring width in pixels. 0 draws a pie."},
        {"set_radial_start","Where a named radial starts, in degrees. 0 is 12 o'clock."},
        {"set_radial_spin","Spin a named radial as a loader."},
        {"set_radial_clockwise","Fill a named radial clockwise."},
        {"set_radial_percent","Show a named radial's fill as a percent."},
        {"set_slot","Select a slot on a named slot grid. -1 clears it."},
        {"slot_value","The selected slot on a named grid, or -1."},
        // grok/ui-controls
        {"set_dropdown_search","Turn a named dropdown's search row on or off."},
        {"set_dropdown_max","Cap how many options an open dropdown shows. 0 shows all."},
        {"set_dropdown_icon","Set the short glyph in front of one dropdown option."},
        {"set_scroll_inertia","Wheel glide for a named scroll view. 0 scrolls immediately."},
        {"set_scroll_elastic","Let a named scroll view pass its ends, then spring back."},
        {"set_scroll_bar","Scrollbar style: 0 usual, 1 thin, 2 hidden until scrolling."},
        {"set_tabs_closable","Draw an X on each tab of a named tab bar."},
        {"close_tab","Remove one tab. Fires on_tab_close."},
        {"set_tooltip_rich","Draw <b> and #rrggbb in a named tooltip."},
        {"set_tooltip_offset","Move a tooltip relative to the cursor. 14, 14 is the usual spot."},
        {"set_tooltip_delay","Seconds a named tooltip waits before it shows."},
        {"set_input_multiline","Enter inserts a newline in a named text field."},
        {"set_input_limit","Character limit of a named text field."},
        {"set_input_type","0 standard, 1 integer, 2 decimal, 3 password."},
        // grok/ui-toast
        {"toast_push","Show a toast on the scene's UI Toast. kind: 0 info, 1 success, 2 warning, 3 error."},
        {"set_toast_duration","How long a new toast stays up, in seconds."},
        {"set_toast_corner","0 top-right, 1 top-left, 2 bottom-right, 3 bottom-left, 4 top-center."},
        {"set_toast_max","How many toasts stay on screen."},
        {"toast_count","How many toasts are on screen right now."},
        {"modal_open","Show the first modal dialog."},
        {"modal_close","Hide the first modal dialog. Fires on_modal_close."},
        {"set_modal_style","Turn the dialog's card styling on or off."},
        {"set_modal_title","Set the dialog title. Empty leaves the label already there."},
        {"set_modal_message","Set the dialog message. Empty leaves the label already there."},
        {"set_modal_confirm","Set the confirm button's label."},
        {"set_modal_cancel","Set the cancel button's label."},
        {"on_modal_close","Fires when a modal dialog closes."},
    };
    auto it = doc.find(name);
    return it == doc.end() ? nullptr : &it->second;
}

// Members offered after "<receiver>." in the editor (API surface, so
// e.g. typing `transform.` lists position/rotation/Translate/…). Returns an empty
// list for unknown receivers.
const std::vector<std::string>& ScriptMembers(const std::string& receiver) {
    static const std::unordered_map<std::string, std::vector<std::string>> M = {
        {"transform", {"position","rotation","localPosition","localScale","eulerAngles",
                       "right","up","forward","Translate","Rotate","LookAt"}},
        {"gameObject", {"name","tag","activeSelf","SetActive","GetComponent"}},
        {"Input", {"GetAxis","GetButton","GetButtonDown","GetButtonUp","GetKey","GetKeyDown",
                   "GetKeyUp","GetMouseButton","GetMouseButtonDown","GetMouseButtonUp","mousePosition"}},
        {"Time", {"deltaTime","time","timeScale","fixedDeltaTime","unscaledDeltaTime","unscaledTime","sessionTime",
                  "sessionUnscaledTime","sessionFrameCount","fps","hitchCount","paused",
                  "HitStop","SlowMotion","RampTimeScale","PushPause","PopPause","IsPausedBy"}},
        {"Mathf", {"Abs","Acos","Asin","Atan","Ceil","Clamp","Clamp01","Cos","Deg2Rad","DeltaAngle",
                   "Epsilon","Exp","Floor","Infinity","InverseLerp","Lerp","LerpAngle","Log","Max",
                   "Min","MoveTowards","PI","PerlinNoise","PingPong","Pow","Rad2Deg","Repeat","Round",
                   "Sign","Sin","SmoothStep","Sqrt","Tan"}},
        {"Physics2D", {"Raycast","OverlapCircle"}},
        {"Debug", {"Log","LogWarning","LogError"}},
        {"Random", {"Range","value"}},
        {"Quaternion", {"Euler","identity","AngleAxis"}},
        {"Vector3", {"zero","one","up","down","left","right","forward","back","Angle","Distance",
                     "Dot","Cross","Lerp","LerpUnclamped","Normalize"}},
        {"Vector2", {"zero","one","up","down","left","right","Lerp","Distance"}},
        {"Color", {"red","green","blue","white","black","gray","grey","yellow","cyan","magenta","Lerp"}},
        {"SceneManager", {"LoadScene","GetActiveScene"}},
        {"Screen", {"width","height"}},
        {"Application", {"Quit"}},
    };
    static const std::vector<std::string> empty;
    auto it = M.find(receiver);
    return it == M.end() ? empty : it->second;
}

// ---- C#-aware member completion ---------------------------------------------
// After `expr.` the popup lists what `expr` actually has: we work out its type
// from the script itself (declarations `Type name`, `var x = new T(..)`,
// `GetComponent<T>()`, method parameters, enums) or from well-known engine
// properties (`transform.position` is a Vector3). Methods carry a trailing '('
// so the popup can show "()" and put the caret inside on accept.
static const std::vector<std::string>* CsTypeMembers(const std::string& type) {
    static const std::unordered_map<std::string, std::vector<std::string>> T = {
        {"string", {"Length", "ToUpper(", "ToLower(", "Contains(", "StartsWith(", "EndsWith(", "IndexOf(",
                    "LastIndexOf(", "Substring(", "Replace(", "Split(", "Trim(", "TrimStart(", "TrimEnd(",
                    "PadLeft(", "PadRight(", "Insert(", "Remove(", "Equals(", "ToString("}},
        {"List", {"Count", "Add(", "AddRange(", "Insert(", "Remove(", "RemoveAt(", "RemoveAll(", "Contains(",
                  "IndexOf(", "Clear(", "Sort(", "Reverse(", "Find(", "FindIndex(", "FindAll(", "ForEach(",
                  "Where(", "Select(", "Any(", "All(", "First(", "Last(", "Sum(", "Max(", "Min(", "Average(",
                  "OrderBy(", "OrderByDescending(", "ToArray(", "ToList("}},
        {"Array", {"Length", "Contains(", "IndexOf(", "Where(", "Select(", "Any(", "All(", "First(", "Last(",
                   "Sum(", "Max(", "Min(", "OrderBy(", "ToList("}},
        {"Dictionary", {"Count", "Keys", "Values", "Add(", "TryAdd(", "ContainsKey(", "ContainsValue(", "Remove(",
                        "Clear(", "GetValueOrDefault("}},
        {"Vector3", {"x", "y", "z", "magnitude", "sqrMagnitude", "normalized"}},
        {"Transform", {"position", "localPosition", "localScale", "rotation", "eulerAngles", "localEulerAngles",
                       "forward", "up", "right", "Translate(", "Rotate(", "LookAt("}},
        {"GameObject", {"name", "tag", "activeSelf", "transform", "SetActive(", "CompareTag(", "GetComponent<"}},
        {"Other", {"name", "tag", "gameObject", "transform", "CompareTag(", "GetComponent<"}},
        {"Rigidbody2D", {"velocity", "gravityScale", "AddForce(", "MovePosition("}},
        {"Rigidbody", {"velocity", "AddForce(", "MovePosition("}},
        {"SpriteRenderer", {"color", "enabled", "flipX"}},
        {"Renderer", {"color", "enabled", "material"}},
        {"Animator", {"SetTrigger(", "SetBool(", "SetFloat(", "SetInteger(", "Play(", "CrossFade("}},
        {"AudioSource", {"volume", "Play(", "Stop("}},
        {"Text", {"text", "color"}},
        {"Lambda", {"Invoke("}},
    };
    auto it = T.find(type);
    return it == T.end() ? nullptr : &it->second;
}
// Normalise a declared C# type name to a CsTypeMembers key ("" = unknown).
static std::string CsTypeKey(std::string ty) {
    if (ty.empty()) return "";
    if (ty.size() > 2 && ty.compare(ty.size() - 2, 2, "[]") == 0) return "Array";
    if (auto lt = ty.find('<'); lt != std::string::npos) ty = ty.substr(0, lt);
    static const std::unordered_map<std::string, std::string> alias = {
        {"string", "string"}, {"String", "string"}, {"char", "string"},
        {"List", "List"}, {"HashSet", "List"}, {"Queue", "List"}, {"Stack", "List"}, {"IEnumerable", "List"},
        {"Dictionary", "Dictionary"}, {"SortedDictionary", "Dictionary"},
        {"Vector3", "Vector3"}, {"Vector2", "Vector3"}, {"Color", "Vector3"},
        {"Transform", "Transform"}, {"GameObject", "GameObject"},
        {"Collider", "Other"}, {"Collider2D", "Other"}, {"Collision", "Other"}, {"Collision2D", "Other"},
        {"Rigidbody2D", "Rigidbody2D"}, {"Rigidbody", "Rigidbody"}, {"SpriteRenderer", "SpriteRenderer"},
        {"Renderer", "Renderer"}, {"MeshRenderer", "Renderer"}, {"SkinnedMeshRenderer", "Renderer"},
        {"Animator", "Animator"}, {"AudioSource", "AudioSource"},
        {"Text", "Text"}, {"TMP_Text", "Text"}, {"TextMeshPro", "Text"}, {"TextMeshProUGUI", "Text"},
        {"Action", "Lambda"}, {"Func", "Lambda"},
    };
    auto it = alias.find(ty);
    return it == alias.end() ? "" : it->second;
}
// The declared type of `var` in `src` ("" if unknown): `Type var`, `Type<..> var`,
// `Type[] var`, `var var = new Type(..)` / `GetComponent<Type>()` / "text" / { .. }.
static std::string CsDeclaredType(const std::string& src, const std::string& var) {
    auto isW = [](char c) { return std::isalnum((unsigned char)c) || c == '_'; };
    const std::size_t n = src.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (src.compare(i, var.size(), var) != 0) continue;
        if (i > 0 && isW(src[i - 1])) continue;
        const std::size_t e = i + var.size();
        if (e < n && isW(src[e])) continue;
        // What follows must look like a declaration: '=', ';', ',', ')' or ' in '.
        std::size_t a = e; while (a < n && (src[a] == ' ' || src[a] == '\t')) ++a;
        if (a >= n) continue;
        const bool declEnd = src[a] == '=' || src[a] == ';' || src[a] == ',' || src[a] == ')' ||
                             src.compare(a, 3, "in ") == 0 || src[a] == '{';
        if (!declEnd || (src[a] == '=' && a + 1 < n && src[a + 1] == '=')) continue;
        // The type just before the name (skip spaces, then a generic / array suffix).
        std::size_t b = i; while (b > 0 && (src[b - 1] == ' ' || src[b - 1] == '\t')) --b;
        if (b == i) continue;   // no space between type and name
        std::size_t te = b;
        if (b > 0 && (src[b - 1] == '>' || src[b - 1] == ']')) {
            int depth = 0; std::size_t k = b;
            while (k > 0) {
                char c = src[k - 1];
                if (c == '>' || c == ']') ++depth;
                else if (c == '<' || c == '[') { if (--depth == 0) { --k; break; } }
                --k;
            }
            b = k;
        }
        std::size_t ts = b; while (ts > 0 && isW(src[ts - 1])) --ts;
        if (ts == b) continue;
        std::string ty = src.substr(ts, te - ts);
        ty.erase(std::remove(ty.begin(), ty.end(), ' '), ty.end());
        static const std::unordered_set<std::string> notTypes = {
            "return", "new", "else", "case", "in", "out", "ref", "public", "private", "static", "const"};
        if (notTypes.count(ty)) continue;
        if (ty == "var") {   // infer from the initializer
            if (src[a] != '=') continue;
            std::size_t v = a + 1; while (v < n && src[v] == ' ') ++v;
            if (v < n && (src[v] == '"' || (src[v] == '$' && v + 1 < n && src[v + 1] == '"'))) return "string";
            if (v < n && src[v] == '{') return "Array";
            if (src.compare(v, 4, "new ") == 0) {
                std::size_t w = v + 4; std::size_t w2 = w; while (w2 < n && (isW(src[w2]) || src[w2] == '.')) ++w2;
                std::string nt = src.substr(w, w2 - w);
                if (w2 < n && src[w2] == '[') return "Array";
                return CsTypeKey(nt);
            }
            if (src.compare(v, 13, "GetComponent<") == 0) {
                std::size_t w = v + 13, w2 = src.find('>', w);
                if (w2 != std::string::npos) return CsTypeKey(src.substr(w, w2 - w));
            }
            if (src.compare(v, 15, "GameObject.Find") == 0) return "GameObject";
            continue;
        }
        if (std::string k = CsTypeKey(ty); !k.empty()) return k;
        return ty;   // maybe an enum / class declared in this file
    }
    return "";
}
// Enum members declared in `src` for enum `name` (empty if not an enum).
static std::vector<std::string> CsEnumMembers(const std::string& src, const std::string& name) {
    std::vector<std::string> out;
    auto p = src.find("enum " + name);
    if (p == std::string::npos) return out;
    auto lb = src.find('{', p), rb = src.find('}', p);
    if (lb == std::string::npos || rb == std::string::npos || rb < lb) return out;
    std::string body = src.substr(lb + 1, rb - lb - 1), cur;
    auto flush = [&]() {
        auto a = cur.find_first_not_of(" \t\r\n"); if (a == std::string::npos) { cur.clear(); return; }
        std::string t = cur.substr(a); auto e = t.find_first_of(" =\t\r\n"); if (e != std::string::npos) t = t.substr(0, e);
        if (!t.empty()) out.push_back(t);
        cur.clear();
    };
    for (char c : body) { if (c == ',') flush(); else cur += c; }
    flush();
    return out;
}
// The fields and methods this script declares (for `this.`).
static std::vector<std::string> CsOwnMembers(const std::string& src) {
    std::vector<std::string> out;
    std::unordered_set<std::string> have;
    std::istringstream in(src); std::string line; int depth = 0;
    while (std::getline(in, line)) {
        const int d0 = depth;
        for (char c : line) { if (c == '{') ++depth; else if (c == '}') --depth; }
        if (d0 > 1) continue;   // only class-level declarations
        auto cm = line.find("//"); if (cm != std::string::npos) line = line.substr(0, cm);
        {
            auto f = line.find_first_not_of(" \t");
            if (f == std::string::npos) continue;
            static const char* skip[] = {"using ", "namespace ", "[", "}"};
            bool sk = false; for (const char* k : skip) sk = sk || line.compare(f, std::strlen(k), k) == 0;
            if (sk || line.find(" enum ") != std::string::npos || line.compare(f, 5, "enum ") == 0 ||
                line.find(" class ") != std::string::npos || line.compare(f, 6, "class ") == 0) continue;
        }
        auto paren = line.find('('), eq = line.find('='), semi = line.find(';');
        std::size_t endName = std::min({paren, eq, semi});
        if (endName == std::string::npos || endName == 0) continue;
        std::size_t e = endName; while (e > 0 && line[e - 1] == ' ') --e;
        std::size_t b = e; while (b > 0 && (std::isalnum((unsigned char)line[b - 1]) || line[b - 1] == '_')) --b;
        if (b == e || b == 0) continue;   // needs a type before the name
        std::string nm = line.substr(b, e - b);
        static const std::unordered_set<std::string> kw = {"if", "for", "while", "switch", "foreach", "return", "using", "new"};
        if (kw.count(nm) || have.count(nm)) continue;
        have.insert(nm);
        out.push_back(paren == endName ? nm + "(" : nm);
    }
    return out;
}
// Everything to offer after `receiver.` (receiver = the expression before the dot).
std::vector<std::string> CsMembersFor(const std::string& src, const std::string& receiver) {
    if (receiver.empty()) return {};
    if (receiver == "this") return CsOwnMembers(src);
    std::string last = receiver;
    if (auto d = receiver.rfind('.'); d != std::string::npos) last = receiver.substr(d + 1);
    std::string key;
    if (receiver.rfind("GetComponent<", 0) == 0 || last.rfind("GetComponent<", 0) == 0) {
        auto lt = receiver.find('<'), gt = receiver.find('>');
        if (lt != std::string::npos && gt != std::string::npos) key = CsTypeKey(receiver.substr(lt + 1, gt - lt - 1));
    } else if (receiver == "transform" || last == "transform") key = "Transform";
    else if (receiver == "gameObject" || last == "gameObject") key = "GameObject";
    else if (receiver.find('.') != std::string::npos) {
        static const std::unordered_set<std::string> vec = {"position", "localPosition", "localScale", "eulerAngles",
            "localEulerAngles", "forward", "up", "right", "velocity", "normalized", "mousePosition"};
        static const std::unordered_set<std::string> str = {"name", "tag", "text"};
        if (vec.count(last)) key = "Vector3";
        else if (str.count(last)) key = "string";
    } else if (receiver.back() == ']') {
        key = "";
    } else {
        std::string ty = CsDeclaredType(src, receiver);
        if (!ty.empty()) {
            if (CsTypeMembers(ty)) key = ty;
            else { auto em = CsEnumMembers(src, ty); if (!em.empty()) return {}; }
        }
        if (key.empty()) {   // an enum type used directly: State.
            auto em = CsEnumMembers(src, receiver);
            if (!em.empty()) return em;
        }
    }
    if (const auto* m = CsTypeMembers(key)) return *m;
    return {};
}

// ---- Autocomplete details: description + code example -----------------------
// Shown beside the suggestion list (like Rider / VS Code's details pane). Curated
// entries cover the APIs people reach for most; any other builtin gets an example
// generated from its signature. Keys are "Receiver.Member" or a bare name.
const ScriptHelp* ScriptHelpFor(const std::string& key) {
    static const std::unordered_map<std::string, ScriptHelp> H = {
        // ---- Physics queries (OkayScript builtins)
        {"raycast3", {"Cast a 3D ray; returns the name of the first thing hit (\"\" = nothing). "
            "Optional: max distance, layers (\"0,3\" or \"Default,Enemy\"), ignore triggers. Details via ray3_*.",
            "if (raycast3(x, y, z, 0, -1, 0, 2, \"Default\", true) != \"\")\n{\n    grounded = true;\n}"}},
        {"raycast_all3", {"Every object a 3D ray passes through, nearest first (a list of names). "
            "ray_all_count() / ray_all_dist(i) / ray_all_point(i) give details.",
            "var hits = raycast_all3(x, y, z, 1, 0, 0, 50);\nfor (var i = 0; i < ray_all_count(); i++)\n{\n    print(ray_all_object(i) + \" at \" + ray_all_dist(i));\n}"}},
        {"sphere_cast3", {"Sweep a ball along a direction and return the first thing it touches. "
            "A thick ray that can't slip through gaps; skips triggers.",
            "string hit = sphere_cast3(x, y, z, 0, 0, 1, 0.5, 10);\nif (hit != \"\") print(\"blocked by \" + hit);"}},
        {"box_cast3", {"Sweep a box (centre, half size) along a direction; the first thing it touches.",
            "string hit = box_cast3(x, y, z, 0.5, 1, 0.5, 0, 0, 1, 5);"}},
        {"box_cast", {"Sweep a flat box and return the name of the first thing it touches. Details are ray_hit, ray_dist, ray_x, ray_y, ray_nx, ray_ny.",
            "string hit = box_cast(0, 0, 0.5, 0.5, 1, 0, 10);\nif (hit != \"\") print(hit + \" at \" + ray_dist());"}},
        {"capsule_cast", {"Sweep a flat capsule (a line with round ends) and return the first thing it touches.",
            "string hit = capsule_cast(0, -0.5, 0, 0.5, 0.3, 1, 0, 10);"}},
        {"overlap_box", {"Names of the objects inside a flat box. The last number is a turn in degrees.",
            "var near = overlap_box(x, y, 1, 0.5, \"\", false, 30);"}},
        {"overlap_capsule", {"Names of the objects inside a flat capsule.",
            "var near = overlap_capsule(x, y - 1, x, y + 1, 0.4);"}},
        {"add_explosion_force", {"Shove this 2D body away from a point. The push fades to nothing at the radius. The last number pops it upward.",
            "add_explosion_force(0, 0, 20, 8, 0.4);"}},
        {"overlap_box3", {"Names of the objects inside a box (centre, half size). Optional layers / ignore triggers.",
            "var inside = overlap_box3(x, y, z, 2, 2, 2);\nprint(count(inside) + \" things nearby\");"}},
        {"overlap_sphere3", {"Names of the objects inside a ball. Great for explosions.",
            "var near = overlap_sphere3(x, y, z, 5);\nprint(count(near) + \" caught in the blast\");"}},
        {"contact_point", {"Inside a collision / trigger handler: where the touch happened (a vector). "
            "Also contact_normal, contact_x/_y/_z, impact_speed and other_tag.",
            "void OnCollisionEnter(string other)\n{\n    if (impact_speed > 8) spawn3(\"Sparks\", contact_x, contact_y, contact_z);\n}"}},
        // ---- C# / API
        {"Input.GetAxis", {"-1..1 from the keyboard (A/D, arrows) or a stick. \"Horizontal\" or \"Vertical\".",
            "float h = Input.GetAxis(\"Horizontal\");\nfloat v = Input.GetAxis(\"Vertical\");\ntransform.position += new Vector3(h, 0, v) * speed * Time.deltaTime;"}},
        {"Input.GetKeyDown", {"True only on the frame the key goes down.",
            "if (Input.GetKeyDown(KeyCode.Space))\n{\n    Jump();\n}"}},
        {"Input.GetKey", {"True every frame while the key is held.",
            "if (Input.GetKey(KeyCode.LeftShift))\n    speed = runSpeed;"}},
        {"Input.GetKeyUp", {"True only on the frame the key is released.",
            "if (Input.GetKeyUp(KeyCode.E))\n    Debug.Log(\"Released E\");"}},
        {"Input.GetMouseButtonDown", {"True on the frame a mouse button is pressed (0 left, 1 right, 2 middle).",
            "if (Input.GetMouseButtonDown(0))\n    Shoot();"}},
        {"Input.GetMouseButton", {"True while a mouse button is held (0 left, 1 right, 2 middle).",
            "if (Input.GetMouseButton(1))\n    Aim();"}},
        {"Input.GetButtonDown", {"True on the frame a named button (\"Jump\", \"Fire1\") is pressed.",
            "if (Input.GetButtonDown(\"Jump\"))\n    rb.AddForce(Vector3.up * jumpForce);"}},
        {"Input.mousePosition", {"The mouse position in screen pixels.",
            "Vector3 m = Input.mousePosition;\nDebug.Log($\"Mouse at {m.x}, {m.y}\");"}},
        {"Time.deltaTime", {"Seconds since the last frame. Multiply speeds by it so movement is frame-rate independent.",
            "transform.Rotate(0, 90 * Time.deltaTime, 0);"}},
        {"Time.time", {"Seconds since the game started.",
            "float y = Mathf.Sin(Time.time * 2f) * 0.5f;"}},
        {"Time.timeScale", {"Game speed: 1 = normal, 0.5 = slow motion, 0 = paused.",
            "Time.timeScale = paused ? 0f : 1f;"}},
        {"Time.fixedDeltaTime", {"Seconds between FixedUpdate calls (0.02 = 50 a second).",
            "rb.AddForce(Vector3.up * lift * Time.fixedDeltaTime);"}},
        {"Time.unscaledDeltaTime", {"Real seconds since the last frame: not slowed by timeScale, a hit stop or a pause. Animate a pause menu with it.",
            "panel.alpha += 4f * Time.unscaledDeltaTime;"}},
        {"Time.unscaledTime", {"Real seconds since the game started: a pause, slow motion and hit stops do not change it.",
            "float blink = Mathf.PingPong(Time.unscaledTime, 1f);"}},
        {"Time.sessionTime", {"Game seconds since this Play began. Time.time keeps counting across Plays; this starts from 0 each time.",
            "if (Time.sessionTime > 30f)\n    SpawnBoss();"}},
        {"Time.sessionUnscaledTime", {"Real seconds since this Play began: a pause, slow motion and hit stops do not change it.",
            "Debug.Log($\"Playing for {Time.sessionUnscaledTime}s\");"}},
        {"Time.sessionFrameCount", {"Frames since this Play began.",
            "if (Time.sessionFrameCount == 1)\n    Debug.Log(\"first frame\");"}},
        {"Time.fps", {"Frames per second over the last half second of real frame times (0 before the first frame). A stall shows.",
            "if (Time.fps < 30f)\n    LowerQuality();"}},
        {"Time.hitchCount", {"How many frames took over a tenth of a second since this Play began.",
            "Debug.Log($\"Hitches so far: {Time.hitchCount}\");"}},
        {"Time.paused", {"True while the game is paused (a menu, a cutscene or the pause button holds it).",
            "if (Time.paused)\n    return;"}},
        {"Time.HitStop", {"A freeze frame or a brief slow-down for an impact: for some real seconds the game runs at this scale (0 = frozen), then goes back by itself.",
            "Time.HitStop(0.08f, 0f);   // a heavy hit"}},
        {"Time.SlowMotion", {"Bullet time: run at this speed (0.2 = a fifth) for some real seconds, then back to normal. Composes with Time.timeScale.",
            "Time.SlowMotion(0.2f, 2f);"}},
        {"Time.RampTimeScale", {"Ease Time.timeScale to a new value over some real seconds, in a straight line.",
            "Time.RampTimeScale(1f, 0.5f);   // ease back to normal"}},
        {"Time.PushPause", {"Hold the game paused for a reason of your own (\"dialogue\", \"shop\"). It stays paused until every reason is let go, so two scripts that pause never resume each other.",
            "Time.PushPause(\"shop\");"}},
        {"Time.PopPause", {"Let go of one hold of that reason. False when nothing held it.",
            "Time.PopPause(\"shop\");"}},
        {"Time.IsPausedBy", {"True while that reason is holding the pause.",
            "if (Time.IsPausedBy(\"shop\"))\n    ShowShopUI();"}},
        {"Debug.Log", {"Print a message to the Console.",
            "Debug.Log(\"Hello\");\nDebug.Log($\"Score: {score}\");"}},
        {"Debug.LogWarning", {"Print a yellow warning to the Console.",
            "if (health < 10)\n    Debug.LogWarning(\"Low health!\");"}},
        {"Debug.LogError", {"Print a red error to the Console.",
            "if (target == null)\n    Debug.LogError(\"No target set\");"}},
        {"Mathf.Lerp", {"Blend from a to b by t (0..1).",
            "speed = Mathf.Lerp(speed, maxSpeed, 5f * Time.deltaTime);"}},
        {"Mathf.Clamp", {"Keep a value between min and max.",
            "health = Mathf.Clamp(health - damage, 0, maxHealth);"}},
        {"Mathf.Clamp01", {"Keep a value between 0 and 1.",
            "float t = Mathf.Clamp01(elapsed / duration);"}},
        {"Mathf.Abs", {"The value without its sign.",
            "if (Mathf.Abs(h) > 0.1f)\n    walking = true;"}},
        {"Mathf.Sin", {"Sine of an angle in radians: a smooth -1..1 wave.",
            "float bob = Mathf.Sin(Time.time * 3f) * 0.25f;"}},
        {"Mathf.MoveTowards", {"Step from current toward target by at most maxDelta (never overshoots).",
            "x = Mathf.MoveTowards(x, targetX, 4f * Time.deltaTime);"}},
        {"Mathf.SmoothStep", {"Like Lerp but eases in and out.",
            "float a = Mathf.SmoothStep(0f, 1f, t);"}},
        {"Mathf.PingPong", {"Goes 0 -> length -> 0 over and over.",
            "float x = Mathf.PingPong(Time.time, 3f);"}},
        {"Mathf.Max", {"The larger of the values.",
            "int best = Mathf.Max(best, score);"}},
        {"Mathf.Min", {"The smaller of the values.",
            "ammo = Mathf.Min(ammo + 10, maxAmmo);"}},
        {"Random.Range", {"Random number in [min, max). Ints exclude max; floats can include it.",
            "int roll = Random.Range(1, 7);          // 1..6\nfloat x = Random.Range(-5f, 5f);"}},
        {"Random.value", {"Random float from 0 to 1.",
            "if (Random.value < 0.25f)\n    DropLoot();"}},
        {"Vector3.Distance", {"Distance between two points.",
            "if (Vector3.Distance(transform.position, player.position) < 3f)\n    Attack();"}},
        {"Vector3.Lerp", {"Blend between two points by t (0..1).",
            "transform.position = Vector3.Lerp(transform.position, target, 5f * Time.deltaTime);"}},
        {"Vector3.up", {"Shorthand for (0, 1, 0).",
            "transform.position += Vector3.up * 2f * Time.deltaTime;"}},
        {"Vector3.zero", {"Shorthand for (0, 0, 0).",
            "rb.velocity = Vector3.zero;"}},
        {"Quaternion.Euler", {"A rotation from angles in degrees (x, y, z).",
            "transform.rotation = Quaternion.Euler(0, 90, 0);"}},
        {"SceneManager.LoadScene", {"Load another scene by name.",
            "SceneManager.LoadScene(\"Level2\");"}},
        {"Translate", {"Move by an offset (local space by default).",
            "transform.Translate(Vector3.forward * speed * Time.deltaTime);"}},
        {"Rotate", {"Turn by angles in degrees.",
            "transform.Rotate(0, 90 * Time.deltaTime, 0);"}},
        {"LookAt", {"Turn to face a target.",
            "transform.LookAt(player);"}},
        {"position", {"World position.",
            "transform.position = new Vector3(0, 1, 0);"}},
        {"localScale", {"Scale relative to the parent.",
            "transform.localScale = Vector3.one * 2f;"}},
        {"eulerAngles", {"Rotation as angles in degrees.",
            "Vector3 r = transform.eulerAngles;\ntransform.eulerAngles = new Vector3(0, r.y + 45, 0);"}},
        {"SetActive", {"Turn the object on or off.",
            "door.SetActive(false);"}},
        {"CompareTag", {"True when the object has that tag (faster than tag == ...).",
            "void OnCollisionEnter(Collision other)\n{\n    if (other.gameObject.CompareTag(\"Enemy\"))\n        TakeDamage(10);\n}"}},
        {"GetComponent", {"Find a component on this object.",
            "Rigidbody rb = GetComponent<Rigidbody>();\nrb.AddForce(Vector3.up * 5f);"}},
        {"AddForce", {"Push a Rigidbody.",
            "rb.AddForce(Vector3.up * jumpForce);"}},
        {"MovePosition", {"Move a Rigidbody, respecting collisions.",
            "rb.MovePosition(rb.position + dir * speed * Time.deltaTime);"}},
        {"velocity", {"Current speed and direction of a Rigidbody.",
            "rb.velocity = new Vector2(h * speed, rb.velocity.y);"}},
        {"SetTrigger", {"Fire an Animator trigger parameter.",
            "animator.SetTrigger(\"Jump\");"}},
        {"SetBool", {"Set an Animator bool parameter.",
            "animator.SetBool(\"Running\", Mathf.Abs(h) > 0.1f);"}},
        {"SetFloat", {"Set an Animator float parameter.",
            "animator.SetFloat(\"Speed\", rb.velocity.magnitude);"}},
        {"Play", {"Start playing (a sound, or an animation state).",
            "audioSource.Play();"}},
        {"Add", {"Add an item to the end of a list.",
            "List<string> names = new List<string>();\nnames.Add(\"Ada\");"}},
        {"Remove", {"Remove the first matching item.",
            "enemies.Remove(enemy);"}},
        {"RemoveAt", {"Remove the item at an index.",
            "items.RemoveAt(0);"}},
        {"Contains", {"True when the collection (or string) has the item.",
            "if (inventory.Contains(\"Key\"))\n    OpenDoor();"}},
        {"ContainsKey", {"True when the dictionary has the key.",
            "if (!scores.ContainsKey(name))\n    scores[name] = 0;"}},
        {"Count", {"How many items are in the collection.",
            "for (int i = 0; i < items.Count; i++)\n    Debug.Log(items[i]);"}},
        {"Length", {"How many characters (string) or items (array).",
            "if (playerName.Length > 12)\n    playerName = playerName.Substring(0, 12);"}},
        {"Clear", {"Remove everything.",
            "bullets.Clear();"}},
        {"Split", {"Cut a string into parts.",
            "string[] parts = \"a,b,c\".Split(',');"}},
        {"ToUpper", {"An upper-case copy of the string.",
            "string shout = message.ToUpper();"}},
        {"StartCoroutine", {"Run an IEnumerator over several frames.",
            "StartCoroutine(Blink());\n\nIEnumerator Blink()\n{\n    yield return new WaitForSeconds(0.5f);\n}"}},
        {"Instantiate", {"Spawn a copy of a prefab or object.",
            "GameObject b = Instantiate(bulletPrefab, transform.position, transform.rotation);"}},
        {"Destroy", {"Remove an object (optionally after a delay).",
            "Destroy(gameObject, 2f);   // gone in 2 seconds"}},
        {"WaitForSeconds", {"Pause a coroutine.",
            "yield return new WaitForSeconds(1f);"}},
        {"Vector3", {"A 3D point or direction (x, y, z).",
            "Vector3 dir = new Vector3(1, 0, 0);\ntransform.position += dir * speed * Time.deltaTime;"}},
        {"Vector2", {"A 2D point or direction (x, y).",
            "Vector2 move = new Vector2(h, v).normalized;"}},
        {"List", {"A resizable list.",
            "List<int> scores = new List<int>();\nscores.Add(10);\nforeach (int s in scores)\n    Debug.Log(s);"}},
        {"Dictionary", {"Key -> value lookup.",
            "Dictionary<string, int> stock = new Dictionary<string, int>();\nstock[\"apples\"] = 3;"}},
        {"Color", {"An RGBA colour (0..1 each).",
            "GetComponent<SpriteRenderer>().color = Color.red;"}},
        // ---- keywords
        {"if", {"Run code only when a condition is true.",
            "if (health <= 0)\n{\n    Die();\n}\nelse if (health < 20)\n{\n    Debug.Log(\"Low!\");\n}"}},
        {"for", {"Loop a set number of times.",
            "for (int i = 0; i < 5; i++)\n{\n    Debug.Log(i);\n}"}},
        {"foreach", {"Loop over every item in a collection.",
            "foreach (var enemy in enemies)\n{\n    enemy.TakeDamage(1);\n}"}},
        {"while", {"Loop while a condition stays true.",
            "while (ammo > 0)\n{\n    Fire();\n    ammo--;\n}"}},
        {"switch", {"Pick a branch by value.",
            "switch (state)\n{\n    case 0: Idle(); break;\n    case 1: Run(); break;\n    default: break;\n}"}},
        {"class", {"Not needed: a script's fields and functions are already the component, so a class wrapper does nothing.",
            "public int value = 1;\n\nvoid Start()\n{\n    Debug.Log(value);\n}"}},
        {"const", {"A value that never changes. Assigning to it again is an error.",
            "const float GRAVITY = 9.8;"}},
        {"static", {"One value shared by every object running this script.",
            "static int alive = 0;\n\nvoid Start() { alive++; }"}},
        {"enum", {"A named set of values.",
            "enum State { Idle, Walk, Jump }\nState state = State.Idle;"}},
        {"return", {"Leave the function (with a value, if it returns one).",
            "int Double(int x)\n{\n    return x * 2;\n}"}},
        {"new", {"Create an object.",
            "Vector3 p = new Vector3(0, 1, 0);"}},
        // ---- OkayScript one-liners
        {"spin", {nullptr, "void Update()\n{\n    spin(90);   // 90 degrees per second\n}"}},
        {"walk", {nullptr, "walk(1, 0, 3);   // right at 3 units/second"}},
        {"on_key_move", {nullptr, "void Update()\n{\n    on_key_move(5);\n}"}},
        {"platformer", {nullptr, "void Update()\n{\n    platformer(6, 8);   // speed, jump\n}"}},
        {"follow", {nullptr, "follow(\"Player\", 3, 1.5);"}},
        {"timer", {nullptr, "if (timer(\"spawn\", 2))\n    spawn(\"Enemy\", 0, 5);"}},
        {"cooldown", {nullptr, "if (key_down(\"space\") && cooldown(\"shoot\", 0.3))\n    shoot();"}},
        {"once", {nullptr, "if (once(\"intro\"))\n    print(\"Welcome!\");"}},
        {"print", {"Print to the Console.", "print(\"Score: \" + score);"}},
        {"camera_shake", {nullptr, "camera_shake(0.6);   // a hit"}},
        {"camera_zoom_to", {nullptr, "camera_zoom_to(2, 0.5);   // 2x over half a second"}},
    };
    auto it = H.find(key);
    return it == H.end() ? nullptr : &it->second;
}

// An example call built from a signature such as "walk(dx, dy, speed [, x])":
// optional parts dropped, each parameter given a plausible value by its name.
std::string ScriptExampleFromSignature(const std::string& sig) {
    const auto lp = sig.find('('), rp = sig.rfind(')');
    if (lp == std::string::npos || rp == std::string::npos || rp < lp || sig.find('|') != std::string::npos) return "";
    std::string inner = sig.substr(lp + 1, rp - lp - 1);
    if (auto ob = inner.find('['); ob != std::string::npos) inner = inner.substr(0, ob);
    std::vector<std::string> args;
    std::string cur;
    auto flush = [&] {
        std::string a = cur; cur.clear();
        while (!a.empty() && (a.back() == ' ' || a.back() == ',')) a.pop_back();
        std::size_t s = a.find_first_not_of(' '); if (s == std::string::npos) return;
        a = a.substr(s);
        if (a == "...") return;
        std::string v;
        auto has = [&](const char* w) { return a.find(w) != std::string::npos; };
        if (a[0] == '"') v = a;                                            // "name" stays a string
        else if (has("speed")) v = "5";
        else if (has("deg") || has("angle")) v = "90";
        else if (has("dur") || has("time") || has("sec")) v = "0.5";
        else if (has("count") || a == "n") v = "3";
        else if (has("radius") || has("dist")) v = "2";
        else if (has("amount") || has("intensity") || has("strength")) v = "0.5";
        else if (has("key")) v = "\"space\"";
        else if (has("name") || has("target") || has("tag") || has("prefab")) v = "\"Player\"";
        else if (has("fn")) v = "\"OnHit\"";
        else if (a == "r") v = "1"; else if (a == "g" || a == "b") v = "0";
        else if (has("text") || has("msg") || has("str")) v = "\"Hello\"";
        else if (a == "dx" || a == "x" || a == "x1") v = "1";
        else if (a == "dy" || a == "y" || a == "y1" || a == "z" || a == "dz") v = "0";
        else v = "1";
        args.push_back(v);
    };
    for (char c : inner) { if (c == ',') flush(); else cur += c; }
    flush();
    std::string out = sig.substr(0, lp) + "(";
    for (std::size_t i = 0; i < args.size(); ++i) out += (i ? ", " : "") + args[i];
    return out + ");";
}

// Colour a code snippet with the editor's palette (keywords, types, strings,
// numbers, calls, comments) using the code font.
void DrawCodeSnippet(const std::string& code) {
    static const std::unordered_set<std::string> kw = {
        "if","else","for","foreach","in","while","do","return","break","continue","switch","case","default",
        "new","public","private","static","void","int","float","bool","string","var",
        "enum","true","false","null","this","yield","const","readonly"};
    static const std::unordered_set<std::string> types = {
        "Vector3","Vector2","Quaternion","Color","Mathf","Time","Input","KeyCode","Debug","Random",
        "List","Dictionary","GameObject","Transform","Rigidbody","Rigidbody2D","SpriteRenderer",
        "Animator","AudioSource","Collision","Collision2D","IEnumerator","WaitForSeconds",
        "SceneManager","State","Coin"};
    const ImU32 cDef = IM_COL32(169, 183, 198, 255), cKw = IM_COL32(204, 120, 50, 255),
                cTy = IM_COL32(78, 201, 176, 255), cStr = IM_COL32(106, 135, 89, 255),
                cNum = IM_COL32(104, 151, 187, 255), cFn = IM_COL32(255, 198, 109, 255),
                cCm = IM_COL32(128, 138, 128, 255), cMem = IM_COL32(176, 154, 196, 255);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (g_codeFont) ImGui::PushFont(g_codeFont, ImGui::GetFontSize());
    const float lh = ImGui::GetTextLineHeight();
    std::size_t ls = 0;
    while (ls <= code.size()) {
        std::size_t le = code.find('\n', ls); if (le == std::string::npos) le = code.size();
        const std::string line = code.substr(ls, le - ls);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float x = pos.x;
        auto put = [&](std::size_t a, std::size_t b, ImU32 col) {
            if (b <= a) return;
            const char* s = line.c_str() + a; const char* e = line.c_str() + b;
            dl->AddText(ImVec2(x, pos.y), col, s, e);
            x += ImGui::CalcTextSize(s, e).x;
        };
        std::size_t i = 0;
        while (i < line.size()) {
            const char c = line[i];
            if (c == '/' && i + 1 < line.size() && line[i + 1] == '/') { put(i, line.size(), cCm); break; }
            if (c == '"' || ((c == '$' || c == '@') && i + 1 < line.size() && line[i + 1] == '"')) {
                std::size_t j = i + (c == '"' ? 1 : 2);
                while (j < line.size() && line[j] != '"') { if (line[j] == '\\') ++j; ++j; }
                put(i, std::min(j + 1, line.size()), cStr); i = j + 1; continue;
            }
            if (c == '\'') { std::size_t j = line.find('\'', i + 1); j = j == std::string::npos ? line.size() : j + 1; put(i, j, cStr); i = j; continue; }
            if (std::isdigit((unsigned char)c)) {
                std::size_t j = i; while (j < line.size() && (std::isalnum((unsigned char)line[j]) || line[j] == '.')) ++j;
                put(i, j, cNum); i = j; continue;
            }
            if (std::isalpha((unsigned char)c) || c == '_') {
                std::size_t j = i; while (j < line.size() && (std::isalnum((unsigned char)line[j]) || line[j] == '_')) ++j;
                const std::string w = line.substr(i, j - i);
                std::size_t k = j; while (k < line.size() && line[k] == ' ') ++k;
                ImU32 col = cDef;
                if (kw.count(w)) col = cKw;
                else if (types.count(w)) col = cTy;
                else if (k < line.size() && (line[k] == '(' || line[k] == '<') && !(i > 0 && line[i - 1] == '.' && line[k] == '<')) col = cFn;
                else if (i > 0 && line[i - 1] == '.') col = cMem;
                put(i, j, col); i = j; continue;
            }
            put(i, i + 1, cDef); ++i;
        }
        ImGui::Dummy(ImVec2(x - pos.x, lh));
        if (le == code.size()) break;
        ls = le + 1;
    }
    if (g_codeFont) ImGui::PopFont();
}

// ---- The code widget's palette and language ------------------------------------
// A Darcula-flavoured palette matching the old hand-drawn highlighter (ABGR, so
// 0xAABBGGRR), in scripteditor::PaletteIndex order. Known identifiers (our builtins)
// are purple; calls are yellow.
const scripteditor::ScriptPalette& OkayScriptPalette() {
    static const scripteditor::ScriptPalette p = { {
        0xffc6b7a9,  // Default            (169,183,198) soft gray
        0xff3278cc,  // Keyword            (204,120,50)  orange
        0xffbb9768,  // Number             (104,151,187) blue
        0xff59876a,  // String             (106,135,89)  green
        0xff59876a,  // CharLiteral        green
        0xffc8c8c8,  // Punctuation        light gray
        0xff808080,  // Preprocessor
        0xffc6b7a9,  // Identifier         default text
        0xffaa7698,  // KnownIdentifier    (152,118,170) purple — builtin properties (transform, deltaTime)
        0xffaa7698,  // PreprocIdentifier  purple
        0xff808a80,  // Comment            (128,138,128) gray
        0xff808a80,  // MultiLineComment   gray
        0xff241f1c,  // Background         (28,31,36) calm blue-grey
        0xffe8e6e2,  // Cursor
        0x8c915c3a,  // Selection          Rider blue, translucent
        0x80302820,  // ErrorMarker        soft red wash
        0x40eca25c,  // Breakpoint         bookmark blue (92,162,236)
        0xffc4bbb0,  // LineNumber         readable
        0x3a403028,  // CurrentLineFill
        0x22403028,  // CurrentLineFillInactive
        0x55ffffff,  // CurrentLineEdge
        0xffb0c94e,  // TypeName           (78,201,176)  teal — Vector3, List, your classes
        0xff6dc6ff,  // Function           (255,198,109) yellow — calls + method declarations
        0xffc49ab0,  // Member             (176,154,196) light purple — .position, .x
    } };
    return p;
}

// OkayScript's words: our keywords, the built-in types (teal, "X (built-in type)" on
// hover), and every VM builtin as a known name (its signature and doc as the hover
// tooltip). "//" and "#" line comments, "/* */" block comments. Built once.
const scripteditor::LanguageWords& OkayScriptLanguageWords() {
    static bool inited = false;
    static scripteditor::LanguageWords d;
    if (!inited) {
        static const char* kw[] = {
            "if","else","for","while","do","return","break","continue","switch","case","default",
            "var","let","const","function","func","def","class","struct","enum","interface","new",
            "public","private","protected","internal","static","readonly","override","virtual","abstract",
            "sealed","ref","out","is","as","typeof","base","get","set","yield","using","namespace",
            "void","int","float","double","bool","string","char","long","byte","uint","object",
            "true","false","null","this","foreach","in","and","or","not","try","catch","finally","throw",
            "short","ushort","ulong","sbyte","decimal","async","await","params","delegate","event",
            "operator","nameof","when","where","partial",
        };
        for (auto* k : kw) d.keywords.push_back(k);
        // Type names OkayScript understands: teal, with a hover tooltip. Classes /
        // enums declared in the script are added per editor (SetUserTypes).
        static const char* kTypes[] = {
            "GameObject","Transform","Vector3","Vector2","Quaternion","Color",
            "Mathf","Time","Input","KeyCode","Debug","Random","Space","Screen","List","Dictionary",
            "KeyValuePair","HashSet","Queue","Stack","IEnumerator","Coroutine","WaitForSeconds",
            "WaitForEndOfFrame","Rigidbody","Rigidbody2D","SpriteRenderer","MeshRenderer","Renderer",
            "Animator","AudioSource","Collider","Collider2D","Collision","Collision2D","ForceMode2D",
            "SerializeField","Header","Range","Tooltip","Action","Func","String","Text",
            "SortedDictionary","IEnumerable",
        };
        for (auto* t : kTypes) {
            d.types.push_back(t);
            d.known.push_back(t);
            d.docs.emplace(t, std::string(t) + " (built-in type)");
        }
        // Every completion name (curated API + all VM builtins) is a known name; its
        // signature + doc is what hovering it shows.
        for (const auto& name : ScriptCompletions()) {
            std::string decl;
            if (const std::string* sg = ScriptSignature(name)) decl = *sg;
            if (const std::string* dc = ScriptDoc(name)) decl += (decl.empty() ? "" : "\n") + *dc;
            if (decl.empty()) decl = "OkayScript builtin";
            d.known.push_back(name);
            d.docs.emplace(name, decl);   // a type keeps its own line (emplace does not overwrite)
        }
        d.lineComments = {"//", "#"};     // '#' comments; a "#" inside a string stays part of the string
        d.blockStart = "/*";
        d.blockEnd = "*/";
        inited = true;
    }
    return d;
}

// Lua scripts (language "lua", our own interpreter okay::lua): the Lua keywords, '--' and '--[[ ]]' comments,
// and every bound engine type and member (LuaScriptVM::CompletionNames) with its hover doc (CompletionDoc).
const scripteditor::LanguageWords& LuaLanguageWords() {
    static bool inited = false;
    static scripteditor::LanguageWords d;
    if (!inited) {
        for (const char* k : okay::lua::kKeywords) d.keywords.push_back(k);
        d.keywords.push_back("self");
        for (const char* t : {"GameObject", "Transform", "Vector3", "Input", "Time", "Debug", "Mathf", "Rigidbody"})
            d.types.push_back(t);
        for (const auto& name : okay::LuaScriptVM::CompletionNames()) {
            if (std::find(d.keywords.begin(), d.keywords.end(), name) != d.keywords.end()) continue;
            std::string doc = okay::LuaScriptVM::CompletionDoc(name);
            d.known.push_back(name);
            d.docs.emplace(name, doc.empty() ? std::string("Lua name") : doc);
        }
        d.lineComments = {"--"};
        d.blockStart = "--[[";
        d.blockEnd = "]]";
        inited = true;
    }
    return d;
}

} // namespace oked
