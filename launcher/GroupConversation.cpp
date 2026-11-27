// The open group conversation (GroupConversation.hpp): header, members menu, the multi-sender thread and the composer.
// Moved out of launcher/main.cpp unchanged (docs/engine/modularization_plan.md, Part C section 13, batch L7).
#include "LauncherShared.hpp"
#include "GroupConversation.hpp"
#include "MemberModerationUI.hpp"
#include "okay/Platform/Messages.hpp"
#include "okay/Platform/Friends.hpp"
#include "okay/Platform/Account/AccountService.hpp"
#include "okay/Platform/Platform.hpp"
#include "OkayTheme.hpp"
#include "ChatText.hpp"          // RenderChatBody, emoji, reactions
#include "CoverArtAvatars.hpp"   // DrawAvatar, AvatarTexture
#include "AdminPage.hpp"         // OpenReport
#include "NavIcons.hpp"
#include "PainterBridge.hpp"     // okay::pb

namespace okl {

// Render the open group conversation (header + members menu + multi-sender
// thread + composer). Actions run on the main thread (same as DM sends); the
// background poller publishes glog/greacts. Kept a free function to keep the
// huge DrawChatWindow readable.
void DrawGroupConversation(
    okay::Messages& dm,
    const std::vector<okay::Messages::GroupConvo>& groups,
    const std::vector<okay::Messages::GroupMessage>& glog,
    const std::map<long, std::map<std::string,long>>& greacts,
    const std::map<long, std::set<std::string>>& gmyreacts,
    const std::map<std::string, const okay::Friend*>& fById,
    char* gInput, long& gEditId, long& gReplyTo,
    std::string& gReplyName, std::string& gReplySnippet,
    std::map<long,long>& gSeenId,
    const std::string& myName)
{
    const long gid = g_chatSelGroup;
    const okay::Messages::GroupConvo* gp = nullptr;
    for (const auto& gg : groups) if (gg.id == gid) gp = &gg;
    const std::string title = (gp && !gp->title.empty()) ? gp->title : "Group chat";
    const int memberCount = gp ? gp->members : 0;
    const std::string gkey = "g:" + std::to_string(gid);
    const ImVec4 myBubble = ChatThemeFor(gkey);   // this group's theme colour for my bubbles
    const Uint32 now = okay::platform::TicksMs32();
    const bool demo = ChatDemo();

    // Members of this group (names, avatars, read position) from the poller.
    const std::vector<okay::Messages::GroupMemberInfo> noMembers;
    const auto& members = g_groupMembersFor == gid ? g_groupMembers : noMembers;
    auto isMe = [&](const okay::Messages::GroupMemberInfo& mi) { return !myName.empty() && (mi.username == myName || mi.name == myName); };
    auto avatarOf = [&](const std::string& uid) -> std::string {
        if (auto it = fById.find(uid); it != fById.end()) return it->second->avatar;
        for (const auto& mi : members) if (mi.uid == uid) return mi.avatar;
        return std::string();
    };
    auto onlineOf = [&](const okay::Messages::GroupMemberInfo& mi) {
        if (auto it = fById.find(mi.uid); it != fById.end()) return it->second->online;
        return mi.lastSeen > 0 && (long)std::time(nullptr) - mi.lastSeen < 180;
    };

    // Per-group view state, reset when another group opens.
    static long sGid = 0;
    static bool showSearch = false; static char search[96] = "";
    static long unreadBaseline = 0;                 // newest id I'd seen before opening (-> "New messages")
    static bool openStarred = false, openMedia = false, openMembers = false;
    static long gScrollTo = 0, gFlashId = 0; static Uint32 gFlashUntil = 0, gScrollSince = 0; static int gHits = 0;
    static bool wantBottom = false, gRefocus = false;
    if (sGid != gid) {
        sGid = gid; showSearch = false; search[0] = '\0';
        unreadBaseline = std::max(gSeenId.count(gid) ? gSeenId[gid] : 0L, gp ? gp->lastRead : 0L);
        gScrollTo = 0; gRefocus = true;
    }
    auto jumpTo = [&](long id) { gScrollTo = gFlashId = id; gFlashUntil = now + 1800; gScrollSince = now; gHits = 0; };
    if (g_chatJumpGroup) { jumpTo(g_chatJumpGroup); g_chatJumpGroup = 0; }
    if (gScrollTo && now - gScrollSince > 4000) gScrollTo = 0;

    // ---- Header (the same as a DM's): group avatar, name, members / online,
    // then members, options and search on the right, and a hairline.
    const ImVec2 hdrPos = ImGui::GetCursorScreenPos();
    const float hdrW = ImGui::GetContentRegionAvail().x;
    {
        ImDrawList* hd = ImGui::GetWindowDrawList();
        const float av = 28.0f;
        ImVec4 nc = NameColor(title);
        hd->AddCircleFilled(ImVec2(hdrPos.x + 10, hdrPos.y + 10), 10.0f, ImGui::GetColorU32(kAccentDim), 20);
        hd->AddCircleFilled(ImVec2(hdrPos.x + 19, hdrPos.y + 18), 11.5f, ImGui::GetColorU32(kChatWinBg), 20);
        hd->AddCircleFilled(ImVec2(hdrPos.x + 19, hdrPos.y + 18), 10.0f, ImGui::GetColorU32(ImVec4(nc.x * 0.6f, nc.y * 0.6f, nc.z * 0.6f, 1.0f)), 20);
        char ini[2] = {title.empty() ? '#' : (char)std::toupper((unsigned char)title[0]), 0};
        const ImVec2 ts = ImGui::CalcTextSize(ini);
        hd->AddText(ImVec2(hdrPos.x + 19 - ts.x * 0.5f, hdrPos.y + 18 - ts.y * 0.5f), IM_COL32_WHITE, ini);
        ImGui::SetWindowFontScale(1.12f);
        const float nameH = ImGui::GetTextLineHeight();
        ImGui::SetCursorScreenPos(ImVec2(hdrPos.x + av + 10, hdrPos.y + (av - nameH) * 0.5f - 1));
        ImGui::TextUnformatted(FitText(title, std::max(60.0f, hdrW - av - 10 - 260)).c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::SameLine(0, 10);
        ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x, hdrPos.y + (av - ImGui::GetTextLineHeight()) * 0.5f));
        int online = 0;
        for (const auto& mi : members) if (!isMe(mi) && onlineOf(mi)) ++online;
        const int n = members.empty() ? memberCount : (int)members.size();
        char sub[64];
        if (online > 0) std::snprintf(sub, sizeof(sub), "%d members \xC2\xB7 %d online", n, online);
        else std::snprintf(sub, sizeof(sub), "%d member%s", n, n == 1 ? "" : "s");
        ImGui::PushStyleColor(ImGuiCol_Text, online > 0 ? okaytheme::Current().success : okaytheme::kTextDim);
        ImGui::TextUnformatted(sub);
        ImGui::PopStyleColor();
        ImVec2 c;
        if (ChatHeaderIconButton("##gmembers", ImVec2(hdrPos.x + hdrW - 96, hdrPos.y - 2), openMembers, "Members", &c)) openMembers = true;
        DrawNavIcon(hd, 6, c, ImGui::GetColorU32(ImGuiCol_TextDisabled));
        if (ChatHeaderIconButton("##gmenu", ImVec2(hdrPos.x + hdrW - 64, hdrPos.y - 2), ImGui::IsPopupOpen("##gmenu_pop"), "Group options", &c))
            ImGui::OpenPopup("##gmenu_pop");
        DrawDotsIcon(hd, c, ImGui::GetColorU32(ImGuiCol_TextDisabled));
        if (ChatHeaderIconButton("##gsrch", ImVec2(hdrPos.x + hdrW - 32, hdrPos.y - 2), showSearch,
                                 showSearch ? "Close search" : "Search in this group", &c)) { showSearch = !showSearch; if (!showSearch) search[0] = '\0'; }
        const ImU32 ic = ImGui::GetColorU32(showSearch ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        if (showSearch) {
            hd->AddLine(ImVec2(c.x - 4, c.y - 4), ImVec2(c.x + 4, c.y + 4), ic, 1.8f);
            hd->AddLine(ImVec2(c.x + 4, c.y - 4), ImVec2(c.x - 4, c.y + 4), ic, 1.8f);
        } else {
            hd->AddCircle(ImVec2(c.x - 2, c.y - 2), 5.0f, ic, 16, 1.8f);
            hd->AddLine(ImVec2(c.x + 2, c.y + 2), ImVec2(c.x + 6, c.y + 6), ic, 1.9f);
        }
    }
    static char renameBuf[64] = "";
    if (ImGui::BeginPopup("##gmenu_pop")) {
        if (ImGui::BeginMenu("Add member")) {
            int n = 0;
            for (const auto& kv : fById) {
                const okay::Friend* f = kv.second;
                if (!f || f->status != "accepted") continue;
                bool already = false; for (const auto& mi : members) if (mi.uid == f->id) already = true;
                if (already) continue;
                std::string nm = f->display.empty() ? f->username : f->display;
                if (ImGui::MenuItem((nm + "##gaddm" + f->id).c_str())) {
                    if (dm.AddGroupMember(gid, f->id)) { Toast("Added " + nm + " to the group", 1); g_chatForceLog = true; }
                    else Toast("Couldn't add " + nm + ": only the group owner can add members, and banned people can't be added until unbanned", 2);
                }
                ++n;
            }
            if (n == 0) ImGui::TextDisabled("All your friends are in this group.");
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Rename group")) {
            ImGui::SetNextItemWidth(180);
            ImGui::InputTextWithHint("##grn", "New name", renameBuf, sizeof(renameBuf));
            if (ImGui::Button("Save##grn") && renameBuf[0]) {
                if (dm.RenameGroup(gid, renameBuf)) { g_chatForceLog = true; Toast("Group renamed", 1); renameBuf[0] = '\0'; }
                else Toast("Only the group owner can rename it", 2);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        const bool pin = g_pinnedChats.count(gkey) != 0, mut = g_mutedChats.count(gkey) != 0;
        if (ImGui::MenuItem(pin ? "Unpin conversation" : "Pin conversation")) { if (pin) g_pinnedChats.erase(gkey); else g_pinnedChats.insert(gkey); SavePrefs(); }
        if (ImGui::MenuItem(mut ? "Unmute" : "Mute notifications")) { if (mut) g_mutedChats.erase(gkey); else g_mutedChats.insert(gkey); SavePrefs(); }
        if (ImGui::BeginMenu("Theme colour")) {
            const int cur = g_chatColor.count(gkey) ? g_chatColor[gkey] : 0;
            for (int ci = 0; ci < kChatThemeCount; ++ci) {
                const ImVec2 sp = ImGui::GetCursorScreenPos();
                if (ImGui::MenuItem((std::string("      ") + kChatThemeNames[ci] + "##gth" + std::to_string(ci)).c_str(), nullptr, cur == ci)) {
                    if (ci == 0) g_chatColor.erase(gkey); else g_chatColor[gkey] = ci;
                    SavePrefs();
                }
                ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(sp.x + 9, sp.y + ImGui::GetTextLineHeight() * 0.5f), 6.0f,
                                                            ImGui::GetColorU32(ChatThemeColor(ci)), 16);
            }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Members")) openMembers = true;
        if (ImGui::MenuItem("Starred messages")) openStarred = true;
        if (ImGui::MenuItem("Media & links")) openMedia = true;
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, okaytheme::Current().danger);
        if (ImGui::MenuItem("Leave group")) {
            if (dm.LeaveGroup(gid)) { g_chatSelGroup = 0; g_chatForceLog = true; Toast("You left the group", 1); }
        }
        ImGui::PopStyleColor();
        ImGui::EndPopup();
    }
    if (g_chatSelGroup == 0) return;   // left the group this frame

    // My role here (owner / admin / member) decides which moderation controls the members list shows.
    // Real groups only: the demo data has no server to answer.
    std::string gMyUid; for (const auto& mi : members) if (isMe(mi)) gMyUid = mi.uid;
    auto gRoleOf = [&](const okay::Messages::GroupMemberInfo& mi) {
        return (gp && !gp->owner.empty() && gp->owner == mi.uid) ? mm::Role::Owner : mm::ParseRole(mi.role); };
    mm::Role gActor = mm::Role::None;
    if (!demo && !gMyUid.empty()) for (const auto& mi : members) if (mi.uid == gMyUid) gActor = gRoleOf(mi);
    if (ModDrain(mm::Scope::Group)) g_chatForceLog = true;   // a kick / ban / role change went through: reload the members

    // Members list: who's in the group, online / last seen, the owner.
    if (openMembers) { ImGui::OpenPopup("##gmemberspop"); openMembers = false; }
    ImGui::SetNextWindowSizeConstraints(ImVec2(280, 0), ImVec2(380, 420));
    if (ImGui::BeginPopup("##gmemberspop")) {
        ImGui::TextDisabled("Members (%d)", members.empty() ? memberCount : (int)members.size());
        ImGui::Separator();
        if (members.empty()) ImGui::TextDisabled("Loading\xE2\x80\xA6");
        std::vector<const okay::Messages::GroupMemberInfo*> sorted;
        for (const auto& mi : members) sorted.push_back(&mi);
        std::stable_sort(sorted.begin(), sorted.end(), [&](const auto* a, const auto* b) {
            const bool ma = isMe(*a), mb2 = isMe(*b); if (ma != mb2) return ma;
            const bool oa = onlineOf(*a), ob = onlineOf(*b); if (oa != ob) return oa;
            return a->name < b->name; });
        for (const auto* mi : sorted) {
            ImGui::PushID(mi->uid.c_str());
            const ImVec2 rp = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(300, 34));
            ImDrawList* md = ImGui::GetWindowDrawList();
            ImGui::SetCursorScreenPos(ImVec2(rp.x, rp.y + 3));
            DrawAvatar(avatarOf(mi->uid), mi->name, 28.0f);
            if (onlineOf(*mi)) {
                md->AddCircleFilled(ImVec2(rp.x + 24, rp.y + 27), 5.5f, ImGui::GetColorU32(ImGuiCol_PopupBg), 12);
                md->AddCircleFilled(ImVec2(rp.x + 24, rp.y + 27), 4.0f, okaytheme::SuccessFillU32(), 12);
            }
            std::string nm = mi->name + (isMe(*mi) ? "  (you)" : "");
            md->AddText(ImVec2(rp.x + 38, rp.y + 2), ImGui::GetColorU32(ImGuiCol_Text), nm.c_str());
            std::string st = onlineOf(*mi) ? "Online" : mi->lastSeen > 978307200 ? "Last seen " + RelTime(mi->lastSeen) : "Offline";
            if (gp && gp->owner == mi->uid) st += "  \xC2\xB7  owner";
            else if (gRoleOf(*mi) == mm::Role::Admin) st += "  \xC2\xB7  admin";
            md->AddText(ImVec2(rp.x + 38, rp.y + 18), ImGui::GetColorU32(ImGuiCol_TextDisabled), st.c_str());
            {   // Manage: Make / remove admin, Remove, Ban (only what my role allows on this person)
                const bool selfRow = !gMyUid.empty() && mi->uid == gMyUid;
                const mm::Role tr = gRoleOf(*mi);
                ModRowButton("##gmmenu", ImVec2(rp.x + 300 - 26, rp.y + 6), 22.0f, ModHasActions(gActor, tr, selfRow));
                if (ImGui::BeginPopup("##gmmenu")) { ModMenuItems(mm::Scope::Group, gid, gActor, mi->uid, mi->name, tr, selfRow); ImGui::EndPopup(); }
            }
            ImGui::SetCursorScreenPos(ImVec2(rp.x, rp.y + 36));
            ImGui::Dummy(ImVec2(0, 0));
            ImGui::PopID();
        }
        if (!members.empty()) ModDrawBanned(mm::Scope::Group, gid, gActor);
        ModDialog();
        ImGui::EndPopup();
    }
    // Starred messages in this group: click to jump.
    if (openStarred) { ImGui::OpenPopup("##gstarredpop"); openStarred = false; }
    ImGui::SetNextWindowSizeConstraints(ImVec2(280, 0), ImVec2(420, 360));
    if (ImGui::BeginPopup("##gstarredpop")) {
        ImGui::TextDisabled("Starred messages");
        ImGui::Separator();
        long unstar = 0; int shownS = 0;
        for (const auto& kv : g_starred) {
            if (kv.second.uid != gkey) continue;
            ++shownS;
            ImGui::PushID((int)kv.first);
            const ImVec2 sp = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable(("      " + kv.second.preview).c_str(), false, 0, ImVec2(ImGui::GetContentRegionAvail().x - 26, 0))) {
                jumpTo(-kv.first); ImGui::CloseCurrentPopup();
            }
            DrawStar(ImGui::GetWindowDrawList(), ImVec2(sp.x + 8, sp.y + ImGui::GetTextLineHeight() * 0.5f), 6.0f, okaytheme::WarningFillU32());
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) unstar = kv.first;
            ImGui::PopID();
        }
        if (unstar) { g_starred.erase(unstar); SavePrefs(); }
        if (shownS == 0) ImGui::TextDisabled("No starred messages yet.\nHover a message > More > Star.");
        ImGui::EndPopup();
    }
    // Media & links shared in the group.
    if (openMedia) { ImGui::OpenPopup("##gmediapop"); openMedia = false; }
    ImGui::SetNextWindowSizeConstraints(ImVec2(360, 0), ImVec2(460, 420));
    if (ImGui::BeginPopup("##gmediapop")) {
        std::vector<std::string> imgs, links;
        for (auto it = glog.rbegin(); it != glog.rend(); ++it) {
            const std::string iu = FirstImageUrl(it->body);
            if (!iu.empty()) imgs.push_back(iu);
            std::size_t i = 0, n = it->body.size();
            while (i < n) {
                while (i < n && (it->body[i] == ' ' || it->body[i] == '\n')) ++i;
                std::size_t j = i; while (j < n && it->body[j] != ' ' && it->body[j] != '\n') ++j;
                std::string tok = it->body.substr(i, j - i); i = j;
                if ((tok.rfind("http://", 0) == 0 || tok.rfind("https://", 0) == 0) && tok != iu) links.push_back(tok);
            }
        }
        ImGui::TextDisabled("Media (%d)", (int)imgs.size());
        ImGui::Separator();
        if (imgs.empty()) ImGui::TextDisabled("No photos shared yet.");
        const float th = 76.0f;
        int col = 0; const int perRow = std::max(1, (int)((ImGui::GetContentRegionAvail().x + 6) / (th + 6)));
        for (std::size_t k = 0; k < imgs.size(); ++k) {
            ImGui::PushID((int)k);
            if (col > 0) ImGui::SameLine(0, 6);
            const ImVec2 tp = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##thumb", ImVec2(th, th))) { g_chatLightboxReq = imgs[k]; ImGui::CloseCurrentPopup(); }
            ImDrawList* md = ImGui::GetWindowDrawList();
            if (okay::ui2d::Tex tx = AvatarTexture(imgs[k]))
                md->AddImageRounded((ImTextureID)(intptr_t)tx, tp, ImVec2(tp.x + th, tp.y + th), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, 8.0f);
            else md->AddRectFilled(tp, ImVec2(tp.x + th, tp.y + th), okaytheme::U32(okaytheme::kField), 8.0f);
            ImGui::PopID();
            if (++col >= perRow) col = 0;
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Links (%d)", (int)links.size());
        ImGui::Separator();
        if (links.empty()) ImGui::TextDisabled("No links shared yet.");
        for (std::size_t k = 0; k < links.size(); ++k) {
            ImGui::PushID((int)(1000 + k));
            ImGui::PushStyleColor(ImGuiCol_Text, okaytheme::Current().info);
            if (ImGui::Selectable(ChatPreview(links[k], 52).c_str())) { OpenChatLink(links[k]); ImGui::CloseCurrentPopup(); }
            ImGui::PopStyleColor();
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    ImGui::SetCursorScreenPos(ImVec2(hdrPos.x, hdrPos.y + 34));
    {
        const ImVec2 lp = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine(lp, ImVec2(lp.x + hdrW, lp.y), okaytheme::Ink(16), 1.0f);
    }
    ImGui::Dummy(ImVec2(0, 6));
    if (showSearch) {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 10.0f);
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##gmsgsearch", "Find a message in this group", search, sizeof(search));
        ImGui::PopStyleVar();
    }
    {   // Pinned messages in this group (shared with everyone in it).
        std::vector<ChatPinRef> pins;
        for (const auto& gm : glog)
            if (auto pi = g_pinsGroup.find(gm.id); pi != g_pinsGroup.end())
                pins.push_back({gm.id, pi->second, ChatPreviewSmart(gm.body, 90), gm.mine ? std::string("You") : gm.senderName});
        long jump = 0;
        DrawPinnedBar(gkey.c_str(), pins, true, jump);
        if (jump) jumpTo(jump);
    }

    // Mark the newest message seen (clears the unread dot in the list).
    if (!glog.empty()) gSeenId[gid] = glog.back().id;

    // ---- Composer layout (computed first so the thread knows its height) ----
    // @mention autocomplete: the word being typed starts with '@' -> matching names.
    std::size_t mentionAt = std::string::npos;
    std::vector<std::string> mentionHits;
    {
        std::string cur = gInput;
        std::size_t sp = cur.find_last_of(" \n");
        std::size_t start = (sp == std::string::npos) ? 0 : sp + 1;
        if (start < cur.size() && cur[start] == '@') {
            mentionAt = start;
            std::string pre = cur.substr(start + 1);
            for (auto& ch : pre) ch = (char)std::tolower((unsigned char)ch);
            std::set<std::string> names;
            for (const auto& mi : members) if (!isMe(mi) && !mi.username.empty()) names.insert(mi.username);
            for (const auto& gm : glog) if (!gm.senderName.empty() && gm.senderName != myName) names.insert(gm.senderName);
            for (const auto& nm : names) {
                std::string low = nm; for (auto& ch : low) ch = (char)std::tolower((unsigned char)ch);
                if (low.compare(0, pre.size(), pre) == 0) mentionHits.push_back(nm);
                if (mentionHits.size() >= 6) break;
            }
        }
    }
    int clines = 1; for (const char* c = gInput; *c; ++c) if (*c == '\n') ++clines;
    if (clines > 4) clines = 4;
    const ImGuiStyle& cst = ImGui::GetStyle();
    float composerH = 4.0f + cst.ItemSpacing.y + (ImGui::GetTextLineHeight() * clines + 16.0f)
                    + cst.ItemSpacing.y * 2.0f + cst.WindowPadding.y + 4.0f
                    + (gReplyTo || gEditId ? ImGui::GetTextLineHeightWithSpacing() + 4.0f : 0.0f)
                    + (mentionHits.empty() ? 0.0f : ImGui::GetFrameHeightWithSpacing());
    for (const auto& sm : g_scheduled) if (sm.toGroup == gid) { composerH += ImGui::GetTextLineHeightWithSpacing() + 2.0f; break; }

    // ---- Thread ---------------------------------------------------------------
    std::map<long, const okay::Messages::GroupMessage*> byId;
    for (const auto& m : glog) byId[m.id] = &m;
    long lastMineId = 0; for (const auto& m : glog) if (m.mine) lastMineId = m.id;
    long firstUnread = 0;
    if (unreadBaseline > 0) for (const auto& m : glog) if (!m.mine && m.id > unreadBaseline) { firstUnread = m.id; break; }
    std::string filt = search; for (auto& ch : filt) ch = (char)std::tolower((unsigned char)ch);

    ImGui::BeginChild("##glog", ImVec2(0, ImGui::GetContentRegionAvail().y - composerH), false,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    const float avW = ImGui::GetContentRegionAvail().x;
    const float baseX = ImGui::GetCursorPosX();
    const float avatarW = 28.0f, avatarGap = 6.0f, padX = 14.0f, padY = 8.0f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    long prevDay = 0, prevWhen = 0; std::string prevSender; bool first = true;
    int matches = 0;
    for (const auto& m : glog) {
        const ChatFwd fwd = ParseChatFwd(m.body);
        const std::string& mb = fwd.ok ? fwd.body : m.body;
        if (!filt.empty()) {
            std::string low = m.body; for (auto& ch : low) ch = (char)std::tolower((unsigned char)ch);
            if (low.find(filt) == std::string::npos) continue;
            ++matches;
        }
        const long day = DayKey(m.when);
        if (day != 0 && day != prevDay) { ImGui::SetCursorPosX(baseX); ChatDateSeparator(m.when); prevDay = day; first = true; }
        if (firstUnread != 0 && m.id == firstUnread && filt.empty()) {   // "New messages" divider
            ImGui::SetCursorPosX(baseX); ImGui::Dummy(ImVec2(0, 4));
            const ImVec2 up = ImGui::GetCursorScreenPos(); const float uw = ImGui::GetContentRegionAvail().x;
            const ImU32 uc = ImGui::GetColorU32(kAccent);
            const char* ul = "New messages"; const float ulw = ImGui::CalcTextSize(ul).x;
            const float uy = up.y + ImGui::GetTextLineHeight() * 0.5f;
            dl->AddLine(ImVec2(up.x, uy), ImVec2(up.x + (uw - ulw) * 0.5f - 8, uy), uc, 1.0f);
            dl->AddLine(ImVec2(up.x + (uw + ulw) * 0.5f + 8, uy), ImVec2(up.x + uw, uy), uc, 1.0f);
            ImGui::SetCursorPosX(baseX + (uw - ulw) * 0.5f);
            ImGui::PushStyleColor(ImGuiCol_Text, uc); ImGui::TextUnformatted(ul); ImGui::PopStyleColor();
            ImGui::SetCursorPosX(baseX); ImGui::Dummy(ImVec2(0, 2));
            first = true;
        }
        const bool grouped = !first && prevSender == m.sender && (m.when - prevWhen) < 300 && m.when > 0;
        ImGui::Dummy(ImVec2(0, grouped ? 2.0f : 9.0f));

        int ecount = 0; const bool emojiOnly = IsEmojiOnly(mb, ecount);
        const float bigEm = ImGui::GetTextLineHeight() * 2.3f;
        float bubbleMax = std::min(avW * 0.72f, 460.0f); if (bubbleMax < 120) bubbleMax = (avW < 120 ? avW : 120);
        ChatFileInfo gfile; const bool gIsFile = ParseChatFile(mb, gfile);
        ChatVoiceInfo gvoice; const bool gIsVoice = !gIsFile && ParseChatVoice(mb, gvoice);
        ChatPollInfo gpoll; const bool gIsPoll = !gIsFile && !gIsVoice && ParseChatPoll(mb, gpoll);
        ChatInviteInfo ginv; const bool gIsInvite = !gIsFile && !gIsVoice && !gIsPoll && ParseChatInvite(mb, ginv);
        const float bodyW = gIsFile ? ChatFileCardWidth(gfile, bubbleMax - padX * 2)
                          : gIsVoice ? ChatVoiceWidth(bubbleMax - padX * 2)
                          : gIsInvite ? ChatInviteWidth(bubbleMax - padX * 2)
                          : gIsPoll ? ChatPollWidth(bubbleMax - padX * 2) : MeasureChatBodyWidth(mb, bubbleMax - padX * 2);
        const okay::Messages::GroupMessage* rep = (m.replyTo && byId.count(m.replyTo)) ? byId[m.replyTo] : nullptr;
        const std::string repName = rep ? (rep->mine ? std::string("You") : rep->senderName) : std::string();
        const std::string repText = rep ? ChatPreviewSmart(rep->body, 36) : std::string();
        const float repW = rep ? std::min(bubbleMax - padX * 2, std::max(ImGui::CalcTextSize(repText.c_str()).x, ImGui::CalcTextSize(repName.c_str()).x) + 8) : 0.0f;
        char tbuf[24] = ""; if (m.when > 0) { std::time_t tt = (std::time_t)m.when; std::tm* tm = std::localtime(&tt); if (tm) std::strftime(tbuf, sizeof(tbuf), "%H:%M", tm); }
        std::string meta = tbuf; if (m.edited) meta += " \xC2\xB7 edited";
        const float metaW = ImGui::CalcTextSize(meta.c_str()).x;
        const long starKey = -m.id;   // group ids are stored negated (DM ids share the map)
        const bool starred = g_starred.count(starKey) != 0;
        const bool bareEmoji = emojiOnly && !rep && !fwd.ok;
        std::string mediaUrl; bool isSticker = false;
        if (!rep && !fwd.ok && !gIsFile && !gIsVoice) {
            if (mb.rfind("[Sticker] ", 0) == 0) { mediaUrl = mb.substr(10); isSticker = true; }
            else {
                std::string t = mb;
                while (!t.empty() && (t.back() == ' ' || t.back() == '\n')) t.pop_back();
                while (!t.empty() && t.front() == ' ') t.erase(0, 1);
                if (!t.empty() && FirstImageUrl(t) == t) mediaUrl = t;
            }
        }
        const bool bareMedia = !mediaUrl.empty();
        const bool bare = bareEmoji || bareMedia;
        ImVec2 mediaSz(0, 0);
        if (bareMedia) {
            int iw = 0, ih = 0; ChatImageTexture(mediaUrl, &iw, &ih);
            const float mw = isSticker ? 150.0f : std::min(bubbleMax - padX * 2, 260.0f), mh = isSticker ? 150.0f : 220.0f;
            if (iw > 0 && ih > 0) { float k = std::min(mw / iw, mh / ih); if (!isSticker) k = std::min(k, 2.0f); mediaSz = ImVec2(iw * k, ih * k); }
            else mediaSz = ImVec2(mw, isSticker ? mw : mh * 0.7f);
        }
        const std::string imgUrl = (bare || gIsFile || gIsVoice) ? std::string() : FirstImageUrl(mb);
        const float fwdW = fwd.ok ? ImGui::CalcTextSize(("Forwarded from " + fwd.from).c_str()).x + 16.0f : 0.0f;
        float contentW = bareEmoji ? std::max((float)ecount * (bigEm + 3), metaW)
                       : bareMedia ? std::max(mediaSz.x, metaW)
                                   : std::max({bodyW + 2.0f, repW, metaW + (starred ? 14.0f : 0.0f), fwdW});
        if (!imgUrl.empty()) contentW = std::max(contentW, std::min(bubbleMax - padX * 2, 240.0f));
        const float useW = std::max(contentW + padX * 2, 46.0f);
        const float leftPad = m.mine ? 0.0f : (avatarW + avatarGap);
        float x0 = m.mine ? (avW - useW - 8.0f) : leftPad; if (x0 < leftPad) x0 = leftPad;

        // Sender name + avatar on the first message of someone else's run.
        if (!m.mine && !grouped) {
            if (!m.senderName.empty()) {
                ImGui::SetCursorPosX(baseX + leftPad + 2);
                ImGui::PushStyleColor(ImGuiCol_Text, NameColor(m.senderName));
                ImGui::TextUnformatted(m.senderName.c_str());
                ImGui::PopStyleColor();
            }
            const ImVec2 cp = ImGui::GetCursorScreenPos();
            const ImVec2 ac(cp.x + avatarW * 0.5f, cp.y + avatarW * 0.5f);
            if (okay::ui2d::Tex at = AvatarTexture(avatarOf(m.sender)))
                dl->AddImageRounded((ImTextureID)(intptr_t)at, cp, ImVec2(cp.x + avatarW, cp.y + avatarW), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, avatarW * 0.5f);
            else {
                const ImVec4 col = NameColor(m.senderName);
                dl->AddCircleFilled(ac, avatarW * 0.5f, ImGui::GetColorU32(ImVec4(col.x * 0.55f, col.y * 0.55f, col.z * 0.55f, 1)), 20);
                char init[2] = {m.senderName.empty() ? '?' : (char)std::toupper((unsigned char)m.senderName[0]), 0};
                const ImVec2 tsz = ImGui::CalcTextSize(init);
                dl->AddText(ImVec2(ac.x - tsz.x * 0.5f, ac.y - tsz.y * 0.5f), IM_COL32_WHITE, init);
            }
        }

        ImGui::PushID((int)m.id);
        ImGui::SetCursorPosX(baseX + x0);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, bare ? ImVec4(0, 0, 0, 0.0f) : (m.mine ? myBubble : okaytheme::kControl));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padX, padY));
        ImGui::BeginChild("##gbub", ImVec2(useW, 0),
                          ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ChatBubbleTextPush(m.mine && !bare);   // my bubble is a filled colour: its text is white in every theme
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
        ImDrawList* bdl = ImGui::GetWindowDrawList();
        if (rep) {   // quoted message (click to jump)
            ImGui::BeginGroup();
            const ImVec2 qp = ImGui::GetCursorScreenPos();
            bdl->AddRectFilled(ImVec2(qp.x - 4, qp.y), ImVec2(qp.x - 1, qp.y + ImGui::GetTextLineHeight() * 1.7f), ImGui::GetColorU32(kAccent), 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(okaytheme::WithAlpha(okaytheme::kText, 0.9f)));
            ImGui::TextUnformatted(repName.c_str());
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
            ImGui::TextUnformatted(repText.c_str());
            ImGui::PopStyleColor();
            ImGui::EndGroup();
            if (ImGui::IsItemHovered()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) jumpTo(rep->id);
            }
        }
        if (fwd.ok) ChatForwardedLabel(fwd.from);
        if (bareEmoji) {   // big, bubble-less emoji
            const std::string eb = NormalizeEmoji(mb);
            std::size_t bi = 0, bn = eb.size(); bool firstTok = true;
            while (bi < bn) {
                while (bi < bn && (eb[bi] == ' ' || eb[bi] == '\n')) ++bi;
                if (bi >= bn) break;
                std::size_t bj = bi; while (bj < bn && eb[bj] != ' ' && eb[bj] != '\n') ++bj;
                const std::string etok = eb.substr(bi, bj - bi); bi = bj;
                if (!firstTok) ImGui::SameLine(0, 3);
                const ImVec2 ep = ImGui::GetCursorScreenPos();
                DrawEmojiToken(bdl, ImVec2(ep.x + bigEm * 0.5f, ep.y + bigEm * 0.5f), bigEm, etok);
                ImGui::Dummy(ImVec2(bigEm, bigEm));
                firstTok = false;
            }
        } else if (bareMedia) {   // sticker / GIF / photo without a bubble
            const ImVec2 ip = ImGui::GetCursorScreenPos();
            const bool mclick = ImGui::InvisibleButton("##media", mediaSz);
            const bool mhov = ImGui::IsItemHovered();
            if (okay::ui2d::Tex mt = ChatImageTexture(mediaUrl))
                bdl->AddImageRounded((ImTextureID)(intptr_t)mt, ip, ImVec2(ip.x + mediaSz.x, ip.y + mediaSz.y),
                                     ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, isSticker ? 0.0f : 12.0f);
            else {
                bdl->AddRectFilled(ip, ImVec2(ip.x + mediaSz.x, ip.y + mediaSz.y), okaytheme::Ink(14), 12.0f);
                const float a0 = (float)ImGui::GetTime() * 6.0f;
                bdl->PathArcTo(ImVec2(ip.x + mediaSz.x * 0.5f, ip.y + mediaSz.y * 0.5f), 10.0f, a0, a0 + 4.6f, 20);
                bdl->PathStroke(okaytheme::Ink(120), 0, 2.0f);
            }
            if (!isSticker && mhov) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); if (mclick) g_chatLightboxReq = mediaUrl; }
        } else if (gIsFile) RenderChatFileCard(gfile, ImGui::GetContentRegionAvail().x);
        else if (gIsVoice) RenderChatVoice(gvoice, ImGui::GetContentRegionAvail().x, m.mine, myBubble);
        else if (gIsInvite) RenderChatInvite(ginv, m.mine, m.when, ImGui::GetContentRegionAvail().x);
        else if (gIsPoll) {
            auto rc = greacts.find(m.id); auto mr = gmyreacts.find(m.id);
            RenderChatPoll(gpoll, true, m.id, rc != greacts.end() ? &rc->second : nullptr,
                           mr != gmyreacts.end() ? &mr->second : nullptr, ImGui::GetContentRegionAvail().x, m.mine, myBubble);
        }
        else RenderChatBody(mb, ImGui::GetContentRegionAvail().x);
        if (!imgUrl.empty()) {   // inline preview of an image link (click: full size)
            if (okay::ui2d::Tex it = ChatImageTexture(imgUrl)) {
                int iw = 0, ih = 0; okay::pb::QueryTexture(g_renderer, it, &iw, &ih);
                const float availW = ImGui::GetContentRegionAvail().x;
                float w = (iw > 0 && iw > availW) ? availW : (float)iw; if (w < 1) w = availW;
                float h = (iw > 0) ? w * (float)ih / (float)iw : w * 0.6f;
                if (h > 220.0f) { w *= 220.0f / h; h = 220.0f; }
                ImGui::Dummy(ImVec2(0, 2));
                if (ImGui::ImageButton("##img", (ImTextureID)(intptr_t)it, ImVec2(w, h))) g_chatLightboxReq = imgUrl;
                if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }
        }
        {   // Timestamp tucked bottom-right (+ a star when saved).
            const float av2 = ImGui::GetContentRegionAvail().x, mw2 = ImGui::CalcTextSize(meta.c_str()).x;
            if (!bare && mw2 < av2) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (av2 - mw2));
            if (starred) {
                const ImVec2 sp = ImGui::GetCursorScreenPos();
                DrawStar(bdl, ImVec2(sp.x - 8.0f, sp.y + ImGui::GetTextLineHeight() * 0.5f), 5.0f, okaytheme::WarningFillU32());
            }
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(bare ? okaytheme::WithAlpha(okaytheme::kTextDim, 0.9f)
                                : (m.mine ? okaytheme::WithAlpha(okaytheme::Current().info, 0.6f) : okaytheme::WithAlpha(okaytheme::Current().info, 0.7f))));
        ImGui::TextUnformatted(meta.c_str());
        ImGui::PopStyleColor();
        if (m.edited && ImGui::IsItemHovered()) {   // "edited" -> earlier versions
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("Edited \xE2\x80\x94 click to see earlier versions");
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) OpenEditHistory(dm, true, m.id, m.body);
        }
        ImGui::PopStyleVar();
        ChatBubbleTextPop(m.mine && !bare);
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
        const ImVec2 bmin = ImGui::GetItemRectMin(), bmax = ImGui::GetItemRectMax();
        if (gScrollTo == m.id) { ImGui::SetScrollHereY(0.4f); if (++gHits >= 5) { gScrollTo = 0; gHits = 0; } }
        if (gFlashId == m.id && now < gFlashUntil) {
            const float al = 0.55f * ((gFlashUntil - now) / 1800.0f);
            dl->AddRect(ImVec2(bmin.x - 3, bmin.y - 3), ImVec2(bmax.x + 3, bmax.y + 3),
                        ImGui::GetColorU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, al)), 14.0f, 0, 2.5f);
        }
        if (!m.mine && !myName.empty()) {   // a message that @mentions you gets an accent outline
            std::string lb = mb, tag = "@" + myName;
            for (auto& ch : lb)  ch = (char)std::tolower((unsigned char)ch);
            for (auto& ch : tag) ch = (char)std::tolower((unsigned char)ch);
            if (lb.find(tag) != std::string::npos)
                dl->AddRect(ImVec2(bmin.x - 1.5f, bmin.y - 1.5f), ImVec2(bmax.x + 1.5f, bmax.y + 1.5f), ImGui::GetColorU32(kAccent), 15.0f, 0, 2.0f);
        }

        // Right-click (or the toolbar's "more"): everything you can do with it.
        auto myit = gmyreacts.find(m.id);
        auto startReply = [&]() { gReplyTo = m.id; gReplyName = m.mine ? "yourself" : m.senderName; gReplySnippet = ChatPreviewSmart(mb, 40); gEditId = 0; gRefocus = true; };
        auto startEdit = [&]() { gEditId = m.id; gReplyTo = 0; std::snprintf(gInput, 1024, "%s", m.body.c_str()); gRefocus = true; };
        if (ImGui::BeginPopupContextItem("##gmsgctx")) {
            if (ImGui::BeginMenu("React")) {
                for (const auto& rdef : kReactions) {
                    const bool have = myit != gmyreacts.end() && myit->second.count(rdef.key);
                    const ImVec2 mp = ImGui::GetCursorScreenPos();
                    if (ImGui::MenuItem((std::string("     ") + (have ? "remove" : "add") + "##grx" + rdef.key).c_str()))
                        { dm.GroupReact(m.id, rdef.key, !have); g_chatForceLog = true; }
                    DrawEmoji(ImGui::GetWindowDrawList(), ImVec2(mp.x + 9, mp.y + ImGui::GetTextLineHeight() * 0.5f), 18, rdef.id);
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Reply")) startReply();
            if (ImGui::MenuItem("Copy text")) ImGui::SetClipboardText(mb.c_str());
            if (ImGui::MenuItem("Forward\xE2\x80\xA6")) ChatForward(m.body, m.mine ? myName : m.senderName);
            if (!m.mine && ImGui::MenuItem("Report message\xE2\x80\xA6")) OpenReport(okayrep::Target::GroupMessage, std::to_string(m.id), "a message from " + m.senderName);
            if (ImGui::MenuItem(ChatIsPinned(true, m.id) ? "Unpin message" : "Pin message")) ChatPinToggle(true, m.id);
            if (ImGui::MenuItem(starred ? "Unstar" : "Star message")) {
                if (starred) g_starred.erase(starKey);
                else {
                    StarredMsg sm; sm.uid = gkey; sm.when = m.when; sm.preview = ChatPreviewSmart(mb, 60);
                    for (auto& ch : sm.preview) if (ch == '\n' || ch == '\r') ch = ' ';
                    g_starred[starKey] = sm;
                }
                SavePrefs();
            }
            if (m.edited && ImGui::MenuItem("Edit history")) OpenEditHistory(dm, true, m.id, m.body);
            if (m.mine) {
                ImGui::Separator();
                if (ImGui::MenuItem("Edit")) startEdit();
                if (ImGui::MenuItem("Delete")) { if (dm.DeleteGroup(m.id)) g_chatForceLog = true; }
            }
            ImGui::EndPopup();
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {   // double-click: heart
            const bool have = myit != gmyreacts.end() && myit->second.count("heart");
            dm.GroupReact(m.id, "heart", !have); g_chatForceLog = true;
        }
        // Hover toolbar, the same as DMs and server channels.
        if (!ImGui::IsPopupOpen("##gmsgctx")) {
            const bool liked = myit != gmyreacts.end() && myit->second.count("heart");
            ChatTbBtn btns[4]; int nb = 0;
            btns[nb++] = {'L', liked ? "Unlike" : "Like  (double-click)"};
            btns[nb++] = {'R', "Reply"};
            if (m.mine) btns[nb++] = {'E', "Edit"};
            btns[nb++] = {'M', "More"};
            const int hit = ChatMsgToolbar(-m.id, bmin, bmax, btns, nb, liked);   // negative: never clashes with a DM id
            if (hit >= 0) switch (btns[hit].icon) {
                case 'L': dm.GroupReact(m.id, "heart", !liked); g_chatForceLog = true; break;
                case 'R': startReply(); break;
                case 'E': startEdit(); break;
                default:  ImGui::OpenPopup("##gmsgctx"); break;
            }
        }
        // Reaction pills under the bubble (tap to toggle).
        auto cit = greacts.find(m.id);
        if (cit != greacts.end() && HasVisibleReactions(cit->second)) {
            ImGui::SetCursorPosX(baseX + x0);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 2));
            bool firstPill = true;
            for (const auto& kv : cit->second) {
                if (kv.second <= 0 || IsPollReaction(kv.first)) continue;
                if (!firstPill) ImGui::SameLine();
                firstPill = false;
                const bool mineR = myit != gmyreacts.end() && myit->second.count(kv.first);
                const std::string cnt = std::to_string(kv.second);
                if (ReactionPill(dl, kv.first, cnt, mineR)) { dm.GroupReact(m.id, kv.first, !mineR); g_chatForceLog = true; }
            }
            ImGui::PopStyleVar();
        }
        ImGui::PopID();
        ImGui::SetCursorPosX(baseX);
        prevSender = m.sender; prevWhen = m.when; first = false;
    }
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    const int pendingOut = DrawOutbox(std::string(), gid, avW, baseX, myBubble, [&](const std::string& body, long when) {
        for (auto it = glog.rbegin(); it != glog.rend(); ++it)
            if (it->mine && it->body == body && it->when >= when - 120) return true;
        return false;
    });
    // "Seen by" under the newest message (from each member's last_read).
    if (!glog.empty() && filt.empty() && pendingOut == 0 && !members.empty()) {
        const long lastId = glog.back().id;
        std::vector<const okay::Messages::GroupMemberInfo*> seen; int others = 0;
        for (const auto& mi : members) {
            if (isMe(mi) || mi.uid == glog.back().sender) continue;
            ++others;
            if (mi.lastRead >= lastId) seen.push_back(&mi);
        }
        if (others > 0 && (glog.back().mine || !seen.empty())) {
            std::string label;
            if (seen.empty()) label = "Sent";
            else if ((int)seen.size() == others) label = others == 1 ? "Seen by " + seen[0]->name : "Seen by everyone";
            else {
                label = "Seen by ";
                for (std::size_t k = 0; k < seen.size() && k < 3; ++k) label += (k ? ", " : "") + seen[k]->name;
                if (seen.size() > 3) label += " +" + std::to_string(seen.size() - 3);
            }
            const float faces = std::min<std::size_t>(seen.size(), 5) * 13.0f + (seen.empty() ? 0.0f : 6.0f);
            const float lw = ImGui::CalcTextSize(label.c_str()).x;
            const float x = glog.back().mine ? baseX + avW - lw - faces - 10.0f : baseX + avatarW + avatarGap;
            ImGui::SetCursorPosX(x);
            const ImVec2 sp = ImGui::GetCursorScreenPos();
            for (std::size_t k = 0; k < seen.size() && k < 5; ++k) {   // little overlapping faces
                ImGui::SetCursorScreenPos(ImVec2(sp.x + k * 13.0f, sp.y + 1));
                DrawAvatar(seen[k]->avatar, seen[k]->name, 16.0f);
            }
            ImGui::SetCursorScreenPos(ImVec2(sp.x + faces, sp.y));
            ImGui::TextColored(seen.empty() ? okaytheme::WithAlpha(okaytheme::kTextDim, 0.9f) : okaytheme::WithAlpha(okaytheme::Current().info, 0.95f), "%s", label.c_str());
            if (ImGui::IsItemHovered() && !seen.empty()) {
                std::string all; for (const auto* s2 : seen) all += (all.empty() ? "" : "\n") + s2->name;
                ImGui::SetTooltip("%s", all.c_str());
            }
        }
    }
    if (glog.empty()) ImGui::TextDisabled("No messages yet \xE2\x80\x94 say hello!");
    else if (!filt.empty() && matches == 0) ImGui::TextDisabled("No messages match \"%s\".", search);
    // Stay at the newest message on open / a new message (unless you scrolled up).
    const bool scrolledUp = ImGui::GetScrollMaxY() > 1.0f && (ImGui::GetScrollMaxY() - ImGui::GetScrollY()) > 80.0f;
    static int gStick = 0; static long gLastGid = 0; static size_t gLastN = 0;
    if (wantBottom) { wantBottom = false; gStick = 4; }
    else if (gid != gLastGid || glog.size() != gLastN) {
        if (filt.empty() && (gid != gLastGid || !scrolledUp)) gStick = 4;
        gLastGid = gid; gLastN = glog.size();
    }
    {   // content grew while at the bottom (an image loaded): stay there
        static float prevMaxY = 0.0f, prevY = 0.0f;
        const float maxY = ImGui::GetScrollMaxY();
        if (maxY > prevMaxY + 0.5f && prevY >= prevMaxY - 4.0f && filt.empty()) gStick = std::max(gStick, 2);
        prevMaxY = maxY; prevY = ImGui::GetScrollY();
    }
    if (gScrollTo) gStick = 0;   // a jump to a message wins over "stay at the bottom"
    if (gStick > 0) { ImGui::SetScrollHereY(1.0f); --gStick; }
    if (scrolledUp && gStick == 0) {   // floating "jump to latest" button
        const ImVec2 keep = ImGui::GetCursorPos();
        const ImVec2 wp = ImGui::GetWindowPos(), wsz = ImGui::GetWindowSize();
        ImGui::SetCursorScreenPos(ImVec2(wp.x + wsz.x - ImGui::GetStyle().ScrollbarSize - 48, wp.y + wsz.y - 46));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##gjumpbtn", ImVec2(36, 36), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
        const ImVec2 jp = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##gjump", ImVec2(34, 34))) wantBottom = true;
        const bool jh = ImGui::IsItemHovered();
        ImDrawList* jd = ImGui::GetWindowDrawList();
        const ImVec2 jc(jp.x + 17, jp.y + 17);
        jd->AddCircleFilled(jc, 16.0f, ImGui::GetColorU32(jh ? okaytheme::kTextDim : okaytheme::WithAlpha(okaytheme::kControl, 0.97f)), 24);
        jd->AddCircle(jc, 16.0f, okaytheme::Ink(34), 24, 1.0f);
        jd->AddLine(ImVec2(jc.x - 5.5f, jc.y - 2.5f), ImVec2(jc.x, jc.y + 3.0f), okaytheme::TextU32(), 2.0f);
        jd->AddLine(ImVec2(jc.x, jc.y + 3.0f), ImVec2(jc.x + 5.5f, jc.y - 2.5f), okaytheme::TextU32(), 2.0f);
        if (jh) ImGui::SetTooltip("Jump to latest");
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::SetCursorPos(keep);
        ImGui::Dummy(ImVec2(0, 0));
    }
    ImGui::EndChild();

    // ---- Composer: reply/edit banner, mentions, emoji, +, message box, send ----
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {   // Esc cancels, Up edits your last message
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            if (gReplyTo) gReplyTo = 0;
            else if (gEditId) { gEditId = 0; gInput[0] = '\0'; }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && gInput[0] == '\0' && !gEditId && lastMineId > 0)
            if (auto it = byId.find(lastMineId); it != byId.end()) { gEditId = lastMineId; gReplyTo = 0; std::snprintf(gInput, 1024, "%s", it->second->body.c_str()); }
    }
    DrawScheduledStrip(std::string(), gid);
    DrawScheduleDialog("Schedule message", gInput, 1024, std::string(), title, gid);
    if (gReplyTo) {
        ImGui::TextDisabled("Replying to"); ImGui::SameLine(0, 4);
        ImGui::TextColored(kAccent, "@%s", gReplyName.c_str());
        ImGui::SameLine(0, 6); ImGui::TextDisabled("%s", gReplySnippet.c_str());
        ImGui::SameLine(0, 8); if (ImGui::SmallButton("cancel##gcancelreply")) gReplyTo = 0;
    } else if (gEditId) {
        ImGui::TextDisabled("Editing a message"); ImGui::SameLine();
        if (ImGui::SmallButton("cancel edit##gcanceledit")) { gEditId = 0; gInput[0] = '\0'; }
    }
    if (!mentionHits.empty()) {   // suggestion chips: click to complete the @name
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Mention");
        for (const auto& nm : mentionHits) {
            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, okaytheme::Current().info);
            if (ImGui::SmallButton(("@" + nm + "##mh").c_str())) {
                std::string cur = gInput;
                cur = cur.substr(0, mentionAt) + "@" + nm + " ";
                std::snprintf(gInput, 1024, "%s", cur.c_str());
                gRefocus = true;
            }
            ImGui::PopStyleColor();
        }
    }
    ImGui::Dummy(ImVec2(0, 4));
    const float inH = ImGui::GetTextLineHeight() * clines + 16;
    {   // Emoji / GIFs / stickers (GIFs + stickers send straight away)
        const std::string media = ChatMediaPicker("gmedia", gInput, 1024, nullptr, true);
        if (!media.empty() && !demo) { OutboxSend(std::string(), gid, media); wantBottom = true; }
        ImGui::SameLine(0, 6);
    }
    ComposerPlusButton("gplus", ImGui::GetTextLineHeight() + 16.0f, std::string(), gid);   // photo/file, poll, schedule
    ImGui::SameLine(0, 6);
    if (VoiceRecording() && !VoiceRecordingFor(std::string(), gid)) FinishVoiceRecord(false);   // switched chats
    if (VoiceRecordingFor(std::string(), gid)) { DrawVoiceRecorderBar(inH, myBubble); return; }
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(11, 8));
    if (gRefocus) { ImGui::SetKeyboardFocusHere(); gRefocus = false; }
    const bool send = ImGui::InputTextMultiline("##ginput", gInput, 1024, ImVec2(ImGui::GetContentRegionAvail().x - 70, inH),
                                                ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine);
    if (!gInput[0] && !ImGui::IsItemActive()) {   // placeholder (multiline inputs have no hint)
        const std::string hint = g_attBusy ? "Uploading " + ChatPreview(g_attName, 28) + "\xE2\x80\xA6" : "Message " + title + "  (@ to mention)";
        const ImVec2 ip = ImGui::GetItemRectMin();
        ImGui::GetWindowDrawList()->AddText(ImVec2(ip.x + 11, ip.y + 8), ImGui::GetColorU32(ImGuiCol_TextDisabled), hint.c_str());
    }
    ImGui::PopStyleVar(2);
    ImGui::SameLine(0, 8);
    if (!gInput[0] && !gEditId && !g_attBusy) {   // empty composer -> mic (WhatsApp)
        DrawVoiceMicButton("##gmic", ImVec2(62, inH), myBubble, std::string(), gid);
        return;
    }
    auto brighten = [](ImVec4 c, float k) { return ImVec4(std::min(1.0f, c.x * k), std::min(1.0f, c.y * k), std::min(1.0f, c.z * k), 1.0f); };
    ImGui::PushStyleColor(ImGuiCol_Button, brighten(myBubble, 1.25f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, brighten(myBubble, 1.45f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
    const bool sendBtn = ImGui::Button(gEditId ? "Save##g" : "Send##g", ImVec2(62, inH));
    if (!gEditId && ImGui::BeginPopupContextItem("##gsendctx")) {   // right-click Send
        if (ImGui::MenuItem("Schedule message\xE2\x80\xA6")) g_schedOpenReq = true;
        ImGui::EndPopup();
    }
    if (!gEditId && ImGui::IsItemHovered() && !ImGui::IsPopupOpen("##gsendctx")) ImGui::SetTooltip("Send  (right-click to schedule)");
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    if ((send || sendBtn) && gInput[0]) {
        if (gEditId) {
            std::string ob; if (auto it = byId.find(gEditId); it != byId.end()) ob = it->second->body;
            if (demo || ChatSaveEdit(dm, true, gEditId, ob, gInput)) { gEditId = 0; gInput[0] = '\0'; g_chatForceLog = true; }
            else Toast("Couldn't save the edit", 2);
        } else {
            if (!demo && !ChatSlashCommand(gInput, std::string(), "the group", gid)) OutboxSend(std::string(), gid, ExpandCustomEmoji(gInput), gReplyTo);
            gInput[0] = '\0'; gReplyTo = 0; wantBottom = true;
        }
        gRefocus = true;   // keep typing without clicking back into the box
    }
}

} // namespace okl
