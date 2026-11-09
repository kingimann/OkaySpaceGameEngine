#include "okay/Scene/ProjectLists.hpp"
#include "okay/Scene/Layers.hpp"
#include "okay/Scene/Tags.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace okay {

namespace {
const std::string kLayerKey = "layer_name_";

bool IsOurKey(const std::string& key) {
    return key == "tag" || key == "strict_tags" || key.compare(0, kLayerKey.size(), kLayerKey) == 0;
}
void StripCr(std::string& s) { while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back(); }
}

std::string ProjectLists::Write() {
    std::string out;
    for (const std::string& t : Tags::Custom()) if (Tags::IsValidName(t)) out += "tag=" + t + "\n";
    if (Tags::Strict()) out += "strict_tags=1\n";
    for (int i = 0; i < Layers::kCount; ++i) {
        const std::string& now = Layers::Names()[static_cast<std::size_t>(i)];
        if (now != Layers::DefaultName(i)) out += kLayerKey + std::to_string(i) + "=" + now + "\n";
    }
    return out;
}

bool ProjectLists::ApplyLine(const std::string& key, const std::string& value) {
    if (key == "tag") return Tags::Add(value);
    if (key == "strict_tags") { Tags::Strict() = (std::atoi(value.c_str()) != 0); return true; }
    if (key.compare(0, kLayerKey.size(), kLayerKey) != 0) return false;
    const std::string num = key.substr(kLayerKey.size());
    if (num.empty() || num.size() > 2) return false;
    for (char c : num) if (c < '0' || c > '9') return false;
    const int i = std::atoi(num.c_str());
    if (!Layers::IsValid(i)) return false;
    if (value.empty()) return Layers::ClearName(i);
    return Layers::SetName(i, value);
}

int ProjectLists::Apply(const std::string& text) {
    int taken = 0;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        StripCr(line);
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        if (ApplyLine(line.substr(0, eq), line.substr(eq + 1))) ++taken;
    }
    return taken;
}

bool ProjectLists::Load(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    Apply(ss.str());
    return true;
}

bool ProjectLists::Save(const std::string& path) {
    std::string kept;   // every line that is not ours, in order
    {
        std::ifstream f(path, std::ios::binary);
        if (f) {
            std::string line;
            while (std::getline(f, line)) {
                StripCr(line);
                const std::size_t eq = line.find('=');
                if (eq != std::string::npos && IsOurKey(line.substr(0, eq))) continue;
                kept += line + "\n";
            }
        }
    }
    const std::string tmp = path + ".savetmp";
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o) return false;
        o << kept << Write();
        o.flush();
        if (!o) { std::error_code rc; std::filesystem::remove(tmp, rc); return false; }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {   // cross-device or a locked target: copy over it
        std::error_code ce;
        std::filesystem::copy_file(tmp, path, std::filesystem::copy_options::overwrite_existing, ce);
        std::filesystem::remove(tmp, ec);
        return !ce;
    }
    return true;
}

void ProjectLists::Reset() {
    Tags::Custom().clear();
    Tags::Strict() = false;
    Layers::ResetNames();
}

} // namespace okay
