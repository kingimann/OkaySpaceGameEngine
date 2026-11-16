// Builds a skinned, animatable Guided human in a scene. Written from the engine's own SkinnedMesh and humanoid bone rules
// (Components/SkinnedMesh.hpp, Animation/HumanoidBones.hpp); no outside code.
#include "okay/Modeling/GuidedRig.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/SkinnedMesh.hpp"
#include <cmath>
#include <map>
#include <tuple>

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
    // which part each vertex came from decides which bones may move it (a hand never follows a leg, even when it touches the thigh)
    std::map<std::tuple<long long, long long, long long>, unsigned char> owner;
    for (const GeneratedPart& p : Generate(recipe))
        for (const Vec3& v : p.mesh.verts) owner[{std::llround(v.x * 1e4), std::llround(v.y * 1e4), std::llround(v.z * 1e4)}] = RigGroupsOfSlot(p.slot);
    std::vector<unsigned char> groups(mesh.vertices.size(), 0);
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        const Vec3& v = mesh.vertices[i];
        auto it = owner.find({std::llround(v.x * 1e4), std::llround(v.y * 1e4), std::llround(v.z * 1e4)});
        if (it != owner.end()) groups[i] = it->second;
    }
    SkinVertices(mesh.vertices, joints, sm->jointIdx, sm->jointWt, &groups);
    return root;
}

} // namespace okay::modeling::guided
