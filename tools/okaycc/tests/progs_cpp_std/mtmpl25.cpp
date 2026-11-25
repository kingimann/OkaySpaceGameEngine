// Member function templates declared in the class and defined after it ([temp.mem] p1,
// [temp.arg.explicit]), called with explicit arguments through '.' and '->'.
#include <cstdio>
#include <memory>
#include <vector>
struct Component { virtual ~Component() {} int id = 0; };
struct Light : Component { int lum = 5; };
struct Mesh : Component { int tris = 9; };
class GameObject {
public:
    template <typename T>
    T *GetComponent() const;
    template <typename T>
    std::vector<T *> GetComponents() const;
    template <typename T>
    T *Add() { auto p = std::make_unique<T>(); T *r = p.get(); m_c.push_back(std::move(p)); return r; }
    template <typename T>
    T *GetOrAdd() { if (T *e = GetComponent<T>()) return e; return Add<T>(); }
    bool HasMesh() const { return GetComponent<Mesh>() != nullptr && this->GetComponent<Light>(); }
private:
    std::vector<std::unique_ptr<Component>> m_c;
};
template <typename T>
T *GameObject::GetComponent() const {
    for (std::size_t i = 0; i < m_c.size(); ++i)
        if (T *c = dynamic_cast<T *>(m_c[i].get())) return c;
    return nullptr;
}
template <typename T>
std::vector<T *> GameObject::GetComponents() const {
    std::vector<T *> out;
    for (std::size_t i = 0; i < m_c.size(); ++i)
        if (T *c = dynamic_cast<T *>(m_c[i].get())) out.push_back(c);
    return out;
}
int main() {
    GameObject g;
    g.Add<Light>()->id = 1;
    g.Add<Mesh>()->id = 2;
    g.Add<Light>()->id = 3;
    const GameObject *go = &g;
    const Light *l = go ? go->GetComponent<Light>() : nullptr;
    std::printf("%d %d\n", l->id, l->lum);
    if (go->GetComponent<Mesh>()) std::printf("mesh %d\n", g.GetComponent<Mesh>()->tris);
    for (Light *x : go->GetComponents<Light>()) std::printf("light %d\n", x->id);
    std::printf("%d\n", (int)g.GetComponents<Component>().size());
    GameObject h;
    std::printf("%d %d\n", h.GetOrAdd<Mesh>()->tris, (int)h.HasMesh());
    h.GetOrAdd<Mesh>()->tris = 4;
    std::printf("%d %d %d\n", h.GetOrAdd<Mesh>()->tris, (int)h.GetComponents<Mesh>().size(), (int)g.HasMesh());
}
