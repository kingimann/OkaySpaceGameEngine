// The editor's and launcher's embedded fonts as TrueType bytes, decoded once from our own OKF1 data
// (shared/OkfDecode.hpp, shared/RobotoFontOwn.h, shared/JetBrainsMonoFontOwn.h). Header only, no Dear ImGui here:
//
//   ImFontConfig fc; fc.FontDataOwnedByAtlas = false;   // the bytes below live for the whole process
//   const auto& ttf = okf_fonts::RobotoMedium();
//   if (!ttf.empty()) io.Fonts->AddFontFromMemoryTTF((void*)ttf.data(), (int)ttf.size(), 18.0f, &fc);
//
// The decoded bytes are pinned by assets/fonts/embedded_fonts.sha256 (tests/test_embedded_font.cpp), so the atlas
// Dear ImGui bakes from them is the one it baked from the old base85 arrays. An empty vector means damaged data.
#pragma once
#include <cstdint>
#include <vector>
#include "OkfDecode.hpp"
#include "RobotoFontOwn.h"
#include "JetBrainsMonoFontOwn.h"

namespace okf_fonts {

inline const std::vector<std::uint8_t>& RobotoMedium() {
    static const std::vector<std::uint8_t> bytes = [] {
        std::vector<std::uint8_t> b;
        if (!okf::DecodeLines(okf_data::kRobotoMediumOkf, okf_data::kRobotoMediumOkfLines, b)) b.clear();
        return b;
    }();
    return bytes;
}

inline const std::vector<std::uint8_t>& JetBrainsMono() {
    static const std::vector<std::uint8_t> bytes = [] {
        std::vector<std::uint8_t> b;
        if (!okf::DecodeLines(okf_data::kJetBrainsMonoOkf, okf_data::kJetBrainsMonoOkfLines, b)) b.clear();
        return b;
    }();
    return bytes;
}

} // namespace okf_fonts
