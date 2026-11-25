#pragma once
// GameObject > UI as plain data (no ImGui). Owner request (2026-11-17, "GameObject > UI: clean that up, make it more simple so
// the user understands"): eight short groups with plain names, the common items first, one sentence for every item, and a last
// entry "Browse All UI..." that opens the UI Gallery (searchable list of every component, style, theme, icon and template).
// The editor (main.cpp, DrawUIWidgetItems) draws these entries and runs the ids with RunUIMenuItem; tests/test_menu_bar_model.cpp
// checks that nothing is listed twice, that every item has a tip, and that every item of the old menu still has a home.
// A Canvas is made on its own the first time a widget is added (UIAddParent / EnsureUIRoot), so nobody has to know about it.
#include "MenuBarModel.hpp"
#include <string>
#include <vector>

namespace oked {
namespace uimenu {

using menubar::Entry;
using menubar::Item;
using menubar::Sub;
using menubar::Sep;

inline Entry I(const char* id, const char* label, const char* tip) { return Item(id, label, "", tip); }

/// One section of ready-made screens (Menus, In-Game HUD, ...) as the screen registry lists them (engine/include/okay/Scene/UIScreens.hpp).
struct ScreenItem { std::string id, name, desc; };
struct ScreenGroup { std::string name, tip; std::vector<ScreenItem> items; };

/// The groups, in the order they are shown. The last entry is the Gallery. `screens` are the registry's sections: the host passes them in
/// (so this file needs no engine), and every screen in the registry gets exactly one item `ui.scr.<id>`.
inline std::vector<Entry> UIMenuFull(const std::vector<ScreenGroup>& screens) {
    std::vector<Entry> m;
    m.push_back(Sub("ui.g.basics", "Basics", {
        I("ui.text", "Text", "Words on the screen: a title, a score, a hint."),
        I("ui.button", "Button", "Something the player clicks or taps. Pick what it does in the Inspector."),
        I("ui.image", "Image", "A picture on the screen: a logo, an icon, a portrait."),
        I("ui.panel", "Panel", "A coloured box. Put other things on top of it, like a card or a window background."),
        I("ui.rawimage", "Tiled or Scrolling Picture", "A picture you can repeat or slide, for moving backgrounds."),
        I("ui.divider", "Divider", "A thin line that separates two parts of a screen."),
    }, "The simplest pieces. Start here."));
    m.push_back(Sub("ui.g.input", "Input", {
        I("ui.inputfield", "Text Field", "A box the player types into: a name, a code, a search."),
        I("ui.validatedinput", "Text Field with Checks", "A text box that checks what was typed (email, number, length) and shows a message."),
        I("ui.slider", "Slider", "Drag a handle to pick a number, like volume."),
        I("ui.rangeslider", "Range Slider", "A slider with two handles, to pick from-to, like a price range."),
        I("ui.toggle", "Toggle", "A tick box: on or off."),
        I("ui.switch", "Switch", "An on/off switch, like in a phone's settings."),
        I("ui.radio", "Radio Buttons", "A short list where only one choice can be on."),
        I("ui.dropdown", "Dropdown", "A button that opens a list of choices."),
        I("ui.stepper", "Number Stepper", "A number with plus and minus buttons."),
        I("ui.rating", "Star Rating", "Stars the player clicks to rate something."),
        I("ui.colorpicker", "Color Picker", "Pick a colour. It has an Advanced switch for a full colour square."),
        I("ui.keybind", "Key Binding Field", "Lets the player press a key to rebind a control."),
        I("ui.searchbar", "Search Box", "A text box with a search icon and a clear button."),
        I("ui.datetime", "Date and Time", "Pick a date and a time."),
        I("ui.filepicker", "File Picker", "Browse and choose a file."),
        I("ui.formgroup", "Form", "Several fields with one Submit button that checks all of them."),
    }, "Things the player clicks, drags or types into."));
    m.push_back(Sub("ui.g.layout", "Layout", {
        I("ui.layoutgroup", "Row, Column or Grid", "Lines up its children in a row, a column or a grid, so you do not place each one by hand."),
        I("ui.scrollview", "Scroll Area", "A box that scrolls when its content is bigger than it."),
        I("ui.tabs", "Tabs", "A row of tabs that switch between pages."),
        I("ui.accordion", "Accordion", "Sections that open and close, one title per section."),
        I("ui.flexbox", "Flexible Box", "A row or column that shares space between its children by weight."),
        I("ui.aspectbox", "Fixed Shape Box (16:9)", "A box that keeps its width-to-height shape on any screen."),
        I("ui.mask", "Cropped Panel", "A panel that hides whatever sticks out of it."),
        I("ui.safearea", "Safe Area", "Keeps things clear of phone notches and home bars."),
        I("ui.draggable", "Draggable Item", "An image the player can pick up and move."),
        I("ui.droptarget", "Drop Target", "A box that accepts a dragged item."),
        I("ui.canvas", "Canvas (advanced)", "The surface all UI sits on. It is made for you; add another only for a second screen layer."),
        I("ui.eventsystem", "Event System (advanced)", "Sends clicks to the UI. It is made for you; you rarely add it by hand."),
    }, "Place and arrange other pieces."));
    m.push_back(Sub("ui.g.game", "Game", {
        I("ui.progressbar", "Health Bar / Progress Bar", "A bar that fills: health, stamina, loading."),
        I("ui.hotbar", "Hotbar / Slot Grid", "A grid of item slots: a hotbar or a bag."),
        I("ui.minimap", "Minimap", "A small map in the corner showing where things are."),
        I("ui.crosshair", "Crosshair", "The aiming mark in the middle of the screen."),
        I("ui.ammo", "Ammo Counter", "Bullets in the magazine and in reserve, with a reload bar."),
        I("ui.compass", "Compass", "A strip showing which way you face, with markers."),
        I("ui.objective", "Objective Tracker", "The quest or goal list at the side of the screen."),
        I("ui.prompt", "Interaction Prompt", "A hint such as \"Press E to open\" with a hold ring."),
        I("ui.killfeed", "Kill Feed", "A short list of the latest events, like who beat whom."),
        I("ui.damage", "Damage Numbers", "Numbers that float up when something is hit or healed."),
        I("ui.nameplate", "Name and Health Plate", "A name, level and health bar. It follows a character's health."),
        I("ui.nameplateworld", "Name Plate over a Character", "The same plate as a 3D label above a character's head."),
        I("ui.worldlabel", "World Label (3D)", "Text that floats in the 3D world."),
        I("ui.abilityicon", "Ability Icon", "A skill icon with a cooldown sweep and a key label."),
        I("ui.itemcard", "Item Card", "A card with an item's name, rarity and stats."),
        I("ui.radialmenu", "Radial Menu", "A wheel of choices around the middle of the screen."),
        I("ui.radialprogress", "Round Progress", "A circle that fills, for timers and cooldowns."),
        I("ui.inventory.mc", "Inventory (hotbar + backpack)", "A ready hotbar and backpack with sample items. Keys 1-9 and the wheel pick a slot, E opens it. The items go on the selected or \"Player\"-tagged object."),
        I("ui.inventory.grid", "Grid Inventory", "A bag where items take up several cells; press I in Play to open it and drag items around. Comes with sample loot."),
        I("ui.inventory.unturned", "Survival Inventory (worn bags + loot)", "A player with a worn vest and backpack and a nearby loot crate. Press I in Play and drag loot across bags."),
        Sub("ui.g.world3d", "3D UI (in the world)", {
            I("ui.w3d.button", "3D Button", "A button that floats in the 3D world."),
            I("ui.w3d.text", "3D Text", "Text that floats in the 3D world."),
            I("ui.w3d.panel", "3D Panel", "A panel that floats in the 3D world."),
            I("ui.w3d.image", "3D Image", "A picture that floats in the 3D world."),
        }, "UI placed in the world, not on the screen."),
        Sub("ui.g.touch", "Touch Controls", {
            I("ui.joystick", "Virtual Joystick", "A thumb stick on a touch screen."),
            I("ui.dpad", "D-Pad", "Four arrow buttons for a touch screen."),
            I("ui.touchbutton", "Touch Button", "A big round button for a touch screen."),
        }, "On-screen controls for phones and tablets."),
    }, "Pieces you see while playing."));
    {
        std::vector<Entry> sc;
        sc.push_back(I("ui.starter", "Starter Pack (a whole game UI)", "Builds a main menu, settings, game HUD, pause menu and game over screen already joined together: Play opens the game, Pause opens the pause menu, and so on."));
        sc.push_back(I("ui.looks", "Choose a Look and Style for new screens...", "Opens the UI Gallery, where you pick the look and style that screens added from this menu get. Each screen keeps its own; there is no global theme."));
        sc.push_back(Sep());
        for (const ScreenGroup& g : screens) {
            std::vector<Entry> items;
            for (const ScreenItem& it : g.items) items.push_back(I(("ui.scr." + it.id).c_str(), it.name.c_str(), it.desc.c_str()));
            if (g.name == "Loading") {
                items.push_back(Sep());
                items.push_back(I("ui.o.0", "Classic overlay", "Covers the game when the scene starts and shows its own bar. Needs no editing."));
                items.push_back(I("ui.o.1", "Minimal overlay", "A thin automatic loading bar."));
                items.push_back(I("ui.o.2", "Cinematic overlay", "An automatic loading screen with black bars."));
                items.push_back(I("ui.o.3", "Splash overlay", "An automatic loading screen with a logo."));
                items.push_back(I("ui.o.4", "Card overlay", "An automatic loading screen on a card."));
                items.push_back(I("ui.o.5", "Corner overlay", "A small automatic loading mark in a corner."));
                items.push_back(I("ui.o.6", "Ring overlay", "An automatic round loading ring."));
            }
            sc.push_back(Sub(("ui.g.sec." + g.name).c_str(), g.name.c_str(), std::move(items), g.tip.c_str()));
        }
        m.push_back(Sub("ui.g.screens", "Menus & Screens", std::move(sc), "Whole screens, ready to use and joined by buttons that already work. Add one, then change it."));
    }
    m.push_back(Sub("ui.g.data", "Data & Lists", {
        I("ui.listview", "List", "A scrolling list of rows."),
        I("ui.datatable", "Table", "Rows and columns. Click a header to sort."),
        I("ui.treeview", "Tree", "A list with folders that open and close."),
        I("ui.boundlist", "List From an Array", "Fills a list from a script array, for scores or items."),
        I("ui.boundslider", "Slider Linked to a Variable", "A slider that reads and writes a game variable."),
        I("ui.visibleif", "Show Only If...", "Shows its children only while a condition is true."),
        I("ui.hotreload", "Hot Reload Watcher", "Re-reads a UI text file whenever you save it."),
        I("ui.pagination", "Page Numbers", "Previous, Next and page numbers for long lists."),
        I("ui.breadcrumbs", "Breadcrumbs", "A path such as Home > Shop > Swords."),
        I("ui.skeleton", "Loading Placeholder", "Grey shapes that stand in while data loads."),
        I("ui.menubar", "Menu Bar", "A row of drop-down menus like File and Edit."),
        I("ui.toolbar", "Toolbar", "A row of icon buttons."),
        I("ui.document", "UI From a Text File", "Builds UI from a few lines of text you can edit, like panel, text and button lines."),
    }, "Show lists, tables and game data."));
    m.push_back(Sub("ui.g.popups", "Windows & Pop-ups", {
        I("ui.dialog", "Dialog", "A pop-up box that asks something and blocks what is behind it."),
        I("ui.toast", "Toast Messages", "Small messages that slide in and fade away."),
        I("ui.notice", "Notification", "A message card at the top right."),
        I("ui.tooltip", "Tooltip", "A small hint that shows when the mouse rests on something."),
        I("ui.contextmenu", "Right-click Menu", "A list that opens on a right click."),
        I("ui.window", "Window", "A panel with a title bar you can drag, and a close button."),
        I("ui.wizard", "Wizard (steps)", "A set-up in steps with Back and Next."),
        I("ui.bottomsheet", "Bottom Sheet", "A panel that slides up from the bottom (phones)."),
        I("ui.spinner", "Spinner", "A turning circle that says \"busy\"."),
    }, "Boxes that appear on top of the game."));
    m.push_back(Sub("ui.g.pieces", "Ready-made Pieces", {
        I("ui.card", "Card", "A raised card with a caption, title, text and a button."),
        I("ui.heading", "Heading", "A small caption over a big title."),
        I("ui.listrow", "List Row", "A picture, a name and a status badge on one line."),
        I("ui.badge", "Badge", "A small tag such as \"New\" or \"3 / 4\"."),
        I("ui.avatar", "Avatar", "A coloured circle with an initial."),
        I("ui.btn.primary", "Main Button", "The coloured button for the main action."),
        I("ui.btn.secondary", "Quiet Button", "A calmer button for other actions."),
        I("ui.btn.danger", "Red Button", "For actions that delete or cannot be undone."),
        I("ui.btn.ghost", "Text Button", "A button with no box, like Cancel or Back."),
        I("ui.saveprefab", "Save Selected as Reusable UI", "Saves the selected UI (a Canvas or a panel with its children) as a prefab in Assets/UI to drop into any scene."),
    }, "Small styled parts that match each other."));
    m.push_back(I("ui.browse", "Browse All UI...", "Opens the UI Gallery: search every part, look, theme, icon and screen, preview it and add it with one click."));
    return m;
}

/// Ids that are no longer listed in the menu (a near-copy of another entry, or made for you). The editor still runs them: the UI Gallery
/// adds them and old projects' tools keep working; no component or scene format changed.
inline const std::vector<const char*>& RemovedFromMenu() {
    static const std::vector<const char*> v = {
        "ui.switch",          // a Toggle drawn as a switch: the Gallery still has it
        "ui.nameplateworld",  // the same plate as 3D Text above a character: use Name and Health Plate or 3D Text
        "ui.worldlabel",      // the same thing as 3D Text
        "ui.eventsystem",     // made for you the first time a widget is added
        "ui.hotreload",       // a developer tool, not a thing to put on a screen
    };
    return v;
}

namespace detail {
/// Takes the entry with this id out of the tree (anywhere in it) and returns it in `out`.
inline bool Take(std::vector<Entry>& es, const std::string& id, Entry& out) {
    for (std::size_t i = 0; i < es.size(); ++i) {
        if (es[i].id == id) { out = std::move(es[i]); es.erase(es.begin() + (std::ptrdiff_t)i); return true; }
        if (Take(es[i].children, id, out)) return true;
    }
    return false;
}
}

/// GameObject > UI: a short flat list of the things nearly everyone needs, then three folders:
///   Text, Image, Button, Panel, Text Field, Slider, Toggle, Dropdown, Scroll Area | Game HUD > | Menus & Screens > | More >
/// Twelve entries at the top and nothing else. Every item adds a working object under a Canvas that is made for you.
inline std::vector<Entry> UIMenu(const std::vector<ScreenGroup>& screens) {
    std::vector<Entry> all = UIMenuFull(screens);
    for (const char* r : RemovedFromMenu()) { Entry dead; detail::Take(all, r, dead); }
    std::vector<Entry> top;
    for (const char* id : {"ui.text", "ui.image", "ui.button", "ui.panel", "ui.inputfield", "ui.slider", "ui.toggle", "ui.dropdown", "ui.scrollview"}) {
        Entry e; if (detail::Take(all, id, e)) top.push_back(std::move(e));
    }
    // Game HUD: bars, slots, map, crosshair and the other things seen while playing (3D and touch pieces go to More).
    Entry world3d, touch, bar, hud;
    detail::Take(all, "ui.g.world3d", world3d);
    detail::Take(all, "ui.g.touch", touch);
    detail::Take(all, "ui.progressbar", bar);
    detail::Take(all, "ui.g.game", hud);
    hud.label = "Game HUD";
    hud.tip = "Health bar, inventory, minimap, crosshair, ammo and the other things shown while playing.";
    hud.children.insert(hud.children.begin(), std::move(bar));
    top.push_back(std::move(hud));
    Entry scr; if (detail::Take(all, "ui.g.screens", scr)) top.push_back(std::move(scr));
    Entry browse; detail::Take(all, "ui.browse", browse);
    // More: everything else, in the old groups.
    std::vector<Entry> more;
    for (Entry& g : all) {
        if (g.kind != menubar::Kind::Submenu) continue;
        if (g.id == "ui.g.basics") g.label = "Other Basics";
        more.push_back(std::move(g));
    }
    more.push_back(std::move(world3d));
    more.push_back(std::move(touch));
    more.push_back(Sep());
    more.push_back(std::move(browse));
    top.push_back(Sub("ui.g.more", "More", std::move(more), "Every other UI piece: more inputs, layouts, tables, pop-ups, 3D and touch controls, and the gallery."));
    return top;
}

/// The old GameObject > UI menu (before 2026-11-17) -> the id that now holds it. Labels that were listed twice appear once.
struct Legacy { const char* group; const char* label; const char* id; };
inline const std::vector<Legacy>& LegacyItems() {
    static const std::vector<Legacy> v = {
        {"Basic", "Button", "ui.button"}, {"Basic", "Panel", "ui.panel"}, {"Basic", "Image", "ui.image"}, {"Basic", "Raw Image", "ui.rawimage"},
        {"Basic", "Safe Area", "ui.safearea"}, {"Basic", "Text", "ui.text"},
        {"Controls", "Slider", "ui.slider"}, {"Controls", "Range Slider", "ui.rangeslider"}, {"Controls", "Virtual Joystick", "ui.joystick"},
        {"Controls", "D-Pad", "ui.dpad"}, {"Controls", "Touch Button", "ui.touchbutton"}, {"Controls", "Toggle", "ui.toggle"},
        {"Controls", "Stepper", "ui.stepper"}, {"Controls", "Rating", "ui.rating"}, {"Controls", "Tabs", "ui.tabs"},
        {"Controls", "Input Field", "ui.inputfield"}, {"Controls", "Input Field with Validation", "ui.validatedinput"},
        {"Controls", "Dropdown", "ui.dropdown"}, {"Controls", "Draggable", "ui.draggable"}, {"Controls", "Drop Target", "ui.droptarget"},
        {"HUD", "Progress Bar", "ui.progressbar"}, {"HUD", "Radial Progress", "ui.radialprogress"}, {"HUD", "Minimap", "ui.minimap"},
        {"HUD", "Crosshair", "ui.crosshair"}, {"HUD", "Color Picker", "ui.colorpicker"}, {"HUD", "Slot Grid", "ui.hotbar"},
        {"HUD", "Tooltip", "ui.tooltip"},
        {"Layout & Overlays", "Scroll View", "ui.scrollview"}, {"Layout & Overlays", "Layout Group", "ui.layoutgroup"},
        {"Layout & Overlays", "Flex Box", "ui.flexbox"}, {"Layout & Overlays", "Aspect Ratio Box", "ui.aspectbox"},
        {"Layout & Overlays", "Masked Panel", "ui.mask"}, {"Layout & Overlays", "List View", "ui.listview"},
        {"Layout & Overlays", "Modal Dialog", "ui.dialog"}, {"Layout & Overlays", "Toast Stack", "ui.toast"},
        {"Layout & Overlays", "Context Menu Panel", "ui.contextmenu"}, {"Layout & Overlays", "Keybind Field", "ui.keybind"},
        {"Game HUD", "Radial Menu", "ui.radialmenu"}, {"Game HUD", "Kill Feed", "ui.killfeed"}, {"Game HUD", "Ammo Counter", "ui.ammo"},
        {"Game HUD", "Interaction Prompt", "ui.prompt"}, {"Game HUD", "Objective Tracker", "ui.objective"}, {"Game HUD", "Compass", "ui.compass"},
        {"Game HUD", "Item Tooltip Card", "ui.itemcard"}, {"Game HUD", "Ability Icon", "ui.abilityicon"},
        {"Data", "Slider Bound to a Variable", "ui.boundslider"}, {"Data", "List From an Array", "ui.boundlist"},
        {"Data", "Visible If (condition)", "ui.visibleif"}, {"Data", "Hot Reload Watcher", "ui.hotreload"},
        {"App", "Search Bar", "ui.searchbar"}, {"App", "Date and Time", "ui.datetime"}, {"App", "File Picker", "ui.filepicker"},
        {"App", "Accordion", "ui.accordion"}, {"App", "Breadcrumbs", "ui.breadcrumbs"}, {"App", "Pagination", "ui.pagination"},
        {"App", "Skeleton", "ui.skeleton"}, {"App", "Bottom Sheet", "ui.bottomsheet"}, {"App", "Menu Bar", "ui.menubar"},
        {"App", "Toolbar", "ui.toolbar"}, {"App", "Tree View", "ui.treeview"}, {"App", "Data Table", "ui.datatable"},
        {"App", "Wizard", "ui.wizard"}, {"App", "Form Group", "ui.formgroup"},
        {"Kit", "Card", "ui.card"}, {"Kit", "Heading", "ui.heading"}, {"Kit", "List Row", "ui.listrow"}, {"Kit", "Badge", "ui.badge"},
        {"Kit", "Avatar", "ui.avatar"}, {"Kit", "Divider", "ui.divider"}, {"Kit", "Primary Button", "ui.btn.primary"},
        {"Kit", "Secondary Button", "ui.btn.secondary"}, {"Kit", "Danger Button", "ui.btn.danger"}, {"Kit", "Ghost Button", "ui.btn.ghost"},
        {"Kit", "Switch", "ui.switch"}, {"Kit", "Spinner", "ui.spinner"}, {"Kit", "Notification", "ui.notice"},
        {"Prebuilt Screens", "Main Menu", "ui.scr.main_menu"}, {"Prebuilt Screens", "Pause Menu", "ui.scr.pause_menu"},
        {"Prebuilt Screens", "Settings", "ui.scr.settings"}, {"Prebuilt Screens", "Save Slots", "ui.scr.save_slots"},
        {"Prebuilt Screens", "Confirm Dialog", "ui.scr.confirm_popup"}, {"Prebuilt Screens", "UI Notify", "ui.scr.ui_notify_screen"},
        {"Prebuilt Screens", "Loading Screen", "ui.scr.loading_screen"}, {"Prebuilt Screens", "Minimal", "ui.scr.loading_minimal"},
        {"Prebuilt Screens", "Card (loading)", "ui.scr.loading_card"}, {"Prebuilt Screens", "Splash (logo)", "ui.scr.loading_splash"},
        {"Prebuilt Screens", "Cinematic", "ui.scr.loading_cinematic"}, {"Prebuilt Screens", "Classic overlay", "ui.o.0"},
        {"Prebuilt Screens", "Minimal overlay", "ui.o.1"}, {"Prebuilt Screens", "Cinematic overlay", "ui.o.2"},
        {"Prebuilt Screens", "Splash overlay", "ui.o.3"}, {"Prebuilt Screens", "Card overlay", "ui.o.4"},
        {"Prebuilt Screens", "Corner overlay", "ui.o.5"}, {"Prebuilt Screens", "Ring overlay", "ui.o.6"},
        {"Prebuilt Screens", "HUD (health + score)", "ui.scr.hud_basic"}, {"Prebuilt Screens", "Health Bar", "ui.scr.health_bar_screen"},
        {"Prebuilt Screens", "Dialog Box", "ui.scr.dialog_box_old"}, {"Prebuilt Screens", "Notification (screen)", "ui.scr.notification_list"},
        {"Prebuilt Screens", "UI Showcase", "ui.scr.ui_showcase"}, {"Prebuilt Screens", "Shop", "ui.scr.shop"},
        {"Prebuilt Screens", "Game Over", "ui.scr.game_over"}, {"Prebuilt Screens", "Lobby", "ui.scr.lobby"},
        {"Prebuilt Screens", "Scoreboard", "ui.scr.scoreboard"}, {"Prebuilt Screens", "Leaderboard", "ui.scr.leaderboard"},
        {"Prebuilt Screens", "Matchmaking", "ui.scr.matchmaking"}, {"Prebuilt Screens", "UI Color", "ui.scr.ui_color_screen"},
        {"Prebuilt Screens", "UI HUD", "ui.scr.hud_full"}, {"Prebuilt Screens", "UI Controls", "ui.scr.ui_controls_screen"},
        {"Prebuilt Screens", "UI Options", "ui.scr.ui_options_screen"},
        {"(loose)", "Window", "ui.window"}, {"(loose)", "Radio Buttons", "ui.radio"}, {"(loose)", "Damage Numbers", "ui.damage"},
        {"(loose)", "Nameplate", "ui.nameplate"}, {"(loose)", "Nameplate (in the world)", "ui.nameplateworld"},
        {"(loose)", "Inventory", "ui.scr.inventory_bag"}, {"(loose)", "Dialogue", "ui.scr.dialogue_native"},
        {"(below the list)", "Canvas", "ui.canvas"}, {"(below the list)", "Event System", "ui.eventsystem"},
        {"(below the list)", "World Label (3D)", "ui.worldlabel"},
        {"(below the list)", "Inventory (Minecraft-style)", "ui.inventory.mc"}, {"(below the list)", "Grid Inventory (DayZ/Unturned-style)", "ui.inventory.grid"},
        {"(below the list)", "Unturned Inventory (worn bags + nearby loot)", "ui.inventory.unturned"}, {"(below the list)", "UI Document", "ui.document"},
        {"(below the list)", "Save Selected as UI", "ui.saveprefab"}, {"3D UI (in-world)", "Button", "ui.w3d.button"}, {"3D UI (in-world)", "Text", "ui.w3d.text"},
        {"3D UI (in-world)", "Panel", "ui.w3d.panel"}, {"3D UI (in-world)", "Image", "ui.w3d.image"},
    };
    return v;
}

} // namespace uimenu
} // namespace oked
