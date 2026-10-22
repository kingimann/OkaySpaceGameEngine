#pragma once
// Actions component -> node graph. The editor has one graph editor; an older
// Actions (ActionList) component is turned into an .okayvs graph that does the
// same thing: one event chain per trigger, Condition nodes for its gate, and its
// steps as Action nodes (or one Actions block when they use Wait / If / loops,
// which keeps their exact timing).
#include <string>

namespace okay {
class ActionList;
namespace vs {

/// OkayVS text for a graph equivalent to `al` (with node positions laid out).
std::string ActionListToGraph(const ActionList& al);

/// The event node type + argument a trigger becomes ("OnKey" + "e", "OnEvent" + "Hit").
void ActionTriggerToEvent(int trigger, const std::string& key, std::string& type, std::string& arg);

} // namespace vs
} // namespace okay
