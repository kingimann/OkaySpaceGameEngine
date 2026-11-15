// Our own NDO (Nendo) reader is a SEAM ONLY (engine/src/Render/NdoReader.cpp): it recognises the "nendo 1.x" header and refuses every file, saying
// which of the two cases it is. This test pins that: nothing is ever read from a Nendo body, no model comes back, hostile input is safe.
// No real .ndo file exists on this machine, so nothing here says the format is understood. Builds on its own with -DOKAY_STANDALONE_TEST.
#include "test_framework.hpp"
#include "e3_fixtures.hpp"
#include "okay/Render/ModelData.hpp"
#include <string>

using namespace okay;

int main() {
    RUN_SUITE("ndo_reader");
    ImportedModel m; std::string e;
    CHECK(!ReadNdoBuffer(std::string(), "", m, &e) && e.find("not a Nendo file") != std::string::npos);
    CHECK(!ReadNdoBuffer("nendo", "", m, &e) && e.find("not a Nendo file") != std::string::npos);
    CHECK(!ReadNdoBuffer("hello nendo 1.2 body", "", m, &e) && e.find("not a Nendo file") != std::string::npos);
    for (const char* v : {"nendo 1.0", "nendo 1.1a", "nendo 1.2", "nendo 9.9"}) {
        std::string f = v; f.append(200, '\x7f');
        CHECK(!ReadNdoBuffer(f, "", m, &e) && e.find("recognised but not read") != std::string::npos && m.parts.empty() && m.nodes.empty() && m.backend.empty());
    }
    {   const std::string f = std::string("nendo 1.2") + std::string(1000, '\xff');
        CHECK(!ReadNdoBuffer(f, "", m, &e) && e.find("version \"1.2\"") != std::string::npos);
        // every byte of a header-sized buffer, and 0xFF everywhere: no crash, always a refusal
        for (std::size_t n = 0; n < 40; ++n) CHECK(!ReadNdoBuffer(f.substr(0, n), "", m, &e) && !e.empty());
    }
    CHECK(!ReadNdoFile("/definitely/not/here.ndo", m, &e) && !e.empty());
    TEST_MAIN_RESULT();
}
