#pragma once
// ---------------------------------------------------------------------------
// EXPERIMENTAL Xbox One Developer Mode export: AppxManifest.xml for a UWP package.
//
// Pure text in, text out; tested on Linux. Written from Microsoft's public package manifest
// schema documentation (Package, Identity, Properties, Dependencies/TargetDeviceFamily,
// Applications/Application, uap:VisualElements, Capabilities) and the Identity name rules
// (3 to 50 characters, letters, digits, '.' and '-', no reserved device names).
//
// UNVERIFIED: no package made from this manifest has been built with MakeAppx, installed or
// run. The names and the "Windows.Universal" device family come from the documentation; whether
// an Xbox One in Developer Mode accepts the result is not known.
// ---------------------------------------------------------------------------
#include <cctype>
#include <string>
#include <vector>

namespace okay::xbox {

struct ManifestOptions {
    std::string identityName   = "OkaySpace.Game";          // Package/Identity/@Name
    std::string publisher      = "CN=OkaySpaceDev";          // Identity/@Publisher: must equal the signing certificate's subject
    std::string version        = "1.0.0.0";                  // four numbers, each 0..65535
    std::string displayName    = "OkaySpace Game";
    std::string publisherDisplayName = "OkaySpace Developer";
    std::string description    = "An OkaySpace game (experimental Xbox Developer Mode package)";
    std::string executable     = "OkaySpaceXbox.exe";        // the UWP app's exe inside the package
    std::string applicationId  = "App";
    std::string entryPoint     = "OkaySpaceXbox.App";
    std::string minVersion     = "10.0.17763.0";             // TargetDeviceFamily MinVersion
    std::string maxVersionTested = "10.0.19041.0";
    std::string backgroundColor = "#101018";                 // tile background (a color name or #RRGGBB)
    std::vector<std::string> capabilities = {"internetClient"};
    bool vclibsDependency = true;                            // needs the Microsoft.VCLibs.140.00 framework package at install time
};

inline std::string XmlEscape(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            case '\'': o += "&apos;"; break;
            default:  o += c;
        }
    }
    return o;
}

namespace detail {
inline bool HasControl(const std::string& s) {
    for (unsigned char c : s) if (c < 0x20 || c == 0x7f) return true;
    return false;
}
inline std::string Upper(std::string s) { for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return s; }
inline bool ReservedDeviceName(const std::string& name) {
    static const char* kNames[] = {"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7", "COM8", "COM9",
                                   "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    const std::string u = Upper(name);
    for (const char* n : kNames) if (u == n) return true;
    return false;
}
} // namespace detail

// "1.2.3" -> "1.2.3.0", "1.2" -> "1.2.0.0". Anything that is not 1 to 4 numbers in range gives "" (a problem).
inline std::string NormalizeVersion(const std::string& v) {
    std::vector<long> parts;
    std::size_t i = 0;
    if (v.empty()) return "";
    while (true) {
        std::size_t j = i;
        while (j < v.size() && v[j] >= '0' && v[j] <= '9') ++j;
        if (j == i || j - i > 5) return "";
        const long n = std::stol(v.substr(i, j - i));
        if (n > 65535) return "";
        parts.push_back(n);
        if (j == v.size()) break;
        if (v[j] != '.') return "";
        i = j + 1;
    }
    if (parts.size() > 4) return "";
    while (parts.size() < 4) parts.push_back(0);
    return std::to_string(parts[0]) + "." + std::to_string(parts[1]) + "." + std::to_string(parts[2]) + "." + std::to_string(parts[3]);
}

// A publisher is a distinguished name such as CN=Name, O=Org, C=US. It has to match the signing certificate's subject
// exactly, so we only accept a plain "KEY=value, KEY=value" list that has a CN (no quoting tricks).
inline bool PublisherLooksValid(const std::string& p) {
    if (p.empty() || p.size() > 8192 || detail::HasControl(p)) return false;
    bool haveCn = false;
    std::size_t i = 0;
    while (i < p.size()) {
        while (i < p.size() && p[i] == ' ') ++i;
        std::size_t eq = p.find('=', i);
        if (eq == std::string::npos || eq == i) return false;
        std::string key = p.substr(i, eq - i);
        for (char c : key) if (!std::isalpha(static_cast<unsigned char>(c))) return false;
        std::size_t end = p.find(',', eq + 1);
        if (end == std::string::npos) end = p.size();
        std::string val = p.substr(eq + 1, end - eq - 1);
        while (!val.empty() && val.back() == ' ') val.pop_back();
        if (val.empty() || val.find('"') != std::string::npos || val.find('\\') != std::string::npos ||
            val.find('<') != std::string::npos || val.find('>') != std::string::npos || val.find(';') != std::string::npos ||
            val.find('+') != std::string::npos) return false;
        if (detail::Upper(key) == "CN") haveCn = true;
        i = end + 1;
    }
    return haveCn;
}

// One problem per line. Empty means the options are usable. Warnings go to `warnings` if given.
inline std::vector<std::string> ValidateManifest(const ManifestOptions& o, std::vector<std::string>* warnings = nullptr) {
    std::vector<std::string> bad;
    // Identity name
    {
        const std::string& n = o.identityName;
        bool ok = n.size() >= 3 && n.size() <= 50;
        for (char c : n) if (!(std::isalnum(static_cast<unsigned char>(c)) && static_cast<unsigned char>(c) < 128) && c != '.' && c != '-') ok = false;
        if (!n.empty() && (n.front() == '.' || n.back() == '.')) ok = false;
        if (ok && detail::ReservedDeviceName(n)) ok = false;
        if (!ok) bad.push_back("Package name \"" + n + "\" is not allowed: use 3 to 50 letters, digits, '.' or '-', not starting or ending with '.', and not a reserved device name such as CON.");
    }
    if (NormalizeVersion(o.version).empty()) bad.push_back("Version \"" + o.version + "\" is not allowed: use up to four numbers, each 0 to 65535, such as 1.2.3.0.");
    else if (warnings) {
        const std::string nv = NormalizeVersion(o.version);
        if (nv.size() < 2 || nv.substr(nv.size() - 2) != ".0")
            warnings->push_back("The last version number is not 0. The Microsoft Store reserves it; a sideloaded test package does not care.");
    }
    if (!PublisherLooksValid(o.publisher)) bad.push_back("Publisher \"" + o.publisher + "\" is not allowed: use a name such as CN=MyStudio (it must match the test certificate's subject).");
    auto textOk = [&](const std::string& s, std::size_t max) { return !s.empty() && s.size() <= max && !detail::HasControl(s); };
    if (!textOk(o.displayName, 256)) bad.push_back("Display name must be 1 to 256 characters.");
    if (!textOk(o.publisherDisplayName, 256)) bad.push_back("Publisher display name must be 1 to 256 characters.");
    if (o.description.size() > 2048 || detail::HasControl(o.description)) bad.push_back("Description must be at most 2048 characters.");
    // Executable: a plain file name ending in .exe
    {
        const std::string& e = o.executable;
        bool ok = e.size() > 4 && e.size() <= 256 && e.substr(e.size() - 4) == ".exe";
        for (char c : e) if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' || static_cast<unsigned char>(c) < 0x20) ok = false;
        if (!ok) bad.push_back("Executable \"" + e + "\" must be a plain file name ending in .exe.");
    }
    {   // Application Id: starts with a letter; letters, digits, '.' and '_'; at most 64
        const std::string& a = o.applicationId;
        bool ok = !a.empty() && a.size() <= 64 && std::isalpha(static_cast<unsigned char>(a[0])) && static_cast<unsigned char>(a[0]) < 128;
        for (char c : a) if (!(std::isalnum(static_cast<unsigned char>(c)) && static_cast<unsigned char>(c) < 128) && c != '.' && c != '_') ok = false;
        if (!ok) bad.push_back("Application id \"" + a + "\" is not allowed.");
    }
    if (o.entryPoint.empty() || o.entryPoint.size() > 256 || detail::HasControl(o.entryPoint)) bad.push_back("Entry point must be 1 to 256 characters.");
    for (const std::string* v : {&o.minVersion, &o.maxVersionTested}) {
        bool ok = false;
        std::size_t dots = 0;
        for (char c : *v) { if (c == '.') ++dots; else if (c < '0' || c > '9') { dots = 99; break; } }
        ok = dots == 3 && !v->empty() && v->front() != '.' && v->back() != '.' && v->find("..") == std::string::npos;
        if (!ok) bad.push_back("Windows version \"" + *v + "\" must be four numbers such as 10.0.17763.0.");
    }
    {
        static const char* kOk[] = {"internetClient", "internetClientServer", "privateNetworkClientServer", "microphone"};
        for (const std::string& c : o.capabilities) {
            bool known = false;
            for (const char* k : kOk) if (c == k) known = true;
            if (!known) bad.push_back("Capability \"" + c + "\" is not one this export knows (internetClient, internetClientServer, privateNetworkClientServer, microphone).");
        }
    }
    {   // tile background: a #RRGGBB or "transparent"
        const std::string& b = o.backgroundColor;
        bool ok = b == "transparent";
        if (b.size() == 7 && b[0] == '#') { ok = true; for (std::size_t i = 1; i < 7; ++i) if (!std::isxdigit(static_cast<unsigned char>(b[i]))) ok = false; }
        if (!ok) bad.push_back("Tile background \"" + b + "\" must be #RRGGBB or transparent.");
    }
    return bad;
}

// The images the manifest names (base file = 100% scale; a 200% copy sits beside it as <name>.scale-200.png).
struct AssetSpec { const char* file; const char* manifestUse; int w, h; };
inline const std::vector<AssetSpec>& RequiredAssets() {
    static const std::vector<AssetSpec> k = {
        {"Square44x44Logo.png",   "VisualElements Square44x44Logo (app list, taskbar)", 44, 44},
        {"Square71x71Logo.png",   "DefaultTile Square71x71Logo (small tile)", 71, 71},
        {"Square150x150Logo.png", "VisualElements Square150x150Logo (medium tile)", 150, 150},
        {"Wide310x150Logo.png",   "DefaultTile Wide310x150Logo (wide tile)", 310, 150},
        {"Square310x310Logo.png", "DefaultTile Square310x310Logo (large tile)", 310, 310},
        {"StoreLogo.png",         "Properties Logo (package logo)", 50, 50},
        {"SplashScreen.png",      "SplashScreen Image", 620, 300},
    };
    return k;
}

// The manifest text. Call ValidateManifest first: this does not check, it only escapes.
inline std::string GenerateManifest(const ManifestOptions& o) {
    const std::string v = NormalizeVersion(o.version);
    std::string x;
    x += "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
    x += "<!-- EXPERIMENTAL, UNVERIFIED: written by OkaySpace for an Xbox One Developer Mode test package (docs/xbox_uwp.md).\n"
         "     No package built from this file has been installed or run. -->\n";
    x += "<Package xmlns=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10\"\n"
         "         xmlns:uap=\"http://schemas.microsoft.com/appx/manifest/uap/windows10\"\n"
         "         IgnorableNamespaces=\"uap\">\n";
    x += "  <Identity Name=\"" + XmlEscape(o.identityName) + "\" Publisher=\"" + XmlEscape(o.publisher) + "\" Version=\"" + v + "\" ProcessorArchitecture=\"x64\" />\n";
    x += "  <Properties>\n";
    x += "    <DisplayName>" + XmlEscape(o.displayName) + "</DisplayName>\n";
    x += "    <PublisherDisplayName>" + XmlEscape(o.publisherDisplayName) + "</PublisherDisplayName>\n";
    x += "    <Logo>Assets\\StoreLogo.png</Logo>\n";
    x += "  </Properties>\n";
    x += "  <Dependencies>\n";
    x += "    <TargetDeviceFamily Name=\"Windows.Universal\" MinVersion=\"" + XmlEscape(o.minVersion) + "\" MaxVersionTested=\"" + XmlEscape(o.maxVersionTested) + "\" />\n";
    if (o.vclibsDependency)
        x += "    <PackageDependency Name=\"Microsoft.VCLibs.140.00\" MinVersion=\"14.0.0.0\" Publisher=\"CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, S=Washington, C=US\" />\n";
    x += "  </Dependencies>\n";
    x += "  <Resources>\n    <Resource Language=\"en-us\" />\n  </Resources>\n";
    x += "  <Applications>\n";
    x += "    <Application Id=\"" + XmlEscape(o.applicationId) + "\" Executable=\"" + XmlEscape(o.executable) + "\" EntryPoint=\"" + XmlEscape(o.entryPoint) + "\">\n";
    x += "      <uap:VisualElements DisplayName=\"" + XmlEscape(o.displayName) + "\" Description=\"" + XmlEscape(o.description.empty() ? o.displayName : o.description) + "\"\n"
         "                          BackgroundColor=\"" + XmlEscape(o.backgroundColor) + "\"\n"
         "                          Square150x150Logo=\"Assets\\Square150x150Logo.png\" Square44x44Logo=\"Assets\\Square44x44Logo.png\">\n";
    x += "        <uap:DefaultTile Wide310x150Logo=\"Assets\\Wide310x150Logo.png\" Square71x71Logo=\"Assets\\Square71x71Logo.png\" Square310x310Logo=\"Assets\\Square310x310Logo.png\" />\n";
    x += "        <uap:SplashScreen Image=\"Assets\\SplashScreen.png\" />\n";
    x += "      </uap:VisualElements>\n";
    x += "    </Application>\n";
    x += "  </Applications>\n";
    if (!o.capabilities.empty()) {
        x += "  <Capabilities>\n";
        for (const std::string& c : o.capabilities) x += "    <Capability Name=\"" + XmlEscape(c) + "\" />\n";
        x += "  </Capabilities>\n";
    }
    x += "</Package>\n";
    return x;
}

// Identity name from the project's bundle id ("com.my studio.Game 2" -> "com.my-studio.Game-2"). Always passes the name rules.
inline std::string IdentityNameFrom(const std::string& bundleId) {
    std::string o;
    for (char c : bundleId) {
        if ((std::isalnum(static_cast<unsigned char>(c)) && static_cast<unsigned char>(c) < 128) || c == '.' || c == '-') o += c;
        else if (!o.empty() && o.back() != '-') o += '-';
    }
    while (!o.empty() && (o.back() == '.' || o.back() == '-')) o.pop_back();
    while (!o.empty() && (o.front() == '.' || o.front() == '-')) o.erase(o.begin());
    if (o.size() > 50) { o.resize(50); while (!o.empty() && (o.back() == '.' || o.back() == '-')) o.pop_back(); }
    while (o.size() < 3) o += "x";
    if (detail::ReservedDeviceName(o)) o += "-app";
    return o;
}
// Publisher "CN=<company>" with characters a distinguished name cannot hold removed.
inline std::string PublisherFrom(const std::string& company) {
    std::string o;
    for (unsigned char c : company) {
        if (c < 0x20 || c == ',' || c == '"' || c == '\\' || c == '<' || c == '>' || c == ';' || c == '+' || c == '=' || c == 0x7f) continue;
        o += static_cast<char>(c);
    }
    while (!o.empty() && o.front() == ' ') o.erase(o.begin());
    while (!o.empty() && o.back() == ' ') o.pop_back();
    if (o.empty()) o = "OkaySpaceDev";
    return "CN=" + o;
}

} // namespace okay::xbox
