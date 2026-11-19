#pragma once
// The text files the Build Settings write next to an exported game: the Web build script, the Android manifest and
// Gradle file, and a Windows installer script (NSIS). Pure functions of their inputs, so the tests check the exact text
// (docs/build_settings_upgrade.md). NSIS and the Android tools cannot run here: the text is checked, the tools are not run.
// Written from the NSIS script reference, the Android manifest and Gradle documentation and the Emscripten settings list.
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace oked::outtext {

// ---- Web ---------------------------------------------------------------------------------------------------
struct WebInfo { int memoryMB = 0; bool compress = false; };
/// build-web.sh. With the defaults (no memory setting, no compression) this is exactly the script older exports wrote.
inline std::string WebBuildScript(const WebInfo& w) {
    std::string flags = "--preload-file game@/";
    if (w.memoryMB > 0) flags += " -sINITIAL_MEMORY=" + std::to_string(std::max(16, std::min(2048, w.memoryMB))) + "MB -sALLOW_MEMORY_GROWTH=1";
    std::string s =
        "#!/usr/bin/env bash\n"
        "# Build this game for the web (WebAssembly + WebGL) with Emscripten.\n"
        "# Prereqs: install emsdk (https://emscripten.org), 'source emsdk_env.sh',\n"
        "# and set OKAY_SRC to your OkaySpace engine source checkout.\n"
        "set -euo pipefail\n"
        "cd \"$(dirname \"$0\")\"\n"
        ": \"${OKAY_SRC:?Set OKAY_SRC to the OkaySpace engine source folder}\"\n"
        "command -v emcmake >/dev/null || { echo 'Emscripten not found: source emsdk_env.sh'; exit 1; }\n"
        "emcmake cmake -S \"$OKAY_SRC\" -B build-web -DCMAKE_BUILD_TYPE=Release \\\n"
        "  -DOKAY_BUILD_PLAYER=ON -DOKAY_BUILD_TESTS=OFF -DOKAY_BUILD_SANDBOX=OFF -DOKAY_BUILD_LAUNCHER=OFF \\\n"
        "  -DCMAKE_EXE_LINKER_FLAGS=\"" + flags + "\"\n"
        "cmake --build build-web -j\n";
    if (w.compress)
        s += "# Compression: a .gz copy of each big file, for a web server that serves them with Content-Encoding: gzip.\n"
             "for f in build-web/web/*.wasm build-web/web/*.js build-web/web/*.data; do [ -f \"$f\" ] && gzip -kf9 \"$f\"; done\n";
    s += "echo 'Built build-web/web/okay-player.html — serve it: python3 -m http.server -d build-web/web'\n";
    return s;
}

// ---- Android -----------------------------------------------------------------------------------------------
struct AndroidInfo {
    std::string appName = "Game", package = "com.okayspace.game", version = "1.0.0";
    int buildNumber = 1, minSdk = 24, targetSdk = 34, orientation = 0;   // 0 any, 1 landscape, 2 portrait
    std::string permissions = "INTERNET";                               // NAME;NAME (android.permission. is added)
};
/// A Java-style package name from the identifier: lower case, segments of letters, digits and _, never starting with a digit.
inline std::string AndroidPackageName(const std::string& id) {
    std::vector<std::string> seg; std::string cur;
    auto flush = [&]() { if (!cur.empty()) { if (std::isdigit((unsigned char)cur[0])) cur = "g" + cur; seg.push_back(cur); cur.clear(); } };
    for (char c : id) {
        if (c == '.') flush();
        else if (std::isalnum((unsigned char)c)) cur.push_back((char)std::tolower((unsigned char)c));
        else if (c == '-' || c == '_' || c == ' ') cur.push_back('_');
    }
    flush();
    if (seg.size() < 2) { seg.insert(seg.begin(), "com"); if (seg.size() < 2) seg.push_back("game"); }
    std::string out;
    for (const std::string& s : seg) out += (out.empty() ? "" : ".") + s;
    return out;
}
inline std::string XmlEscape(const std::string& s) {
    std::string o;
    for (char c : s) { if (c == '&') o += "&amp;"; else if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;"; else if (c == '"') o += "&quot;"; else if ((unsigned char)c >= 32) o.push_back(c); }
    return o;
}
/// The permissions as full names, each once, valid names only (A-Z, digits, _ ; or a dotted name).
inline std::vector<std::string> AndroidPermissionList(const std::string& text) {
    std::vector<std::string> out; std::size_t pos = 0;
    while (pos <= text.size()) {
        std::size_t e = text.find(';', pos); if (e == std::string::npos) e = text.size();
        std::string p = text.substr(pos, e - pos); pos = e + 1;
        const std::size_t a = p.find_first_not_of(' '), z = p.find_last_not_of(' ');
        if (a == std::string::npos) continue;
        p = p.substr(a, z - a + 1);
        bool ok = !p.empty();
        for (char c : p) if (!(std::isalnum((unsigned char)c) || c == '_' || c == '.')) ok = false;
        if (!ok) continue;
        if (p.find('.') == std::string::npos) { for (char& c : p) c = (char)std::toupper((unsigned char)c); p = "android.permission." + p; }
        if (std::find(out.begin(), out.end(), p) == out.end()) out.push_back(p);
    }
    return out;
}
inline const char* AndroidOrientationName(int o) { return o == 1 ? "sensorLandscape" : o == 2 ? "sensorPortrait" : "fullSensor"; }
inline std::string AndroidManifest(const AndroidInfo& a) {
    std::string s = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        "<!-- Written by OkaySpace Build Settings. Put it at app/src/main/AndroidManifest.xml of the SDL2 Android project (see README.txt). -->\n"
        "<manifest xmlns:android=\"http://schemas.android.com/apk/res/android\"\n"
        "    package=\"" + XmlEscape(a.package) + "\"\n"
        "    android:versionCode=\"" + std::to_string(std::max(1, a.buildNumber)) + "\"\n"
        "    android:versionName=\"" + XmlEscape(a.version) + "\">\n"
        "    <uses-sdk android:minSdkVersion=\"" + std::to_string(a.minSdk) + "\" android:targetSdkVersion=\"" + std::to_string(a.targetSdk) + "\" />\n";
    for (const std::string& p : AndroidPermissionList(a.permissions)) s += "    <uses-permission android:name=\"" + XmlEscape(p) + "\" />\n";
    s += "    <uses-feature android:glEsVersion=\"0x00020000\" android:required=\"true\" />\n"
         "    <application android:label=\"" + XmlEscape(a.appName) + "\" android:allowBackup=\"true\" android:hardwareAccelerated=\"true\">\n"
         "        <activity android:name=\"org.libsdl.app.SDLActivity\"\n"
         "            android:exported=\"true\"\n"
         "            android:screenOrientation=\"" + AndroidOrientationName(a.orientation) + "\"\n"
         "            android:configChanges=\"keyboard|keyboardHidden|orientation|screenSize|screenLayout|uiMode\">\n"
         "            <intent-filter>\n"
         "                <action android:name=\"android.intent.action.MAIN\" />\n"
         "                <category android:name=\"android.intent.category.LAUNCHER\" />\n"
         "            </intent-filter>\n"
         "        </activity>\n"
         "    </application>\n"
         "</manifest>\n";
    return s;
}
inline std::string AndroidGradle(const AndroidInfo& a) {
    return "// Written by OkaySpace Build Settings: the android { defaultConfig { ... } } values for app/build.gradle.\n"
           "android {\n"
           "    defaultConfig {\n"
           "        applicationId \"" + a.package + "\"\n"
           "        minSdkVersion " + std::to_string(a.minSdk) + "\n"
           "        targetSdkVersion " + std::to_string(a.targetSdk) + "\n"
           "        versionCode " + std::to_string(std::max(1, a.buildNumber)) + "\n"
           "        versionName \"" + a.version + "\"\n"
           "    }\n"
           "}\n";
}

// ---- Windows installer (NSIS) ------------------------------------------------------------------------------
struct InstallerInfo {
    std::string gameName = "Game", company = "OkaySpace", version = "1.0.0";
    std::string exeName = "Game.exe";        // the game's program inside the build folder
    std::string installFolder;               // "" = the game name
    std::string licenseFile;                 // a text file next to the script ("" = no licence page)
    std::string scriptName = "Game-installer.nsi";
    bool startMenu = true, desktopShortcut = false;
};
/// A text in an NSIS string: quotes, $ and line breaks made safe.
inline std::string NsisText(const std::string& s) {
    std::string o;
    for (char c : s) { if (c == '"') o += "$\\\""; else if (c == '$') o += "$$"; else if ((unsigned char)c >= 32) o.push_back(c); }
    return o;
}
/// A folder name that is safe to create under $LOCALAPPDATA: no path characters.
inline std::string InstallFolderName(const std::string& want, const std::string& fallback) {
    std::string o;
    for (char c : want.empty() ? fallback : want) if (!(c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*' || (unsigned char)c < 32)) o.push_back(c);
    while (!o.empty() && (o.back() == ' ' || o.back() == '.')) o.pop_back();
    return o.empty() ? std::string("Game") : o;
}
inline std::string InstallerScript(const InstallerInfo& i) {
    const std::string folder = InstallFolderName(i.installFolder, i.gameName);
    const std::string name = NsisText(i.gameName), fol = NsisText(folder), exe = NsisText(i.exeName), ver = NsisText(i.version), comp = NsisText(i.company);
    std::string s =
        "; Installer for " + name + ", written by OkaySpace Build Settings. Build it with NSIS: makensis \"" + NsisText(i.scriptName) + "\"\n"
        "; Run it from the folder this file is in (the game's build folder). One user, no administrator needed.\n"
        "Unicode true\n"
        "!include \"MUI2.nsh\"\n\n"
        "Name \"" + name + " " + ver + "\"\n"
        "OutFile \"" + name + "-Setup.exe\"\n"
        "InstallDir \"$LOCALAPPDATA\\" + fol + "\"\n"
        "InstallDirRegKey HKCU \"Software\\" + fol + "\" \"InstallDir\"\n"
        "RequestExecutionLevel user\n"
        "SetCompressor /SOLID lzma\n\n"
        "!insertmacro MUI_PAGE_WELCOME\n";
    if (!i.licenseFile.empty()) s += "!insertmacro MUI_PAGE_LICENSE \"" + NsisText(i.licenseFile) + "\"\n";
    s += "!insertmacro MUI_PAGE_DIRECTORY\n"
         "!insertmacro MUI_PAGE_INSTFILES\n"
         "!insertmacro MUI_PAGE_FINISH\n"
         "!insertmacro MUI_UNPAGE_CONFIRM\n"
         "!insertmacro MUI_UNPAGE_INSTFILES\n"
         "!insertmacro MUI_LANGUAGE \"English\"\n\n"
         "Section \"Install\"\n"
         "  SetOutPath \"$INSTDIR\"\n"
         "  File /r /x \"" + NsisText(i.scriptName) + "\"" + (i.licenseFile.empty() ? "" : " /x \"" + NsisText(i.licenseFile) + "\"") + " /x \"*-Setup.exe\" \"*.*\"\n"
         "  WriteUninstaller \"$INSTDIR\\Uninstall.exe\"\n"
         "  WriteRegStr HKCU \"Software\\" + fol + "\" \"InstallDir\" \"$INSTDIR\"\n";
    if (i.startMenu) s += "  CreateDirectory \"$SMPROGRAMS\\" + fol + "\"\n"
                          "  CreateShortcut \"$SMPROGRAMS\\" + fol + "\\" + name + ".lnk\" \"$INSTDIR\\" + exe + "\"\n"
                          "  CreateShortcut \"$SMPROGRAMS\\" + fol + "\\Uninstall " + name + ".lnk\" \"$INSTDIR\\Uninstall.exe\"\n";
    if (i.desktopShortcut) s += "  CreateShortcut \"$DESKTOP\\" + name + ".lnk\" \"$INSTDIR\\" + exe + "\"\n";
    s += "  WriteRegStr HKCU \"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + fol + "\" \"DisplayName\" \"" + name + "\"\n"
         "  WriteRegStr HKCU \"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + fol + "\" \"DisplayVersion\" \"" + ver + "\"\n"
         "  WriteRegStr HKCU \"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + fol + "\" \"Publisher\" \"" + comp + "\"\n"
         "  WriteRegStr HKCU \"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + fol + "\" \"InstallLocation\" \"$INSTDIR\"\n"
         "  WriteRegStr HKCU \"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + fol + "\" \"UninstallString\" \"$INSTDIR\\Uninstall.exe\"\n"
         "SectionEnd\n\n"
         "Section \"Uninstall\"\n"
         "  Delete \"$DESKTOP\\" + name + ".lnk\"\n"
         "  RMDir /r \"$SMPROGRAMS\\" + fol + "\"\n"
         "  RMDir /r \"$INSTDIR\"\n"
         "  DeleteRegKey HKCU \"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + fol + "\"\n"
         "  DeleteRegKey HKCU \"Software\\" + fol + "\"\n"
         "SectionEnd\n";
    return s;
}

}  // namespace oked::outtext
