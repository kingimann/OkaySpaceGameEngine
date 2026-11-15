#pragma once
// STUB (old behaviour, test first): replaced by the real rule in the next commit.
#include <cstddef>
#include <string>
#include <vector>

namespace okay::editor::startup {

enum class Modal { None, ProjectVersion, SaveChanges, ProjectChooser, SceneAutosave, Recovery };

struct Pending {
    bool projectVersion = false;
    bool saveChanges = false;
    bool projectChooser = false;
    bool recovery = false;
    bool sceneAutosave = false;
};

inline bool Wants(Modal m, const Pending& p) {
    switch (m) {
        case Modal::ProjectVersion: return p.projectVersion;
        case Modal::SaveChanges:    return p.saveChanges;
        case Modal::ProjectChooser: return p.projectChooser;
        case Modal::SceneAutosave:  return p.sceneAutosave;
        case Modal::Recovery:       return p.recovery;
        case Modal::None:           return false;
    }
    return false;
}

// old: every question asks whenever it wants
inline bool MayAsk(Modal m, const Pending& p) { return Wants(m, p); }

// old: only the answered copy is deleted
inline std::vector<std::size_t> CopiesToDiscard(const std::vector<std::string>& sessions, std::size_t answered, bool answeredReadable) {
    (void)answeredReadable;
    if (answered >= sessions.size()) return {};
    return {answered};
}

struct RecoveryFolders {
    std::string folder;
    std::string legacy;
};
// old: the relative folder "okay_recovery"
inline RecoveryFolders PlanRecoveryFolders(const std::string& userDataDir, const std::string& startDir) {
    (void)userDataDir; (void)startDir;
    return {"okay_recovery", ""};
}

}  // namespace okay::editor::startup
