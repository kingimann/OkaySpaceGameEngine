#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/MeshRenderer.hpp"
#include "okay/Components/Light.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Components/ActionList.hpp"
#include "okay/Components/CharacterController2D.hpp"
#include "okay/Components/CharacterController3D.hpp"
#include "okay/Components/FollowTarget2D.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Components/Mover.hpp"
#include "okay/Components/Spinner.hpp"
#include "okay/Components/Lifetime.hpp"
#include "okay/Components/CameraFollow.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/SpriteAnimator.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/Tilemap.hpp"
#include "okay/Components/TilemapCollider2D.hpp"
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Components/Canvas.hpp"
#include "okay/Components/EventSystem.hpp"
#include "okay/Components/UIDocument.hpp"
#include "okay/Components/UIImage.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Components/UISlider.hpp"
#include "okay/Components/UIToggle.hpp"

#include <cctype>
#include <functional>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace okay {

namespace {
// Quote a string for the line-based format (escapes quotes/backslashes).
std::string Quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

// Read a quoted token from a stream (assumes leading quote already trimmed by >>).
std::string ReadQuoted(std::istream& in) {
    std::string tok;
    in >> std::ws;
    if (in.peek() != '"') { in >> tok; return tok; }
    in.get(); // opening quote
    std::string out;
    char c;
    while (in.get(c)) {
        if (c == '\\') { if (in.get(c)) out += c; }
        else if (c == '"') break;
        else out += c;
    }
    return out;
}

// Read an optional trailing UI anchor enum (added in a later format version).
// Older scenes lack the field, so we only consume it when an integer follows;
// otherwise the anchor stays at its default (TopLeft).
void ReadAnchor(std::istream& in, UIAnchor& anchor) {
    in >> std::ws;
    int ch = in.peek();
    if (ch >= '0' && ch <= '9') {
        int a = 0; in >> a;
        anchor = static_cast<UIAnchor>(a);
    }
}

int IndexOf(const Scene& scene, const GameObject* go) {
    const auto& objs = scene.Objects();
    for (std::size_t i = 0; i < objs.size(); ++i)
        if (objs[i].get() == go) return static_cast<int>(i);
    return -1;
}

// Write a GameObject's Transform + all known components (no header/parent line).
void WriteComponents(std::ostream& out, GameObject* go) {
    Transform* t = go->transform;
    const Vec3& p = t->localPosition;
    const Quat& q = t->localRotation;
    const Vec3& s = t->localScale;
    out << "  transform " << p.x << " " << p.y << " " << p.z << " "
        << q.x << " " << q.y << " " << q.z << " " << q.w << " "
        << s.x << " " << s.y << " " << s.z << "\n";
    if (auto* sr = go->GetComponent<SpriteRenderer>()) {
        out << "  sprite " << static_cast<int>(sr->glyph) << " "
            << sr->color.r << " " << sr->color.g << " " << sr->color.b << " "
            << sr->color.a << " " << sr->size.x << " " << sr->size.y << " "
            << Quote(sr->texture)
            << " " << sr->uvMin.x << " " << sr->uvMin.y
            << " " << sr->uvMax.x << " " << sr->uvMax.y
            << " " << sr->sortOrder
            << " " << (sr->flipX ? 1 : 0) << " " << (sr->flipY ? 1 : 0) << "\n";
    }
    if (auto* cam = go->GetComponent<Camera>()) {
        out << "  camera " << (int)cam->projection << " " << cam->orthographicSize << " "
            << cam->fieldOfView << " "
            << cam->backgroundColor.r << " " << cam->backgroundColor.g << " "
            << cam->backgroundColor.b << " " << cam->backgroundColor.a << " "
            << (cam->main ? 1 : 0) << "\n";
    }
    if (auto* mr = go->GetComponent<MeshRenderer>()) {
        out << "  mesh " << Quote(mr->mesh.name.empty() ? "Cube" : mr->mesh.name) << " "
            << mr->color.r << " " << mr->color.g << " " << mr->color.b << " "
            << mr->color.a << " " << (mr->wireframe ? 1 : 0) << " "
            << Quote(mr->meshPath) << " " << (mr->doubleSided ? 1 : 0) << "\n";
        // Material (emissive rgb, specular, shininess, unlit) — separate record
        // so older scenes without it still load.
        out << "  material " << mr->emissive.r << " " << mr->emissive.g << " "
            << mr->emissive.b << " " << mr->specular << " " << mr->shininess << " "
            << (mr->unlit ? 1 : 0) << " " << Quote(mr->texture) << " "
            << mr->tiling.x << " " << mr->tiling.y << "\n";
    }
    if (auto* li = go->GetComponent<Light>()) {
        out << "  light " << li->color.r << " " << li->color.g << " " << li->color.b << " "
            << li->color.a << " " << li->ambient << " " << li->intensity << "\n";
    }
    if (auto* rb = go->GetComponent<Rigidbody2D>()) {
        out << "  rigidbody2d " << (int)rb->bodyType << " " << rb->gravityScale << " "
            << rb->mass << " " << rb->drag << " " << rb->bounciness << "\n";
    }
    if (auto* bc = go->GetComponent<BoxCollider2D>()) {
        out << "  boxcollider2d " << bc->size.x << " " << bc->size.y << " "
            << bc->offset.x << " " << bc->offset.y << " " << (bc->isTrigger ? 1 : 0)
            << " " << bc->layer << "\n";
    }
    if (auto* cc = go->GetComponent<CircleCollider2D>()) {
        out << "  circlecollider2d " << cc->radius << " "
            << cc->offset.x << " " << cc->offset.y << " " << (cc->isTrigger ? 1 : 0)
            << " " << cc->layer << "\n";
    }
    if (auto* cap = go->GetComponent<CapsuleCollider2D>()) {
        out << "  capsulecollider2d " << cap->size.x << " " << cap->size.y << " "
            << (int)cap->direction << " " << cap->offset.x << " " << cap->offset.y << " "
            << (cap->isTrigger ? 1 : 0) << " " << cap->layer << "\n";
    }
    if (auto* rb = go->GetComponent<Rigidbody3D>()) {
        out << "  rigidbody3d " << (int)rb->bodyType << " " << rb->gravityScale << " "
            << rb->mass << " " << rb->drag << " " << rb->bounciness << " "
            << (rb->freezeX ? 1 : 0) << " " << (rb->freezeY ? 1 : 0) << " "
            << (rb->freezeZ ? 1 : 0) << "\n";
    }
    if (auto* bc = go->GetComponent<BoxCollider3D>()) {
        out << "  boxcollider3d " << bc->size.x << " " << bc->size.y << " " << bc->size.z << " "
            << bc->offset.x << " " << bc->offset.y << " " << bc->offset.z << " "
            << (bc->isTrigger ? 1 : 0) << " " << bc->layer << "\n";
    }
    if (auto* sc = go->GetComponent<SphereCollider3D>()) {
        out << "  spherecollider3d " << sc->radius << " "
            << sc->offset.x << " " << sc->offset.y << " " << sc->offset.z << " "
            << (sc->isTrigger ? 1 : 0) << " " << sc->layer << "\n";
    }
    if (auto* cap = go->GetComponent<CapsuleCollider3D>()) {
        out << "  capsulecollider3d " << cap->radius << " " << cap->height << " " << cap->axis << " "
            << cap->offset.x << " " << cap->offset.y << " " << cap->offset.z << " "
            << (cap->isTrigger ? 1 : 0) << " " << cap->layer << "\n";
    }
    if (auto* sc = go->GetComponent<ScriptComponent>()) {
        out << "  script " << Quote(sc->Language()) << " " << Quote(sc->Source()) << "\n";
        if (!sc->Path().empty()) out << "  scriptpath " << Quote(sc->Path()) << "\n";
    }
    if (auto* vsc = go->GetComponent<VisualScriptComponent>()) {
        out << "  visualscript " << Quote(vsc->Source()) << "\n";
    }
    if (auto* al = go->GetComponent<ActionList>()) {
        out << "  actions " << Quote(al->ToText()) << "\n";
    }
    if (auto* mv = go->GetComponent<Mover>()) {
        out << "  mover " << mv->velocity.x << " " << mv->velocity.y << " " << mv->velocity.z << "\n";
    }
    if (auto* cc = go->GetComponent<CharacterController2D>()) {
        out << "  charctrl2d " << (int)cc->mode << " " << cc->speed << " " << cc->jumpForce << "\n";
    }
    if (auto* cc = go->GetComponent<CharacterController3D>()) {
        out << "  charctrl3d " << cc->speed << " " << cc->jumpForce << " " << (cc->canJump ? 1 : 0) << "\n";
    }
    if (auto* ft = go->GetComponent<FollowTarget2D>()) {
        out << "  follow2d " << Quote(ft->target) << " " << ft->speed << " " << ft->stopDistance << "\n";
    }
    if (auto* sp = go->GetComponent<Spinner>()) {
        out << "  spinner " << sp->angularVelocity.x << " " << sp->angularVelocity.y
            << " " << sp->angularVelocity.z << "\n";
    }
    if (auto* lt = go->GetComponent<Lifetime>()) {
        out << "  lifetime " << lt->seconds << "\n";
    }
    if (auto* cf = go->GetComponent<CameraFollow>()) {
        out << "  camerafollow " << Quote(cf->targetName) << " "
            << cf->offset.x << " " << cf->offset.y << " " << cf->offset.z << " "
            << cf->smoothing << "\n";
    }
    if (auto* tr = go->GetComponent<TextRenderer>()) {
        out << "  text " << Quote(tr->text) << " "
            << tr->color.r << " " << tr->color.g << " " << tr->color.b << " " << tr->color.a << " "
            << tr->pixelSize << " " << (tr->screenSpace ? 1 : 0) << " "
            << tr->screenPos.x << " " << tr->screenPos.y << " " << (int)tr->anchor << " "
            << (tr->shadow ? 1 : 0) << " "
            << tr->shadowColor.r << " " << tr->shadowColor.g << " " << tr->shadowColor.b << " " << tr->shadowColor.a << " "
            << tr->shadowOffset.x << " " << tr->shadowOffset.y << "\n";
    }
    if (auto* an = go->GetComponent<SpriteAnimator>()) {
        out << "  spriteanim " << an->fps << " " << (an->loop ? 1 : 0) << " "
            << (an->playing ? 1 : 0) << " " << an->frames.size();
        for (const auto& f : an->frames) out << " " << Quote(f);
        out << " " << an->atlasColumns << " " << an->atlasRows << " " << an->atlasCount << "\n";
    }
    if (auto* au = go->GetComponent<AudioSource>()) {
        out << "  audio " << Quote(au->clipPath) << " " << au->volume << " "
            << (au->loop ? 1 : 0) << " " << (au->playOnAwake ? 1 : 0) << " "
            << (au->spatial ? 1 : 0) << " " << au->minDistance << " " << au->maxDistance << "\n";
    }
    if (auto* tm = go->GetComponent<Tilemap>()) {
        out << "  tilemap " << tm->tileSize << " " << tm->Width() << " " << tm->Height();
        for (int t : tm->Tiles()) out << " " << t;
        out << "\n";
    }
    if (go->GetComponent<TilemapCollider2D>()) out << "  tilemapcollider\n";
    if (auto* btn = go->GetComponent<UIButton>()) {
        out << "  uibutton " << Quote(btn->label) << " "
            << btn->position.x << " " << btn->position.y << " "
            << btn->size.x << " " << btn->size.y << " "
            << btn->color.r << " " << btn->color.g << " " << btn->color.b << " " << btn->color.a << " "
            << btn->hoverColor.r << " " << btn->hoverColor.g << " " << btn->hoverColor.b << " " << btn->hoverColor.a << " "
            << btn->textColor.r << " " << btn->textColor.g << " " << btn->textColor.b << " " << btn->textColor.a
            << " " << (int)btn->anchor << " "
            << btn->pressedColor.r << " " << btn->pressedColor.g << " " << btn->pressedColor.b << " " << btn->pressedColor.a << " "
            << btn->disabledColor.r << " " << btn->disabledColor.g << " " << btn->disabledColor.b << " " << btn->disabledColor.a << " "
            << (btn->interactable ? 1 : 0) << " " << (btn->focusable ? 1 : 0) << "\n";
    }
    if (auto* pn = go->GetComponent<UIPanel>()) {
        out << "  uipanel " << pn->position.x << " " << pn->position.y << " "
            << pn->size.x << " " << pn->size.y << " "
            << pn->color.r << " " << pn->color.g << " " << pn->color.b << " " << pn->color.a
            << " " << (int)pn->anchor << "\n";
    }
    if (auto* doc = go->GetComponent<UIDocument>()) {
        out << "  uidocument " << Quote(doc->markup) << "\n";
    }
    if (auto* cv = go->GetComponent<Canvas>()) {
        out << "  canvas " << (int)cv->scaleMode << " "
            << cv->referenceResolution.x << " " << cv->referenceResolution.y << " "
            << cv->matchWidthOrHeight << " " << cv->scaleFactor << " " << cv->sortOrder << "\n";
    }
    if (go->GetComponent<EventSystem>()) {
        out << "  eventsystem\n";
    }
    if (auto* pb = go->GetComponent<UIProgressBar>()) {
        out << "  uiprogress " << pb->position.x << " " << pb->position.y << " "
            << pb->size.x << " " << pb->size.y << " " << pb->value << " "
            << pb->background.r << " " << pb->background.g << " " << pb->background.b << " " << pb->background.a << " "
            << pb->fill.r << " " << pb->fill.g << " " << pb->fill.b << " " << pb->fill.a
            << " " << (int)pb->anchor << "\n";
    }
    if (auto* im = go->GetComponent<UIImage>()) {
        out << "  uiimage " << im->position.x << " " << im->position.y << " "
            << im->size.x << " " << im->size.y << " "
            << im->color.r << " " << im->color.g << " " << im->color.b << " " << im->color.a << " "
            << Quote(im->texture) << " " << (int)im->anchor << " "
            << (im->nineSlice ? 1 : 0) << " " << im->border << "\n";
    }
    if (auto* sl = go->GetComponent<UISlider>()) {
        out << "  uislider " << sl->position.x << " " << sl->position.y << " "
            << sl->size.x << " " << sl->size.y << " "
            << sl->value << " " << sl->minValue << " " << sl->maxValue << " "
            << sl->background.r << " " << sl->background.g << " " << sl->background.b << " " << sl->background.a << " "
            << sl->fill.r << " " << sl->fill.g << " " << sl->fill.b << " " << sl->fill.a << " "
            << sl->knob.r << " " << sl->knob.g << " " << sl->knob.b << " " << sl->knob.a
            << " " << (int)sl->anchor << "\n";
    }
    if (auto* tg = go->GetComponent<UIToggle>()) {
        out << "  uitoggle " << Quote(tg->label) << " "
            << tg->position.x << " " << tg->position.y << " "
            << tg->size.x << " " << tg->size.y << " " << (tg->on ? 1 : 0) << " "
            << tg->boxColor.r << " " << tg->boxColor.g << " " << tg->boxColor.b << " " << tg->boxColor.a << " "
            << tg->checkColor.r << " " << tg->checkColor.g << " " << tg->checkColor.b << " " << tg->checkColor.a << " "
            << tg->textColor.r << " " << tg->textColor.g << " " << tg->textColor.b << " " << tg->textColor.a
            << " " << (int)tg->anchor << "\n";
    }
    if (auto* ps = go->GetComponent<ParticleSystem>()) {
        out << "  particles " << ps->emissionRate << " " << ps->maxParticles << " "
            << (ps->playing ? 1 : 0) << " " << ps->startLifetime << " " << ps->startSize << " "
            << ps->startColor.r << " " << ps->startColor.g << " " << ps->startColor.b << " "
            << ps->startColor.a << " " << ps->startVelocity.x << " " << ps->startVelocity.y << " "
            << ps->velocityRandom << " " << ps->gravity.x << " " << ps->gravity.y << " "
            << (ps->fadeOverLife ? 1 : 0) << " " << ps->seed << "\n";
    }
}
} // namespace

std::string SceneSerializer::Serialize(const Scene& scene) {
    std::ostringstream out;
    out << "okayscene 1\n";
    out << "name " << Quote(scene.Name()) << "\n";
    out << "gravity " << scene.physics().gravity.x << " " << scene.physics().gravity.y << "\n";
    const auto& objs = scene.Objects();
    for (std::size_t i = 0; i < objs.size(); ++i) {
        GameObject* go = objs[i].get();
        Transform* t = go->transform;
        out << "gameobject " << i << " " << Quote(go->name) << "\n";
        out << "  active " << (go->active ? 1 : 0) << "\n";
        if (!go->tag.empty()) out << "  tag " << Quote(go->tag) << "\n";
        int parent = t->Parent() ? IndexOf(scene, t->Parent()->gameObject) : -1;
        out << "  parent " << parent << "\n";
        WriteComponents(out, go);
        out << "end\n";
    }
    return out.str();
}

bool SceneSerializer::SaveToFile(const Scene& scene, const std::string& path) {
    std::ofstream f(path);
    if (!f) return false;
    f << Serialize(scene);
    return static_cast<bool>(f);
}

// Parse a document into `scene`. If `clear` is false, objects are appended
// (used by Instantiate); `firstNew` receives the first created GameObject.
static bool ParseInto(Scene& scene, const std::string& text, bool clear,
                      GameObject** firstNew, std::string* error) {
    if (clear) scene.Clear();
    std::istringstream in(text);
    std::string token;

    if (!(in >> token) || token != "okayscene") {
        if (error) *error = "missing 'okayscene' header";
        return false;
    }
    int version = 0; in >> version;

    // file index -> created GameObject, plus deferred parent links.
    std::unordered_map<int, GameObject*> byIndex;
    std::vector<std::pair<int, int>> parentLinks; // child index -> parent index

    while (in >> token) {
        if (token == "name") {
            scene.SetName(ReadQuoted(in));
        } else if (token == "gravity") {
            Vec2 g; in >> g.x >> g.y;
            scene.physics().gravity = g;
        } else if (token == "gameobject") {
            int idx = -1;
            in >> idx;
            std::string name = ReadQuoted(in);
            GameObject* go = scene.CreateGameObject(name);
            if (firstNew && !*firstNew) *firstNew = go;
            byIndex[idx] = go;

            std::string field;
            while (in >> field && field != "end") {
                if (field == "active") { int a = 1; in >> a; go->active = (a != 0); }
                else if (field == "tag") { go->tag = ReadQuoted(in); }
                else if (field == "parent") { int p = -1; in >> p; if (p >= 0) parentLinks.push_back({idx, p}); }
                else if (field == "transform") {
                    Vec3 p, s; Quat q;
                    in >> p.x >> p.y >> p.z >> q.x >> q.y >> q.z >> q.w >> s.x >> s.y >> s.z;
                    go->transform->localPosition = p;
                    go->transform->localRotation = q;
                    go->transform->localScale = s;
                } else if (field == "sprite") {
                    int glyph = 0; Color c; Vec2 size;
                    in >> glyph >> c.r >> c.g >> c.b >> c.a >> size.x >> size.y;
                    auto* sr = go->AddComponent<SpriteRenderer>();
                    sr->glyph = static_cast<char>(glyph);
                    sr->color = c;
                    sr->size = size;
                    in >> std::ws; // optional texture path (quoted)
                    if (in.peek() == '"') sr->texture = ReadQuoted(in);
                    // Optional uv sub-region (4 floats) for sprite sheets.
                    in >> std::ws;
                    int pk = in.peek();
                    if (pk == '-' || pk == '.' || std::isdigit(pk)) {
                        in >> sr->uvMin.x >> sr->uvMin.y >> sr->uvMax.x >> sr->uvMax.y;
                        in >> std::ws; // optional sortOrder follows uv
                        int pk2 = in.peek();
                        if (pk2 == '-' || std::isdigit(pk2)) {
                            in >> sr->sortOrder;
                            in >> std::ws; // optional flipX/flipY follow sortOrder
                            if (std::isdigit(in.peek())) {
                                int fx = 0, fy = 0; in >> fx >> fy;
                                sr->flipX = (fx != 0); sr->flipY = (fy != 0);
                            }
                        }
                    }
                } else if (field == "camera") {
                    Color c; int proj = 0; float ortho = 5.0f, fov = 60.0f; int main = 1;
                    in >> proj >> ortho >> fov >> c.r >> c.g >> c.b >> c.a >> main;
                    auto* cam = go->AddComponent<Camera>();
                    cam->projection = (Camera::Projection)proj;
                    cam->orthographicSize = ortho;
                    cam->fieldOfView = fov;
                    cam->backgroundColor = c;
                    cam->main = (main != 0);
                } else if (field == "mesh") {
                    std::string kind = ReadQuoted(in);
                    Color c; int wire = 1;
                    in >> c.r >> c.g >> c.b >> c.a >> wire;
                    auto* mr = go->AddComponent<MeshRenderer>();
                    mr->mesh = Mesh::FromName(kind);
                    mr->color = c;
                    mr->wireframe = (wire != 0);
                    in >> std::ws; // optional .OBJ model path (quoted)
                    if (in.peek() == '"') {
                        mr->meshPath = ReadQuoted(in);
                        if (!mr->meshPath.empty()) {
                            bool ok = false;
                            Mesh loaded = Mesh::LoadOBJ(mr->meshPath, &ok);
                            if (ok && !loaded.vertices.empty()) mr->mesh = loaded;
                        }
                    }
                    in >> std::ws; // optional double-sided flag
                    if (std::isdigit(in.peek())) { int ds = 0; in >> ds; mr->doubleSided = (ds != 0); }
                } else if (field == "material") {
                    if (auto* mr = go->GetComponent<MeshRenderer>()) {
                        Color e; float spec = 0, shin = 16; int unlit = 0;
                        in >> e.r >> e.g >> e.b >> spec >> shin >> unlit;
                        mr->emissive = e; mr->specular = spec; mr->shininess = shin;
                        mr->unlit = (unlit != 0);
                        in >> std::ws; // optional texture path (quoted)
                        if (in.peek() == '"') mr->texture = ReadQuoted(in);
                        in >> std::ws; // optional texture tiling (u v)
                        int p = in.peek();
                        if (std::isdigit(p) || p == '-' || p == '.')
                            in >> mr->tiling.x >> mr->tiling.y;
                    }
                } else if (field == "light") {
                    auto* li = go->AddComponent<Light>();
                    Color c;
                    in >> c.r >> c.g >> c.b >> c.a >> li->ambient >> li->intensity;
                    li->color = c;
                } else if (field == "rigidbody2d") {
                    int bt = 0; float gs = 1, mass = 1, drag = 0, bounce = 0;
                    in >> bt >> gs >> mass >> drag >> bounce;
                    auto* rb = go->AddComponent<Rigidbody2D>();
                    rb->bodyType = (Rigidbody2D::BodyType)bt;
                    rb->gravityScale = gs; rb->mass = mass; rb->drag = drag; rb->bounciness = bounce;
                } else if (field == "boxcollider2d") {
                    Vec2 sz, off; int trig = 0, layer = 0;
                    in >> sz.x >> sz.y >> off.x >> off.y >> trig;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> layer; // optional
                    auto* bc = go->AddComponent<BoxCollider2D>();
                    bc->size = sz; bc->offset = off; bc->isTrigger = (trig != 0); bc->layer = layer;
                } else if (field == "circlecollider2d") {
                    float r = 0.5f; Vec2 off; int trig = 0, layer = 0;
                    in >> r >> off.x >> off.y >> trig;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> layer; // optional
                    auto* cc = go->AddComponent<CircleCollider2D>();
                    cc->radius = r; cc->offset = off; cc->isTrigger = (trig != 0); cc->layer = layer;
                } else if (field == "capsulecollider2d") {
                    Vec2 sz{1, 2}, off; int dir = 0, trig = 0, layer = 0;
                    in >> sz.x >> sz.y >> dir >> off.x >> off.y >> trig;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> layer; // optional
                    auto* cap = go->AddComponent<CapsuleCollider2D>();
                    cap->size = sz; cap->direction = (CapsuleCollider2D::Direction)dir;
                    cap->offset = off; cap->isTrigger = (trig != 0); cap->layer = layer;
                } else if (field == "rigidbody3d") {
                    int bt = 0; float gs = 1, mass = 1, drag = 0, bounce = 0;
                    int fx = 0, fy = 0, fz = 0;
                    in >> bt >> gs >> mass >> drag >> bounce >> fx >> fy >> fz;
                    auto* rb = go->AddComponent<Rigidbody3D>();
                    rb->bodyType = (Rigidbody3D::BodyType)bt;
                    rb->gravityScale = gs; rb->mass = mass; rb->drag = drag; rb->bounciness = bounce;
                    rb->freezeX = (fx != 0); rb->freezeY = (fy != 0); rb->freezeZ = (fz != 0);
                } else if (field == "boxcollider3d") {
                    Vec3 sz{1, 1, 1}, off; int trig = 0, layer = 0;
                    in >> sz.x >> sz.y >> sz.z >> off.x >> off.y >> off.z >> trig;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> layer;
                    auto* bc = go->AddComponent<BoxCollider3D>();
                    bc->size = sz; bc->offset = off; bc->isTrigger = (trig != 0); bc->layer = layer;
                } else if (field == "spherecollider3d") {
                    float r = 0.5f; Vec3 off; int trig = 0, layer = 0;
                    in >> r >> off.x >> off.y >> off.z >> trig;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> layer;
                    auto* sc = go->AddComponent<SphereCollider3D>();
                    sc->radius = r; sc->offset = off; sc->isTrigger = (trig != 0); sc->layer = layer;
                } else if (field == "capsulecollider3d") {
                    float r = 0.5f, h = 2.0f; int ax = 1; Vec3 off; int trig = 0, layer = 0;
                    in >> r >> h >> ax >> off.x >> off.y >> off.z >> trig;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> layer;
                    auto* cap = go->AddComponent<CapsuleCollider3D>();
                    cap->radius = r; cap->height = h; cap->axis = ax;
                    cap->offset = off; cap->isTrigger = (trig != 0); cap->layer = layer;
                } else if (field == "script") {
                    std::string lang = ReadQuoted(in);
                    std::string src  = ReadQuoted(in);
                    auto* sc = go->AddComponent<ScriptComponent>(lang);
                    sc->LoadSource(src);
                } else if (field == "scriptpath") {
                    std::string p = ReadQuoted(in);
                    if (auto* sc = go->GetComponent<ScriptComponent>()) sc->SetPath(p);
                } else if (field == "visualscript") {
                    std::string src = ReadQuoted(in);
                    auto* vsc = go->AddComponent<VisualScriptComponent>();
                    vsc->LoadFromText(src);
                } else if (field == "actions") {
                    go->AddComponent<ActionList>()->FromText(ReadQuoted(in));
                } else if (field == "charctrl2d") {
                    int m = 0; float sp = 5, jf = 9;
                    in >> m >> sp >> jf;
                    auto* cc = go->AddComponent<CharacterController2D>();
                    cc->mode = (CharacterController2D::Mode)m; cc->speed = sp; cc->jumpForce = jf;
                } else if (field == "follow2d") {
                    std::string tn = ReadQuoted(in);
                    float sp = 3, sd = 0; in >> sp >> sd;
                    auto* ft = go->AddComponent<FollowTarget2D>();
                    ft->target = tn; ft->speed = sp; ft->stopDistance = sd;
                } else if (field == "charctrl3d") {
                    float sp = 5, jf = 6; int cj = 1;
                    in >> sp >> jf >> cj;
                    auto* cc = go->AddComponent<CharacterController3D>();
                    cc->speed = sp; cc->jumpForce = jf; cc->canJump = (cj != 0);
                } else if (field == "mover") {
                    Vec3 v; in >> v.x >> v.y >> v.z;
                    go->AddComponent<Mover>()->velocity = v;
                } else if (field == "spinner") {
                    Vec3 v; in >> v.x >> v.y >> v.z;
                    go->AddComponent<Spinner>()->angularVelocity = v;
                } else if (field == "lifetime") {
                    float s = 1.0f; in >> s;
                    go->AddComponent<Lifetime>()->seconds = s;
                } else if (field == "camerafollow") {
                    std::string tgt = ReadQuoted(in);
                    Vec3 off; float sm = 5.0f;
                    in >> off.x >> off.y >> off.z >> sm;
                    auto* cf = go->AddComponent<CameraFollow>();
                    cf->targetName = tgt; cf->offset = off; cf->smoothing = sm;
                } else if (field == "text") {
                    std::string str = ReadQuoted(in);
                    Color c; float px = 0.1f; int ss = 0; Vec2 sp;
                    in >> c.r >> c.g >> c.b >> c.a >> px >> ss >> sp.x >> sp.y;
                    auto* tr = go->AddComponent<TextRenderer>();
                    tr->text = str; tr->color = c; tr->pixelSize = px;
                    tr->screenSpace = (ss != 0); tr->screenPos = sp;
                    ReadAnchor(in, tr->anchor);   // optional trailing field
                    // Optional shadow block (added later; absent in older files).
                    in >> std::ws;
                    int sk = in.peek();
                    if (sk >= '0' && sk <= '9') {
                        int sh = 0; Color sc; Vec2 so;
                        in >> sh >> sc.r >> sc.g >> sc.b >> sc.a >> so.x >> so.y;
                        tr->shadow = (sh != 0); tr->shadowColor = sc; tr->shadowOffset = so;
                    }
                } else if (field == "spriteanim") {
                    float fps = 8.0f; int loop = 1, playing = 1, count = 0;
                    in >> fps >> loop >> playing >> count;
                    auto* an = go->AddComponent<SpriteAnimator>();
                    an->fps = fps; an->loop = (loop != 0); an->playing = (playing != 0);
                    for (int k = 0; k < count; ++k) an->frames.push_back(ReadQuoted(in));
                    in >> std::ws; // optional atlas fields (3 ints)
                    if (std::isdigit(in.peek()))
                        in >> an->atlasColumns >> an->atlasRows >> an->atlasCount;
                } else if (field == "audio") {
                    std::string cp = ReadQuoted(in);
                    float vol = 1.0f; int loop = 0, poa = 0;
                    in >> vol >> loop >> poa;
                    auto* au = go->AddComponent<AudioSource>();
                    au->clipPath = cp; au->volume = vol;
                    au->loop = (loop != 0); au->playOnAwake = (poa != 0);
                    in >> std::ws; // optional 3D fields: spatial min max
                    if (std::isdigit(in.peek())) {
                        int sp = 0; in >> sp >> au->minDistance >> au->maxDistance;
                        au->spatial = (sp != 0);
                    }
                } else if (field == "tilemap") {
                    float ts = 1.0f; int tw = 0, th = 0;
                    in >> ts >> tw >> th;
                    auto* tm = go->AddComponent<Tilemap>();
                    tm->tileSize = ts; tm->Resize(tw, th);
                    for (int y = 0; y < th; ++y)
                        for (int x = 0; x < tw; ++x) { int id = 0; in >> id; tm->SetTile(x, y, id); }
                } else if (field == "tilemapcollider") {
                    go->AddComponent<TilemapCollider2D>();
                } else if (field == "uibutton") {
                    auto* btn = go->AddComponent<UIButton>();
                    btn->label = ReadQuoted(in);
                    Color c, h, t;
                    in >> btn->position.x >> btn->position.y >> btn->size.x >> btn->size.y
                       >> c.r >> c.g >> c.b >> c.a >> h.r >> h.g >> h.b >> h.a
                       >> t.r >> t.g >> t.b >> t.a;
                    btn->color = c; btn->hoverColor = h; btn->textColor = t;
                    ReadAnchor(in, btn->anchor);
                    // Optional trailing block (pressed/disabled colors + interactable).
                    in >> std::ws;
                    int pk = in.peek();
                    if (pk >= '0' && pk <= '9') {
                        Color pc, dc; int inter = 1;
                        in >> pc.r >> pc.g >> pc.b >> pc.a
                           >> dc.r >> dc.g >> dc.b >> dc.a >> inter;
                        btn->pressedColor = pc; btn->disabledColor = dc;
                        btn->interactable = (inter != 0);
                        // focusable appended later still within this block.
                        in >> std::ws;
                        int fk = in.peek();
                        if (fk >= '0' && fk <= '9') { int foc = 1; in >> foc; btn->focusable = (foc != 0); }
                    }
                } else if (field == "uipanel") {
                    auto* pn = go->AddComponent<UIPanel>();
                    Color c;
                    in >> pn->position.x >> pn->position.y >> pn->size.x >> pn->size.y
                       >> c.r >> c.g >> c.b >> c.a;
                    pn->color = c;
                    ReadAnchor(in, pn->anchor);
                } else if (field == "uidocument") {
                    auto* doc = go->AddComponent<UIDocument>();
                    doc->markup = ReadQuoted(in);
                } else if (field == "canvas") {
                    auto* cv = go->AddComponent<Canvas>();
                    int sm = 0;
                    in >> sm >> cv->referenceResolution.x >> cv->referenceResolution.y
                       >> cv->matchWidthOrHeight >> cv->scaleFactor >> cv->sortOrder;
                    cv->scaleMode = (Canvas::ScaleMode)sm;
                } else if (field == "eventsystem") {
                    go->AddComponent<EventSystem>();
                } else if (field == "uiprogress") {
                    auto* pb = go->AddComponent<UIProgressBar>();
                    Color bg, fl;
                    in >> pb->position.x >> pb->position.y >> pb->size.x >> pb->size.y >> pb->value
                       >> bg.r >> bg.g >> bg.b >> bg.a >> fl.r >> fl.g >> fl.b >> fl.a;
                    pb->background = bg; pb->fill = fl;
                    ReadAnchor(in, pb->anchor);
                } else if (field == "uiimage") {
                    auto* im = go->AddComponent<UIImage>();
                    Color c;
                    in >> im->position.x >> im->position.y >> im->size.x >> im->size.y
                       >> c.r >> c.g >> c.b >> c.a;
                    im->color = c;
                    im->texture = ReadQuoted(in);
                    ReadAnchor(in, im->anchor);
                    // Optional nine-slice block (added later; absent in older files).
                    in >> std::ws;
                    int nk = in.peek();
                    if (nk >= '0' && nk <= '9') {
                        int ns = 0; in >> ns >> im->border; im->nineSlice = (ns != 0);
                    }
                } else if (field == "uislider") {
                    auto* sl = go->AddComponent<UISlider>();
                    Color bg, fl, kn;
                    in >> sl->position.x >> sl->position.y >> sl->size.x >> sl->size.y
                       >> sl->value >> sl->minValue >> sl->maxValue
                       >> bg.r >> bg.g >> bg.b >> bg.a >> fl.r >> fl.g >> fl.b >> fl.a
                       >> kn.r >> kn.g >> kn.b >> kn.a;
                    sl->background = bg; sl->fill = fl; sl->knob = kn;
                    ReadAnchor(in, sl->anchor);
                } else if (field == "uitoggle") {
                    auto* tg = go->AddComponent<UIToggle>();
                    tg->label = ReadQuoted(in);
                    Color b, c, t;
                    int on = 0;
                    in >> tg->position.x >> tg->position.y >> tg->size.x >> tg->size.y >> on
                       >> b.r >> b.g >> b.b >> b.a >> c.r >> c.g >> c.b >> c.a
                       >> t.r >> t.g >> t.b >> t.a;
                    tg->on = (on != 0);
                    tg->boxColor = b; tg->checkColor = c; tg->textColor = t;
                    ReadAnchor(in, tg->anchor);
                } else if (field == "particles") {
                    auto* ps = go->AddComponent<ParticleSystem>();
                    int playing = 1, fade = 1;
                    unsigned long long seed = 0;
                    in >> ps->emissionRate >> ps->maxParticles >> playing
                       >> ps->startLifetime >> ps->startSize
                       >> ps->startColor.r >> ps->startColor.g >> ps->startColor.b >> ps->startColor.a
                       >> ps->startVelocity.x >> ps->startVelocity.y >> ps->velocityRandom
                       >> ps->gravity.x >> ps->gravity.y >> fade >> seed;
                    ps->playing = (playing != 0);
                    ps->fadeOverLife = (fade != 0);
                    ps->seed = static_cast<std::uint64_t>(seed);
                } else {
                    if (error) *error = "unknown field '" + field + "'";
                    return false;
                }
            }
        } else {
            if (error) *error = "unexpected token '" + token + "'";
            return false;
        }
    }

    // Resolve parent links now that every object exists.
    for (auto& [childIdx, parentIdx] : parentLinks) {
        auto c = byIndex.find(childIdx);
        auto p = byIndex.find(parentIdx);
        if (c != byIndex.end() && p != byIndex.end())
            c->second->transform->SetParent(p->second->transform, /*worldPositionStays=*/false);
    }
    return true;
}

bool SceneSerializer::Deserialize(Scene& scene, const std::string& text, std::string* error) {
    return ParseInto(scene, text, /*clear=*/true, /*firstNew=*/nullptr, error);
}

std::string SceneSerializer::SerializeObject(const GameObject& root) {
    // Gather the object and all its descendants (depth-first).
    std::vector<GameObject*> subtree;
    std::function<void(GameObject*)> gather = [&](GameObject* go) {
        subtree.push_back(go);
        for (Transform* child : go->transform->Children()) gather(child->gameObject);
    };
    gather(const_cast<GameObject*>(&root));

    std::unordered_map<const GameObject*, int> localIndex;
    for (std::size_t i = 0; i < subtree.size(); ++i) localIndex[subtree[i]] = (int)i;

    std::ostringstream out;
    out << "okayscene 1\n";
    for (std::size_t i = 0; i < subtree.size(); ++i) {
        GameObject* go = subtree[i];
        out << "gameobject " << i << " " << Quote(go->name) << "\n";
        out << "  active " << (go->active ? 1 : 0) << "\n";
        if (!go->tag.empty()) out << "  tag " << Quote(go->tag) << "\n";
        Transform* parent = go->transform->Parent();
        int pIdx = -1;
        if (parent) {
            auto it = localIndex.find(parent->gameObject);
            if (it != localIndex.end()) pIdx = it->second;
        }
        out << "  parent " << pIdx << "\n";
        WriteComponents(out, go);
        out << "end\n";
    }
    return out.str();
}

GameObject* SceneSerializer::Instantiate(Scene& scene, const GameObject& prefab) {
    std::string text = SerializeObject(prefab);
    GameObject* root = nullptr;
    ParseInto(scene, text, /*clear=*/false, &root, nullptr);
    return root;
}

bool SceneSerializer::SaveObjectToFile(const GameObject& root, const std::string& path) {
    std::ofstream f(path);
    if (!f) return false;
    f << SerializeObject(root);
    return static_cast<bool>(f);
}

GameObject* SceneSerializer::InstantiateFromFile(Scene& scene, const std::string& path,
                                                 std::string* error) {
    std::ifstream f(path);
    if (!f) { if (error) *error = "cannot open " + path; return nullptr; }
    std::stringstream ss; ss << f.rdbuf();
    GameObject* root = nullptr;
    if (!ParseInto(scene, ss.str(), /*clear=*/false, &root, error)) return nullptr;
    return root;
}

GameObject* SceneSerializer::InstantiateFromText(Scene& scene, const std::string& text,
                                                 std::string* error) {
    GameObject* root = nullptr;
    if (!ParseInto(scene, text, /*clear=*/false, &root, error)) return nullptr;
    return root;
}

std::vector<std::string> SceneSerializer::CollectAssetPaths(const Scene& scene) {
    std::vector<std::string> out;
    auto add = [&](const std::string& p) {
        if (p.empty()) return;
        for (const auto& e : out) if (e == p) return; // unique
        out.push_back(p);
    };
    for (const auto& go : scene.Objects()) {
        if (auto* sr = go->GetComponent<SpriteRenderer>()) add(sr->texture);
        if (auto* au = go->GetComponent<AudioSource>())    add(au->clipPath);
        if (auto* mr = go->GetComponent<MeshRenderer>())   { add(mr->meshPath); add(mr->texture); }
        if (auto* im = go->GetComponent<UIImage>())         add(im->texture);
        if (auto* an = go->GetComponent<SpriteAnimator>())
            for (const auto& f : an->frames) add(f);
    }
    return out;
}

bool SceneSerializer::LoadFromFile(Scene& scene, const std::string& path, std::string* error) {
    std::ifstream f(path);
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    std::stringstream ss;
    ss << f.rdbuf();
    return Deserialize(scene, ss.str(), error);
}

} // namespace okay
