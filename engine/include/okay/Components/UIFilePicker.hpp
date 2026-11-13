#pragma once
#include "okay/Components/UIWidgetUtil.hpp"
#include "okay/UI/UIShape.hpp"
#include "okay/Render/Color.hpp"
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <system_error>

namespace okay {

/// An in-game file list. It only reads inside `root` (a folder the game chose).
/// ".." stops at that folder, and a link that points outside it is not opened.
/// on_change() runs when the folder or the chosen file changes. on_submit() runs
/// when a file is chosen.
class UIFilePicker final : public Behaviour {
public:
    struct Entry {
        std::string name;
        bool directory = false;
    };

    Vec2 position{20.0f, 20.0f};
    Vec2 size{320.0f, 220.0f};
    UIAnchor anchor = UIAnchor::TopLeft;
    /// The only folder this picker may read. Empty until a game sets it.
    std::string root;
    /// Folder currently shown, relative to root. Empty is the root itself.
    std::string current;
    /// Chosen file, relative to root. Empty when a folder is showing.
    std::string selected;
    bool showHidden = false;
    bool interactable = true;
    bool focusable = true;
    /// Highlighted row, including the ".." row when it is showing.
    int value = 0;
    Color background = Color::FromBytes(28, 32, 44);
    Color textColor = Color::White;
    Color accent = Color::FromBytes(90, 150, 230);
    Color muted = Color::FromBytes(150, 158, 176);
    UIShape shape = UIShape::Rounded;
    float cornerRadius = 6.0f;
    float headerHeight = 28.0f;
    float rowHeight = 26.0f;

    bool IsFocused() const { return m_focused; }
    void SetFocused(bool f) { m_focused = f; }

    /// Remember `path` as the sandbox. False when it is missing or not a folder.
    bool SetRoot(const std::string& path) {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path canon = fs::weakly_canonical(fs::path(path), ec);
        if (ec || canon.empty() || !fs::is_directory(canon, ec)) return false;
        root = canon.generic_string();
        current.clear();
        selected.clear();
        value = 0;
        FireUIEvent(gameObject, "on_change");
        return true;
    }

    bool AtRoot() const { return current.empty(); }

    std::string Status() const {
        namespace fs = std::filesystem;
        if (root.empty()) return "Set the folder this picker may open";
        std::error_code ec;
        if (!fs::is_directory(fs::path(root), ec)) return "That folder is not there";
        return {};
    }

    /// True when `candidate` resolves to root or something inside it.
    bool Inside(const std::filesystem::path& candidate) const {
        namespace fs = std::filesystem;
        if (root.empty()) return false;
        std::error_code ec;
        fs::path rootC = fs::weakly_canonical(fs::path(root), ec);
        if (ec || rootC.empty()) return false;
        fs::path can = fs::weakly_canonical(candidate, ec);
        if (ec) {
            fs::path parent = fs::weakly_canonical(candidate.parent_path(), ec);
            if (ec) return false;
            can = parent / candidate.filename();
        }
        if (can == rootC) return true;
        const fs::path rel = can.lexically_relative(rootC);
        if (rel.empty()) return false;
        for (const fs::path& part : rel)
            if (part == "..") return false;
        return true;
    }

    std::filesystem::path Full(const std::string& rel) const {
        namespace fs = std::filesystem;
        fs::path base(root);
        if (rel.empty()) return base;
        return base / fs::path(rel);
    }

    std::vector<Entry> Entries() const {
        namespace fs = std::filesystem;
        std::vector<Entry> out;
        if (Status().size()) return out;
        const fs::path dir = Full(current);
        if (!current.empty() && !Inside(dir)) return out;
        std::error_code ec;
        fs::directory_iterator it(dir, ec);
        if (ec) return out;
        const fs::directory_iterator end;
        for (; it != end; it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            const fs::directory_entry& ent = *it;
            std::error_code sec;
            const fs::path can = fs::weakly_canonical(ent.path(), sec);
            if (sec || !Inside(can.empty() ? ent.path() : can)) continue;
            const std::string name = ent.path().filename().generic_string();
            if (name.empty() || name == "." || name == "..") continue;
            if (!showHidden && !name.empty() && name[0] == '.') continue;
            Entry e;
            e.name = name;
            e.directory = ent.is_directory(sec);
            out.push_back(e);
        }
        std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
            if (a.directory != b.directory) return a.directory && !b.directory;
            return UIWidgetLower(a.name) < UIWidgetLower(b.name);
        });
        if ((int)out.size() > 200) out.resize(200);
        return out;
    }

    /// Rows on screen: an optional ".." then Entries().
    std::vector<Entry> Rows() const {
        std::vector<Entry> rows;
        if (!AtRoot()) rows.push_back(Entry{"..", true});
        const auto entries = Entries();
        rows.insert(rows.end(), entries.begin(), entries.end());
        return rows;
    }

    bool GoUp() {
        if (AtRoot()) return false;
        const auto slash = current.find_last_of('/');
        current = slash == std::string::npos ? std::string{} : current.substr(0, slash);
        selected.clear();
        value = 0;
        FireUIEvent(gameObject, "on_change");
        return true;
    }

    /// Open one child of the folder on screen. A folder is entered. A file is chosen.
    /// Names with a slash, or that leave the sandbox, do nothing.
    bool Open(const std::string& name) {
        namespace fs = std::filesystem;
        if (name == "..") return GoUp();
        if (!SingleSegment(name) || root.empty()) return false;
        const std::string rel = current.empty() ? name : current + "/" + name;
        const fs::path full = Full(rel);
        if (!Inside(full)) return false;
        std::error_code ec;
        if (fs::is_directory(full, ec)) {
            current = rel;
            selected.clear();
            value = 0;
            FireUIEvent(gameObject, "on_change");
            return true;
        }
        if (fs::is_regular_file(full, ec)) {
            if (selected == rel) {
                FireUIEvent(gameObject, "on_submit");
                return true;
            }
            selected = rel;
            FireUIEvent(gameObject, "on_change");
            FireUIEvent(gameObject, "on_submit");
            return true;
        }
        return false;
    }

    /// Choose a file by a path relative to root. Absolute paths and paths that
    /// leave the sandbox are refused.
    bool SetSelected(const std::string& rel) {
        namespace fs = std::filesystem;
        if (rel.empty() || root.empty()) return false;
        if (rel[0] == '/' || rel[0] == '\\') return false;
        if (rel.find(':') != std::string::npos) return false;
        if (rel.find("..") != std::string::npos) return false;
        const fs::path full = Full(rel);
        if (!Inside(full)) return false;
        std::error_code ec;
        if (!fs::is_regular_file(full, ec)) return false;
        if (selected == rel) return true;
        selected = rel;
        FireUIEvent(gameObject, "on_change");
        return true;
    }

    void OpenHighlighted() {
        const auto rows = Rows();
        if (rows.empty()) return;
        int i = value;
        if (i < 0) i = 0;
        if (i >= (int)rows.size()) i = (int)rows.size() - 1;
        Open(rows[(std::size_t)i].name);
    }

    void MoveRow(int dir) {
        const int n = (int)Rows().size();
        if (n <= 0) return;
        int next = value + dir;
        if (next < 0) next = 0;
        if (next >= n) next = n - 1;
        value = next;
    }

    void PressLocal(const Vec2& local) {
        if (!interactable) return;
        if (local.y <= headerHeight) return;
        const int row = (int)((local.y - headerHeight) / rowHeight);
        const auto rows = Rows();
        if (row < 0 || row >= (int)rows.size()) return;
        value = row;
        Open(rows[(std::size_t)row].name);
    }

    void Update(float) override {
        if (!enabled || !interactable) return;
        Vec2 local;
        if (UIWidgetPointerLocal(gameObject, anchor, position, size, local)) PressLocal(local);
    }

private:
    static bool SingleSegment(const std::string& name) {
        if (name.empty() || name == "." || name == "..") return false;
        if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) return false;
        if (name.find("..") != std::string::npos) return false;
        return true;
    }
    bool m_focused = false;
};

} // namespace okay
