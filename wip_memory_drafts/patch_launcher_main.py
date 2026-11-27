import sys
p = sys.argv[1]
s = open(p).read()
def rep(a, b):
    global s
    assert s.count(a) == 1, (a, s.count(a))
    s = s.replace(a, b)

rep("""#include "okay/Core/FileCopy.hpp"   // CopyFileOver""",
"""#include "okay/Core/Memory.hpp"     // the memory report (OKAY_MEMORY_REPORT=1 prints it at exit)
#include "okay/Graphics/TtfFont.hpp"  // FontCacheBytes, for the memory report
#include "okay/Core/FileCopy.hpp"   // CopyFileOver""")

rep("""        g_fontMono = okf::AddToAtlas(ImGui::GetIO().Fonts, okf::JetBrainsMonoTtf(), 16.0f, fc);
    }""",
"""        g_fontMono = okf::AddToAtlas(ImGui::GetIO().Fonts, okf::JetBrainsMonoTtf(), 16.0f, fc);
    }
    {   // the memory report's named owners (okay/Core/Memory.hpp)
        okay::AddMemoryCounter("Fonts: TrueType cache (OkayUI and engine text)", [] { return okay::FontCacheBytes(); });
#ifndef OKAY_ON_OKAYUI
        okay::AddMemoryCounter("Fonts: Dear ImGui font files", [] {
            std::size_t n = 0;
            for (const ImFontConfig& c : ImGui::GetIO().Fonts->Sources) if (c.FontDataOwnedByAtlas) n += (std::size_t)c.FontDataSize;
            return n;
        });
        okay::AddMemoryCounter("Fonts: Dear ImGui atlas pixels", [] {
            std::size_t n = 0;
            for (ImTextureData* t : ImGui::GetIO().Fonts->TexList) if (t && t->Pixels) n += (std::size_t)t->GetSizeInBytes();
            return n;
        });
#endif
    }""")

rep("""    if (!g_staySignedIn) accountPtr->Logout();   // "Stay signed in" was unticked: the next start asks again""",
"""    if (okay::MemoryReportWanted()) { std::fputs(okay::MemoryReportText("[memory] launcher").c_str(), stdout); std::fflush(stdout); }
    if (!g_staySignedIn) accountPtr->Logout();   // "Stay signed in" was unticked: the next start asks again""")
open(p, 'w').write(s)
print("patched")
