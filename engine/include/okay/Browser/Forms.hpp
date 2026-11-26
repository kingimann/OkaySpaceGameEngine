#pragma once
// Browser (docs/feature_map/17-launcher-platform.md 17.18): forms. What the person typed and ticked in a page's form controls,
// and what a form sends, written from the WHATWG HTML standard, section 4.10 (forms): the form owner (4.10.18.3, the form=""
// attribute or the nearest <form> ancestor), the controls' values and checkedness (input text / password / email / search /
// url / tel / number / hidden / checkbox / radio / submit / button / reset, textarea, select with option), the radio button
// group (4.10.5.1.16: same name, same form owner, one checked), labels (4.10.4: for="" or the first labelable descendant),
// disabled controls and fieldsets, constructing the entry list (4.10.21.4), the application/x-www-form-urlencoded and
// text/plain encodings (4.10.21.6, 4.10.21.8), the form submission algorithm's GET ("mutate action URL") and POST ("submit as
// entity body") (4.10.21.3), implicit submission with Enter (4.10.21.2) and reset (4.10.22).
// multipart/form-data (files) is not done: such a form is sent urlencoded, without its files, and Submission::note says so.
// The page engine keeps one FormState per shown page (BrowserTab::Forms); the view types into it through OkayUI. No network.
// Standard library only.
#include "okay/Browser/Html.hpp"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace okay::browser {

/// What kind of control an element is.
enum class ControlKind { None, Text, Password, Checkbox, Radio, Submit, Reset, Button, Hidden, TextArea, Select, File, Image };
ControlKind KindOf(const Node& el);
/// The element's lower-cased type="" for an <input> ("text" when missing or unknown).
std::string InputType(const Node& el);

struct Submission {
    bool ok = false;
    std::string url;       ///< where to go (GET: with the query; POST: the action)
    std::string method;    ///< "GET" or "POST"
    std::string body;      ///< POST: the encoded entries
    std::string enctype;   ///< the Content-Type of the body
    std::string note;      ///< something that was left out ("files are not sent"), or why nothing is sent
};

class FormState {
public:
    /// Start from the document's own values (value="", checked, selected, the text of a textarea).
    void Reset(const HtmlDocument& doc);
    /// The form's controls back to the document's values (a reset button).
    void ResetForm(const Node* form);

    std::string Value(const Node& control) const;
    void SetValue(const Node& control, const std::string& value);
    bool Checked(const Node& control) const;
    /// Tick or untick. A radio button that is ticked unticks the others of its group; a ticked radio cannot be unticked by a click.
    void SetChecked(const Node& control, bool checked);
    /// A click on a checkbox or radio button (what a person does).
    void Toggle(const Node& control);
    /// The options of a <select>: (value, text), and which one is chosen (-1: none).
    std::vector<std::pair<std::string, std::string>> Options(const Node& select) const;
    int SelectedIndex(const Node& select) const;
    void Select(const Node& select, int index);
    /// The text a control shows: a field's value (a password's as bullets), a button's label, the chosen option's text; for a
    /// checkbox or radio button "\x01" when it is ticked, "" when not.
    std::string Display(const Node& control) const;
    /// True for a field that takes typing (text-like inputs and textarea).
    static bool IsTypable(const Node& control);
    static bool IsDisabled(const Node& control);
    /// The form a control belongs to (null: none).
    static const Node* FormOwner(const Node& control, const HtmlDocument& doc);
    /// The control a <label> is for (null: none).
    static const Node* LabelTarget(const Node& label, const HtmlDocument& doc);

    /// Send `form` with `submitter` (the button pressed, or null: Enter in a field). `pageUrl`: the document's address; the
    /// action is resolved against it (and <base href>).
    Submission Submit(const Node& form, const Node* submitter, const std::string& pageUrl, const HtmlDocument& doc) const;
    /// The entry list (name, value) the form would send (4.10.21.4), for tests and tools.
    std::vector<std::pair<std::string, std::string>> Entries(const Node& form, const Node* submitter, const HtmlDocument& doc) const;
    /// Enter in field `control`: the form's default button, or the form itself when it has no button and one field (4.10.21.2).
    /// False when Enter does not submit.
    bool ImplicitSubmitter(const Node& control, const HtmlDocument& doc, const Node*& form, const Node*& submitter) const;

    static constexpr std::size_t kMaxValueBytes = 1u << 20;   ///< what one field keeps

private:
    std::map<const Node*, std::string> m_values;
    std::map<const Node*, bool> m_checked;
    std::map<const Node*, int> m_selected;
    std::map<const Node*, std::string> m_defaultValues;
    std::map<const Node*, bool> m_defaultChecked;
    std::map<const Node*, int> m_defaultSelected;
    const HtmlDocument* m_doc = nullptr;   // the page (for radio groups and form owners)
};

} // namespace okay::browser
