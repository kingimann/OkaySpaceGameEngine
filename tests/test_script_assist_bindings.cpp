// The Script Editor's assist from the engine's binding table (editor/ScriptAssistBindings.hpp): the members listed after
// GetComponent("X"). and after a variable that holds a component, the argument hints of bound methods and the hover docs,
// all read from okay::scriptbind and the PropertyRegistry (no hand-written list).
#include "test_framework.hpp"
#include <Okay.hpp>
#include "ScriptAssistBindings.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include "okay/Scripting/ScriptBindings.hpp"

#include <algorithm>
#include <string>
#include <vector>

using namespace oked;

namespace {
bool Has(const std::vector<std::string>& v, const std::string& w) { return std::find(v.begin(), v.end(), w) != v.end(); }
}

int main() {
    RUN_SUITE("script_assist_bindings");
    // a class's members: its properties, gameObject, enabled, its methods with '('
    const std::vector<BoundMember>* light = BoundMembersOf("Light");
    CHECK(light != nullptr);
    if (light) {
        bool intensity = false, go = false;
        for (const BoundMember& m : *light) {
            if (m.name == "intensity") intensity = !m.method && m.detail.find("float intensity") == 0;
            if (m.name == "gameObject") go = true;
        }
        CHECK(intensity && go);
        for (size_t i = 1; i < light->size(); ++i) CHECK((*light)[i - 1].name < (*light)[i].name);   // sorted, one per name
    }
    CHECK(BoundMembersOf("light") != nullptr && BoundMembersOf("NoSuchThing") == nullptr && BoundMembersOf("") == nullptr);
    // every bound class has an entry, and every bound method of it is listed
    int classes = 0;
    for (const okay::scriptbind::ClassBinding& c : okay::scriptbind::AllClasses()) {
        const std::vector<BoundMember>* m = BoundMembersOf(c.name);
        CHECK(m != nullptr);
        if (!m) continue;
        ++classes;
        for (const okay::scriptbind::MethodBinding& b : c.methods) {
            bool found = false;
            for (const BoundMember& x : *m) if (x.name == b.name) found = true;
            CHECK(found);
        }
    }
    CHECK(classes > 100);
    // receivers: GetComponent in its three spellings, at the end of a chain, a variable set from one, a declared type
    const std::string src = "var rb = GetComponent(\"Rigidbody\");\nLight lamp;\nvar n = 3;\nfunction update() { }\n";
    CHECK(BoundClassOf(src, "GetComponent(\"Light\")") == "Light");
    CHECK(BoundClassOf(src, "GetComponent<Light>()") == "Light");
    CHECK(BoundClassOf(src, "gameObject.GetComponent('Camera')") == "Camera");
    CHECK(BoundClassOf(src, "rb") == "Rigidbody3D");                             // the script name of Rigidbody3D
    CHECK(BoundClassOf(src, "lamp") == "Light");
    CHECK(BoundClassOf(src, "n").empty() && BoundClassOf(src, "nobody").empty() && BoundClassOf(src, "GetComponent(\"Nope\")").empty());
    const std::vector<std::string> words = BoundMemberWords(src, "rb");
    CHECK(Has(words, "AddForce(") && Has(words, "gameObject") && !Has(words, "AddForce"));
    CHECK(BoundMemberWords(src, "n").empty());
    // the argument hint of a bound method names the class and the argument types; optional ones in brackets
    const std::string* sig = BoundMethodSignature("AddForce");
    CHECK(sig != nullptr && sig->find("Rigidbody3D.AddForce(Vector3") != std::string::npos && sig->find("Rigidbody2D.AddForce(Vector2") != std::string::npos);
    CHECK(sig && sig->find("Rigidbody2D.AddForce(Vector2, int)") != std::string::npos);   // each overload, with its enum argument
    CHECK(BoundMethodSignature("NoSuchMethodAnywhere") == nullptr);
    // hover docs: the class, and a member with its class
    const auto& docs = BoundDocs();
    CHECK(docs.count("Light") && docs.at("Light").find("Light (component)") == 0);
    CHECK(docs.count("intensity") && docs.at("intensity").find("Light: float intensity") != std::string::npos);
    TEST_MAIN_RESULT();
}
