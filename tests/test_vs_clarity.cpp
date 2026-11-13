// The "what does it do / need / give" line for every visual-script node, and the
// "connects to / comes from" text on pin tooltips, which is generated from the
// same PinCompatible rule the editor uses to refuse a wire.
#include "test_framework.hpp"
#include "VisualScriptEditor.hpp"

#include <cstring>
#include <set>
#include <string>

using namespace okay::editor::vsed;
using okay::vs::PinType;

static bool Has(const std::string& hay, const char* needle) { return hay.find(needle) != std::string::npos; }

static const VsDef* Find(const char* type) {
    for (const VsDef& d : Registry()) if (std::strcmp(d.type, type) == 0) return &d;
    return nullptr;
}

int main() {
    RUN_SUITE("vs_clarity");

    // ---- every node has a one-liner with all three parts, and none is a placeholder ----
    int checked = 0;
    for (const VsDef& d : Registry()) {
        const VsNodeBrief b = VsNodeBriefFor(d);
        const std::string line = b.Line();
        if (b.does.size() < 8 || b.needs.size() < 3 || b.gives.size() < 3 || line.size() > 600 ||
            Has(line, "TODO") || Has(line, "placeholder")) {
            std::printf("  weak one-liner for %s: %s\n", d.type, line.c_str());
            CHECK(false);
        }
        ++checked;
    }
    CHECK(checked == (int)Registry().size());
    std::printf("  one-liners for %d nodes\n", checked);

    // ---- the everyday nodes read as real sentences, not the generic builtin text ----
    const char* core[] = {
        "OnStart", "OnUpdate", "OnKey", "OnCollision", "OnTrigger", "OnClick", "Branch", "Sequence", "Once", "Timer",
        "Delay", "GetVar", "SetVar", "AddVar", "Const", "Add", "Sub", "Mul", "Div", "Min", "Max", "Clamp",
        "Compare", "And", "Or", "Not", "Equals", "KeyDown", "Key", "AxisX", "Time", "DeltaTime", "GetX",
        "Translate", "SetPosition", "Rotate", "Print", "PlaySound", "Spawn", "Destroy", "SetText", "SetColor",
        "SetActive", "Self", "FindObject", "ScoreAdd", "RandomRange", "ToString", "IsDead", "LoadScene",
    };
    int coreCount = 0;
    for (const char* t : core) {
        const VsDef* d = Find(t);
        CHECK(d != nullptr);
        if (!d) continue;
        ++coreCount;
        const VsNodeBrief b = VsNodeBriefFor(*d);
        CHECK(!Has(b.does, "text-script"));
        if (b.does.size() < 15) std::printf("  short 'does' for %s: %s\n", t, b.does.c_str());
        CHECK(b.does.size() >= 15);
        CHECK(b.does.back() == '.' && b.needs.back() == '.' && b.gives.back() == '.');
    }
    CHECK(coreCount >= 40);

    {   // specific content, read off the node table
        const VsNodeBrief tr = VsNodeBriefFor(*Find("Translate"));
        CHECK(Has(tr.needs, "a flow wire"));
        CHECK(Has(tr.needs, "X (Number)"));
        CHECK(Has(tr.needs, "Target (optional: empty means this object)"));
        CHECK(Has(tr.gives, "the flow continues"));
        const VsNodeBrief ev = VsNodeBriefFor(*Find("OnStart"));
        CHECK(Has(ev.needs, "it starts the flow"));
        CHECK(Has(ev.gives, "the flow it starts"));
        const VsNodeBrief kd = VsNodeBriefFor(*Find("KeyDown"));
        CHECK(Has(kd.gives, "Pressed (Bool)"));
        CHECK(!Has(kd.needs, "a flow wire"));
        const VsNodeBrief br = VsNodeBriefFor(*Find("Branch"));
        CHECK(Has(br.needs, "Condition (required: condition)"));
        CHECK(Has(br.gives, "flow out: True, False"));
        const VsNodeBrief sp = VsNodeBriefFor(*Find("Spawn"));
        CHECK(Has(sp.needs, "a prefab in its text box"));
        CHECK(VsNodeBriefFor(*Find("Timer")).does.find("Every Seconds.") == std::string::npos);   // not its own title
        CHECK(Has(sp.gives, "Object (Object)"));
        const VsNodeBrief fn = VsNodeBriefFor(*Find("Fn.print"));
        CHECK(Has(fn.does, "text-script function print"));
        CHECK(Has(fn.needs, "First (optional)"));
        const VsNodeBrief rb = VsNodeBriefFor(*Find("AddForce"));
        CHECK(Has(rb.needs, "Rigidbody3D/Rigidbody2D"));
    }
    CHECK(VsFirstSentence("One. Two.") == "One. Two.");   // too short on its own
    CHECK(VsFirstSentence("This is a long enough first sentence. Second.") == "This is a long enough first sentence.");
    CHECK(VsFirstSentence(std::string(300, 'x')).size() == 120);

    // ---- the connect text is generated from PinCompatible ----
    const auto& types = VsConcretePinTypes();
    CHECK(types.size() == 7);
    int pairs = 0, accepted = 0;
    for (PinType f : types) {
        const auto feeds = VsFeedsTypes(f);
        for (PinType t : types) {
            const bool ok = okay::vs::PinCompatible(f, t);
            bool listed = false;
            for (PinType x : feeds) if (x == t) listed = true;
            CHECK(ok == listed);
            const auto from = VsComesFromTypes(t);
            bool back = false;
            for (PinType x : from) if (x == f) back = true;
            CHECK(ok == back);
            CHECK((VsConversionNote(f, t) != nullptr) == (ok && f != t));
            ++pairs;
            if (ok) ++accepted;
        }
    }
    std::printf("  %d type pairs, %d accepted, %d refused\n", pairs, accepted, pairs - accepted);

    {
        const std::string n = VsPinConnectText(PinType::Number, true, false);
        CHECK(Has(n, "Connects to: Number, Bool and Text inputs"));
        CHECK(Has(n, "0 is false, anything else is true"));
        CHECK(Has(n, "Not into: Vector3, Object, List and Map."));
        CHECK(Has(n, "Make Vec3"));            // the hint for a Number that is wanted as a vector
        const std::string v = VsPinConnectText(PinType::Vec3, true, false);
        CHECK(Has(v, "Connects to: Text and Vector3 inputs"));
        CHECK(Has(v, "Vec X"));                // the hint for a vector that is wanted as a number
        const std::string in = VsPinConnectText(PinType::Bool, false, false);
        CHECK(Has(in, "Comes from: Number, Bool and Text outputs") == false);   // a Bool input takes Number, Bool, Object, not Text
        CHECK(Has(in, "Comes from: Number, Bool and Object outputs"));
        CHECK(Has(in, "true while the object still exists"));
        const std::string obj = VsPinConnectText(PinType::Object, false, false);
        CHECK(Has(obj, "Text") && Has(obj, "read as an object's name"));
        const std::string tin = VsPinConnectText(PinType::Text, false, false);   // a Text input takes everything
        CHECK(Has(tin, "Comes from: any value output."));
        CHECK(Has(tin, "Number, Bool, Vector3, Object, List and Map going into Text is written out as text."));
        CHECK(!Has(tin, "Not from:"));
        CHECK(Has(VsPinConnectText(PinType::Any, true, false), "type is not checked"));
        CHECK(Has(VsPinConnectText(PinType::Any, false, true), "Comes from: an event"));
        CHECK(Has(VsPinConnectText(PinType::Any, true, true), "flow input (triangle)"));
        const std::string txt = VsPinConnectText(PinType::Text, true, false);
        CHECK(Has(txt, "Connects to: Text and Object inputs"));
    }

    // ---- every suggested partner is a real pin that PinCompatible accepts ----
    int partnerChecks = 0;
    for (PinType t : types) {
        for (int asOut = 0; asOut < 2; ++asOut) {
            const auto ps = VsPinPartners(t, asOut == 1);
            CHECK(ps.size() <= 3);
            std::set<std::string> seen;
            for (const VsPinPartner& p : ps) {
                CHECK(seen.insert(p.node).second);   // no node twice
                bool found = false;
                for (const VsDef& d : Registry()) {
                    if (!d.title || p.node != d.title || std::strchr(d.type, '.')) continue;
                    const auto& pins = asOut ? d.dataIn : d.dataOut;
                    for (int i = 0; i < (int)pins.size(); ++i) {
                        if (p.pin != pins[i]) continue;
                        const PinType other = asOut ? okay::vs::InputType(d.type, i) : okay::vs::OutputType(d.type, i, "");
                        if (asOut ? okay::vs::PinCompatible(t, other) : okay::vs::PinCompatible(other, t)) found = true;
                    }
                }
                CHECK(found);
                ++partnerChecks;
            }
        }
    }
    CHECK(partnerChecks > 0);
    CHECK(VsPinPartners(PinType::Number, true).size() == 3);   // Number has plenty of everyday partners
    CHECK(VsPinPartners(PinType::Bool, true).size() >= 1);
    std::printf("  checked %d suggested partner pins\n", partnerChecks);

    TEST_MAIN_RESULT();
}
