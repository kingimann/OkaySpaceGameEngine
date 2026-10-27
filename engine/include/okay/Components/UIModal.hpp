#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Render/Color.hpp"
#include <cstdint>
#include <string>

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

    // Opt-in card styling. Off by default, so existing confirms keep the colours
    // they were authored with. When on, each Update restyles descendants named
    // Card, Title, Message, ConfirmButton and CancelButton. Empty text leaves
    // the label that was already on the widget.
    bool  styleCard = false;
    float cardRadius = 14.0f;
    Color cardColor = Color::FromBytes(62, 48, 28);
    Color titleColor = Color::FromBytes(255, 214, 120);
    Color bodyColor = Color::FromBytes(230, 214, 186);
    Color confirmColor = Color::FromBytes(214, 140, 48);
    Color cancelColor = Color::FromBytes(72, 58, 44);
    std::string titleText;
    std::string bodyText;
    std::string confirmText;
    std::string cancelText;

    void Open();
    void Close();
    void SetStyleCard(bool on) { styleCard = on; if (styleCard) ApplyChrome(); }
    void SetCardRadius(float r) { cardRadius = r < 0.0f ? 0.0f : r; if (styleCard) ApplyChrome(); }
    void SetTitle(const std::string& t) { titleText = t; if (styleCard) ApplyChrome(); }
    void SetMessage(const std::string& t) { bodyText = t; if (styleCard) ApplyChrome(); }
    void SetConfirmText(const std::string& t) { confirmText = t; if (styleCard) ApplyChrome(); }
    void SetCancelText(const std::string& t) { cancelText = t; if (styleCard) ApplyChrome(); }
    /// Restyle the named descendants. Safe to call when the card has no children yet.
    void ApplyChrome();
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
