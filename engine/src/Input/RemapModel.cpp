#include "okay/Input/RemapModel.hpp"
#include "okay/Input/Input.hpp"

#include <algorithm>
#include <cctype>

namespace okay {

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

bool Listed(const std::set<std::string>& names, const std::string& name) {
    const std::string n = Lower(name);
    for (const auto& s : names) if (Lower(s) == n) return true;
    return false;
}

bool IsDpadButton(int code) {
    return code == Input::PadUp || code == Input::PadDown || code == Input::PadLeft || code == Input::PadRight;
}

// Which of a four-key direction's keys is `key` (0..3), or -1.
int KeyIndex(const InputBinding& b, int key) {
    if (b.kind != InputBinding::Kind::Keys4) return -1;
    for (int i = 0; i < 4; ++i) if ((unsigned char)b.keys[i] == (unsigned char)key) return i;
    return -1;
}

// The binding a rebind for this device changes (InputActions::Current's rule) in a list.
const InputBinding* SlotIn(const std::vector<InputBinding>& list, bool gamepad) {
    for (const InputBinding& b : list)
        if (b.IsGamepad() == gamepad && b.kind != InputBinding::Kind::MouseDelta) return &b;
    return nullptr;
}

RemapSlot MakeSlot(const InputActions::Action& a, bool gamepad) {
    RemapSlot s;
    if (const InputBinding* cur = SlotIn(a.bindings, gamepad)) {
        s.bound = true;
        s.binding = *cur;
        s.label = cur->Label();
        if (cur->kind == InputBinding::Kind::Keys4)
            for (int i = 0; i < 4; ++i) s.partLabels.push_back(Input::KeyName(cur->keys[i]));
    }
    if (const InputBinding* def = SlotIn(a.defaults, gamepad)) s.defaultBinding = *def;
    s.isDefault = s.bound ? (s.defaultBinding.kind != InputBinding::Kind::None && s.binding.ToString() == s.defaultBinding.ToString())
                          : s.defaultBinding.kind == InputBinding::Kind::None;
    return s;
}

// The name InputBinding::Parse reads back for a key ("key:space").
std::string KeyBindingText(int code) {
    InputBinding b;
    b.kind = InputBinding::Kind::Key;
    b.code = code;
    return b.ToString();
}

} // namespace

bool RemapOverlap(const InputBinding& a, const InputBinding& b) {
    using K = InputBinding::Kind;
    if (a.kind == K::None || b.kind == K::None) return false;
    if (a.kind == b.kind) {
        switch (a.kind) {
            case K::Key: case K::Mouse: case K::PadButton: return a.code == b.code;
            case K::Keys4:
                for (int i = 0; i < 4; ++i) if (KeyIndex(b, (unsigned char)a.keys[i]) >= 0) return true;
                return false;
            default: return true;   // the same stick, the d-pad, mouse movement
        }
    }
    if (a.kind == K::Key && b.kind == K::Keys4) return KeyIndex(b, a.code) >= 0;
    if (a.kind == K::Keys4 && b.kind == K::Key) return KeyIndex(a, b.code) >= 0;
    if (a.kind == K::DPad && b.kind == K::PadButton) return IsDpadButton(b.code);
    if (a.kind == K::PadButton && b.kind == K::DPad) return IsDpadButton(a.code);
    return false;
}

std::vector<RemapRow> RemapRows(const RemapOptions& o) {
    std::vector<RemapRow> rows;
    for (const InputActions::Action& a : InputActions::All()) {
        if (Listed(o.hidden, a.name)) continue;
        RemapRow r;
        r.action = a.name;
        r.direction = a.direction;
        r.reserved = Listed(o.reserved, a.name);
        r.keyboard = MakeSlot(a, false);
        r.gamepad = MakeSlot(a, true);
        rows.push_back(std::move(r));
    }
    return rows;
}

std::vector<std::string> RemapDirectionChoices() { return {"stick:left", "stick:right", "dpad"}; }

std::vector<RemapConflict> RemapConflicts(const std::string& action, const std::string& bindingText, int part, const RemapOptions& o) {
    std::vector<RemapConflict> out;
    const InputBinding nb = InputBinding::Parse(bindingText);
    if (nb.kind == InputBinding::Kind::None) return out;
    const std::string self = Lower(action);
    for (const InputActions::Action& a : InputActions::All()) {
        const bool same = Lower(a.name) == self;
        const InputBinding* slot = same ? SlotIn(a.bindings, nb.IsGamepad()) : nullptr;
        for (const InputBinding& b : a.bindings) {
            if (same) {
                // Editing one key of this action's own four-key direction: its other keys are in the way.
                if (part < 0 || part > 3 || &b != slot || b.kind != InputBinding::Kind::Keys4 || nb.kind != InputBinding::Kind::Key) continue;
                for (int j = 0; j < 4; ++j)
                    if (j != part && (unsigned char)b.keys[j] == (unsigned char)nb.code) {
                        RemapConflict c;
                        c.action = a.name; c.binding = b.ToString(); c.gamepad = false; c.part = j;
                        out.push_back(std::move(c));
                    }
                continue;
            }
            if (!RemapOverlap(nb, b)) continue;
            RemapConflict c;
            c.action = a.name;
            c.binding = b.ToString();
            c.gamepad = b.IsGamepad();
            c.part = nb.kind == InputBinding::Kind::Key ? KeyIndex(b, nb.code) : -1;
            c.reserved = Listed(o.reserved, a.name);
            out.push_back(std::move(c));
        }
    }
    return out;
}

std::vector<RemapDuplicate> RemapAllConflicts(const RemapOptions& o) {
    std::vector<RemapDuplicate> out;
    std::vector<const InputActions::Action*> list;
    for (const InputActions::Action& a : InputActions::All()) if (!Listed(o.hidden, a.name)) list.push_back(&a);
    for (std::size_t i = 0; i < list.size(); ++i)
        for (std::size_t j = i + 1; j < list.size(); ++j) {
            bool found = false;
            for (const InputBinding& x : list[i]->bindings) {
                for (const InputBinding& y : list[j]->bindings)
                    if (RemapOverlap(x, y)) {
                        RemapDuplicate d;
                        d.first = list[i]->name; d.second = list[j]->name; d.binding = x.Label();
                        out.push_back(std::move(d));
                        found = true;
                        break;
                    }
                if (found) break;
            }
        }
    return out;
}

RemapResult RemapBind(const std::string& action, const std::string& bindingText, RemapResolve how, int part, const RemapOptions& o) {
    RemapResult r;
    InputActions::Action* a = InputActions::Find(action);
    if (!a) { r.error = "no action named " + action; return r; }
    if (Listed(o.reserved, a->name)) { r.error = a->name + " can't be rebound"; return r; }
    const InputBinding nb = InputBinding::Parse(bindingText);
    if (nb.kind == InputBinding::Kind::None) { r.error = "\"" + bindingText + "\" is not an input"; return r; }
    const bool pad = nb.IsGamepad();
    const InputBinding* slotNow = SlotIn(a->bindings, pad);
    if (part >= 0) {
        if (part > 3 || !slotNow || slotNow->kind != InputBinding::Kind::Keys4 || nb.kind != InputBinding::Kind::Key) {
            r.error = "only one key of a four-key direction can take a part";
            return r;
        }
    }
    const InputBinding before = slotNow ? *slotNow : InputBinding{};   // what this action gives up

    r.conflicts = RemapConflicts(action, bindingText, part, o);
    const std::string taken = nb.Label();

    if (r.conflicts.empty() || how == RemapResolve::Allow) {
        if (!InputActions::Rebind(action, bindingText, part)) { r.error = "couldn't rebind " + a->name; return r; }
        r.ok = true;
        r.changed.push_back(a->name);
        return r;
    }
    if (how == RemapResolve::Cancel) {
        r.error = taken + " is already used by " + r.conflicts.front().action;
        return r;
    }
    for (const RemapConflict& c : r.conflicts)
        if (c.reserved) { r.error = taken + " belongs to " + c.action + ", which can't be rebound"; return r; }

    if (how == RemapResolve::Swap) {
        if (r.conflicts.size() != 1) { r.error = taken + " is used by several actions: pick Unbind or Allow"; return r; }
        const RemapConflict& c = r.conflicts.front();
        if (before.kind == InputBinding::Kind::None) { r.error = a->name + " has no binding to give " + c.action; return r; }
        // What the other action receives: this action's old key (one key of a direction) or its whole binding.
        std::string give;
        int givePart = -1;
        if (part >= 0) {
            give = KeyBindingText((unsigned char)before.keys[part]);
            givePart = c.part;
        } else if (c.part >= 0) {
            if (before.kind != InputBinding::Kind::Key) { r.error = "can't give a four-key direction one key's place"; return r; }
            give = before.ToString();
            givePart = c.part;
        } else {
            give = before.ToString();
        }
        if (!InputActions::Rebind(action, bindingText, part)) { r.error = "couldn't rebind " + a->name; return r; }
        if (!InputActions::Rebind(c.action, give, givePart)) {   // undo: put this action back
            InputActions::Rebind(action, before.ToString(), -1);
            r.error = "couldn't give " + c.action + " the old binding";
            return r;
        }
        r.ok = true;
        r.changed.push_back(a->name);
        if (c.action != a->name) r.changed.push_back(c.action);
        return r;
    }

    // Unbind: every other action loses what clashes, as long as it keeps another binding.
    for (const RemapConflict& c : r.conflicts) {
        if (c.part >= 0) { r.error = "can't unbind one key of " + c.action + "'s four-key direction: pick Swap"; return r; }
        if (Lower(c.action) == Lower(a->name)) { r.error = "can't unbind a key of the action being changed"; return r; }
        const InputActions::Action* other = InputActions::Find(c.action);
        if (!other || other->bindings.size() < 2) { r.error = c.action + " would be left with no binding"; return r; }
    }
    if (!InputActions::Rebind(action, bindingText, part)) { r.error = "couldn't rebind " + a->name; return r; }
    r.changed.push_back(a->name);
    for (const RemapConflict& c : r.conflicts) {
        InputActions::Action* other = InputActions::Find(c.action);
        if (!other) continue;
        auto& list = other->bindings;
        list.erase(std::remove_if(list.begin(), list.end(), [&](const InputBinding& b) { return RemapOverlap(nb, b); }), list.end());
        if (list.empty()) list = other->defaults;   // never leave an action with nothing
        if (std::find(r.changed.begin(), r.changed.end(), other->name) == r.changed.end()) r.changed.push_back(other->name);
    }
    InputActions::Save();
    r.ok = true;
    return r;
}

} // namespace okay
