// Member moderation for servers and group chats (MemberModerationUI.hpp): kick / ban / unban / admin role from a
// members list, the confirm dialog and the "Banned" list; requests run off-thread. Moved out of launcher/main.cpp
// unchanged (docs/engine/modularization_plan.md, Part C section 13, batch L7).
#include "LauncherShared.hpp"
#include "NavIcons.hpp"   // SlateCombo, the launcher's dropdown
#include "MemberModerationUI.hpp"
#include "okay/Platform/Platform.hpp"
#include "OkayTheme.hpp"   // the theme palette's colours

namespace okl {

// ---- Member moderation (servers and group chats): kick / ban / unban / admin role ----
// One set of helpers for both members lists (shared/MemberModeration.hpp holds the
// rules and request building; docs/sql/member_moderation.sql enforces them). Calls run
// off-thread; the outcome comes back as a toast that says what the server answered.
struct ModJob { int kind = 0; mm::Scope scope = mm::Scope::Server; long cid = 0; mm::Request req; std::string what; };   // kind 0 action, 1 banned list
struct ModResult { int kind = 0; mm::Scope scope = mm::Scope::Server; long cid = 0; mm::Outcome out; std::vector<mm::BanEntry> bans; };
struct ModBanList {
    long cid = 0; bool loaded = false, inflight = false; Uint32 nextAt = 0;
    std::vector<mm::BanEntry> list; std::string problem;   // problem: why the list could not load ("" = fine)
};
static okay::thr::Mutex* g_modMu = nullptr;
static std::vector<ModResult> g_modDone;          // (guarded)
static ModBanList g_modBans[2];                   // by scope; main thread only
static int ModIdx(mm::Scope s) { return s == mm::Scope::Server ? 0 : 1; }
static int ModWorker(void* p) {
    std::unique_ptr<ModJob> j(static_cast<ModJob*>(p));
    ModResult r; r.kind = j->kind; r.scope = j->scope; r.cid = j->cid;
    try {
        okay::account::AccountService a(fs::path(g_pollCfgDir), g_pollUrl, g_pollKey);
        auto api = [&](const std::string& path, const std::string& method, const std::string& body) { return a.Api(path, method, body); };
        if (j->kind == 0) r.out = mm::Execute(api, j->req, j->what);
        else {
            auto resp = a.Api(j->req.path, "GET");
            r.out = mm::MapResponse(resp.ok, resp.reached, resp.status, resp.body, "List");
            if (r.out.ok()) {
                r.bans = mm::ParseBans(resp.body, (long)std::time(nullptr));
                std::string ids; for (const auto& b : r.bans) { if (!ids.empty()) ids += ","; ids += b.uid; }
                if (!ids.empty()) {
                    auto p2 = a.Api("/rest/v1/profiles?select=id,username,display_name&id=in.(" + ids + ")", "GET");
                    if (p2.ok) for (const auto& o : okay::Servers::SplitObjects(p2.body)) {
                        const std::string id = okay::Servers::Field(o, "id");
                        for (auto& b : r.bans) if (b.uid == id) {
                            b.name = okay::account::PublicName(okay::Servers::Field(o, "display_name"));
                            if (b.name.empty()) b.name = okay::account::PublicName(okay::Servers::Field(o, "username"));
                        }
                    }
                }
            }
        }
    } catch (...) { r.out.status = mm::Status::Offline; r.out.message = "Couldn't reach the server. Nothing was changed."; }
    if (g_modMu) { okay::thr::LockMutex(g_modMu); g_modDone.push_back(std::move(r)); okay::thr::UnlockMutex(g_modMu); }
    return 0;
}
static void ModStart(ModJob* j) {
    if (!g_modMu) g_modMu = okay::thr::CreateMutex();
    if (okay::thr::Thread* t = okay::thr::CreateThread(ModWorker, "okay-modmember", j)) okay::thr::DetachThread(t);
    else delete j;
}
static void ModAction(mm::Scope sc, long cid, const mm::Request& req, const std::string& what) {
    if (!req.valid()) { Toast("That request isn't valid.", 2); return; }
    auto* j = new ModJob; j->kind = 0; j->scope = sc; j->cid = cid; j->req = req; j->what = what;
    ModStart(j);
}
static void ModFetchBans(mm::Scope sc, long cid) {
    ModBanList& b = g_modBans[ModIdx(sc)];
    if (b.inflight) return;
    b.inflight = true; b.nextAt = okay::platform::TicksMs32() + 20000;
    auto* j = new ModJob; j->kind = 1; j->scope = sc; j->cid = cid; j->req = mm::BansListRequest(sc, cid);
    ModStart(j);
}
// Main thread, once per frame per screen: toast finished actions, store fetched lists.
// Returns true when a member change went through (the caller refreshes its members).
bool ModDrain(mm::Scope sc) {
    std::vector<ModResult> mine;
    if (g_modMu) {
        okay::thr::LockMutex(g_modMu);
        for (auto it = g_modDone.begin(); it != g_modDone.end();) {
            if (it->scope == sc) { mine.push_back(std::move(*it)); it = g_modDone.erase(it); } else ++it;
        }
        okay::thr::UnlockMutex(g_modMu);
    }
    bool changed = false;
    for (auto& r : mine) {
        ModBanList& b = g_modBans[ModIdx(sc)];
        if (r.kind == 1) {
            b.inflight = false;
            if (r.cid != b.cid) continue;   // another server / group was opened meanwhile
            b.loaded = r.out.ok(); b.problem = r.out.ok() ? std::string() : r.out.message;
            if (r.out.ok()) b.list = std::move(r.bans);
        } else {
            Toast(r.out.message, r.out.ok() ? 1 : 2);
            if (r.out.ok()) { changed = true; b.nextAt = 0; }   // refetch the banned list now
        }
    }
    return changed;
}
// Banned list follows the open server / group: reset when it changes.
static ModBanList& ModBansFor(mm::Scope sc, long cid) {
    ModBanList& b = g_modBans[ModIdx(sc)];
    if (b.cid != cid) { b = ModBanList(); b.cid = cid; }
    return b;
}

struct ModDialogState {
    bool openReq = false; int action = 0;   // 0 remove, 1 ban
    mm::Scope scope = mm::Scope::Server; long cid = 0; std::string uid, name;
    char reason[320] = ""; int dur = 1;     // index into mm::Durations() (default: 1 day)
};
static ModDialogState g_modDlg;

bool ModHasActions(mm::Role actor, mm::Role target, bool isSelf) {
    return mm::CanModerate(actor, target, isSelf) || mm::CanSetRole(actor, target, isSelf);
}
// The menu body for one member. Call inside a BeginPopup; every item shown works.
void ModMenuItems(mm::Scope sc, long cid, mm::Role actor, const std::string& uid, const std::string& name, mm::Role target, bool isSelf) {
    const char* noun = mm::ScopeNoun(sc);
    if (mm::CanSetRole(actor, target, isSelf)) {
        const bool toAdmin = target != mm::Role::Admin;
        if (ImGui::MenuItem(toAdmin ? "Make admin" : "Remove admin"))
            ModAction(sc, cid, mm::SetRoleRequest(sc, cid, uid, toAdmin ? mm::Role::Admin : mm::Role::Member),
                      toAdmin ? name + " is now an admin" : name + " is no longer an admin");
    }
    if (mm::CanModerate(actor, target, isSelf)) {
        if (ImGui::MenuItem((std::string("Remove from ") + noun).c_str())) {
            g_modDlg = ModDialogState(); g_modDlg.openReq = true; g_modDlg.action = 0; g_modDlg.scope = sc; g_modDlg.cid = cid; g_modDlg.uid = uid; g_modDlg.name = name;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, okaytheme::Current().danger);
        if (ImGui::MenuItem("Ban\xE2\x80\xA6")) {
            g_modDlg = ModDialogState(); g_modDlg.openReq = true; g_modDlg.action = 1; g_modDlg.scope = sc; g_modDlg.cid = cid; g_modDlg.uid = uid; g_modDlg.name = name;
        }
        ImGui::PopStyleColor();
    }
}
// The "..." button at the right end of a member row; opens the menu. Returns the menu id state.
void ModRowButton(const char* popupId, ImVec2 topLeft, float size, bool hasActions) {
    if (!hasActions) return;
    ImGui::SetCursorScreenPos(topLeft);
    const bool clicked = ImGui::InvisibleButton("##modmore", ImVec2(size, size));
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* d = ImGui::GetWindowDrawList();
    if (hov) { d->AddRectFilled(topLeft, ImVec2(topLeft.x + size, topLeft.y + size), okaytheme::Ink(26), 6.0f); ImGui::SetTooltip("Manage member"); ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); }
    const ImU32 c = ImGui::GetColorU32(hov ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    for (int i = -1; i <= 1; ++i) d->AddCircleFilled(ImVec2(topLeft.x + size * 0.5f + i * 5.0f, topLeft.y + size * 0.5f), 1.7f, c, 8);
    if (clicked) ImGui::OpenPopup(popupId);
}
// Call once per screen at a fixed spot in the ID stack (not inside a member row).
void ModDialog() {
    ModDialogState& D = g_modDlg;
    if (D.openReq) { ImGui::OpenPopup("Member moderation##mm"); D.openReq = false; }
    ImGui::SetNextWindowSizeConstraints(ImVec2(340, 0), ImVec2(460, 400));
    if (!ImGui::BeginPopupModal("Member moderation##mm", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    const char* noun = mm::ScopeNoun(D.scope);
    const std::string nm = D.name.empty() ? std::string("this member") : D.name;
    if (D.action == 0) {
        ImGui::TextWrapped("Remove %s from this %s?", nm.c_str(), noun);
        ImGui::TextDisabled("They can join again with an invite. To stop that, ban them instead.");
    } else {
        ImGui::TextWrapped("Ban %s from this %s?", nm.c_str(), noun);
        ImGui::TextDisabled("They are removed now and can't rejoin, read or post until the ban ends or is lifted.");
        ImGui::Spacing();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##modreason", "Reason (optional, shown to them)", D.reason, 300);
        const auto& durs = mm::Durations();
        ImGui::SetNextItemWidth(-1);
        if (BeginSlateCombo("##moddur", durs[(std::size_t)std::max(0, std::min(D.dur, (int)durs.size() - 1))].label)) {
            for (int i = 0; i < (int)durs.size(); ++i) if (ImGui::Selectable(durs[(std::size_t)i].label, i == D.dur)) D.dur = i;
            ImGui::EndCombo();
        }
    }
    ImGui::Spacing();
    if (ImGui::Button(D.action == 0 ? "Remove" : "Ban", ImVec2(120, 0))) {
        if (D.action == 0) ModAction(D.scope, D.cid, mm::KickRequest(D.scope, D.cid, D.uid), "Removed " + nm);
        else ModAction(D.scope, D.cid, mm::BanRequest(D.scope, D.cid, D.uid, D.reason, mm::Durations()[(std::size_t)std::max(0, std::min(D.dur, (int)mm::Durations().size() - 1))].seconds), "Banned " + nm);
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(okl::FitButtonW("Cancel", okl::UiPx(120.0f)), 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}
// "Banned" list for owners and admins, with Unban. Fetches itself (and says plainly when it can't).
void ModDrawBanned(mm::Scope sc, long cid, mm::Role actor) {
    if (!mm::CanSeeBans(actor) || cid <= 0) return;
    ModBanList& b = ModBansFor(sc, cid);
    if (!b.inflight && okay::platform::TicksMs32() >= b.nextAt) ModFetchBans(sc, cid);
    if (b.loaded && b.problem.empty() && b.list.empty()) return;   // nothing to manage: no "BANNED - 0" section (owner 2026-11-25)
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::SetWindowFontScale(0.80f);
    ImGui::TextDisabled("  BANNED  \xE2\x80\x94  %d", (int)b.list.size());
    ImGui::SetWindowFontScale(1.0f);
    if (!b.problem.empty()) { ImGui::PushTextWrapPos(0); ImGui::TextColored(okaytheme::Current().danger, "%s", b.problem.c_str()); ImGui::PopTextWrapPos(); return; }
    if (!b.loaded) { ImGui::TextDisabled("Loading\xE2\x80\xA6"); return; }
    for (const auto& e : b.list) {
        ImGui::PushID(e.uid.c_str());
        const std::string nm = e.name.empty() ? std::string("(unknown)") : e.name;
        ImGui::TextUnformatted(nm.c_str());
        std::string sub = e.reason.empty() ? std::string("No reason given") : e.reason;
        if (e.until == 0) sub += "  \xC2\xB7  permanent";
        else { const long left = std::max(0L, e.until - (long)std::time(nullptr));
               sub += "  \xC2\xB7  ends in " + (left < 3600 ? std::to_string(std::max(1L, left / 60)) + "m"
                                                 : left < 86400 ? std::to_string(left / 3600) + "h" : std::to_string(left / 86400) + "d"); }
        ImGui::SameLine(ImGui::GetContentRegionMax().x - 56);
        if (ImGui::SmallButton("Unban")) ModAction(sc, cid, mm::UnbanRequest(sc, cid, e.uid), "Unbanned " + nm);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 260);
        ImGui::TextDisabled("%s", sub.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopID();
    }
}

} // namespace okl
