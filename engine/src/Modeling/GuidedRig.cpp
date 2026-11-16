// Builds a skinned, animatable Guided human in a scene. Written from the engine's own SkinnedMesh and humanoid bone rules
// (Components/SkinnedMesh.hpp, Animation/HumanoidBones.hpp); no outside code.
#include "okay/Modeling/GuidedRig.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/SkinnedMesh.hpp"

namespace okay::modeling::guided {

GameObject* BuildRiggedHuman(Scene& scene, const Recipe& recipe, const std::string& name, std::string* why) {
    std::vector<RigJoint> joints;
    if (!HumanoidSkeleton(recipe, joints)) { if (why) *why = "only a human has a skeleton"; return nullptr; }
    const PolyMesh pm = Create(recipe);
    Mesh mesh = pm.ToMesh();
    mesh.name = "";
    if (DetailOf(recipe) >= 0.9f && (int)recipe.Param("style", 0) != 1) mesh.ComputeAutoSmoothNormals(40.0f);   // the same shading the editor gives a human

    GameObject* root = scene.CreateGameObject(name);
    std::vector<GameObject*> bones(joints.size(), nullptr);
    for (size_t i = 0; i < joints.size(); ++i) {
        GameObject* b = scene.CreateGameObject(joints[i].name);
        const int p = joints[i].parent;
        b->transform->SetParent(p < 0 ? root->transform : bones[(size_t)p]->transform, false);
        b->transform->localPosition = p < 0 ? joints[i].pos : joints[i].pos - joints[(size_t)p].pos;
        bones[i] = b;
    }
    GameObject* body = scene.CreateGameObject("Body");
    body->transform->SetParent(root->transform, false);
    auto* mr = body->AddComponent<MeshRenderer>();
    mr->mesh = mesh;
    mr->color = Color{1, 1, 1, 1};   // faces carry their own colours
    auto* sm = body->AddComponent<SkinnedMesh>();
    sm->bind = mesh;
    for (size_t i = 0; i < bones.size(); ++i) {
        sm->joints.push_back(bones[i]->transform);
        sm->jointNames.push_back(joints[i].name);
        sm->inverseBind.push_back(bones[i]->transform->LocalToWorldMatrix().Inverse());
    }
    SkinVertices(mesh.vertices, joints, sm->jointIdx, sm->jointWt);
    return root;
}

} // namespace okay::modeling::guided
