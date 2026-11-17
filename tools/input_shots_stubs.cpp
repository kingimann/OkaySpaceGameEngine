// Stand-ins so tools/input_shots links without the whole engine (the localization checks are never called by the input windows).
#include "okay/Core/LocCheck.hpp"
namespace okay {
std::set<std::string> LocPlaceholders(const std::string&) { return {}; }
bool LocHasLetters(const std::string&) { return false; }
}
