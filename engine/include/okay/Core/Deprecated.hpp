#pragma once
// OKAY_DEPRECATED("use X instead"): marks a function, class or variable that a dedup has replaced.
//
//   OKAY_DEPRECATED("use okay::StringId::HashOf (Core/StringId.hpp)")
//   std::uint32_t HashName(const std::string& s);
//
// The compiler warns at every remaining use (the standard C++14 [[deprecated]] attribute), and
// scripts/dup_lint.py check fails a branch that ADDS a new call to a name marked this way, in any file.
// Delete the old function one release after its last caller moved (docs/process/no_duplicates.md).
// Written from the C++14 standard's description of [[deprecated]]; no third-party code.

#define OKAY_DEPRECATED(message) [[deprecated(message)]]
