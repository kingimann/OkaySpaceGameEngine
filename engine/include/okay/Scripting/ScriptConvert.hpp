#pragma once
#include <string>
#include <vector>

namespace okay {

/// What converting an old OkayScript script to C# gave (ConvertOkayScriptToCSharp).
struct OkayScriptConversion {
    bool ok = false;                  ///< the old script parsed and a C# file was made
    std::string csharp;               ///< the C# file (a class extending MonoBehaviour)
    std::string error;                ///< why nothing was made (the old script doesn't parse)
    /// Things to look at, in plain words: guessed types, built-ins that still run through
    /// the old language (OkayScript.Call), parts that couldn't be converted.
    std::vector<std::string> notes;
    /// The old built-ins the C# still calls by name, because the C# binding table has no
    /// equivalent yet (each is a binding to write; then the converter maps it).
    std::vector<std::string> bridged;
};

/// Turn an old OkayScript script into a C# file. Works over the old VM's own parser
/// output (its syntax tree), never by text replacement: globals become typed public
/// fields (the type worked out from how each is used), start / update become
/// Start / Update, functions become public methods (UI events still find them by
/// name), and each built-in either maps to its C# binding or is called through
/// OkayScript.Call so the script keeps doing exactly what it did. The original is not
/// changed. `className` names the class (the file name, usually).
OkayScriptConversion ConvertOkayScriptToCSharp(const std::string& source, const std::string& className);

} // namespace okay
