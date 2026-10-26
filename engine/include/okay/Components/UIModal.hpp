#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Color.hpp"
#include <cstdint>

namespace okay {

class GameObject;
class Scene;

/// Makes its object (a panel with buttons, say) a modal dialog while it is shown:
/// it draws above all other UI behind an optional dimmed backdrop, and only the
/// widgets inside it can be hovered, clicked or reach keyboard / gamepad focus.
/// Modals stack: the one shown last is on top and takes input. Show it with
/// Open() (or by activating the object), hide it with Close(); Escape or gamepad B
/// closes the top one when `closeOnCancel` is on. Closing fires the object's
/// script on_modal_close().
class UIModal final : public Behaviour {
public:
    UIModal();
    ~UIModal() override;

    bool  dimBackground = true;
    Color dimColor = Color::FromBytes(0, 0, 0, 140);
    bool  closeOnCancel = true;       // Escape / gamepad B closes it when on top
    bool  closeOnClickOutside = false;

    void Open();
    void Close();
    /// Shown: the object is active and visible (not under a hidden canvas / parent).
    bool IsOpen() const;
    /// Order it was opened in among the open modals (0 = not open). Higher = on top.
    std::uint64_t OpenOrder() const { return m_order; }

    void Update(float dt) override;

    /// Refresh which modals are open and in what order. UpdateUILayouts calls this
    /// every frame (the editor preview too).
    static void RefreshOpenOrder();
    /// The modal on top in `scene`, or null when none is open.
    static UIModal* Top(const Scene* scene);
    /// Is input to `go` blocked by an open modal it isn't part of?
    static bool Blocks(GameObject* go);

private:
    std::uint64_t m_order = 0;
};

} // namespace okay
