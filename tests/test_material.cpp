#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("material");

    // Material fields round-trip through the serializer.
    {
        Scene s("mat");
        GameObject* o = s.CreateGameObject("Glowing");
        auto* mr = o->AddComponent<MeshRenderer>();
        mr->mesh = Mesh::Sphere();
        mr->color = {0.2f, 0.4f, 0.6f, 1.0f};
        mr->emissive = {1.0f, 0.5f, 0.0f, 1.0f};
        mr->specular = 0.75f;
        mr->shininess = 48.0f;
        mr->unlit = true;

        std::string text = SceneSerializer::Serialize(s);
        Scene s2("x");
        SceneSerializer::Deserialize(s2, text);
        auto* m2 = s2.Find("Glowing") ? s2.Find("Glowing")->GetComponent<MeshRenderer>() : nullptr;
        CHECK(m2 != nullptr);
        if (m2) {
            CHECK_NEAR(m2->emissive.r, 1.0f, 1e-5f);
            CHECK_NEAR(m2->emissive.g, 0.5f, 1e-5f);
            CHECK_NEAR(m2->specular, 0.75f, 1e-5f);
            CHECK_NEAR(m2->shininess, 48.0f, 1e-4f);
            CHECK(m2->unlit);
        }
    }

    // Older scenes without a "material" record still load (defaults applied).
    {
        Scene s("old");
        GameObject* o = s.CreateGameObject("Cube");
        o->AddComponent<MeshRenderer>();
        std::string text = SceneSerializer::Serialize(s);
        // Drop every "  material ..." line to simulate a pre-material save file.
        std::string stripped;
        std::size_t pos = 0;
        while (pos < text.size()) {
            std::size_t nl = text.find('\n', pos);
            std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos + 1);
            if (line.find("material ") == std::string::npos) stripped += line;
            pos = (nl == std::string::npos) ? text.size() : nl + 1;
        }
        CHECK(stripped.find("material ") == std::string::npos);

        Scene s2("x");
        SceneSerializer::Deserialize(s2, stripped);
        auto* mr = s2.Find("Cube") ? s2.Find("Cube")->GetComponent<MeshRenderer>() : nullptr;
        CHECK(mr != nullptr);
        if (mr) { CHECK(!mr->unlit); CHECK_NEAR(mr->specular, 0.0f, 1e-6f); }
    }

    TEST_MAIN_RESULT();
}
