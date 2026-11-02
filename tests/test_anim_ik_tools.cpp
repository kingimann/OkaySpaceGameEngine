#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scripting/OkayScriptVM.hpp"
#include "okay/Scripting/NextScriptVM.hpp"
#include "okay/VisualScript/VsApi.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include <cmath>
#include <string>

using namespace okay;

static vs::VsValue G(OkayScriptVM& vm, const char* n) { return vm.GetGlobal(n); }

int main() {
    RUN_SUITE("anim_ik_tools");

    // ---- OkayScript builtins ----
    {
        Scene sc;
        GameObject* root = sc.CreateGameObject("Hero");
        auto* base = root->AddComponent<AnimStateMachine>();
        base->layer = 0;
        auto* upper = root->AddComponent<AnimStateMachine>();
        upper->layer = 1;
        upper->layerName = "Upper";
        upper->layerWeight = 1.0f;
        GameObject* model = sc.CreateGameObject("Model");
        model->transform->SetParent(root->transform, false);
        auto* ma = model->AddComponent<ModelAnimator>();
        ma->rootMotion = false;
        auto* limb = root->AddComponent<LimbIK>();
        auto* chain = root->AddComponent<ChainIK>();
        auto* look = root->AddComponent<LookAtIK>();
        auto* aim = root->AddComponent<AimIK>();
        auto* feet = root->AddComponent<FootIK>();
        auto* fb = root->AddComponent<FullBodyIK>();
        limb->weight = look->weight = aim->weight = chain->weight = fb->weight = feet->weight = 1.0f;
        GameObject* marker = sc.CreateGameObject("Marker");
        marker->transform->SetPosition({2, 1, 4});
        // IK on a child: the kind calls look there too.
        GameObject* child = sc.CreateGameObject("Rig");
        child->transform->SetParent(root->transform, false);
        auto* childFeet = child->AddComponent<FootIK>();
        childFeet->weight = 1.0f;

        OkayScriptVM vm;
        ScriptHost host;
        host.gameObject = root;
        host.transform = root->transform;
        vm.Bind(&host);
        std::string err;
        const bool loaded = vm.Load(
            "anim_set_float(\"speed\", 3.5)\n"
            "anim_set_bool(\"grounded\", 1)\n"
            "anim_set_float(\"pose\", 2)\n"
            "gf = anim_get_float(\"speed\")\n"
            "gb = anim_get_bool(\"grounded\")\n"
            "gi = anim_get_int(\"pose\")\n"
            "gz = anim_get_float(\"missing\")\n"
            "lw = anim_set_layer_weight(1, 0.4)\n"
            "ln = anim_set_layer_weight(\"Upper\", 0.25)\n"
            "lg = anim_get_layer_weight(\"Upper\")\n"
            "li = anim_get_layer_weight(1)\n"
            "lb = anim_get_layer_weight(0)\n"
            "miss = anim_set_layer_weight(9, 1)\n"
            "ik_set_weight(0.5)\n"
            "kf = ik_set_weight(\"foot\", 0.2)\n"
            "kl = ik_set_weight(\"look\", 0.3)\n"
            "ka = ik_set_weight(\"aim\", 0.4)\n"
            "kh = ik_set_weight(\"hand_l\", 0.6)\n"
            "kr = ik_set_weight(\"hand_r\", 0.7)\n"
            "ku = ik_set_weight(\"full\", 0.15)\n"
            "bad = ik_set_weight(\"nope\", 1)\n"
            "ik_set_target(8, 9, 10)\n"
            "th = ik_set_target(\"hand_r\", 1, 2, 3)\n"
            "tl = ik_set_target(\"look\", \"Marker\")\n"
            "ta = ik_set_target(\"aim\", 4, 5, 6)\n"
            "tm = ik_set_target(\"hand_l\", \"Missing\")\n"
            "rm = set_root_motion(1)\n"
            "ro = set_root_motion(0)\n"
            "sm = set_character_model(\"no-such-model.glb\")\n",
            &err);
        if (!err.empty()) std::printf("  script error: %s\n", err.c_str());
        CHECK(loaded);
        CHECK_NEAR(G(vm, "gf").AsFloat(), 3.5f, 1e-4f);
        CHECK_NEAR(G(vm, "gb").AsFloat(), 1.0f, 1e-4f);
        CHECK_NEAR(G(vm, "gi").AsFloat(), 2.0f, 1e-4f);
        CHECK_NEAR(G(vm, "gz").AsFloat(), 0.0f, 1e-4f);
        CHECK_NEAR(G(vm, "lw").AsFloat(), 1.0f, 1e-4f);
        CHECK_NEAR(G(vm, "ln").AsFloat(), 1.0f, 1e-4f);
        CHECK_NEAR(upper->layerWeight, 0.25f, 1e-4f);
        CHECK_NEAR(G(vm, "lg").AsFloat(), 0.25f, 1e-4f);
        CHECK_NEAR(G(vm, "li").AsFloat(), 0.25f, 1e-4f);
        CHECK_NEAR(base->layerWeight, 1.0f, 1e-4f);
        CHECK_NEAR(G(vm, "lb").AsFloat(), 1.0f, 1e-4f);
        CHECK_NEAR(G(vm, "miss").AsFloat(), 0.0f, 1e-4f);

        // A number still sets every solver on this object, and leaves Foot IK alone.
        CHECK_NEAR(limb->weight, 0.5f, 1e-4f);
        CHECK_NEAR(chain->weight, 0.5f, 1e-4f);
        CHECK_NEAR(G(vm, "kf").AsFloat(), 1.0f, 1e-4f);
        CHECK_NEAR(feet->weight, 0.2f, 1e-4f);
        CHECK_NEAR(childFeet->weight, 0.2f, 1e-4f);
        CHECK_NEAR(look->weight, 0.3f, 1e-4f);
        CHECK_NEAR(aim->weight, 0.4f, 1e-4f);
        CHECK_NEAR(fb->weight, 0.15f, 1e-4f);
        CHECK(fb->effectors[FullBodyIK::LeftHand].enabled);
        CHECK_NEAR(fb->effectors[FullBodyIK::LeftHand].positionWeight, 0.6f, 1e-4f);
        CHECK_NEAR(fb->effectors[FullBodyIK::RightHand].positionWeight, 0.7f, 1e-4f);
        CHECK_NEAR(G(vm, "bad").AsFloat(), 0.0f, 1e-4f);

        // Numeric target still aims Limb / Look / Aim at a point.
        CHECK_NEAR(limb->target.x, 8.0f, 1e-4f);
        CHECK_NEAR(look->target.z, 10.0f, 1e-4f);
        CHECK_NEAR(G(vm, "th").AsFloat(), 1.0f, 1e-4f);
        CHECK_NEAR(G(vm, "tl").AsFloat(), 1.0f, 1e-4f);
        CHECK(look->targetObject == marker->transform);
        CHECK(look->targetName == "Marker");
        CHECK(aim->ikActive);
        CHECK_NEAR(aim->target.y, 5.0f, 1e-4f);
        CHECK(aim->targetObject == nullptr);
        CHECK_NEAR(G(vm, "tm").AsFloat(), 0.0f, 1e-4f);
        CHECK(fb->effectors[FullBodyIK::RightHand].enabled);
        CHECK_NEAR(fb->effectors[FullBodyIK::RightHand].position.y, 2.0f, 1e-4f);
        CHECK(vm.Load("cl = ik_clear_target(\"look\")\nch = ik_clear_target(\"hand_r\")\n", &err));
        CHECK_NEAR(G(vm, "cl").AsFloat(), 1.0f, 1e-4f);
        CHECK(look->targetObject == nullptr);
        CHECK(!look->aimAtPoint);
        CHECK_NEAR(G(vm, "ch").AsFloat(), 1.0f, 1e-4f);
        CHECK(!fb->effectors[FullBodyIK::RightHand].enabled);

        CHECK_NEAR(G(vm, "rm").AsFloat(), 1.0f, 1e-4f);
        CHECK_NEAR(G(vm, "ro").AsFloat(), 1.0f, 1e-4f);
        CHECK(!ma->rootMotion);
        CHECK_NEAR(G(vm, "sm").AsFloat(), 0.0f, 1e-4f);
    }

    // ---- Typed script (okayscript-next) and the generated visual-script nodes ----
    {
        CHECK(vs::VsApiFind("Animator.GetFloat/1") && !vs::VsApiFind("Animator.GetFloat/1")->exec);
        CHECK(vs::VsApiFind("Animator.GetBool/1") && vs::VsApiFind("Animator.GetInt/1"));
        CHECK(vs::VsApiFind("Animator.SetLayerWeight/2"));
        CHECK(vs::VsApiFind("Animator.SetLayerWeight/2:if"));
        CHECK(vs::VsApiFind("Animator.GetLayerWeight/1"));
        CHECK(vs::VsApiFind("Animator.GetLayerWeight/1:i"));
        CHECK(vs::VsApiFind("Animator.SetRootMotion/1") && vs::VsApiFind("Animator.SetRootMotion/1")->exec);
        CHECK(vs::VsApiFind("Animator.SetCharacterModel/1"));
        CHECK(vs::VsApiFind("IK.SetWeight/1"));
        CHECK(vs::VsApiFind("IK.SetKindWeight/2") && vs::VsApiFind("IK.SetKindWeight/2")->exec);
        CHECK(vs::VsApiFind("IK.SetLimbTarget/2") && vs::VsApiFind("IK.SetLimbTarget/4"));
        CHECK(vs::VsApiFind("IK.ClearLimbTarget/1"));
        CHECK(vs::MakeApiNode("IK.SetKindWeight/2") != nullptr);

        Scene sc;
        GameObject* hero = sc.CreateGameObject("Hero");
        auto* sm = hero->AddComponent<AnimStateMachine>();
        sm->layer = 0;
        auto* upper = hero->AddComponent<AnimStateMachine>();
        upper->layer = 1;
        upper->layerName = "Upper";
        auto* ma = hero->AddComponent<ModelAnimator>();
        auto* feet = hero->AddComponent<FootIK>();
        auto* look = hero->AddComponent<LookAtIK>();
        auto* fb = hero->AddComponent<FullBodyIK>();
        feet->weight = look->weight = 1.0f;
        GameObject* marker = sc.CreateGameObject("Marker");
        auto* typed = hero->AddComponent<ScriptComponent>(NextScriptVM::kLanguage);
        std::string err;
        CHECK(typed->LoadSource(
            "class T : Behaviour {\n"
            "    public float speed; public bool grounded; public int pose;\n"
            "    public float upperW; public bool rooted; public bool modelMissing;\n"
            "    public bool kind; public bool reached; public bool cleared;\n"
            "    void Start() {\n"
            "        Animator a = GetComponent<Animator>();\n"
            "        a.SetFloat(\"speed\", 3.5f);\n"
            "        a.SetBool(\"grounded\", true);\n"
            "        a.SetFloat(\"pose\", 2);\n"
            "        speed = a.GetFloat(\"speed\");\n"
            "        grounded = a.GetBool(\"grounded\");\n"
            "        pose = a.GetInt(\"pose\");\n"
            "        a.SetLayerWeight(1, 0.4f);\n"
            "        a.SetLayerWeight(\"Upper\", 0.25f);\n"
            "        upperW = a.GetLayerWeight(\"Upper\");\n"
            "        rooted = a.SetRootMotion(true);\n"
            "        modelMissing = a.SetCharacterModel(\"no-such-model.glb\");\n"
            "        IK ik = GetComponent<IK>();\n"
            "        kind = ik.SetKindWeight(\"foot\", 0.2f);\n"
            "        ik.SetKindWeight(\"look\", 0.35f);\n"
            "        ik.SetKindWeight(\"hand_l\", 0.5f);\n"
            "        reached = ik.SetLimbTarget(\"look\", \"Marker\");\n"
            "        ik.SetLimbTarget(\"hand_r\", 1, 2, 3);\n"
            "        cleared = ik.ClearLimbTarget(\"hand_r\");\n"
            "    }\n"
            "}\n", &err));
        if (!err.empty()) std::printf("  typed script: %s\n", err.c_str());
        sc.Start();
        auto V = [&](const char* n) { return typed->VM()->GetGlobal(n); };
        CHECK_NEAR(V("speed").AsFloat(), 3.5f, 1e-4f);
        CHECK(V("grounded").AsBool());
        CHECK_NEAR(V("pose").AsFloat(), 2.0f, 1e-4f);
        CHECK_NEAR(upper->layerWeight, 0.25f, 1e-4f);
        CHECK_NEAR(V("upperW").AsFloat(), 0.25f, 1e-4f);
        CHECK(V("rooted").AsBool() && ma->rootMotion);
        CHECK(!V("modelMissing").AsBool());
        CHECK(V("kind").AsBool());
        CHECK_NEAR(feet->weight, 0.2f, 1e-4f);
        CHECK_NEAR(look->weight, 0.35f, 1e-4f);
        CHECK(V("reached").AsBool());
        CHECK(look->targetObject == marker->transform);
        CHECK(fb->effectors[FullBodyIK::LeftHand].enabled);
        CHECK_NEAR(fb->effectors[FullBodyIK::LeftHand].positionWeight, 0.5f, 1e-4f);
        CHECK(V("cleared").AsBool());
        CHECK(!fb->effectors[FullBodyIK::RightHand].enabled);

        auto* vs = hero->AddComponent<VisualScriptComponent>();
        const std::string graph =
            "node 1 OnStart\n"
            "node 2 IK.SetKindWeight/2\n"
            "node 3 Const \"foot\"\n"
            "node 4 Const 0.55\n"
            "node 5 Animator.GetFloat/1\n"
            "node 6 Const \"speed\"\n"
            "node 7 SetVar spd\n"
            "exec 1 0 2\nexec 2 0 7\n"
            "data 2 0 3 0\ndata 2 1 4 0\n"
            "data 5 0 6 0\ndata 7 0 5 0\n"
            "entry OnStart 1\n";
        std::string gerr;
        CHECK(vs->LoadFromText(graph, &gerr));
        if (!gerr.empty()) std::printf("  graph: %s\n", gerr.c_str());
        sc.Start();
        CHECK_NEAR(feet->weight, 0.55f, 1e-4f);
        CHECK_NEAR(vs->GetVariable("spd").AsFloat(), 3.5f, 1e-4f);
    }

    TEST_MAIN_RESULT();
}
