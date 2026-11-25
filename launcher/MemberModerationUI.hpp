#pragma once
// Member moderation for servers and group chats (MemberModerationUI.cpp). Moved out of launcher/main.cpp unchanged
// (docs/engine/modularization_plan.md, Part C section 13, batch L7).
#include "LauncherShared.hpp"
#include "MemberModeration.hpp"   // okay::modmember

namespace okl {

namespace mm = okay::modmember;

// What main.cpp and GroupConversation.cpp use from MemberModerationUI.cpp (call these on the main thread).
bool ModDrain(mm::Scope sc);                     // once per frame per screen: toast finished actions; true = members changed
bool ModHasActions(mm::Role actor, mm::Role target, bool isSelf);
void ModMenuItems(mm::Scope sc, long cid, mm::Role actor, const std::string& uid, const std::string& name, mm::Role target, bool isSelf);
void ModRowButton(const char* popupId, ImVec2 topLeft, float size, bool hasActions);   // the "..." button on a member row
void ModDialog();                                // the confirm dialog (once per screen)
void ModDrawBanned(mm::Scope sc, long cid, mm::Role actor);   // the "Banned" list with Unban
} // namespace okl
