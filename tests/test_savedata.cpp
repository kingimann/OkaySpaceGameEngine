#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdio>

using namespace okay;

// Easy-Save-3-style save system: typed values, many files, disk round-trip.
int main() {
    RUN_SUITE("savedata");

    const std::string path = "/tmp/okay_test_save.okaysave";
    std::remove(path.c_str());

    // --- Typed set/get round-trip through a file ------------------------------
    {
        SaveFile sf;
        sf.SetInt("coins", 120);
        sf.SetFloat("health", 7.5f);
        sf.SetBool("tutorialDone", true);
        sf.SetString("name", "Hero of Okay");
        sf.SetVec3("spawn", Vec3{1.5f, 0.0f, -3.0f});
        CHECK(sf.Save(path));

        SaveFile in;
        CHECK(in.Load(path));
        CHECK(in.GetInt("coins") == 120);
        CHECK_NEAR(in.GetFloat("health"), 7.5f, 0.001f);
        CHECK(in.GetBool("tutorialDone") == true);
        CHECK(in.GetString("name") == "Hero of Okay");
        Vec3 sp = in.GetVec3("spawn");
        CHECK_NEAR(sp.x, 1.5f, 0.001f);
        CHECK_NEAR(sp.z, -3.0f, 0.001f);
    }

    // --- Defaults for missing keys + Has/Delete -------------------------------
    {
        SaveFile sf;
        sf.Load(path);
        CHECK(sf.GetInt("missing", 42) == 42);
        CHECK(sf.Has("coins"));
        sf.Delete("coins");
        CHECK(!sf.Has("coins"));
    }

    // --- Save facade: write-through + per-file isolation ----------------------
    {
        const std::string f1 = "/tmp/okay_save_slot1.okaysave";
        const std::string f2 = "/tmp/okay_save_slot2.okaysave";
        std::remove(f1.c_str()); std::remove(f2.c_str());
        Save::DeleteFile(f1); Save::DeleteFile(f2);

        Save::SetInt("level", 3, f1);
        Save::SetInt("level", 9, f2);
        CHECK(Save::GetInt("level", 0, f1) == 3);   // files don't bleed into each other
        CHECK(Save::GetInt("level", 0, f2) == 9);
        CHECK(Save::FileExists(f1));                 // write-through persisted it

        // A fresh facade read (clear the in-memory cache) still sees the disk value.
        Save::DeleteFile(f2);                        // removes file + cache entry
        CHECK(!Save::FileExists(f2));
        CHECK(Save::GetInt("level", -1, f2) == -1);  // gone -> default

        std::remove(f1.c_str());
    }

    std::remove(path.c_str());
    TEST_MAIN_RESULT();
}
