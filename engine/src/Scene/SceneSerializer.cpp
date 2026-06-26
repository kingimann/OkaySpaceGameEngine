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
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Components/VehicleController.hpp"
#include "okay/Components/VehicleController2D.hpp"
#include "okay/Components/BlockBuilder.hpp"
#include "okay/Components/SurvivalStats.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Components/SurvivalAfflictions.hpp"
#include "okay/Components/SurvivalSystems.hpp"
#include "okay/Components/SurvivalZone.hpp"
#include "okay/Components/Consumables.hpp"
#include "okay/Components/DayNightCycle.hpp"
#include "okay/Components/NPCController.hpp"
#include "okay/Components/Crafting.hpp"
#include "okay/Components/MeleeAttacker.hpp"
#include "okay/Components/Spawner.hpp"
#include "okay/Components/CraftingMenu.hpp"
#include "okay/Components/ThirdPersonShooterController.hpp"
#include "okay/Components/TopDownController.hpp"
#include "okay/Components/FreeRoamController.hpp"
#include "okay/Components/ClickToMoveController.hpp"
#include "okay/Components/FollowTarget2D.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Physics/Collider2D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Components/Mover.hpp"
#include "okay/Components/Spinner.hpp"
#include "okay/Components/Lifetime.hpp"
#include "okay/Components/Stats.hpp"
#include "okay/Components/Inventory.hpp"
#include "okay/Components/TurnManager.hpp"
#include "okay/Components/CameraFollow.hpp"
#include "okay/Components/DollyPath.hpp"
#include "okay/Components/VirtualCamera.hpp"
#include "okay/Components/CinemachineBrain.hpp"
#include "okay/Components/TextRenderer.hpp"
#include "okay/Components/SpriteAnimator.hpp"
#include "okay/Components/Animator.hpp"
#include "okay/Components/AudioSource.hpp"
#include "okay/Components/Tilemap.hpp"
#include "okay/Components/TilemapCollider2D.hpp"
#include "okay/Components/ParticleSystem.hpp"
#include "okay/Components/UIButton.hpp"
#include "okay/Components/UIPanel.hpp"
#include "okay/Components/Canvas.hpp"
#include "okay/Components/UIScrollView.hpp"
#include "okay/Components/UILayoutGroup.hpp"
#include "okay/Components/UIInputField.hpp"
#include "okay/Components/WorldUI.hpp"
#include "okay/Components/WorldSpaceUI.hpp"
#include "okay/Components/UIDropdown.hpp"
#include "okay/Components/UITooltip.hpp"
#include "okay/Components/UITextBind.hpp"
#include "okay/Components/UIBarBind.hpp"
#include "okay/Components/UIDraggable.hpp"
#include "okay/Components/Draggable.hpp"
#include "okay/Components/EventSystem.hpp"
#include "okay/Components/UIDocument.hpp"
#include "okay/Net/NetworkManager.hpp"
#include "okay/Components/Terrain.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/UIImage.hpp"
#include "okay/Components/UIProgressBar.hpp"
#include "okay/Components/UIRadialProgress.hpp"
#include "okay/Components/Minimap.hpp"
#include "okay/Components/Crosshair.hpp"
#include "okay/Components/UISlider.hpp"
#include "okay/Components/UIStepper.hpp"
#include "okay/Components/UIRating.hpp"
#include "okay/Components/UIToggle.hpp"
#include "okay/Components/UITabs.hpp"

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

// Read the three shared publish toggles trailing every stat_* line.
void ReadStatTail(std::istream& in, StatComponent* c) {
    int pp = 1, pb = 1, sm = 1; in >> pp >> pb >> sm;
    c->publishPrefs = (pp != 0); c->publishBar = (pb != 0); c->sendMessages = (sm != 0);
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
            << (cam->main ? 1 : 0)
            << " " << (int)cam->clearFlags << " " << cam->depth
            << " " << cam->nearClip << " " << cam->farClip
            << " " << (cam->fovAxisHorizontal ? 1 : 0)
            << " " << cam->rectX << " " << cam->rectY << " " << cam->rectW << " " << cam->rectH
            << " " << cam->cullingMask
            << " " << (cam->physicalCamera ? 1 : 0) << " " << cam->focalLength << " " << cam->sensorHeight
            << "\n";
    }
    // A Terrain owns its (generated) mesh, so don't serialize the big MeshRenderer
    // geometry for it — the component below rebuilds it on load.
    if (auto* mr = go->GetComponent<MeshRenderer>();
        mr && !go->GetComponent<Terrain>() && !go->GetComponent<Character>()) {
        out << "  mesh " << Quote(mr->mesh.name) << " "
            << mr->color.r << " " << mr->color.g << " " << mr->color.b << " "
            << mr->color.a << " " << (mr->wireframe ? 1 : 0) << " "
            << Quote(mr->meshPath) << " " << (mr->doubleSided ? 1 : 0) << "\n";
        // Persist edited/custom geometry: a mesh with no primitive name and no
        // .OBJ path is hand-edited, so its vertices/triangles can't be regenerated
        // from a name — write them verbatim. This record follows the `mesh` line
        // and overwrites the placeholder geometry on load.
        if (mr->mesh.name.empty() && mr->meshPath.empty() && !mr->mesh.vertices.empty()) {
            out << "  meshgeo " << mr->mesh.vertices.size();
            for (const Vec3& v : mr->mesh.vertices)
                out << " " << v.x << " " << v.y << " " << v.z;
            out << " " << mr->mesh.triangles.size();
            for (int t : mr->mesh.triangles) out << " " << t;
            out << "\n";
        }
        // Material (emissive rgb, specular, shininess, unlit) — separate record
        // so older scenes without it still load.
        out << "  material " << mr->emissive.r << " " << mr->emissive.g << " "
            << mr->emissive.b << " " << mr->specular << " " << mr->shininess << " "
            << (mr->unlit ? 1 : 0) << " " << Quote(mr->texture) << " "
            << mr->tiling.x << " " << mr->tiling.y << "\n";
        // Normal map (separate record so older scenes still load).
        if (!mr->normalMap.empty())
            out << "  normalmap " << Quote(mr->normalMap) << " " << mr->normalStrength << "\n";
        // Environment reflectivity + metalness (separate record, metalness is an
        // optional trailing field so older scenes still load).
        if (mr->reflectivity > 0.0f || mr->metallic > 0.0f)
            out << "  reflect " << mr->reflectivity << " " << mr->metallic << "\n";
        // Specular/gloss map (separate record so older scenes still load).
        if (!mr->specularMap.empty())
            out << "  specmap " << Quote(mr->specularMap) << "\n";
        // Shading model (separate record so older scenes still load).
        if (mr->shader != MeshRenderer::Shader::Standard)
            out << "  shader " << (int)mr->shader << " " << mr->toonBands << "\n";
        // Per-material rim (Fresnel) backlight (separate record).
        if (mr->rimStrength > 0.0f)
            out << "  rim " << mr->rimStrength << " " << mr->rimPower << " "
                << mr->rimColor.r << " " << mr->rimColor.g << " " << mr->rimColor.b << "\n";
        // Silhouette outline (separate record).
        if (mr->outline)
            out << "  outline " << mr->outlineWidth << " "
                << mr->outlineColor.r << " " << mr->outlineColor.g << " " << mr->outlineColor.b << "\n";
        // Scrolling UV + triplanar mapping (separate record).
        if (mr->uvScroll.x != 0.0f || mr->uvScroll.y != 0.0f || mr->triplanar)
            out << "  uvanim " << mr->uvScroll.x << " " << mr->uvScroll.y << " "
                << (mr->triplanar ? 1 : 0) << "\n";
    }
    if (auto* tr = go->GetComponent<Terrain>()) {
        out << "  terrain " << tr->resolution << " " << tr->size << " "
            << tr->color.r << " " << tr->color.g << " " << tr->color.b << " " << tr->color.a
            << " " << tr->heights.size();
        for (float h : tr->heights) out << " " << h;
        // Auto-color "layers" (optional trailing fields; older scenes lack them).
        auto wcol = [&](const Color& c) { out << " " << c.r << " " << c.g << " " << c.b << " " << c.a; };
        out << " " << (tr->autoColor ? 1 : 0);
        wcol(tr->waterColor); wcol(tr->sandColor); wcol(tr->grassColor);
        wcol(tr->rockColor); wcol(tr->snowColor);
        out << " " << tr->waterLevel << " " << tr->snowLevel << " " << tr->rockSlope;
        out << "\n";
    }
    if (auto* ch = go->GetComponent<Character>()) out << "  character " << ch->ToText() << "\n";
    if (auto* li = go->GetComponent<Light>()) {
        out << "  light " << li->color.r << " " << li->color.g << " " << li->color.b << " "
            << li->color.a << " " << li->ambient << " " << li->intensity
            << " " << (int)li->type << " " << li->range << " " << li->spotAngle
            << " " << li->spotSoftness << " " << (li->useTemperature ? 1 : 0) << " " << li->temperature
            << " " << li->ambientColor.r << " " << li->ambientColor.g << " " << li->ambientColor.b << "\n";
    }
    if (auto* rb = go->GetComponent<Rigidbody2D>()) {
        out << "  rigidbody2d " << (int)rb->bodyType << " " << rb->gravityScale << " "
            << rb->mass << " " << rb->drag << " " << rb->bounciness << "\n";
    }
    if (auto* bc = go->GetComponent<BoxCollider2D>()) {
        out << "  boxcollider2d " << bc->size.x << " " << bc->size.y << " "
            << bc->offset.x << " " << bc->offset.y << " " << (bc->isTrigger ? 1 : 0)
            << " " << bc->layer << " " << (bc->autoFit ? 1 : 0) << "\n";
    }
    if (auto* cc = go->GetComponent<CircleCollider2D>()) {
        out << "  circlecollider2d " << cc->radius << " "
            << cc->offset.x << " " << cc->offset.y << " " << (cc->isTrigger ? 1 : 0)
            << " " << cc->layer << " " << (cc->autoFit ? 1 : 0) << "\n";
    }
    if (auto* cap = go->GetComponent<CapsuleCollider2D>()) {
        out << "  capsulecollider2d " << cap->size.x << " " << cap->size.y << " "
            << (int)cap->direction << " " << cap->offset.x << " " << cap->offset.y << " "
            << (cap->isTrigger ? 1 : 0) << " " << cap->layer << " " << (cap->autoFit ? 1 : 0) << "\n";
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
            << (bc->isTrigger ? 1 : 0) << " " << bc->layer << " " << (bc->autoFit ? 1 : 0) << "\n";
    }
    if (auto* sc = go->GetComponent<SphereCollider3D>()) {
        out << "  spherecollider3d " << sc->radius << " "
            << sc->offset.x << " " << sc->offset.y << " " << sc->offset.z << " "
            << (sc->isTrigger ? 1 : 0) << " " << sc->layer << " " << (sc->autoFit ? 1 : 0) << "\n";
    }
    if (auto* cap = go->GetComponent<CapsuleCollider3D>()) {
        out << "  capsulecollider3d " << cap->radius << " " << cap->height << " " << cap->axis << " "
            << cap->offset.x << " " << cap->offset.y << " " << cap->offset.z << " "
            << (cap->isTrigger ? 1 : 0) << " " << cap->layer << " " << (cap->autoFit ? 1 : 0) << "\n";
    }
    // Each script is one self-contained line (lang, source, then optional path
    // and field overrides) so a GameObject can carry several scripts.
    for (auto* sc : go->GetComponents<ScriptComponent>()) {
        out << "  script " << Quote(sc->Language()) << " " << Quote(sc->Source())
            << " " << Quote(sc->Path()) << " " << sc->fields.size();
        for (const auto& kv : sc->fields) out << " " << Quote(kv.first) << " " << Quote(kv.second);
        out << "\n";
    }
    if (auto* vsc = go->GetComponent<VisualScriptComponent>()) {
        out << "  visualscript " << Quote(vsc->Source()) << "\n";
    }
    for (auto* al : go->GetComponents<ActionList>()) {
        out << "  actions " << Quote(al->ToText()) << "\n";
    }
    if (auto* mv = go->GetComponent<Mover>()) {
        out << "  mover " << mv->velocity.x << " " << mv->velocity.y << " " << mv->velocity.z << "\n";
    }
    if (auto* cc = go->GetComponent<CharacterController2D>()) {
        out << "  charctrl2d " << (int)cc->mode << " " << cc->speed << " " << cc->jumpForce
            // extended (optional, back-compatible trailing fields)
            << " " << cc->runSpeed << " " << (cc->sprintKey ? cc->sprintKey : '-')
            << " " << cc->acceleration << " " << cc->deceleration
            << " " << (cc->normalizeDiagonal ? 1 : 0) << " " << (cc->useGamepad ? 1 : 0)
            << " " << (cc->flipSprite ? 1 : 0) << " " << cc->maxJumps
            << " " << (cc->variableJump ? 1 : 0) << " " << cc->jumpCutMultiplier
            << " " << cc->coyoteTime << " " << cc->jumpBuffer << " " << cc->airControl
            << " " << cc->maxFallSpeed << " " << cc->extraFallGravity << "\n";
    }
    if (auto* cc = go->GetComponent<CharacterController3D>()) {
        out << "  charctrl3d " << cc->speed << " " << cc->jumpForce << " " << (cc->canJump ? 1 : 0) << "\n";
    }
    if (auto* v = go->GetComponent<VehicleController>()) {
        out << "  vehicle " << v->acceleration << " " << v->maxSpeed << " " << v->reverseSpeed
            << " " << v->brakeForce << " " << v->drag << " " << v->turnSpeed << " " << v->grip
            << " " << v->handbrakeGrip << " " << v->groundCheckDistance
            << " " << (v->followCamera ? 1 : 0) << " " << v->camDistance << " " << v->camHeight
            << " " << v->camLerp << " " << (int)(unsigned char)v->handbrakeKey << "\n";
        if (v->suspension)
            out << "  vehiclesusp 1 " << v->rideHeight << " " << v->springStrength << " "
                << v->springDamping << " " << v->suspensionTravel << " " << v->wheelBase << " "
                << v->trackWidth << " " << v->bodyLean << " " << v->maxTilt << " " << v->tiltSmooth << "\n";
    }
    if (auto* bb = go->GetComponent<BlockBuilder>()) {
        out << "  blockbuilder " << bb->blockSize << " " << bb->reach << " "
            << bb->blockColor.r << " " << bb->blockColor.g << " " << bb->blockColor.b << " " << bb->blockColor.a
            << " " << bb->placeButton << " " << bb->removeButton
            << " " << Quote(bb->blockTag) << " " << Quote(bb->blockTexture)
            << " " << (bb->showPreview ? 1 : 0) << " " << (bb->showCrosshair ? 1 : 0) << "\n";
    }
    if (auto* v2 = go->GetComponent<VehicleController2D>()) {
        out << "  vehicle2d " << v2->acceleration << " " << v2->maxSpeed << " " << v2->reverseSpeed
            << " " << v2->brakeForce << " " << v2->drag << " " << v2->turnSpeed << " " << v2->grip
            << " " << v2->handbrakeGrip << " " << (v2->sideView ? 1 : 0)
            << " " << (int)(unsigned char)v2->handbrakeKey << "\n";
    }
    if (auto* sv = go->GetComponent<SurvivalStats>()) {
        out << "  survival " << sv->maxHealth << " " << sv->armor << " " << sv->regenWhenFed
            << " " << sv->regenDelay << " " << sv->maxHunger << " " << sv->hungerDrain << " "
            << sv->starveDamage << " " << sv->maxThirst << " " << sv->thirstDrain << " "
            << sv->dehydrateDamage << " " << sv->maxStamina << " " << sv->staminaRegen << " "
            << sv->sprintCost << " " << sv->sprintDrainMult << " " << sv->maxOxygen << " "
            << sv->oxygenDrain << " " << sv->oxygenRefill << " " << sv->drownDamage << " "
            << sv->maxWarmth << " " << sv->coldDrain << " " << sv->warmRegen << " "
            << sv->freezeDamage << " " << (sv->publishPrefs ? 1 : 0) << " "
            << (sv->publishBars ? 1 : 0) << " " << (sv->sendMessages ? 1 : 0)
            << " " << sv->resistance << " " << (sv->godMode ? 1 : 0) << " " << sv->invincibleTime << "\n";
    }
    auto statTail = [](std::ostream& o, StatComponent* c) {
        o << " " << (c->publishPrefs ? 1 : 0) << " " << (c->publishBar ? 1 : 0)
          << " " << (c->sendMessages ? 1 : 0) << "\n";
    };
    // The 3 shared toggles without the trailing newline, so extra fields can follow.
    auto statToggles = [](std::ostream& o, StatComponent* c) {
        o << " " << (c->publishPrefs ? 1 : 0) << " " << (c->publishBar ? 1 : 0)
          << " " << (c->sendMessages ? 1 : 0);
    };
    if (auto* c = go->GetComponent<HealthStat>()) {
        out << "  stat_health " << c->maxHealth << " " << c->armor << " " << c->regenPerSecond
            << " " << c->regenDelay << " " << c->lowThreshold;
        statToggles(out, c);
        out << " " << c->resistance << " " << c->minDamage << " " << (c->godMode ? 1 : 0)
            << " " << c->invincibleTime << " " << c->overhealMax << " " << c->overhealDecay
            << " " << (c->destroyOnDeath ? 1 : 0) << " " << (c->respawn ? 1 : 0)
            << " " << c->respawnDelay << " " << c->lives << "\n";
    }
    if (auto* c = go->GetComponent<HungerStat>()) {
        out << "  stat_hunger " << c->maxHunger << " " << c->drainPerSecond << " "
            << c->sprintMultiplier << " " << c->lowThreshold;
        statToggles(out, c);
        out << " " << c->starveDamage << " " << c->overeatMax << " " << (c->slowWhenStarving ? 1 : 0)
            << " " << c->minSpeedFactor << "\n";
    }
    if (auto* c = go->GetComponent<ThirstStat>()) {
        out << "  stat_thirst " << c->maxThirst << " " << c->drainPerSecond << " "
            << c->sprintMultiplier << " " << c->lowThreshold;
        statToggles(out, c);
        out << " " << c->dehydrateDamage << " " << c->overdrinkMax << " "
            << (c->slowWhenDehydrated ? 1 : 0) << " " << c->minSpeedFactor << "\n";
    }
    if (auto* c = go->GetComponent<StaminaStat>()) {
        out << "  stat_stamina " << c->maxStamina << " " << c->regenPerSecond << " " << c->regenDelay
            << " " << c->sprintCost << " " << c->jumpCost << " " << c->exhaustedUntil;
        statToggles(out, c);
        out << " " << c->lowThreshold << " " << (c->slowWhenExhausted ? 1 : 0) << " "
            << c->minSpeedFactor << " " << c->regenScale << "\n";
    }
    if (auto* c = go->GetComponent<OxygenStat>()) {
        out << "  stat_oxygen " << c->maxOxygen << " " << c->drainPerSecond << " "
            << c->refillPerSecond;
        statToggles(out, c);
        out << " " << c->lowThreshold << " " << c->drownDamage << "\n";
    }
    if (auto* c = go->GetComponent<TemperatureStat>()) {
        out << "  stat_temp " << c->maxWarmth << " " << c->coldDrain << " "
            << c->warmRegen;
        statToggles(out, c);
        out << " " << c->lowThreshold << " " << c->freezeDamage << "\n";
    }
    if (auto* c = go->GetComponent<SleepStat>()) {
        out << "  stat_sleep " << c->maxEnergy << " " << c->drainPerSecond << " "
            << c->restPerSecond << " " << c->tiredThreshold;
        statToggles(out, c);
        out << " " << c->exhaustDamage << " " << (c->slowWhenTired ? 1 : 0) << " "
            << c->minSpeedFactor << "\n";
    }
    if (auto* c = go->GetComponent<SanityStat>()) {
        out << "  stat_sanity " << c->maxSanity << " " << c->drainInDark << " "
            << c->regenInLight << " " << c->lowThreshold;
        statToggles(out, c);
        out << " " << c->insaneDamage << "\n";
    }
    if (auto* c = go->GetComponent<RadiationStat>()) {
        out << "  stat_radiation " << c->maxRadiation << " " << c->gainPerSecond << " "
            << c->decayPerSecond << " " << c->sickThreshold << " " << c->damagePerSecond; statTail(out, c);
    }
    if (auto* c = go->GetComponent<BleedingStat>()) {
        out << "  stat_bleed " << c->maxBleed << " " << c->damagePerSecond << " "
            << c->clotPerSecond; statTail(out, c);
    }
    if (auto* c = go->GetComponent<PoisonStat>()) {
        out << "  stat_poison " << c->maxPoison << " " << c->damagePerSecond << " "
            << c->decayPerSecond; statTail(out, c);
    }
    if (auto* c = go->GetComponent<WetnessStat>()) {
        out << "  stat_wet " << c->maxWetness << " " << c->soakPerSecond << " " << c->dryPerSecond
            << " " << c->chillPerSecond << " " << c->soakedThreshold; statTail(out, c);
    }
    if (auto* c = go->GetComponent<CarryWeightStat>()) {
        out << "  stat_carry " << c->maxLoad << " " << c->overStaminaDrain << " "
            << c->minSpeedFactor; statTail(out, c);
    }
    if (auto* c = go->GetComponent<StatusEffectStat>()) {
        out << "  statuseffect " << (c->sendMessages ? 1 : 0) << "\n";
    }
    if (auto* c = go->GetComponent<SurvivalSave>()) {
        out << "  survivalsave " << Quote(c->saveKey) << " " << (c->loadOnStart ? 1 : 0)
            << " " << (c->saveContinuously ? 1 : 0) << "\n";
    }
    if (auto* c = go->GetComponent<SurvivalZone>()) {
        out << "  survivalzone " << c->effect << " " << c->amount << " " << c->duration
            << " " << Quote(c->effectName) << "\n";
    }
    if (auto* c = go->GetComponent<Consumables>()) {
        out << "  consumables " << (c->requireInventory ? 1 : 0) << " " << (c->consumeOnDrop ? 1 : 0)
            << " " << (c->destroyDroppedItem ? 1 : 0) << " " << c->recipes.size();
        for (const auto& r : c->recipes)
            out << " " << Quote(r.item) << " " << Quote(r.action) << " " << r.amount;
        out << "\n";
    }
    if (auto* c = go->GetComponent<DayNightCycle>()) {
        auto col = [&](const Color& k) { out << " " << k.r << " " << k.g << " " << k.b; };
        out << "  daynight " << c->dayLengthSeconds << " " << c->time << " " << (c->paused ? 1 : 0)
            << " " << (c->controlSun ? 1 : 0) << " " << (c->rotateSun ? 1 : 0) << " " << (c->controlSky ? 1 : 0)
            << " " << c->dayIntensity << " " << c->nightIntensity << " " << c->dayAmbient << " " << c->nightAmbient;
        col(c->dayLight); col(c->nightLight); col(c->skyDay); col(c->skyHorizon); col(c->skyNight);
        out << "\n";
    }
    if (auto* c = go->GetComponent<NPCController>()) {
        out << "  npc " << c->behavior << " " << c->moveSpeed << " " << c->sightRange << " "
            << c->wanderRadius << " " << c->attackRange << " " << c->attackDamage << " "
            << c->attackInterval << " " << (c->faceMovement ? 1 : 0) << " " << Quote(c->targetName)
            << " " << c->maxHealth << " " << (c->invulnerable ? 1 : 0) << "\n";
    }
    if (auto* c = go->GetComponent<MeleeAttacker>()) {
        out << "  melee " << c->damage << " " << c->range << " " << c->arc << " " << c->cooldown
            << " " << (int)(unsigned char)c->attackKey << " " << (c->useMouse ? 1 : 0) << "\n";
    }
    if (auto* c = go->GetComponent<Spawner>()) {
        out << "  spawner " << Quote(c->templateName) << " " << c->interval << " " << c->maxAlive
            << " " << c->totalToSpawn << " " << c->spawnRadius << " " << c->startDelay
            << " " << (c->deactivateTemplate ? 1 : 0) << "\n";
    }
    if (auto* c = go->GetComponent<CraftingMenu>()) {
        out << "  craftmenu " << (int)(unsigned char)c->toggleKey << " " << (c->open ? 1 : 0)
            << " " << c->position.x << " " << c->position.y << " " << c->buttonSize.x << " "
            << c->buttonSize.y << " " << c->spacing << " " << (int)c->anchor << "\n";
    }
    if (auto* c = go->GetComponent<Crafting>()) {
        out << "  crafting " << c->recipes.size();
        for (const auto& r : c->recipes) {
            out << " " << Quote(r.output) << " " << r.outputCount << " " << r.inputs.size();
            for (const auto& in : r.inputs) out << " " << Quote(in.item) << " " << in.count;
        }
        out << "\n";
    }
    if (auto* fp = go->GetComponent<FirstPersonController>()) {
        out << "  fpctrl " << fp->walkSpeed << " " << fp->runSpeed << " " << fp->jumpForce << " "
            << fp->mouseSensitivity << " " << (fp->canJump ? 1 : 0) << " " << (fp->driveAnimation ? 1 : 0)
            << " " << (fp->invertY ? 1 : 0)
            // extended (back-compatible trailing fields): momentum + jump feel
            << " " << fp->acceleration << " " << fp->deceleration << " " << fp->airControl
            << " " << fp->coyoteTime << " " << fp->jumpBufferTime
            // extended: run + stance (sprint/crouch/prone). Keys as ints.
            << " " << (int)(unsigned char)fp->sprintKey << " " << (fp->toggleRun ? 1 : 0)
            << " " << (int)(unsigned char)fp->crouchKey << " " << (int)(unsigned char)fp->proneKey
            << " " << (fp->toggleStance ? 1 : 0)
            << " " << fp->crouchSpeed << " " << fp->proneSpeed
            << " " << fp->standEyeHeight << " " << fp->crouchEyeHeight << " " << fp->proneEyeHeight
            << " " << fp->stanceLerp
            // extended: lean (peek with Q/E)
            << " " << (int)(unsigned char)fp->leanLeftKey << " " << (int)(unsigned char)fp->leanRightKey
            << " " << fp->leanAngle << " " << fp->leanOffset << " " << fp->leanSpeed
            << " " << fp->maxJumps << "\n";
    }
    if (auto* tp = go->GetComponent<ThirdPersonController>()) {
        out << "  tpctrl " << tp->walkSpeed << " " << tp->runSpeed << " " << tp->jumpForce << " "
            << tp->mouseSensitivity << " " << tp->turnSpeed << " " << tp->distance << " "
            << tp->cameraHeight << " " << (tp->canJump ? 1 : 0) << " " << (tp->driveAnimation ? 1 : 0)
            // extended (optional, back-compatible trailing fields)
            << " " << (tp->invertX ? 1 : 0) << " " << (tp->invertY ? 1 : 0)
            << " " << tp->minDistance << " " << tp->maxDistance << " " << tp->zoomSpeed
            << " " << tp->shoulderOffset << " " << tp->cameraDamping
            << " " << tp->minPitch << " " << tp->maxPitch << " " << (int)tp->faceMode
            // extended (back-compatible trailing fields): momentum + jump feel
            << " " << tp->acceleration << " " << tp->deceleration << " " << tp->airControl
            << " " << tp->coyoteTime << " " << tp->jumpBufferTime
            // camera collision (spring arm)
            << " " << (tp->cameraCollision ? 1 : 0) << " " << tp->cameraCollisionSkin
            // extended: run + stance (sprint/crouch/prone). Keys as ints.
            << " " << (int)(unsigned char)tp->sprintKey << " " << (tp->toggleRun ? 1 : 0)
            << " " << (int)(unsigned char)tp->crouchKey << " " << (int)(unsigned char)tp->proneKey
            << " " << (tp->toggleStance ? 1 : 0)
            << " " << tp->crouchSpeed << " " << tp->proneSpeed
            << " " << tp->crouchHeightDrop << " " << tp->proneHeightDrop
            << " " << tp->stanceLerp
            // extended: lean (peek with Q/E)
            << " " << (int)(unsigned char)tp->leanLeftKey << " " << (int)(unsigned char)tp->leanRightKey
            << " " << tp->leanAngle << " " << tp->leanOffset << " " << tp->leanSpeed
            << " " << tp->maxJumps << "\n";
    }
    if (auto* td = go->GetComponent<TopDownController>()) {
        out << "  tdctrl " << td->walkSpeed << " " << td->runSpeed << " "
            << (td->sprintKey ? td->sprintKey : '-') << " "
            << (td->driveAnimation ? 1 : 0) << " " << (td->rotateToFace ? 1 : 0) << " " << td->turnSpeed << " "
            << td->acceleration << " " << td->deceleration << " " << (td->cameraRelative ? 1 : 0) << " "
            << td->cameraDistance << " " << td->cameraPitch << " " << td->cameraYaw << " "
            << td->lookHeight << " " << td->cameraDamping << "\n";
    }
    if (auto* fr = go->GetComponent<FreeRoamController>()) {
        out << "  frctrl " << fr->moveSpeed << " " << fr->boostMultiplier << " "
            << (int)(unsigned char)fr->sprintKey << " " << (int)(unsigned char)fr->upKey << " "
            << (int)(unsigned char)fr->downKey << " " << fr->mouseSensitivity << " "
            << (fr->invertY ? 1 : 0) << " " << (fr->lockCursor ? 1 : 0) << " "
            << (fr->lookRequiresRightMouse ? 1 : 0) << " " << fr->minPitch << " " << fr->maxPitch << " "
            << fr->acceleration << " " << fr->yaw << " " << fr->pitch << "\n";
    }
    if (auto* ts = go->GetComponent<ThirdPersonShooterController>()) {
        out << "  tpsctrl " << ts->walkSpeed << " " << ts->runSpeed << " " << ts->jumpForce << " "
            << (ts->sprintKey ? ts->sprintKey : '-') << " " << (ts->canJump ? 1 : 0) << " "
            << (ts->driveAnimation ? 1 : 0) << " " << ts->turnSpeed << " "
            << ts->acceleration << " " << ts->deceleration << " " << ts->airControl << " "
            << ts->coyoteTime << " " << ts->jumpBufferTime << " "
            << ts->mouseSensitivity << " " << (ts->invertY ? 1 : 0) << " "
            << ts->minPitch << " " << ts->maxPitch << " " << ts->distance << " " << ts->cameraHeight << " "
            << ts->shoulderOffset << " " << (ts->cameraCollision ? 1 : 0) << " " << ts->cameraCollisionSkin << " "
            << ts->aimButton << " " << ts->aimDistance << " " << ts->aimShoulder << " " << ts->aimSpeed << " "
            << ts->fireButton << " " << (ts->autoFire ? 1 : 0) << " " << ts->fireRate << " "
            << (ts->lockCursor ? 1 : 0)
            // extended: lean (peek with Q/E)
            << " " << (int)(unsigned char)ts->leanLeftKey << " " << (int)(unsigned char)ts->leanRightKey
            << " " << ts->leanAngle << " " << ts->leanOffset << " " << ts->leanSpeed
            << " " << ts->maxJumps << "\n";
    }
    if (auto* cm = go->GetComponent<ClickToMoveController>()) {
        out << "  ctmctrl " << cm->walkSpeed << " " << cm->runSpeed << " "
            << (cm->runKey ? cm->runKey : '-') << " " << cm->stopDistance << " " << cm->turnSpeed
            << " " << cm->mouseButton << " " << (cm->holdToMove ? 1 : 0) << " "
            << (cm->driveAnimation ? 1 : 0) << " " << (cm->usePlayerHeight ? 1 : 0)
            << " " << cm->groundY
            // extended: follow camera (optional, back-compatible trailing fields)
            << " " << (cm->followCamera ? 1 : 0) << " " << cm->cameraHeight << " " << cm->cameraDistance
            << " " << cm->minDistance << " " << cm->maxDistance << " " << cm->cameraYaw
            << " " << cm->cameraPitch << " " << cm->minPitch << " " << cm->maxPitch
            << " " << cm->rotateSpeed << " " << cm->keyRotateSpeed
            << " " << (cm->rotateLeftKey ? cm->rotateLeftKey : '-')
            << " " << (cm->rotateRightKey ? cm->rotateRightKey : '-')
            << " " << cm->cameraDamping
            << " " << cm->arriveRadius << "\n";   // extended (back-compatible trailing field)
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
    if (auto* st = go->GetComponent<Stats>()) {
        out << "  stats " << st->health << " " << st->maxHealth << " " << st->mana << " " << st->maxMana
            << " " << st->level << " " << st->xp << " " << st->xpToNext
            << " " << st->strength << " " << st->defense << " " << st->intelligence << " " << st->agility << "\n";
    }
    if (auto* inv = go->GetComponent<Inventory>()) {
        out << "  inventory " << inv->capacity << " " << inv->slots.size();
        for (const auto& s : inv->slots) out << " " << Quote(s.item) << " " << s.count;
        out << "\n";
    }
    if (auto* tm = go->GetComponent<TurnManager>()) {
        out << "  turnmgr " << tm->current << " " << tm->round << " " << (tm->autoStart ? 1 : 0)
            << " " << tm->participants.size();
        for (const auto& p : tm->participants) out << " " << Quote(p);
        out << "\n";
    }
    if (auto* cf = go->GetComponent<CameraFollow>()) {
        out << "  camerafollow " << Quote(cf->targetName) << " "
            << cf->offset.x << " " << cf->offset.y << " " << cf->offset.z << " "
            << cf->smoothing << "\n";
    }
    if (auto* vc = go->GetComponent<VirtualCamera>()) {
        out << "  vcam " << vc->priority << " " << Quote(vc->follow) << " " << Quote(vc->lookAt)
            << " " << vc->followOffset.x << " " << vc->followOffset.y << " " << vc->followOffset.z
            << " " << vc->positionDamping << " " << vc->rotationDamping
            << " " << vc->fieldOfView << " " << vc->shakeAmplitude << " " << vc->shakeFrequency
            // extended (back-compatible trailing fields)
            << " " << (int)vc->bindingMode
            << " " << vc->lookAtOffset.x << " " << vc->lookAtOffset.y << " " << vc->lookAtOffset.z
            << " " << vc->aimDeadZone << " " << vc->impulseDecay
            // freelook block (back-compatible trailing fields)
            << " " << (vc->freeLook ? 1 : 0) << " " << vc->orbitYaw << " " << vc->orbitPitch
            << " " << vc->orbitRadius << " " << vc->orbitHeight
            << " " << vc->orbitMinPitch << " " << vc->orbitMaxPitch
            << " " << (vc->orbitInput ? 1 : 0) << " " << vc->orbitButton << " " << vc->mouseSensitivity
            // dolly block (back-compatible trailing fields)
            << " " << (vc->dolly ? 1 : 0) << " " << Quote(vc->dollyPath)
            << " " << vc->dollyPosition << " " << (vc->autoDolly ? 1 : 0) << "\n";
    }
    if (auto* cb = go->GetComponent<CinemachineBrain>()) {
        out << "  cmbrain " << cb->blendTime << " " << (cb->easeInOut ? 1 : 0) << "\n";
    }
    if (auto* dp = go->GetComponent<DollyPath>()) {
        out << "  dollypath " << (dp->looped ? 1 : 0) << " " << dp->waypoints.size();
        for (const auto& w : dp->waypoints) out << " " << w.x << " " << w.y << " " << w.z;
        out << "\n";
    }
    if (auto* dc = go->GetComponent<DollyCart>()) {
        out << "  dollycart " << Quote(dc->path) << " " << dc->position << " "
            << dc->speed << " " << (dc->autoMove ? 1 : 0) << "\n";
    }
    if (auto* tr = go->GetComponent<TextRenderer>()) {
        out << "  text " << Quote(tr->text) << " "
            << tr->color.r << " " << tr->color.g << " " << tr->color.b << " " << tr->color.a << " "
            << tr->pixelSize << " " << (tr->screenSpace ? 1 : 0) << " "
            << tr->screenPos.x << " " << tr->screenPos.y << " " << (int)tr->anchor << " "
            << (tr->shadow ? 1 : 0) << " "
            << tr->shadowColor.r << " " << tr->shadowColor.g << " " << tr->shadowColor.b << " " << tr->shadowColor.a << " "
            << tr->shadowOffset.x << " " << tr->shadowOffset.y << " " << tr->align << " "
            << (tr->outline ? 1 : 0) << " "
            << tr->outlineColor.r << " " << tr->outlineColor.g << " " << tr->outlineColor.b << " " << tr->outlineColor.a
            << " " << (tr->bold ? 1 : 0)
            << " " << tr->size.x << " " << tr->size.y << " " << (tr->vcenter ? 1 : 0)
            << " " << (tr->background ? 1 : 0) << " "
            << tr->backgroundColor.r << " " << tr->backgroundColor.g << " "
            << tr->backgroundColor.b << " " << tr->backgroundColor.a
            << " " << tr->letterSpacing << " " << tr->lineSpacing
            << " " << (tr->uppercase ? 1 : 0) << " " << (tr->wrap ? 1 : 0)
            // Newer options (optional, back-compat): italic, gradient + bottom color,
            // typewriter (visibleChars + typeSpeed), bottom-align.
            << " " << (tr->italic ? 1 : 0) << " " << (tr->gradient ? 1 : 0) << " "
            << tr->colorBottom.r << " " << tr->colorBottom.g << " " << tr->colorBottom.b << " " << tr->colorBottom.a
            << " " << tr->visibleChars << " " << tr->typeSpeed << " " << (tr->alignBottom ? 1 : 0)
            << " " << Quote(tr->fontPath) << "\n";   // optional TTF font path (newest)
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
    if (auto* anim = go->GetComponent<Animator>()) {
        out << "  animator " << anim->speed << " " << (anim->playing ? 1 : 0) << " "
            << (anim->clip.loop ? 1 : 0) << " " << Quote(anim->clip.name)
            << " " << anim->clip.Tracks().size();
        for (const auto& tr : anim->clip.Tracks()) {
            out << " " << Quote(tr.first) << " " << tr.second.Keys().size();
            for (const auto& k : tr.second.Keys()) out << " " << k.time << " " << k.value;
        }
        out << "\n";
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
            << (btn->interactable ? 1 : 0) << " " << (btn->focusable ? 1 : 0) << " "
            << btn->cornerRadius << " " << btn->fontScale << " " << btn->borderWidth << " "
            << btn->borderColor.r << " " << btn->borderColor.g << " " << btn->borderColor.b << " " << btn->borderColor.a
            << " " << btn->hoverScale
            << " " << Quote(btn->icon) << " " << btn->iconSize
            << " " << btn->hoverTextColor.r << " " << btn->hoverTextColor.g << " "
            << btn->hoverTextColor.b << " " << btn->hoverTextColor.a
            << " " << btn->transitionSpeed << " " << (btn->toggleMode ? 1 : 0)
            << " " << (btn->isOn ? 1 : 0) << " " << btn->pressOffset
            << " " << (btn->iconRight ? 1 : 0) << " " << (btn->holdRepeat ? 1 : 0)
            << " " << btn->repeatDelay << " " << btn->repeatInterval
            // extended (back-compatible trailing fields): shape + drop shadow (+softness)
            << " " << (int)btn->shape << " " << (btn->shadow ? 1 : 0) << " "
            << btn->shadowColor.r << " " << btn->shadowColor.g << " " << btn->shadowColor.b << " " << btn->shadowColor.a
            << " " << btn->shadowOffset.x << " " << btn->shadowOffset.y
            << " " << btn->shadowSoftness
            // extended: assigned OnClick action (target object + public function)
            << " " << Quote(btn->clickTarget) << " " << Quote(btn->clickFunction)
            << " " << btn->clickArg
            << " " << Quote(btn->fontPath) << "\n";   // optional TTF label font (newest)
    }
    if (auto* pn = go->GetComponent<UIPanel>()) {
        out << "  uipanel " << pn->position.x << " " << pn->position.y << " "
            << pn->size.x << " " << pn->size.y << " "
            << pn->color.r << " " << pn->color.g << " " << pn->color.b << " " << pn->color.a
            << " " << (int)pn->anchor << " "
            << pn->cornerRadius << " " << pn->borderWidth << " "
            << pn->borderColor.r << " " << pn->borderColor.g << " " << pn->borderColor.b << " " << pn->borderColor.a << " "
            << (pn->useGradient ? 1 : 0) << " "
            << pn->colorBottom.r << " " << pn->colorBottom.g << " " << pn->colorBottom.b << " " << pn->colorBottom.a << " "
            << (pn->shadow ? 1 : 0) << " "
            << pn->shadowColor.r << " " << pn->shadowColor.g << " " << pn->shadowColor.b << " " << pn->shadowColor.a << " "
            << pn->shadowOffset.x << " " << pn->shadowOffset.y
            // extended (back-compatible trailing fields): shape + gradient direction + soft shadow
            << " " << (int)pn->shape << " " << (pn->gradientHorizontal ? 1 : 0)
            << " " << pn->shadowSoftness << "\n";
    }
    if (auto* doc = go->GetComponent<UIDocument>()) {
        out << "  uidocument " << Quote(doc->markup) << "\n";
    }
    if (auto* nm = go->GetComponent<NetworkManager>()) {
        out << "  network " << (int)nm->autoStart << " " << nm->autoPort << " "
            << Quote(nm->autoHost) << " " << Quote(nm->startName) << " "
            << Quote(nm->startRoom) << " "
            << nm->maxPlayers << " " << nm->snapshotRate << " "
            << Quote(nm->serverName) << " " << Quote(nm->password) << "\n";
    }
    if (auto* wu = go->GetComponent<WorldUI>()) {
        out << "  worldui " << Quote(wu->text) << " "
            << wu->color.r << " " << wu->color.g << " " << wu->color.b << " " << wu->color.a << " "
            << wu->background.r << " " << wu->background.g << " " << wu->background.b << " " << wu->background.a << " "
            << wu->worldOffset.x << " " << wu->worldOffset.y << " " << wu->worldOffset.z << " "
            << wu->pixelSize << " " << (wu->scaleWithDistance ? 1 : 0) << " "
            << wu->maxDistance << " " << wu->bar << " "
            << wu->barColor.r << " " << wu->barColor.g << " " << wu->barColor.b << " " << wu->barColor.a << "\n";
    }
    if (auto* in = go->GetComponent<UIInputField>()) {
        out << "  uiinput " << in->position.x << " " << in->position.y << " "
            << in->size.x << " " << in->size.y << " " << (int)in->anchor << " "
            << in->maxLength << " " << Quote(in->text) << " " << Quote(in->placeholder) << " "
            << in->color.r << " " << in->color.g << " " << in->color.b << " " << in->color.a << " "
            << (int)in->contentType
            // extended (back-compatible trailing fields): shape + corner + focus ring
            << " " << (int)in->shape << " " << in->cornerRadius << " " << in->borderWidth << " "
            << in->borderColor.r << " " << in->borderColor.g << " " << in->borderColor.b << " " << in->borderColor.a << "\n";
    }
    if (auto* tb = go->GetComponent<UITabs>()) {
        out << "  uitabs " << tb->position.x << " " << tb->position.y << " "
            << tb->size.x << " " << tb->size.y << " " << (int)tb->anchor << " " << tb->value << " "
            << tb->background.r << " " << tb->background.g << " " << tb->background.b << " " << tb->background.a << " "
            << tb->selected.r << " " << tb->selected.g << " " << tb->selected.b << " " << tb->selected.a << " "
            << tb->textColor.r << " " << tb->textColor.g << " " << tb->textColor.b << " " << tb->textColor.a << " "
            << tb->selectedTextColor.r << " " << tb->selectedTextColor.g << " " << tb->selectedTextColor.b << " " << tb->selectedTextColor.a << " "
            << (int)tb->shape << " " << tb->cornerRadius << " " << (tb->interactable ? 1 : 0) << " "
            << tb->tabs.size();
        for (const auto& t : tb->tabs) out << " " << Quote(t);
        // Extended (back-compatible trailing): content page name per tab.
        out << " " << tb->pages.size();
        for (const auto& p : tb->pages) out << " " << Quote(p);
        out << "\n";
    }
    if (auto* dd = go->GetComponent<UIDropdown>()) {
        out << "  uidropdown " << dd->position.x << " " << dd->position.y << " "
            << dd->size.x << " " << dd->size.y << " " << (int)dd->anchor << " "
            << dd->value << " "
            << dd->color.r << " " << dd->color.g << " " << dd->color.b << " " << dd->color.a << " "
            << dd->hoverColor.r << " " << dd->hoverColor.g << " " << dd->hoverColor.b << " " << dd->hoverColor.a << " "
            << dd->listColor.r << " " << dd->listColor.g << " " << dd->listColor.b << " " << dd->listColor.a << " "
            << dd->textColor.r << " " << dd->textColor.g << " " << dd->textColor.b << " " << dd->textColor.a << " "
            << dd->borderColor.r << " " << dd->borderColor.g << " " << dd->borderColor.b << " " << dd->borderColor.a << " "
            << dd->options.size();
        for (const auto& opt : dd->options) out << " " << Quote(opt);
        out << " " << (dd->interactable ? 1 : 0) << " " << Quote(dd->placeholder)
            << " " << (int)dd->shape << " " << dd->cornerRadius << "\n";   // extended (back-compatible)
    }
    if (auto* tb = go->GetComponent<UITextBind>()) {
        out << "  uibind " << Quote(tb->format) << "\n";
    }
    if (auto* bb = go->GetComponent<UIBarBind>()) {
        out << "  uibarbind " << Quote(bb->var) << " " << bb->min << " " << bb->max << "\n";
    }
    if (auto* dg = go->GetComponent<UIDraggable>()) {
        out << "  uidraggable " << (dg->returnToStart ? 1 : 0) << " " << (dg->anyTarget ? 1 : 0)
            << " " << (dg->snapToSlot ? 1 : 0)
            << " " << (int)dg->axis << " " << dg->dragThreshold << " " << (dg->bringToFront ? 1 : 0) << "\n";
    }
    if (auto* dt = go->GetComponent<UIDropTarget>()) {
        out << "  uidroptarget " << Quote(dt->acceptTag)
            << " " << (dt->showHighlight ? 1 : 0)
            << " " << dt->highlight.r << " " << dt->highlight.g
            << " " << dt->highlight.b << " " << dt->highlight.a
            // extended (optional, back-compat): bg + border + reject + snap
            << " " << (dt->drawBackground ? 1 : 0)
            << " " << dt->background.r << " " << dt->background.g << " " << dt->background.b << " " << dt->background.a
            << " " << dt->cornerRadius << " " << dt->borderWidth
            << " " << dt->borderColor.r << " " << dt->borderColor.g << " " << dt->borderColor.b << " " << dt->borderColor.a
            << " " << dt->rejectHighlight.r << " " << dt->rejectHighlight.g << " " << dt->rejectHighlight.b << " " << dt->rejectHighlight.a
            << " " << (dt->snapToCenter ? 1 : 0) << "\n";
    }
    if (auto* dg = go->GetComponent<Draggable>()) {
        out << "  draggable " << (dg->returnToStart ? 1 : 0) << " " << (dg->anyTarget ? 1 : 0)
            << " " << (dg->snapToZone ? 1 : 0)
            << " " << (int)dg->axis << " " << dg->dragThreshold << " " << (dg->bringToFront ? 1 : 0)
            << " " << dg->gridX << " " << dg->gridY << " " << dg->dragScale << "\n";
    }
    if (auto* dz = go->GetComponent<DropZone>()) {
        out << "  dropzone " << Quote(dz->acceptTag) << "\n";
    }
    if (auto* tt = go->GetComponent<UITooltip>()) {
        out << "  uitooltip " << Quote(tt->text) << " " << tt->delay << " "
            << tt->background.r << " " << tt->background.g << " " << tt->background.b << " " << tt->background.a << " "
            << tt->textColor.r << " " << tt->textColor.g << " " << tt->textColor.b << " " << tt->textColor.a << " "
            << tt->borderColor.r << " " << tt->borderColor.g << " " << tt->borderColor.b << " " << tt->borderColor.a << "\n";
    }
    if (auto* lg = go->GetComponent<UILayoutGroup>()) {
        out << "  uilayout " << (int)lg->direction << " " << (int)lg->anchor << " "
            << lg->origin.x << " " << lg->origin.y << " " << lg->spacing << " " << lg->padding << "\n";
    }
    if (auto* sv = go->GetComponent<UIScrollView>()) {
        out << "  uiscroll " << sv->position.x << " " << sv->position.y << " "
            << sv->size.x << " " << sv->size.y << " " << (int)sv->anchor << " "
            << sv->contentHeight << " "
            << sv->background.r << " " << sv->background.g << " " << sv->background.b << " " << sv->background.a << "\n";
    }
    if (auto* cv = go->GetComponent<Canvas>()) {
        out << "  canvas " << (int)cv->scaleMode << " "
            << cv->referenceResolution.x << " " << cv->referenceResolution.y << " "
            << cv->matchWidthOrHeight << " " << cv->scaleFactor << " " << cv->sortOrder
            << " " << (cv->visible ? 1 : 0) << " " << cv->opacity            // optional, back-compat
            << " " << (cv->worldSpace ? 1 : 0) << " " << cv->designResolution.x
            << " " << cv->designResolution.y << " " << cv->worldPixelsPerUnit
            << " " << (cv->billboard ? 1 : 0) << "\n";   // world-space canvas (optional)
    }
    if (go->GetComponent<EventSystem>()) {
        out << "  eventsystem\n";
    }
    if (auto* w3 = go->GetComponent<WorldSpaceUI>()) {
        out << "  worldui3d " << w3->pixelsPerUnit << " " << (w3->billboard ? 1 : 0)
            << " " << (w3->constantSize ? 1 : 0) << " " << w3->constantScale << "\n";
    }
    if (auto* pb = go->GetComponent<UIProgressBar>()) {
        out << "  uiprogress " << pb->position.x << " " << pb->position.y << " "
            << pb->size.x << " " << pb->size.y << " " << pb->value << " "
            << pb->background.r << " " << pb->background.g << " " << pb->background.b << " " << pb->background.a << " "
            << pb->fill.r << " " << pb->fill.g << " " << pb->fill.b << " " << pb->fill.a
            << " " << (int)pb->anchor << " "
            << pb->cornerRadius << " " << (pb->showPercent ? 1 : 0) << " "
            << pb->textColor.r << " " << pb->textColor.g << " " << pb->textColor.b << " " << pb->textColor.a
            << " " << (int)pb->fillDir
            // extended (back-compatible trailing fields): shape + gradient fill
            << " " << (int)pb->shape << " " << (pb->gradientFill ? 1 : 0) << " "
            << pb->fillEnd.r << " " << pb->fillEnd.g << " " << pb->fillEnd.b << " " << pb->fillEnd.a << "\n";
    }
    if (auto* rp = go->GetComponent<UIRadialProgress>()) {
        out << "  uiradial " << rp->position.x << " " << rp->position.y << " "
            << rp->size.x << " " << rp->size.y << " " << rp->value << " "
            << rp->thickness << " " << rp->startAngle << " " << (rp->clockwise ? 1 : 0) << " "
            << rp->background.r << " " << rp->background.g << " " << rp->background.b << " " << rp->background.a << " "
            << rp->fill.r << " " << rp->fill.g << " " << rp->fill.b << " " << rp->fill.a
            << " " << (int)rp->anchor << " " << (rp->showPercent ? 1 : 0) << " "
            << rp->textColor.r << " " << rp->textColor.g << " " << rp->textColor.b << " " << rp->textColor.a
            << " " << (rp->spin ? 1 : 0) << " " << rp->spinSpeed << "\n";   // extended (back-compatible)
    }
    if (auto* mm = go->GetComponent<Minimap>()) {
        out << "  minimap " << mm->position.x << " " << mm->position.y << " "
            << mm->size.x << " " << mm->size.y << " "
            << mm->background.r << " " << mm->background.g << " " << mm->background.b << " " << mm->background.a << " "
            << mm->border.r << " " << mm->border.g << " " << mm->border.b << " " << mm->border.a << " "
            << mm->borderWidth << " " << Quote(mm->target) << " "
            << mm->targetColor.r << " " << mm->targetColor.g << " " << mm->targetColor.b << " " << mm->targetColor.a << " "
            << mm->worldPerPixel << " " << (mm->useXZ ? 1 : 0) << " " << mm->blipSize
            << " " << (int)mm->anchor << "\n";
    }
    if (auto* bl = go->GetComponent<MinimapBlip>()) {
        out << "  minimapblip " << bl->color.r << " " << bl->color.g << " " << bl->color.b << " " << bl->color.a << " "
            << bl->size << " " << (bl->square ? 1 : 0) << "\n";
    }
    if (auto* cr = go->GetComponent<Crosshair>()) {
        out << "  crosshair " << cr->position.x << " " << cr->position.y << " "
            << cr->size.x << " " << cr->size.y << " "
            << cr->color.r << " " << cr->color.g << " " << cr->color.b << " " << cr->color.a << " "
            << cr->thickness << " " << cr->gap << " " << cr->length << " "
            << (cr->showLines ? 1 : 0) << " " << (cr->dot ? 1 : 0) << " " << cr->dotSize << " "
            << cr->dotColor.r << " " << cr->dotColor.g << " " << cr->dotColor.b << " " << cr->dotColor.a << " "
            << (cr->outline ? 1 : 0) << " "
            << cr->outlineColor.r << " " << cr->outlineColor.g << " " << cr->outlineColor.b << " " << cr->outlineColor.a
            << " " << (int)cr->anchor << "\n";
    }
    if (auto* im = go->GetComponent<UIImage>()) {
        out << "  uiimage " << im->position.x << " " << im->position.y << " "
            << im->size.x << " " << im->size.y << " "
            << im->color.r << " " << im->color.g << " " << im->color.b << " " << im->color.a << " "
            << Quote(im->texture) << " " << (int)im->anchor << " "
            << (im->nineSlice ? 1 : 0) << " " << im->border << " "
            << (int)im->fillMode << " " << im->fillAmount << " " << im->cornerRadius
            << " " << (int)im->shape << "\n";   // extended (back-compatible trailing field)
    }
    if (auto* sl = go->GetComponent<UISlider>()) {
        out << "  uislider " << sl->position.x << " " << sl->position.y << " "
            << sl->size.x << " " << sl->size.y << " "
            << sl->value << " " << sl->minValue << " " << sl->maxValue << " "
            << sl->background.r << " " << sl->background.g << " " << sl->background.b << " " << sl->background.a << " "
            << sl->fill.r << " " << sl->fill.g << " " << sl->fill.b << " " << sl->fill.a << " "
            << sl->knob.r << " " << sl->knob.g << " " << sl->knob.b << " " << sl->knob.a
            << " " << (int)sl->anchor << " "
            << sl->cornerRadius << " " << sl->knobSize << " " << (sl->showValue ? 1 : 0) << " "
            << sl->textColor.r << " " << sl->textColor.g << " " << sl->textColor.b << " " << sl->textColor.a
            << " " << (sl->wholeNumbers ? 1 : 0)
            << " " << (sl->interactable ? 1 : 0)
            << " " << (sl->vertical ? 1 : 0)
            // extended (back-compatible trailing fields): track shape + round knob
            << " " << (int)sl->trackShape << " " << (sl->roundKnob ? 1 : 0) << "\n";
    }
    if (auto* st = go->GetComponent<UIStepper>()) {
        out << "  uistepper " << st->position.x << " " << st->position.y << " "
            << st->size.x << " " << st->size.y << " "
            << st->value << " " << st->minValue << " " << st->maxValue << " " << st->step << " "
            << (st->wholeNumbers ? 1 : 0) << " " << (st->wrap ? 1 : 0) << " "
            << st->background.r << " " << st->background.g << " " << st->background.b << " " << st->background.a << " "
            << st->button.r << " " << st->button.g << " " << st->button.b << " " << st->button.a << " "
            << st->textColor.r << " " << st->textColor.g << " " << st->textColor.b << " " << st->textColor.a << " "
            << (int)st->anchor << " " << (int)st->shape << " " << st->cornerRadius << " "
            << (st->interactable ? 1 : 0) << "\n";
    }
    if (auto* rt = go->GetComponent<UIRating>()) {
        out << "  uirating " << rt->position.x << " " << rt->position.y << " "
            << rt->size.x << " " << rt->size.y << " "
            << rt->count << " " << rt->value << " " << (rt->allowHalf ? 1 : 0) << " " << (rt->readOnly ? 1 : 0) << " "
            << rt->on.r << " " << rt->on.g << " " << rt->on.b << " " << rt->on.a << " "
            << rt->off.r << " " << rt->off.g << " " << rt->off.b << " " << rt->off.a << " "
            << (int)rt->anchor << "\n";
    }
    if (auto* tg = go->GetComponent<UIToggle>()) {
        out << "  uitoggle " << Quote(tg->label) << " "
            << tg->position.x << " " << tg->position.y << " "
            << tg->size.x << " " << tg->size.y << " " << (tg->on ? 1 : 0) << " "
            << tg->boxColor.r << " " << tg->boxColor.g << " " << tg->boxColor.b << " " << tg->boxColor.a << " "
            << tg->checkColor.r << " " << tg->checkColor.g << " " << tg->checkColor.b << " " << tg->checkColor.a << " "
            << tg->textColor.r << " " << tg->textColor.g << " " << tg->textColor.b << " " << tg->textColor.a
            << " " << (int)tg->anchor << " " << tg->cornerRadius
            << " " << (int)tg->style << " "
            << tg->knobColor.r << " " << tg->knobColor.g << " " << tg->knobColor.b << " " << tg->knobColor.a
            << " " << (tg->interactable ? 1 : 0)
            << " " << tg->animSpeed << "\n";   // extended (back-compatible trailing field)
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
    if (!scene.uiFont.empty()) out << "uifont " << Quote(scene.uiFont) << "\n";
    out << "gravity " << scene.physics().gravity.x << " " << scene.physics().gravity.y << "\n";
    {
        const auto& rs = scene.renderSettings;
        out << "rendersettings " << (rs.skybox ? 1 : 0) << " "
            << rs.skyTop.r << " " << rs.skyTop.g << " " << rs.skyTop.b << " "
            << rs.skyHorizon.r << " " << rs.skyHorizon.g << " " << rs.skyHorizon.b << " "
            << rs.skyBottom.r << " " << rs.skyBottom.g << " " << rs.skyBottom.b << " "
            << rs.ambient << "\n";
        out << "fog " << (rs.fog ? 1 : 0) << " "
            << rs.fogColor.r << " " << rs.fogColor.g << " " << rs.fogColor.b << " "
            << rs.fogStart << " " << rs.fogEnd << "\n";
    }
    const auto& objs = scene.Objects();
    for (std::size_t i = 0; i < objs.size(); ++i) {
        GameObject* go = objs[i].get();
        Transform* t = go->transform;
        out << "gameobject " << i << " " << Quote(go->name) << "\n";
        out << "  active " << (go->active ? 1 : 0) << "\n";
        if (!go->tag.empty()) out << "  tag " << Quote(go->tag) << "\n";
        if (go->isStatic) out << "  static 1\n";
        if (go->layer != 0) out << "  layer " << go->layer << "\n";
        if (go->uiDrawOrder != 0) out << "  uiorder " << go->uiDrawOrder << "\n";
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
        } else if (token == "uifont") {
            scene.uiFont = ReadQuoted(in);
        } else if (token == "gravity") {
            Vec2 g; in >> g.x >> g.y;
            scene.physics().gravity = g;
        } else if (token == "rendersettings") {
            auto& rs = scene.renderSettings;
            int sky = 1;
            in >> sky >> rs.skyTop.r >> rs.skyTop.g >> rs.skyTop.b
               >> rs.skyHorizon.r >> rs.skyHorizon.g >> rs.skyHorizon.b
               >> rs.skyBottom.r >> rs.skyBottom.g >> rs.skyBottom.b >> rs.ambient;
            rs.skybox = (sky != 0);
        } else if (token == "fog") {
            auto& rs = scene.renderSettings;
            int on = 0;
            in >> on >> rs.fogColor.r >> rs.fogColor.g >> rs.fogColor.b
               >> rs.fogStart >> rs.fogEnd;
            rs.fog = (on != 0);
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
                else if (field == "static") { int s = 0; in >> s; go->isStatic = (s != 0); }
                else if (field == "layer") { in >> go->layer; }
                else if (field == "uiorder") { in >> go->uiDrawOrder; }
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
                    in >> std::ws; // optional clearFlags + depth + near + far (added later)
                    if (std::isdigit(in.peek()) || in.peek() == '-') {
                        int cf = 0; in >> cf >> cam->depth >> cam->nearClip >> cam->farClip;
                        cam->clearFlags = (Camera::ClearFlags)cf;
                        in >> std::ws; // optional FOV axis + viewport rect (added later)
                        if (std::isdigit(in.peek())) {
                            int fax = 0; in >> fax >> cam->rectX >> cam->rectY >> cam->rectW >> cam->rectH;
                            cam->fovAxisHorizontal = (fax != 0);
                            in >> std::ws; // optional culling mask + physical camera (added later)
                            if (std::isdigit(in.peek()) || in.peek() == '-') {
                                int phys = 0;
                                in >> cam->cullingMask >> phys >> cam->focalLength >> cam->sensorHeight;
                                cam->physicalCamera = (phys != 0);
                            }
                        }
                    }
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
                            bool ok = false; std::string tex;
                            Mesh loaded = Mesh::LoadOBJ(mr->meshPath, &ok, &tex);
                            if (ok && !loaded.vertices.empty()) {
                                mr->mesh = loaded;
                                if (!tex.empty() && mr->texture.empty()) mr->texture = tex;
                            }
                        }
                    }
                    in >> std::ws; // optional double-sided flag
                    if (std::isdigit(in.peek())) { int ds = 0; in >> ds; mr->doubleSided = (ds != 0); }
                } else if (field == "meshgeo") {
                    // Custom/edited geometry written by the `meshgeo` record. It
                    // overrides the placeholder geometry the preceding `mesh` line
                    // built from a (now-empty) name. Operate on the existing
                    // MeshRenderer (add one if somehow absent).
                    auto* mr = go->GetComponent<MeshRenderer>();
                    if (!mr) mr = go->AddComponent<MeshRenderer>();
                    int vcount = 0; in >> vcount;
                    mr->mesh.vertices.clear();
                    mr->mesh.vertices.reserve(vcount > 0 ? vcount : 0);
                    for (int i = 0; i < vcount; ++i) {
                        Vec3 v; in >> v.x >> v.y >> v.z; mr->mesh.vertices.push_back(v);
                    }
                    int icount = 0; in >> icount;
                    mr->mesh.triangles.clear();
                    mr->mesh.triangles.reserve(icount > 0 ? icount : 0);
                    for (int i = 0; i < icount; ++i) { int idx = 0; in >> idx; mr->mesh.triangles.push_back(idx); }
                    mr->mesh.name = "";
                    mr->mesh.uvs.clear();
                    mr->mesh.triColors.clear();
                    // Flat (faceted) shading, like the built-in primitives — do NOT
                    // force smooth normals, or an edited box/ground reloads looking like
                    // a soft rounded blob instead of clean flat faces.
                    mr->mesh.normals.clear();
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
                } else if (field == "normalmap") {
                    if (auto* mr = go->GetComponent<MeshRenderer>()) {
                        in >> std::ws;
                        if (in.peek() == '"') mr->normalMap = ReadQuoted(in);
                        in >> std::ws;
                        int p = in.peek();
                        if (std::isdigit(p) || p == '-' || p == '.') in >> mr->normalStrength;
                    }
                } else if (field == "reflect") {
                    if (auto* mr = go->GetComponent<MeshRenderer>()) {
                        in >> mr->reflectivity;
                        in >> std::ws;                       // optional metalness
                        int p = in.peek();
                        if (std::isdigit(p) || p == '-' || p == '.') in >> mr->metallic;
                    }
                } else if (field == "specmap") {
                    if (auto* mr = go->GetComponent<MeshRenderer>()) {
                        in >> std::ws;
                        if (in.peek() == '"') mr->specularMap = ReadQuoted(in);
                    }
                } else if (field == "shader") {
                    if (auto* mr = go->GetComponent<MeshRenderer>()) {
                        int s = 0; in >> s >> mr->toonBands;
                        mr->shader = (MeshRenderer::Shader)s;
                    }
                } else if (field == "rim") {
                    if (auto* mr = go->GetComponent<MeshRenderer>())
                        in >> mr->rimStrength >> mr->rimPower
                           >> mr->rimColor.r >> mr->rimColor.g >> mr->rimColor.b;
                } else if (field == "outline") {
                    if (auto* mr = go->GetComponent<MeshRenderer>()) {
                        in >> mr->outlineWidth >> mr->outlineColor.r >> mr->outlineColor.g >> mr->outlineColor.b;
                        mr->outline = true;
                    }
                } else if (field == "uvanim") {
                    if (auto* mr = go->GetComponent<MeshRenderer>()) {
                        int tp = 0; in >> mr->uvScroll.x >> mr->uvScroll.y >> tp;
                        mr->triplanar = (tp != 0);
                    }
                } else if (field == "light") {
                    auto* li = go->AddComponent<Light>();
                    Color c;
                    in >> c.r >> c.g >> c.b >> c.a >> li->ambient >> li->intensity;
                    li->color = c;
                    in >> std::ws; // optional type + range + spot angle (added later)
                    if (std::isdigit(in.peek())) {
                        int ty = 0; in >> ty >> li->range >> li->spotAngle;
                        li->type = (Light::Type)ty;
                    }
                    in >> std::ws; // optional softness + temperature + ambient tint
                    if (std::isdigit(in.peek())) {
                        int useT = 0;
                        in >> li->spotSoftness >> useT >> li->temperature
                           >> li->ambientColor.r >> li->ambientColor.g >> li->ambientColor.b;
                        li->useTemperature = (useT != 0);
                    }
                } else if (field == "rigidbody2d") {
                    int bt = 0; float gs = 1, mass = 1, drag = 0, bounce = 0;
                    in >> bt >> gs >> mass >> drag >> bounce;
                    auto* rb = go->AddComponent<Rigidbody2D>();
                    rb->bodyType = (Rigidbody2D::BodyType)bt;
                    rb->gravityScale = gs; rb->mass = mass; rb->drag = drag; rb->bounciness = bounce;
                } else if (field == "boxcollider2d") {
                    Vec2 sz, off; int trig = 0, layer = 0, af = 0;
                    in >> sz.x >> sz.y >> off.x >> off.y >> trig;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> layer; // optional
                    in >> std::ws; if (std::isdigit(in.peek())) in >> af;    // optional autoFit
                    auto* bc = go->AddComponent<BoxCollider2D>();
                    bc->size = sz; bc->offset = off; bc->isTrigger = (trig != 0); bc->layer = layer;
                    bc->autoFit = (af != 0);
                } else if (field == "circlecollider2d") {
                    float r = 0.5f; Vec2 off; int trig = 0, layer = 0, af = 0;
                    in >> r >> off.x >> off.y >> trig;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> layer; // optional
                    in >> std::ws; if (std::isdigit(in.peek())) in >> af;    // optional autoFit
                    auto* cc = go->AddComponent<CircleCollider2D>();
                    cc->radius = r; cc->offset = off; cc->isTrigger = (trig != 0); cc->layer = layer;
                    cc->autoFit = (af != 0);
                } else if (field == "capsulecollider2d") {
                    Vec2 sz{1, 2}, off; int dir = 0, trig = 0, layer = 0, af = 0;
                    in >> sz.x >> sz.y >> dir >> off.x >> off.y >> trig;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> layer; // optional
                    in >> std::ws; if (std::isdigit(in.peek())) in >> af;    // optional autoFit
                    auto* cap = go->AddComponent<CapsuleCollider2D>();
                    cap->size = sz; cap->direction = (CapsuleCollider2D::Direction)dir;
                    cap->offset = off; cap->isTrigger = (trig != 0); cap->layer = layer;
                    cap->autoFit = (af != 0);
                } else if (field == "rigidbody3d") {
                    int bt = 0; float gs = 1, mass = 1, drag = 0, bounce = 0;
                    int fx = 0, fy = 0, fz = 0;
                    in >> bt >> gs >> mass >> drag >> bounce >> fx >> fy >> fz;
                    auto* rb = go->AddComponent<Rigidbody3D>();
                    rb->bodyType = (Rigidbody3D::BodyType)bt;
                    rb->gravityScale = gs; rb->mass = mass; rb->drag = drag; rb->bounciness = bounce;
                    rb->freezeX = (fx != 0); rb->freezeY = (fy != 0); rb->freezeZ = (fz != 0);
                } else if (field == "boxcollider3d") {
                    Vec3 sz{1, 1, 1}, off; int trig = 0, layer = 0, af = 0;
                    in >> sz.x >> sz.y >> sz.z >> off.x >> off.y >> off.z >> trig;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> layer;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> af;
                    auto* bc = go->AddComponent<BoxCollider3D>();
                    bc->size = sz; bc->offset = off; bc->isTrigger = (trig != 0); bc->layer = layer;
                    bc->autoFit = (af != 0);
                } else if (field == "spherecollider3d") {
                    float r = 0.5f; Vec3 off; int trig = 0, layer = 0, af = 0;
                    in >> r >> off.x >> off.y >> off.z >> trig;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> layer;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> af;
                    auto* sc = go->AddComponent<SphereCollider3D>();
                    sc->radius = r; sc->offset = off; sc->isTrigger = (trig != 0); sc->layer = layer;
                    sc->autoFit = (af != 0);
                } else if (field == "capsulecollider3d") {
                    float r = 0.5f, h = 2.0f; int ax = 1; Vec3 off; int trig = 0, layer = 0, af = 0;
                    in >> r >> h >> ax >> off.x >> off.y >> off.z >> trig;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> layer;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> af;
                    auto* cap = go->AddComponent<CapsuleCollider3D>();
                    cap->radius = r; cap->height = h; cap->axis = ax;
                    cap->offset = off; cap->isTrigger = (trig != 0); cap->layer = layer;
                    cap->autoFit = (af != 0);
                } else if (field == "script") {
                    std::string lang = ReadQuoted(in);
                    std::string src  = ReadQuoted(in);
                    auto* sc = go->AddComponent<ScriptComponent>(lang);
                    sc->LoadSource(src);
                    // New format: an inline path + field-override block follows the
                    // source (a leading quote). Old scenes used separate lines.
                    in >> std::ws;
                    if (in.peek() == '"') {
                        sc->SetPath(ReadQuoted(in));
                        int n = 0; in >> n;
                        for (int i = 0; i < n; ++i) {
                            std::string k = ReadQuoted(in), v = ReadQuoted(in);
                            sc->fields[k] = v;
                        }
                        sc->ApplyFieldOverrides();
                    }
                } else if (field == "scriptpath") {
                    std::string p = ReadQuoted(in);
                    if (auto* sc = go->GetComponent<ScriptComponent>()) sc->SetPath(p);
                } else if (field == "scriptfields") {
                    int n = 0; in >> n;
                    auto* sc = go->GetComponent<ScriptComponent>();
                    for (int i = 0; i < n; ++i) {
                        std::string k = ReadQuoted(in), v = ReadQuoted(in);
                        if (sc) sc->fields[k] = v;
                    }
                    if (sc) sc->ApplyFieldOverrides();   // re-apply over the loaded defaults
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
                    // extended (optional): only overrides defaults when present.
                    std::string rs, skTok, ac, de, nd, ug, fs, mj, vj, jc, ct, jb, air, mfs, efg;
                    if (in >> rs >> skTok >> ac >> de >> nd >> ug >> fs >> mj >> vj >> jc >> ct >> jb >> air >> mfs >> efg) {
                        cc->runSpeed = (float)std::atof(rs.c_str());
                        cc->sprintKey = (skTok == "-" || skTok.empty()) ? 0 : skTok[0];
                        cc->acceleration = (float)std::atof(ac.c_str());
                        cc->deceleration = (float)std::atof(de.c_str());
                        cc->normalizeDiagonal = (nd != "0");
                        cc->useGamepad = (ug != "0");
                        cc->flipSprite = (fs != "0");
                        cc->maxJumps = std::atoi(mj.c_str());
                        cc->variableJump = (vj != "0");
                        cc->jumpCutMultiplier = (float)std::atof(jc.c_str());
                        cc->coyoteTime = (float)std::atof(ct.c_str());
                        cc->jumpBuffer = (float)std::atof(jb.c_str());
                        cc->airControl = (float)std::atof(air.c_str());
                        cc->maxFallSpeed = (float)std::atof(mfs.c_str());
                        cc->extraFallGravity = (float)std::atof(efg.c_str());
                    }
                } else if (field == "ctmctrl") {
                    float ws = 4, rs = 7, sd = 0.15f, ts = 12, gy = 0; std::string rk = "-";
                    int mb = 0, hold = 0, anim = 1, useH = 1;
                    in >> ws >> rs >> rk >> sd >> ts >> mb >> hold >> anim >> useH >> gy;
                    auto* cm = go->AddComponent<ClickToMoveController>();
                    cm->walkSpeed = ws; cm->runSpeed = rs;
                    cm->runKey = (rk == "-" || rk.empty()) ? 0 : rk[0];
                    cm->stopDistance = sd; cm->turnSpeed = ts; cm->mouseButton = mb;
                    cm->holdToMove = (hold != 0); cm->driveAnimation = (anim != 0);
                    cm->usePlayerHeight = (useH != 0); cm->groundY = gy;
                    // extended follow-camera fields (optional)
                    int fc = 1; float ch = 1, cd = 12, mnd = 4, mxd = 24, cy = 0, cpi = 50,
                                      mnp = 15, mxp = 85, rsp = 0.3f, krs = 90, cdamp = 0;
                    std::string rlk = "-", rrk = "-";
                    if (in >> fc >> ch >> cd >> mnd >> mxd >> cy >> cpi >> mnp >> mxp >> rsp >> krs >> rlk >> rrk >> cdamp) {
                        cm->followCamera = (fc != 0); cm->cameraHeight = ch; cm->cameraDistance = cd;
                        cm->minDistance = mnd; cm->maxDistance = mxd; cm->cameraYaw = cy;
                        cm->cameraPitch = cpi; cm->minPitch = mnp; cm->maxPitch = mxp;
                        cm->rotateSpeed = rsp; cm->keyRotateSpeed = krs;
                        cm->rotateLeftKey = (rlk == "-" || rlk.empty()) ? 0 : rlk[0];
                        cm->rotateRightKey = (rrk == "-" || rrk.empty()) ? 0 : rrk[0];
                        cm->cameraDamping = cdamp;
                        in >> std::ws; // optional arrival radius (added later)
                        if (std::isdigit(in.peek()) || in.peek() == '.') in >> cm->arriveRadius;
                    }
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
                } else if (field == "vehicle") {
                    auto* v = go->AddComponent<VehicleController>();
                    in >> v->acceleration >> v->maxSpeed >> v->reverseSpeed >> v->brakeForce
                       >> v->drag >> v->turnSpeed >> v->grip >> v->handbrakeGrip
                       >> v->groundCheckDistance;
                    int fc = 0; in >> fc; v->followCamera = (fc != 0);
                    in >> v->camDistance >> v->camHeight >> v->camLerp;
                    int hk = 32; in >> hk; v->handbrakeKey = (char)hk;
                } else if (field == "vehiclesusp") {
                    auto* v = go->GetComponent<VehicleController>();
                    int on = 0; in >> on;
                    float rh, ss, sd, st, wb, tw, bl, mt, ts;
                    in >> rh >> ss >> sd >> st >> wb >> tw >> bl >> mt >> ts;
                    if (v) {
                        v->suspension = (on != 0);
                        v->rideHeight = rh; v->springStrength = ss; v->springDamping = sd;
                        v->suspensionTravel = st; v->wheelBase = wb; v->trackWidth = tw;
                        v->bodyLean = bl; v->maxTilt = mt; v->tiltSmooth = ts;
                    }
                } else if (field == "blockbuilder") {
                    auto* bb = go->AddComponent<BlockBuilder>();
                    in >> bb->blockSize >> bb->reach
                       >> bb->blockColor.r >> bb->blockColor.g >> bb->blockColor.b >> bb->blockColor.a
                       >> bb->placeButton >> bb->removeButton;
                    bb->blockTag = ReadQuoted(in);
                    bb->blockTexture = ReadQuoted(in);
                    in >> std::ws;   // optional preview/crosshair toggles (added later)
                    if (std::isdigit(in.peek())) {
                        int sp = 1, sc = 1; in >> sp >> sc;
                        bb->showPreview = (sp != 0); bb->showCrosshair = (sc != 0);
                    }
                } else if (field == "vehicle2d") {
                    auto* v2 = go->AddComponent<VehicleController2D>();
                    in >> v2->acceleration >> v2->maxSpeed >> v2->reverseSpeed >> v2->brakeForce
                       >> v2->drag >> v2->turnSpeed >> v2->grip >> v2->handbrakeGrip;
                    int sv = 0; in >> sv; v2->sideView = (sv != 0);
                    int hk = 32; in >> hk; v2->handbrakeKey = (char)hk;
                } else if (field == "survival") {
                    auto* sv = go->AddComponent<SurvivalStats>();
                    in >> sv->maxHealth >> sv->armor >> sv->regenWhenFed >> sv->regenDelay
                       >> sv->maxHunger >> sv->hungerDrain >> sv->starveDamage
                       >> sv->maxThirst >> sv->thirstDrain >> sv->dehydrateDamage
                       >> sv->maxStamina >> sv->staminaRegen >> sv->sprintCost >> sv->sprintDrainMult
                       >> sv->maxOxygen >> sv->oxygenDrain >> sv->oxygenRefill >> sv->drownDamage
                       >> sv->maxWarmth >> sv->coldDrain >> sv->warmRegen >> sv->freezeDamage;
                    int pp = 1, pb = 1, sm = 1; in >> pp >> pb >> sm;
                    sv->publishPrefs = (pp != 0); sv->publishBars = (pb != 0); sv->sendMessages = (sm != 0);
                    in >> std::ws;   // optional mitigation fields (added later)
                    if (std::isdigit(in.peek()) || in.peek() == '.' || in.peek() == '-') {
                        int gd = 0; in >> sv->resistance >> gd >> sv->invincibleTime; sv->godMode = (gd != 0);
                    }
                } else if (field == "stat_health") {
                    auto* c = go->AddComponent<HealthStat>();
                    in >> c->maxHealth >> c->armor >> c->regenPerSecond >> c->regenDelay >> c->lowThreshold;
                    ReadStatTail(in, c);
                    in >> std::ws;   // optional mitigation / overheal / respawn fields (added later)
                    if (std::isdigit(in.peek()) || in.peek() == '.' || in.peek() == '-') {
                        int gd = 0, dod = 0, rs = 0;
                        in >> c->resistance >> c->minDamage >> gd >> c->invincibleTime
                           >> c->overhealMax >> c->overhealDecay >> dod >> rs >> c->respawnDelay >> c->lives;
                        c->godMode = (gd != 0); c->destroyOnDeath = (dod != 0); c->respawn = (rs != 0);
                    }
                } else if (field == "stat_hunger") {
                    auto* c = go->AddComponent<HungerStat>();
                    in >> c->maxHunger >> c->drainPerSecond >> c->sprintMultiplier >> c->lowThreshold;
                    ReadStatTail(in, c);
                    in >> std::ws;   // optional starve/overeat/slow fields (added later)
                    if (std::isdigit(in.peek()) || in.peek() == '.' || in.peek() == '-') {
                        int sl = 0; in >> c->starveDamage >> c->overeatMax >> sl >> c->minSpeedFactor;
                        c->slowWhenStarving = (sl != 0);
                    }
                } else if (field == "stat_thirst") {
                    auto* c = go->AddComponent<ThirstStat>();
                    in >> c->maxThirst >> c->drainPerSecond >> c->sprintMultiplier >> c->lowThreshold;
                    ReadStatTail(in, c);
                    in >> std::ws;
                    if (std::isdigit(in.peek()) || in.peek() == '.' || in.peek() == '-') {
                        int sl = 0; in >> c->dehydrateDamage >> c->overdrinkMax >> sl >> c->minSpeedFactor;
                        c->slowWhenDehydrated = (sl != 0);
                    }
                } else if (field == "stat_stamina") {
                    auto* c = go->AddComponent<StaminaStat>();
                    in >> c->maxStamina >> c->regenPerSecond >> c->regenDelay >> c->sprintCost
                       >> c->jumpCost >> c->exhaustedUntil;
                    ReadStatTail(in, c);
                    in >> std::ws;
                    if (std::isdigit(in.peek()) || in.peek() == '.' || in.peek() == '-') {
                        int sl = 0; in >> c->lowThreshold >> sl >> c->minSpeedFactor >> c->regenScale;
                        c->slowWhenExhausted = (sl != 0);
                    }
                } else if (field == "stat_oxygen") {
                    auto* c = go->AddComponent<OxygenStat>();
                    in >> c->maxOxygen >> c->drainPerSecond >> c->refillPerSecond;
                    ReadStatTail(in, c);
                    in >> std::ws;
                    if (std::isdigit(in.peek()) || in.peek() == '.' || in.peek() == '-')
                        in >> c->lowThreshold >> c->drownDamage;
                } else if (field == "stat_temp") {
                    auto* c = go->AddComponent<TemperatureStat>();
                    in >> c->maxWarmth >> c->coldDrain >> c->warmRegen;
                    ReadStatTail(in, c);
                    in >> std::ws;
                    if (std::isdigit(in.peek()) || in.peek() == '.' || in.peek() == '-')
                        in >> c->lowThreshold >> c->freezeDamage;
                } else if (field == "stat_sleep") {
                    auto* c = go->AddComponent<SleepStat>();
                    in >> c->maxEnergy >> c->drainPerSecond >> c->restPerSecond >> c->tiredThreshold;
                    ReadStatTail(in, c);
                    in >> std::ws;
                    if (std::isdigit(in.peek()) || in.peek() == '.' || in.peek() == '-') {
                        int sl = 0; in >> c->exhaustDamage >> sl >> c->minSpeedFactor;
                        c->slowWhenTired = (sl != 0);
                    }
                } else if (field == "stat_sanity") {
                    auto* c = go->AddComponent<SanityStat>();
                    in >> c->maxSanity >> c->drainInDark >> c->regenInLight >> c->lowThreshold;
                    ReadStatTail(in, c);
                    in >> std::ws;
                    if (std::isdigit(in.peek()) || in.peek() == '.' || in.peek() == '-')
                        in >> c->insaneDamage;
                } else if (field == "stat_radiation") {
                    auto* c = go->AddComponent<RadiationStat>();
                    in >> c->maxRadiation >> c->gainPerSecond >> c->decayPerSecond
                       >> c->sickThreshold >> c->damagePerSecond;
                    ReadStatTail(in, c);
                } else if (field == "stat_bleed") {
                    auto* c = go->AddComponent<BleedingStat>();
                    in >> c->maxBleed >> c->damagePerSecond >> c->clotPerSecond;
                    ReadStatTail(in, c);
                } else if (field == "stat_poison") {
                    auto* c = go->AddComponent<PoisonStat>();
                    in >> c->maxPoison >> c->damagePerSecond >> c->decayPerSecond;
                    ReadStatTail(in, c);
                } else if (field == "stat_wet") {
                    auto* c = go->AddComponent<WetnessStat>();
                    in >> c->maxWetness >> c->soakPerSecond >> c->dryPerSecond
                       >> c->chillPerSecond >> c->soakedThreshold;
                    ReadStatTail(in, c);
                } else if (field == "stat_carry") {
                    auto* c = go->AddComponent<CarryWeightStat>();
                    in >> c->maxLoad >> c->overStaminaDrain >> c->minSpeedFactor;
                    ReadStatTail(in, c);
                } else if (field == "statuseffect") {
                    auto* c = go->AddComponent<StatusEffectStat>();
                    int sm = 1; in >> sm; c->sendMessages = (sm != 0);
                } else if (field == "survivalsave") {
                    auto* c = go->AddComponent<SurvivalSave>();
                    c->saveKey = ReadQuoted(in);
                    int lo = 1, sc = 0; in >> lo >> sc;
                    c->loadOnStart = (lo != 0); c->saveContinuously = (sc != 0);
                } else if (field == "survivalzone") {
                    auto* c = go->AddComponent<SurvivalZone>();
                    in >> c->effect >> c->amount >> c->duration;
                    c->effectName = ReadQuoted(in);
                } else if (field == "consumables") {
                    auto* c = go->AddComponent<Consumables>();
                    int ri = 1, cd = 1, dd = 1, n = 0;
                    in >> ri >> cd >> dd >> n;
                    c->requireInventory = (ri != 0); c->consumeOnDrop = (cd != 0); c->destroyDroppedItem = (dd != 0);
                    for (int i = 0; i < n; ++i) {
                        Consumables::Recipe r;
                        r.item = ReadQuoted(in); r.action = ReadQuoted(in); in >> r.amount;
                        c->recipes.push_back(r);
                    }
                } else if (field == "daynight") {
                    auto* c = go->AddComponent<DayNightCycle>();
                    int ps = 0, cs = 1, rs = 1, ck = 1;
                    in >> c->dayLengthSeconds >> c->time >> ps >> cs >> rs >> ck
                       >> c->dayIntensity >> c->nightIntensity >> c->dayAmbient >> c->nightAmbient;
                    c->paused = (ps != 0); c->controlSun = (cs != 0); c->rotateSun = (rs != 0); c->controlSky = (ck != 0);
                    auto col = [&](Color& k) { in >> k.r >> k.g >> k.b; k.a = 1.0f; };
                    col(c->dayLight); col(c->nightLight); col(c->skyDay); col(c->skyHorizon); col(c->skyNight);
                } else if (field == "npc") {
                    auto* c = go->AddComponent<NPCController>();
                    int fm = 1;
                    in >> c->behavior >> c->moveSpeed >> c->sightRange >> c->wanderRadius
                       >> c->attackRange >> c->attackDamage >> c->attackInterval >> fm;
                    c->faceMovement = (fm != 0);
                    c->targetName = ReadQuoted(in);
                    in >> std::ws;   // optional combat health (added later)
                    if (std::isdigit(in.peek()) || in.peek() == '.' || in.peek() == '-') {
                        in >> c->maxHealth; c->health = c->maxHealth;
                        int iv = 0; in >> iv; c->invulnerable = (iv != 0);
                    }
                } else if (field == "melee") {
                    auto* c = go->AddComponent<MeleeAttacker>();
                    int key = 'f', um = 1;
                    in >> c->damage >> c->range >> c->arc >> c->cooldown >> key >> um;
                    c->attackKey = (char)key; c->useMouse = (um != 0);
                } else if (field == "spawner") {
                    auto* c = go->AddComponent<Spawner>();
                    c->templateName = ReadQuoted(in);
                    int dt = 1;
                    in >> c->interval >> c->maxAlive >> c->totalToSpawn >> c->spawnRadius
                       >> c->startDelay >> dt;
                    c->deactivateTemplate = (dt != 0);
                } else if (field == "craftmenu") {
                    auto* c = go->AddComponent<CraftingMenu>();
                    int key = 'c', op = 0, anch = 0;
                    in >> key >> op >> c->position.x >> c->position.y >> c->buttonSize.x
                       >> c->buttonSize.y >> c->spacing >> anch;
                    c->toggleKey = (char)key; c->open = (op != 0); c->anchor = (UIAnchor)anch;
                } else if (field == "crafting") {
                    auto* c = go->AddComponent<Crafting>();
                    int n = 0; in >> n;
                    for (int i = 0; i < n; ++i) {
                        Crafting::Recipe r;
                        r.output = ReadQuoted(in); in >> r.outputCount;
                        int ni = 0; in >> ni;
                        for (int j = 0; j < ni; ++j) {
                            Crafting::Ingredient ing;
                            ing.item = ReadQuoted(in); in >> ing.count;
                            r.inputs.push_back(ing);
                        }
                        c->recipes.push_back(std::move(r));
                    }
                } else if (field == "fpctrl") {
                    float ws = 4.5f, rs = 8, jf = 6, ms = 0.15f; int cj = 1, da = 1, iy = 0;
                    in >> ws >> rs >> jf >> ms >> cj >> da;
                    in >> std::ws; if (std::isdigit(in.peek())) in >> iy;   // optional invertY
                    auto* fp = go->AddComponent<FirstPersonController>();
                    fp->walkSpeed = ws; fp->runSpeed = rs; fp->jumpForce = jf;
                    fp->mouseSensitivity = ms; fp->canJump = (cj != 0); fp->driveAnimation = (da != 0);
                    fp->invertY = (iy != 0);
                    // Optional momentum + jump-feel block (absent in older scenes).
                    float ac = fp->acceleration, de = fp->deceleration, aircon = fp->airControl,
                          coy = fp->coyoteTime, jb = fp->jumpBufferTime;
                    in >> std::ws;
                    if ((std::isdigit(in.peek()) || in.peek() == '-') &&
                        (in >> ac >> de >> aircon >> coy >> jb)) {
                        fp->acceleration = ac; fp->deceleration = de; fp->airControl = aircon;
                        fp->coyoteTime = coy; fp->jumpBufferTime = jb;
                        // Optional run + stance block (absent in older scenes).
                        int sk = (unsigned char)fp->sprintKey, tr = fp->toggleRun ? 1 : 0,
                            ck = (unsigned char)fp->crouchKey, pk = (unsigned char)fp->proneKey,
                            tst = fp->toggleStance ? 1 : 0;
                        float csp = fp->crouchSpeed, psp = fp->proneSpeed, seh = fp->standEyeHeight,
                              ceh = fp->crouchEyeHeight, peh = fp->proneEyeHeight, sl = fp->stanceLerp;
                        in >> std::ws;
                        if ((std::isdigit(in.peek()) || in.peek() == '-') &&
                            (in >> sk >> tr >> ck >> pk >> tst >> csp >> psp >> seh >> ceh >> peh >> sl)) {
                            fp->sprintKey = (char)sk; fp->toggleRun = (tr != 0);
                            fp->crouchKey = (char)ck; fp->proneKey = (char)pk; fp->toggleStance = (tst != 0);
                            fp->crouchSpeed = csp; fp->proneSpeed = psp; fp->standEyeHeight = seh;
                            fp->crouchEyeHeight = ceh; fp->proneEyeHeight = peh; fp->stanceLerp = sl;
                            // Optional lean block.
                            int ll = (unsigned char)fp->leanLeftKey, lr = (unsigned char)fp->leanRightKey;
                            float la = fp->leanAngle, lo = fp->leanOffset, lsp = fp->leanSpeed;
                            in >> std::ws;
                            if ((std::isdigit(in.peek()) || in.peek() == '-') &&
                                (in >> ll >> lr >> la >> lo >> lsp)) {
                                fp->leanLeftKey = (char)ll; fp->leanRightKey = (char)lr;
                                fp->leanAngle = la; fp->leanOffset = lo; fp->leanSpeed = lsp;
                                in >> std::ws;
                                if (std::isdigit(in.peek())) { int mj = fp->maxJumps; in >> mj; fp->maxJumps = mj; }
                            }
                        }
                    }
                } else if (field == "tpctrl") {
                    float ws = 4.5f, rs = 8, jf = 6, ms = 0.2f, ts = 12, ds = 5, ch = 1.5f; int cj = 1, da = 1;
                    in >> ws >> rs >> jf >> ms >> ts >> ds >> ch >> cj >> da;
                    auto* tp = go->AddComponent<ThirdPersonController>();
                    tp->walkSpeed = ws; tp->runSpeed = rs; tp->jumpForce = jf; tp->mouseSensitivity = ms;
                    tp->turnSpeed = ts; tp->distance = ds; tp->cameraHeight = ch;
                    tp->canJump = (cj != 0); tp->driveAnimation = (da != 0);
                    // extended (optional): defaults preserved when absent in older scenes
                    int ix = 0, iy = 0, fmode = (int)tp->faceMode;
                    float mnd = tp->minDistance, mxd = tp->maxDistance, zs = tp->zoomSpeed,
                          so = tp->shoulderOffset, cd = tp->cameraDamping,
                          mnp = tp->minPitch, mxp = tp->maxPitch;
                    if (in >> ix >> iy >> mnd >> mxd >> zs >> so >> cd >> mnp >> mxp >> fmode) {
                        tp->invertX = (ix != 0); tp->invertY = (iy != 0);
                        tp->minDistance = mnd; tp->maxDistance = mxd; tp->zoomSpeed = zs;
                        tp->shoulderOffset = so; tp->cameraDamping = cd;
                        tp->minPitch = mnp; tp->maxPitch = mxp;
                        tp->faceMode = (ThirdPersonController::FaceMode)fmode;
                        // Optional momentum + jump-feel block (absent in older scenes).
                        float ac = tp->acceleration, de = tp->deceleration, aircon = tp->airControl,
                              coy = tp->coyoteTime, jb = tp->jumpBufferTime;
                        if (in >> ac >> de >> aircon >> coy >> jb) {
                            tp->acceleration = ac; tp->deceleration = de; tp->airControl = aircon;
                            tp->coyoteTime = coy; tp->jumpBufferTime = jb;
                            // Optional camera-collision block (absent in older scenes).
                            int cc = tp->cameraCollision ? 1 : 0; float skin = tp->cameraCollisionSkin;
                            if (in >> cc >> skin) {
                                tp->cameraCollision = (cc != 0); tp->cameraCollisionSkin = skin;
                                // Optional run + stance block.
                                int sk = (unsigned char)tp->sprintKey, tr = tp->toggleRun ? 1 : 0,
                                    ck = (unsigned char)tp->crouchKey, pk = (unsigned char)tp->proneKey,
                                    tst = tp->toggleStance ? 1 : 0;
                                float csp = tp->crouchSpeed, psp = tp->proneSpeed,
                                      chd = tp->crouchHeightDrop, phd = tp->proneHeightDrop, sl = tp->stanceLerp;
                                in >> std::ws;
                                if ((std::isdigit(in.peek()) || in.peek() == '-') &&
                                    (in >> sk >> tr >> ck >> pk >> tst >> csp >> psp >> chd >> phd >> sl)) {
                                    tp->sprintKey = (char)sk; tp->toggleRun = (tr != 0);
                                    tp->crouchKey = (char)ck; tp->proneKey = (char)pk; tp->toggleStance = (tst != 0);
                                    tp->crouchSpeed = csp; tp->proneSpeed = psp;
                                    tp->crouchHeightDrop = chd; tp->proneHeightDrop = phd; tp->stanceLerp = sl;
                                    // Optional lean block.
                                    int ll = (unsigned char)tp->leanLeftKey, lr = (unsigned char)tp->leanRightKey;
                                    float la = tp->leanAngle, lo = tp->leanOffset, lsp = tp->leanSpeed;
                                    in >> std::ws;
                                    if ((std::isdigit(in.peek()) || in.peek() == '-') &&
                                        (in >> ll >> lr >> la >> lo >> lsp)) {
                                        tp->leanLeftKey = (char)ll; tp->leanRightKey = (char)lr;
                                        tp->leanAngle = la; tp->leanOffset = lo; tp->leanSpeed = lsp;
                                        in >> std::ws;
                                        if (std::isdigit(in.peek())) { int mj = tp->maxJumps; in >> mj; tp->maxJumps = mj; }
                                    }
                                }
                            }
                        }
                    }
                } else if (field == "tdctrl") {
                    auto* td = go->AddComponent<TopDownController>();
                    std::string sk = "-"; int da = 1, rf = 1, cr = 0;
                    in >> td->walkSpeed >> td->runSpeed >> sk >> da >> rf >> td->turnSpeed
                       >> td->acceleration >> td->deceleration >> cr
                       >> td->cameraDistance >> td->cameraPitch >> td->cameraYaw
                       >> td->lookHeight >> td->cameraDamping;
                    td->sprintKey = (sk == "-" || sk.empty()) ? 0 : sk[0];
                    td->driveAnimation = (da != 0); td->rotateToFace = (rf != 0); td->cameraRelative = (cr != 0);
                } else if (field == "frctrl") {
                    auto* fr = go->AddComponent<FreeRoamController>();
                    int sk = (unsigned char)fr->sprintKey, uk = (unsigned char)fr->upKey,
                        dk = (unsigned char)fr->downKey, iy = 0, lc = 0, rr = 1;
                    in >> fr->moveSpeed >> fr->boostMultiplier >> sk >> uk >> dk
                       >> fr->mouseSensitivity >> iy >> lc >> rr
                       >> fr->minPitch >> fr->maxPitch >> fr->acceleration >> fr->yaw >> fr->pitch;
                    fr->sprintKey = (char)sk; fr->upKey = (char)uk; fr->downKey = (char)dk;
                    fr->invertY = (iy != 0); fr->lockCursor = (lc != 0); fr->lookRequiresRightMouse = (rr != 0);
                } else if (field == "tpsctrl") {
                    auto* ts = go->AddComponent<ThirdPersonShooterController>();
                    std::string sk = "-"; int cj = 1, da = 1, iy = 0, cc = 1, af = 0, lc = 1;
                    in >> ts->walkSpeed >> ts->runSpeed >> ts->jumpForce >> sk >> cj >> da >> ts->turnSpeed
                       >> ts->acceleration >> ts->deceleration >> ts->airControl
                       >> ts->coyoteTime >> ts->jumpBufferTime
                       >> ts->mouseSensitivity >> iy >> ts->minPitch >> ts->maxPitch >> ts->distance >> ts->cameraHeight
                       >> ts->shoulderOffset >> cc >> ts->cameraCollisionSkin
                       >> ts->aimButton >> ts->aimDistance >> ts->aimShoulder >> ts->aimSpeed
                       >> ts->fireButton >> af >> ts->fireRate >> lc;
                    ts->sprintKey = (sk == "-" || sk.empty()) ? 0 : sk[0];
                    ts->canJump = (cj != 0); ts->driveAnimation = (da != 0); ts->invertY = (iy != 0);
                    ts->cameraCollision = (cc != 0); ts->autoFire = (af != 0); ts->lockCursor = (lc != 0);
                    // Optional lean block (absent in older scenes).
                    int ll = (unsigned char)ts->leanLeftKey, lr = (unsigned char)ts->leanRightKey;
                    float la = ts->leanAngle, lo = ts->leanOffset, lsp = ts->leanSpeed;
                    in >> std::ws;
                    if ((std::isdigit(in.peek()) || in.peek() == '-') &&
                        (in >> ll >> lr >> la >> lo >> lsp)) {
                        ts->leanLeftKey = (char)ll; ts->leanRightKey = (char)lr;
                        ts->leanAngle = la; ts->leanOffset = lo; ts->leanSpeed = lsp;
                        in >> std::ws;
                        if (std::isdigit(in.peek())) { int mj = ts->maxJumps; in >> mj; ts->maxJumps = mj; }
                    }
                } else if (field == "mover") {
                    Vec3 v; in >> v.x >> v.y >> v.z;
                    go->AddComponent<Mover>()->velocity = v;
                } else if (field == "spinner") {
                    Vec3 v; in >> v.x >> v.y >> v.z;
                    go->AddComponent<Spinner>()->angularVelocity = v;
                } else if (field == "lifetime") {
                    float s = 1.0f; in >> s;
                    go->AddComponent<Lifetime>()->seconds = s;
                } else if (field == "stats") {
                    auto* st = go->AddComponent<Stats>();
                    in >> st->health >> st->maxHealth >> st->mana >> st->maxMana
                       >> st->level >> st->xp >> st->xpToNext
                       >> st->strength >> st->defense >> st->intelligence >> st->agility;
                } else if (field == "inventory") {
                    auto* inv = go->AddComponent<Inventory>();
                    std::size_t count = 0; in >> inv->capacity >> count;
                    inv->slots.clear();
                    for (std::size_t k = 0; k < count; ++k) {
                        Inventory::Slot s; s.item = ReadQuoted(in); in >> s.count;
                        inv->slots.push_back(s);
                    }
                } else if (field == "turnmgr") {
                    auto* tm = go->AddComponent<TurnManager>();
                    int as = 1; std::size_t count = 0;
                    in >> tm->current >> tm->round >> as >> count;
                    tm->autoStart = (as != 0);
                    tm->participants.clear();
                    for (std::size_t k = 0; k < count; ++k) tm->participants.push_back(ReadQuoted(in));
                } else if (field == "camerafollow") {
                    std::string tgt = ReadQuoted(in);
                    Vec3 off; float sm = 5.0f;
                    in >> off.x >> off.y >> off.z >> sm;
                    auto* cf = go->AddComponent<CameraFollow>();
                    cf->targetName = tgt; cf->offset = off; cf->smoothing = sm;
                } else if (field == "vcam") {
                    auto* vc = go->AddComponent<VirtualCamera>();
                    in >> vc->priority;
                    vc->follow = ReadQuoted(in);
                    vc->lookAt = ReadQuoted(in);
                    in >> vc->followOffset.x >> vc->followOffset.y >> vc->followOffset.z
                       >> vc->positionDamping >> vc->rotationDamping
                       >> vc->fieldOfView >> vc->shakeAmplitude >> vc->shakeFrequency;
                    // Optional extended fields (absent in older scenes).
                    in >> std::ws;
                    if (std::isdigit(in.peek()) || in.peek() == '-') {
                        int bm = 0;
                        in >> bm >> vc->lookAtOffset.x >> vc->lookAtOffset.y >> vc->lookAtOffset.z
                           >> vc->aimDeadZone >> vc->impulseDecay;
                        vc->bindingMode = (VirtualCamera::BindingMode)bm;
                    }
                    in >> std::ws;
                    if (std::isdigit(in.peek()) || in.peek() == '-') {
                        int fl = 0, oi = 1;
                        in >> fl >> vc->orbitYaw >> vc->orbitPitch >> vc->orbitRadius >> vc->orbitHeight
                           >> vc->orbitMinPitch >> vc->orbitMaxPitch >> oi >> vc->orbitButton >> vc->mouseSensitivity;
                        vc->freeLook = (fl != 0);
                        vc->orbitInput = (oi != 0);
                    }
                    in >> std::ws;
                    if (std::isdigit(in.peek())) {
                        int dl = 0, ad = 0;
                        in >> dl;
                        vc->dollyPath = ReadQuoted(in);
                        in >> vc->dollyPosition >> ad;
                        vc->dolly = (dl != 0);
                        vc->autoDolly = (ad != 0);
                    }
                } else if (field == "dollypath") {
                    auto* dp = go->AddComponent<DollyPath>();
                    int lp = 0; std::size_t cnt = 0;
                    in >> lp >> cnt;
                    dp->looped = (lp != 0);
                    dp->waypoints.resize(cnt);
                    for (std::size_t k = 0; k < cnt; ++k)
                        in >> dp->waypoints[k].x >> dp->waypoints[k].y >> dp->waypoints[k].z;
                } else if (field == "dollycart") {
                    auto* dc = go->AddComponent<DollyCart>();
                    dc->path = ReadQuoted(in);
                    int am = 1;
                    in >> dc->position >> dc->speed >> am;
                    dc->autoMove = (am != 0);
                } else if (field == "cmbrain") {
                    auto* cb = go->AddComponent<CinemachineBrain>();
                    in >> cb->blendTime;
                    in >> std::ws;
                    if (std::isdigit(in.peek())) { int e = 1; in >> e; cb->easeInOut = (e != 0); }
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
                    in >> std::ws; // optional alignment (added later)
                    if (std::isdigit(in.peek())) in >> tr->align;
                    in >> std::ws; // optional outline (added later still)
                    if (std::isdigit(in.peek())) {
                        int ol = 0; Color oc;
                        in >> ol >> oc.r >> oc.g >> oc.b >> oc.a;
                        tr->outline = (ol != 0); tr->outlineColor = oc;
                    }
                    in >> std::ws; // optional bold flag (added later)
                    if (std::isdigit(in.peek())) { int bd = 0; in >> bd; tr->bold = (bd != 0); }
                    in >> std::ws; // optional box size + vcenter + background (added later)
                    if (std::isdigit(in.peek()) || in.peek() == '-') {
                        int vc = 1, bg = 0; Color bc;
                        in >> tr->size.x >> tr->size.y >> vc >> bg >> bc.r >> bc.g >> bc.b >> bc.a;
                        tr->vcenter = (vc != 0); tr->background = (bg != 0); tr->backgroundColor = bc;
                    }
                    in >> std::ws; // optional spacing + uppercase + wrap (added later)
                    if (std::isdigit(in.peek()) || in.peek() == '-') {
                        int uc = 0, wr = 0;
                        in >> tr->letterSpacing >> tr->lineSpacing >> uc >> wr;
                        tr->uppercase = (uc != 0); tr->wrap = (wr != 0);
                    }
                    in >> std::ws; // optional italic/gradient/typewriter/bottom-align (newer)
                    if (std::isdigit(in.peek()) || in.peek() == '-') {
                        int it = 0, gr = 0, ab = 0; Color cb;
                        in >> it >> gr >> cb.r >> cb.g >> cb.b >> cb.a
                           >> tr->visibleChars >> tr->typeSpeed >> ab;
                        tr->italic = (it != 0); tr->gradient = (gr != 0);
                        tr->colorBottom = cb; tr->alignBottom = (ab != 0);
                    }
                    in >> std::ws; // optional TTF font path (newest)
                    if (in.peek() == '"') tr->fontPath = ReadQuoted(in);
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
                } else if (field == "animator") {
                    float speed = 1.0f; int playing = 1, loop = 1, tc = 0;
                    in >> speed >> playing >> loop;
                    std::string cname = ReadQuoted(in);
                    in >> tc;
                    auto* anim = go->AddComponent<Animator>();
                    anim->speed = speed; anim->playing = (playing != 0);
                    anim->clip.loop = (loop != 0); anim->clip.name = cname;
                    for (int t = 0; t < tc; ++t) {
                        std::string tn = ReadQuoted(in);
                        int kc = 0; in >> kc;
                        for (int k = 0; k < kc; ++k) {
                            float kt = 0, kv = 0; in >> kt >> kv;
                            anim->clip.AddKey(tn, kt, kv);
                        }
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
                    // Optional customization block (corner/font/border, added later).
                    in >> std::ws;
                    if (std::isdigit(in.peek())) {
                        Color bc;
                        in >> btn->cornerRadius >> btn->fontScale >> btn->borderWidth
                           >> bc.r >> bc.g >> bc.b >> bc.a;
                        btn->borderColor = bc;
                        in >> std::ws; // optional hover scale (added later)
                        if (std::isdigit(in.peek())) in >> btn->hoverScale;
                        in >> std::ws; // optional icon path + size (added later)
                        if (in.peek() == '"') { btn->icon = ReadQuoted(in); in >> btn->iconSize; }
                        in >> std::ws; // optional hover-text/transition/toggle block (added later)
                        if (std::isdigit(in.peek()) || in.peek() == '-') {
                            Color ht; int tm = 0, on = 0, ir = 0, hr = 0;
                            in >> ht.r >> ht.g >> ht.b >> ht.a >> btn->transitionSpeed
                               >> tm >> on >> btn->pressOffset >> ir >> hr
                               >> btn->repeatDelay >> btn->repeatInterval;
                            btn->hoverTextColor = ht; btn->toggleMode = (tm != 0); btn->isOn = (on != 0);
                            btn->iconRight = (ir != 0); btn->holdRepeat = (hr != 0);
                            in >> std::ws; // optional shape + drop shadow (added later)
                            if (std::isdigit(in.peek())) {
                                int sp = 0, sh = 0; Color sc;
                                in >> sp >> sh >> sc.r >> sc.g >> sc.b >> sc.a
                                   >> btn->shadowOffset.x >> btn->shadowOffset.y;
                                btn->shape = (UIShape)sp; btn->shadow = (sh != 0); btn->shadowColor = sc;
                                in >> std::ws; // optional shadow softness (added later)
                                if (std::isdigit(in.peek()) || in.peek() == '.') in >> btn->shadowSoftness;
                                in >> std::ws; // optional assigned OnClick (target + function), added later
                                if (in.peek() == '"') {
                                    btn->clickTarget = ReadQuoted(in); btn->clickFunction = ReadQuoted(in);
                                    in >> std::ws; // optional OnClick argument (added later)
                                    if (std::isdigit(in.peek()) || in.peek() == '.' || in.peek() == '-') in >> btn->clickArg;
                                    in >> std::ws; // optional TTF label font (newest)
                                    if (in.peek() == '"') btn->fontPath = ReadQuoted(in);
                                }
                            }
                        }
                    }
                } else if (field == "uipanel") {
                    auto* pn = go->AddComponent<UIPanel>();
                    Color c;
                    in >> pn->position.x >> pn->position.y >> pn->size.x >> pn->size.y
                       >> c.r >> c.g >> c.b >> c.a;
                    pn->color = c;
                    ReadAnchor(in, pn->anchor);
                    // Optional customization block (corner/border, added later).
                    in >> std::ws;
                    if (std::isdigit(in.peek())) {
                        Color bc;
                        in >> pn->cornerRadius >> pn->borderWidth
                           >> bc.r >> bc.g >> bc.b >> bc.a;
                        pn->borderColor = bc;
                    }
                    in >> std::ws; // optional gradient (added later)
                    if (std::isdigit(in.peek())) {
                        int g = 0; Color gb;
                        in >> g >> gb.r >> gb.g >> gb.b >> gb.a;
                        pn->useGradient = (g != 0); pn->colorBottom = gb;
                    }
                    in >> std::ws; // optional drop shadow (added later)
                    if (std::isdigit(in.peek())) {
                        int sh = 0; Color sc;
                        in >> sh >> sc.r >> sc.g >> sc.b >> sc.a >> pn->shadowOffset.x >> pn->shadowOffset.y;
                        pn->shadow = (sh != 0); pn->shadowColor = sc;
                    }
                    in >> std::ws; // optional shape + gradient direction (added later)
                    if (std::isdigit(in.peek())) {
                        int sp = 0, gh = 0; in >> sp >> gh;
                        pn->shape = (UIShape)sp; pn->gradientHorizontal = (gh != 0);
                        in >> std::ws; // optional shadow softness (added later)
                        if (std::isdigit(in.peek()) || in.peek() == '.') in >> pn->shadowSoftness;
                    }
                } else if (field == "uidocument") {
                    auto* doc = go->AddComponent<UIDocument>();
                    doc->markup = ReadQuoted(in);
                } else if (field == "terrain") {
                    auto* tr = go->AddComponent<Terrain>();
                    int res = 32; float sz = 50.0f; Color c; std::size_t count = 0;
                    in >> res >> sz >> c.r >> c.g >> c.b >> c.a >> count;
                    tr->Resize(res); tr->size = sz; tr->color = c;
                    for (std::size_t k = 0; k < count && k < tr->heights.size(); ++k)
                        in >> tr->heights[k];
                    // Optional trailing auto-color fields (added later).
                    auto tmore = [&]() -> bool {
                        while (in.peek() == ' ' || in.peek() == '\t') in.get();
                        int ch = in.peek();
                        return ch != '\n' && ch != '\r' && ch != EOF;
                    };
                    auto rcol = [&](Color& col) { in >> col.r >> col.g >> col.b >> col.a; };
                    if (tmore()) { int ac = 1; in >> ac; tr->autoColor = (ac != 0);
                        rcol(tr->waterColor); rcol(tr->sandColor); rcol(tr->grassColor);
                        rcol(tr->rockColor);  rcol(tr->snowColor);
                        in >> tr->waterLevel >> tr->snowLevel >> tr->rockSlope;
                    }
                    tr->Apply();   // rebuild the mesh into a MeshRenderer
                } else if (field == "character") {
                    std::string rest; std::getline(in, rest);   // rest of the line after the field token
                    if (!rest.empty() && rest[0] == ' ') rest.erase(0, 1);
                    auto* ch = go->AddComponent<Character>();
                    ch->FromText(rest);
                    ch->Apply();    // rebuild the mesh into a MeshRenderer
                } else if (field == "network") {
                    auto* nm = go->AddComponent<NetworkManager>();
                    int as = 0, port = 45000;
                    in >> as >> port;
                    nm->autoStart = (NetworkManager::AutoStart)as;
                    nm->autoPort = (std::uint16_t)port;
                    nm->autoHost = ReadQuoted(in);
                    nm->startName = ReadQuoted(in);
                    in >> std::ws;
                    if (in.peek() == '"') nm->startRoom = ReadQuoted(in);  // optional (newer)
                    in >> std::ws;
                    if (std::isdigit(in.peek())) { in >> nm->maxPlayers >> nm->snapshotRate; } // host settings
                    in >> std::ws;
                    if (in.peek() == '"') nm->serverName = ReadQuoted(in);
                    in >> std::ws;
                    if (in.peek() == '"') nm->password = ReadQuoted(in);
                } else if (field == "worldui") {
                    auto* wu = go->AddComponent<WorldUI>();
                    wu->text = ReadQuoted(in);
                    Color c, bg, bc; Vec3 off; int sd = 1;
                    in >> c.r >> c.g >> c.b >> c.a
                       >> bg.r >> bg.g >> bg.b >> bg.a
                       >> off.x >> off.y >> off.z
                       >> wu->pixelSize >> sd >> wu->maxDistance >> wu->bar
                       >> bc.r >> bc.g >> bc.b >> bc.a;
                    wu->color = c; wu->background = bg; wu->worldOffset = off;
                    wu->scaleWithDistance = (sd != 0); wu->barColor = bc;
                } else if (field == "uiinput") {
                    auto* inp = go->AddComponent<UIInputField>();
                    int an = 0; Color c;
                    in >> inp->position.x >> inp->position.y >> inp->size.x >> inp->size.y >> an >> inp->maxLength;
                    inp->anchor = (UIAnchor)an;
                    inp->text = ReadQuoted(in); inp->placeholder = ReadQuoted(in);
                    in >> c.r >> c.g >> c.b >> c.a; inp->color = c;
                    in >> std::ws; // optional content type (added later)
                    if (std::isdigit(in.peek())) { int ct = 0; in >> ct; inp->contentType = (UIInputField::ContentType)ct; }
                    in >> std::ws; // optional shape + corner + focus ring (added later)
                    if (std::isdigit(in.peek())) {
                        int sp = 0; Color bc;
                        in >> sp >> inp->cornerRadius >> inp->borderWidth >> bc.r >> bc.g >> bc.b >> bc.a;
                        inp->shape = (UIShape)sp; inp->borderColor = bc;
                    }
                } else if (field == "uitabs") {
                    auto* tb = go->AddComponent<UITabs>();
                    int an = 0, sp = 0, it = 1; std::size_t count = 0;
                    Color bg, se, tc, stc;
                    in >> tb->position.x >> tb->position.y >> tb->size.x >> tb->size.y >> an >> tb->value
                       >> bg.r >> bg.g >> bg.b >> bg.a >> se.r >> se.g >> se.b >> se.a
                       >> tc.r >> tc.g >> tc.b >> tc.a >> stc.r >> stc.g >> stc.b >> stc.a
                       >> sp >> tb->cornerRadius >> it >> count;
                    tb->anchor = (UIAnchor)an; tb->background = bg; tb->selected = se;
                    tb->textColor = tc; tb->selectedTextColor = stc;
                    tb->shape = (UIShape)sp; tb->interactable = (it != 0);
                    tb->tabs.clear();
                    for (std::size_t k = 0; k < count; ++k) tb->tabs.push_back(ReadQuoted(in));
                    in >> std::ws;   // optional per-tab content pages (added later)
                    if (std::isdigit(in.peek())) {
                        std::size_t pc = 0; in >> pc;
                        tb->pages.clear();
                        for (std::size_t k = 0; k < pc; ++k) tb->pages.push_back(ReadQuoted(in));
                    }
                } else if (field == "uidropdown") {
                    auto* dd = go->AddComponent<UIDropdown>();
                    int an = 0; std::size_t count = 0;
                    Color c, h, l, t, b;
                    in >> dd->position.x >> dd->position.y >> dd->size.x >> dd->size.y
                       >> an >> dd->value
                       >> c.r >> c.g >> c.b >> c.a >> h.r >> h.g >> h.b >> h.a
                       >> l.r >> l.g >> l.b >> l.a >> t.r >> t.g >> t.b >> t.a
                       >> b.r >> b.g >> b.b >> b.a >> count;
                    dd->anchor = (UIAnchor)an;
                    dd->color = c; dd->hoverColor = h; dd->listColor = l;
                    dd->textColor = t; dd->borderColor = b;
                    dd->options.clear();
                    for (std::size_t k = 0; k < count; ++k) dd->options.push_back(ReadQuoted(in));
                    in >> std::ws; // optional interactable (added later)
                    if (std::isdigit(in.peek())) { int it = 1; in >> it; dd->interactable = (it != 0); }
                    in >> std::ws; // optional placeholder (added later)
                    if (in.peek() == '"') dd->placeholder = ReadQuoted(in);
                    in >> std::ws; // optional shape + corner radius (added later)
                    if (std::isdigit(in.peek())) { int sp = 0; in >> sp >> dd->cornerRadius; dd->shape = (UIShape)sp; }
                } else if (field == "uibind") {
                    auto* tb = go->AddComponent<UITextBind>();
                    tb->format = ReadQuoted(in);
                } else if (field == "uibarbind") {
                    auto* bb = go->AddComponent<UIBarBind>();
                    bb->var = ReadQuoted(in);
                    in >> bb->min >> bb->max;
                } else if (field == "uidraggable") {
                    auto* dg = go->AddComponent<UIDraggable>();
                    int rs = 0, at = 0; in >> rs >> at;
                    dg->returnToStart = (rs != 0); dg->anyTarget = (at != 0);
                    in >> std::ws;
                    if (std::isdigit(in.peek())) { int sn = 0; in >> sn; dg->snapToSlot = (sn != 0); }
                    in >> std::ws;
                    if (std::isdigit(in.peek())) {
                        int ax = 0, bf = 0; in >> ax >> dg->dragThreshold >> bf;
                        dg->axis = (UIDraggable::Axis)ax; dg->bringToFront = (bf != 0);
                    }
                } else if (field == "uidroptarget") {
                    auto* dt = go->AddComponent<UIDropTarget>();
                    in >> std::ws;
                    if (in.peek() == '"') dt->acceptTag = ReadQuoted(in);
                    in >> std::ws;
                    if (std::isdigit(in.peek())) {
                        int sh = 1; in >> sh; dt->showHighlight = (sh != 0);
                        in >> dt->highlight.r >> dt->highlight.g
                           >> dt->highlight.b >> dt->highlight.a;
                    }
                    in >> std::ws;               // extended fields (optional)
                    if (std::isdigit(in.peek())) {
                        int bg = 0, sc = 1;
                        in >> bg >> dt->background.r >> dt->background.g >> dt->background.b >> dt->background.a
                           >> dt->cornerRadius >> dt->borderWidth
                           >> dt->borderColor.r >> dt->borderColor.g >> dt->borderColor.b >> dt->borderColor.a
                           >> dt->rejectHighlight.r >> dt->rejectHighlight.g >> dt->rejectHighlight.b >> dt->rejectHighlight.a
                           >> sc;
                        dt->drawBackground = (bg != 0); dt->snapToCenter = (sc != 0);
                    }
                } else if (field == "draggable") {
                    auto* dg = go->AddComponent<Draggable>();
                    int rs = 0, at = 0, sn = 0, ax = 0, bf = 0;
                    in >> rs >> at >> sn >> ax >> dg->dragThreshold >> bf;
                    dg->returnToStart = (rs != 0); dg->anyTarget = (at != 0);
                    dg->snapToZone = (sn != 0); dg->axis = (Draggable::Axis)ax;
                    dg->bringToFront = (bf != 0);
                    in >> std::ws;
                    if (std::isdigit(in.peek()) || in.peek() == '-')
                        in >> dg->gridX >> dg->gridY >> dg->dragScale;
                } else if (field == "dropzone") {
                    auto* dz = go->AddComponent<DropZone>();
                    in >> std::ws;
                    if (in.peek() == '"') dz->acceptTag = ReadQuoted(in);
                } else if (field == "uitooltip") {
                    auto* tt = go->AddComponent<UITooltip>();
                    tt->text = ReadQuoted(in);
                    Color bg, tc, bc;
                    in >> tt->delay
                       >> bg.r >> bg.g >> bg.b >> bg.a
                       >> tc.r >> tc.g >> tc.b >> tc.a
                       >> bc.r >> bc.g >> bc.b >> bc.a;
                    tt->background = bg; tt->textColor = tc; tt->borderColor = bc;
                } else if (field == "uilayout") {
                    auto* lg = go->AddComponent<UILayoutGroup>();
                    int dir = 0, an = 0;
                    in >> dir >> an >> lg->origin.x >> lg->origin.y >> lg->spacing >> lg->padding;
                    lg->direction = (UILayoutGroup::Direction)dir; lg->anchor = (UIAnchor)an;
                    lg->Arrange();
                } else if (field == "uiscroll") {
                    auto* sv = go->AddComponent<UIScrollView>();
                    int an = 0; Color c;
                    in >> sv->position.x >> sv->position.y >> sv->size.x >> sv->size.y
                       >> an >> sv->contentHeight >> c.r >> c.g >> c.b >> c.a;
                    sv->anchor = (UIAnchor)an; sv->background = c;
                } else if (field == "canvas") {
                    auto* cv = go->AddComponent<Canvas>();
                    int sm = 0;
                    in >> sm >> cv->referenceResolution.x >> cv->referenceResolution.y
                       >> cv->matchWidthOrHeight >> cv->scaleFactor >> cv->sortOrder;
                    cv->scaleMode = (Canvas::ScaleMode)sm;
                    in >> std::ws;                       // optional visible + opacity
                    int p = in.peek();
                    if (std::isdigit(p) || p == '-') {
                        int vis = 1; in >> vis >> cv->opacity; cv->visible = (vis != 0);
                        in >> std::ws;                   // optional world-space canvas fields
                        int p2 = in.peek();
                        if (std::isdigit(p2) || p2 == '-') {
                            int ws = 0;
                            in >> ws >> cv->designResolution.x >> cv->designResolution.y >> cv->worldPixelsPerUnit;
                            cv->worldSpace = (ws != 0);
                            in >> std::ws;
                            int p3 = in.peek();
                            if (std::isdigit(p3) || p3 == '-') { int bb = 1; in >> bb; cv->billboard = (bb != 0); }
                        }
                    }
                } else if (field == "worldui3d") {
                    auto* w3 = go->AddComponent<WorldSpaceUI>();
                    in >> w3->pixelsPerUnit;
                    in >> std::ws;
                    if (std::isdigit(in.peek())) { int bb = 1; in >> bb; w3->billboard = (bb != 0); }
                    in >> std::ws;
                    if (std::isdigit(in.peek())) { int cs = 0; in >> cs; w3->constantSize = (cs != 0); }
                    in >> std::ws;
                    if (std::isdigit(in.peek()) || in.peek() == '-' || in.peek() == '.') in >> w3->constantScale;
                } else if (field == "eventsystem") {
                    go->AddComponent<EventSystem>();
                } else if (field == "uiprogress") {
                    auto* pb = go->AddComponent<UIProgressBar>();
                    Color bg, fl;
                    in >> pb->position.x >> pb->position.y >> pb->size.x >> pb->size.y >> pb->value
                       >> bg.r >> bg.g >> bg.b >> bg.a >> fl.r >> fl.g >> fl.b >> fl.a;
                    pb->background = bg; pb->fill = fl;
                    ReadAnchor(in, pb->anchor);
                    in >> std::ws; // optional style block (added later)
                    if (std::isdigit(in.peek())) {
                        int sp = 0; Color tc;
                        in >> pb->cornerRadius >> sp >> tc.r >> tc.g >> tc.b >> tc.a;
                        pb->showPercent = (sp != 0); pb->textColor = tc;
                        in >> std::ws; // optional fill direction (added later)
                        if (std::isdigit(in.peek())) { int fd = 0; in >> fd; pb->fillDir = (UIProgressBar::FillDir)fd; }
                        in >> std::ws; // optional shape + gradient fill (added later)
                        if (std::isdigit(in.peek())) {
                            int sp = 0, gf = 0; Color fe;
                            in >> sp >> gf >> fe.r >> fe.g >> fe.b >> fe.a;
                            pb->shape = (UIShape)sp; pb->gradientFill = (gf != 0); pb->fillEnd = fe;
                        }
                    }
                } else if (field == "uiradial") {
                    auto* rp = go->AddComponent<UIRadialProgress>();
                    int cw = 1, an = 0, sp = 0; Color bg, fl, tc;
                    in >> rp->position.x >> rp->position.y >> rp->size.x >> rp->size.y >> rp->value
                       >> rp->thickness >> rp->startAngle >> cw
                       >> bg.r >> bg.g >> bg.b >> bg.a >> fl.r >> fl.g >> fl.b >> fl.a
                       >> an >> sp >> tc.r >> tc.g >> tc.b >> tc.a;
                    rp->clockwise = (cw != 0); rp->background = bg; rp->fill = fl;
                    rp->anchor = (UIAnchor)an; rp->showPercent = (sp != 0); rp->textColor = tc;
                    in >> std::ws; // optional spinner (added later)
                    if (std::isdigit(in.peek())) { int sn = 0; in >> sn >> rp->spinSpeed; rp->spin = (sn != 0); }
                } else if (field == "minimap") {
                    auto* mm = go->AddComponent<Minimap>();
                    Color bg, bd, tc; int xz = 1, an = 0;
                    in >> mm->position.x >> mm->position.y >> mm->size.x >> mm->size.y
                       >> bg.r >> bg.g >> bg.b >> bg.a >> bd.r >> bd.g >> bd.b >> bd.a
                       >> mm->borderWidth;
                    mm->target = ReadQuoted(in);
                    in >> tc.r >> tc.g >> tc.b >> tc.a
                       >> mm->worldPerPixel >> xz >> mm->blipSize >> an;
                    mm->background = bg; mm->border = bd; mm->targetColor = tc;
                    mm->useXZ = (xz != 0); mm->anchor = (UIAnchor)an;
                } else if (field == "minimapblip") {
                    auto* bl = go->AddComponent<MinimapBlip>();
                    Color c; int sq = 1;
                    in >> c.r >> c.g >> c.b >> c.a >> bl->size >> sq;
                    bl->color = c; bl->square = (sq != 0);
                } else if (field == "crosshair") {
                    auto* cr = go->AddComponent<Crosshair>();
                    Color c, dc, oc; int sl = 1, dt = 0, ol = 1, an = 0;
                    in >> cr->position.x >> cr->position.y >> cr->size.x >> cr->size.y
                       >> c.r >> c.g >> c.b >> c.a
                       >> cr->thickness >> cr->gap >> cr->length
                       >> sl >> dt >> cr->dotSize
                       >> dc.r >> dc.g >> dc.b >> dc.a
                       >> ol >> oc.r >> oc.g >> oc.b >> oc.a >> an;
                    cr->color = c; cr->dotColor = dc; cr->outlineColor = oc;
                    cr->showLines = (sl != 0); cr->dot = (dt != 0); cr->outline = (ol != 0);
                    cr->anchor = (UIAnchor)an;
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
                    in >> std::ws; // optional fill block (added later)
                    if (std::isdigit(in.peek())) {
                        int fm = 0; in >> fm >> im->fillAmount >> im->cornerRadius;
                        im->fillMode = (UIImage::FillMode)fm;
                    }
                    in >> std::ws; // optional shape (added later)
                    if (std::isdigit(in.peek())) { int sp = 0; in >> sp; im->shape = (UIShape)sp; }
                } else if (field == "uislider") {
                    auto* sl = go->AddComponent<UISlider>();
                    Color bg, fl, kn;
                    in >> sl->position.x >> sl->position.y >> sl->size.x >> sl->size.y
                       >> sl->value >> sl->minValue >> sl->maxValue
                       >> bg.r >> bg.g >> bg.b >> bg.a >> fl.r >> fl.g >> fl.b >> fl.a
                       >> kn.r >> kn.g >> kn.b >> kn.a;
                    sl->background = bg; sl->fill = fl; sl->knob = kn;
                    ReadAnchor(in, sl->anchor);
                    in >> std::ws; // optional style block (added later)
                    if (std::isdigit(in.peek())) {
                        int sv = 0; Color tc;
                        in >> sl->cornerRadius >> sl->knobSize >> sv >> tc.r >> tc.g >> tc.b >> tc.a;
                        sl->showValue = (sv != 0); sl->textColor = tc;
                        in >> std::ws; // optional whole-numbers flag (added later)
                        if (std::isdigit(in.peek())) { int wn = 0; in >> wn; sl->wholeNumbers = (wn != 0); }
                        in >> std::ws;
                        if (std::isdigit(in.peek())) { int it = 1; in >> it; sl->interactable = (it != 0); }
                        in >> std::ws;
                        if (std::isdigit(in.peek())) { int vt = 0; in >> vt; sl->vertical = (vt != 0); }
                        in >> std::ws; // optional track shape + round knob (added later)
                        if (std::isdigit(in.peek())) {
                            int ts = 0, rk = 0; in >> ts >> rk;
                            sl->trackShape = (UIShape)ts; sl->roundKnob = (rk != 0);
                        }
                    }
                } else if (field == "uistepper") {
                    auto* st = go->AddComponent<UIStepper>();
                    Color bg, bt, tc; int wn = 1, wr = 0, sh = 0, it = 1;
                    in >> st->position.x >> st->position.y >> st->size.x >> st->size.y
                       >> st->value >> st->minValue >> st->maxValue >> st->step
                       >> wn >> wr
                       >> bg.r >> bg.g >> bg.b >> bg.a >> bt.r >> bt.g >> bt.b >> bt.a
                       >> tc.r >> tc.g >> tc.b >> tc.a;
                    st->wholeNumbers = (wn != 0); st->wrap = (wr != 0);
                    st->background = bg; st->button = bt; st->textColor = tc;
                    ReadAnchor(in, st->anchor);
                    in >> sh >> st->cornerRadius >> it;
                    st->shape = (UIShape)sh; st->interactable = (it != 0);
                } else if (field == "uirating") {
                    auto* rt = go->AddComponent<UIRating>();
                    Color on, off; int ah = 0, ro = 0;
                    in >> rt->position.x >> rt->position.y >> rt->size.x >> rt->size.y
                       >> rt->count >> rt->value >> ah >> ro
                       >> on.r >> on.g >> on.b >> on.a >> off.r >> off.g >> off.b >> off.a;
                    rt->allowHalf = (ah != 0); rt->readOnly = (ro != 0);
                    rt->on = on; rt->off = off;
                    ReadAnchor(in, rt->anchor);
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
                    in >> std::ws; // optional corner radius (added later)
                    if (std::isdigit(in.peek())) in >> tg->cornerRadius;
                    in >> std::ws; // optional switch style + knob color (added later)
                    if (std::isdigit(in.peek())) {
                        int st = 0; Color kc;
                        in >> st >> kc.r >> kc.g >> kc.b >> kc.a;
                        tg->style = (UIToggle::Style)st; tg->knobColor = kc;
                        in >> std::ws;
                        if (std::isdigit(in.peek())) { int it = 1; in >> it; tg->interactable = (it != 0); }
                        in >> std::ws; // optional animation speed (added later)
                        if (std::isdigit(in.peek())) in >> tg->animSpeed;
                    }
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
        if (go->isStatic) out << "  static 1\n";
        if (go->layer != 0) out << "  layer " << go->layer << "\n";
        if (go->uiDrawOrder != 0) out << "  uiorder " << go->uiDrawOrder << "\n";
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
        if (auto* mr = go->GetComponent<MeshRenderer>())   { add(mr->meshPath); add(mr->texture); add(mr->normalMap); add(mr->specularMap); }
        if (auto* im = go->GetComponent<UIImage>())         add(im->texture);
        if (auto* bt = go->GetComponent<UIButton>())        add(bt->icon);
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
