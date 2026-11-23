#pragma once
// The open group conversation (GroupConversation.cpp). Moved out of launcher/main.cpp unchanged
// (docs/engine/modularization_plan.md, Part C section 13, batch L7).
#include "LauncherShared.hpp"
#include "okay/Platform/Friends.hpp"
#include "okay/Platform/Messages.hpp"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace okl {

// The open group conversation (header + members menu + multi-sender thread + composer), drawn inside DrawChatWindow.
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
    const std::string& myName);
} // namespace okl
