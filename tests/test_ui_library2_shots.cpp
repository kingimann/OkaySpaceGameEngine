// Headless pictures (software renderer, same UI pass as the game) of the UI library slice 2 parts: the game HUD parts, rich text
// links and icons, and the whole example scene. Not a real window.
#define SDL_MAIN_HANDLED
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scene/UILibrary.hpp"
#include "okay/Inventory/ItemDatabase.hpp"
#include "okay/UI/UILayoutUpdate.hpp"
#include "okay/UI/UIPainterSDL.hpp"
#include "okay/UI/UIRenderer.hpp"
#include "okay/Graphics/Image.hpp"
#include <SDL.h>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>

using namespace okay;

namespace {

bool Render(const std::string& path, int width, int height, const std::function<void(Scene&)>& build,
            const std::function<void(Scene&)>& after = nullptr) {
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ABGR8888);
    SDL_Renderer* ren = surf ? SDL_CreateSoftwareRenderer(surf) : nullptr;
    if (!ren) {
        if (surf) SDL_FreeSurface(surf);
        return false;
    }
    std::unordered_map<std::string, SDL_Texture*> cache;
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(ren, 18, 22, 32, 255);
    SDL_RenderClear(ren);
    Scene scene("shot");
    scene.physicsEnabled = false;
    scene.uiFont = "pixel";
    build(scene);
    ui2d::SDLPainter painter(ren, &cache, "");
    ui2d::RenderSceneUI(scene, painter, width, height);
    SDL_RenderPresent(ren);
    if (after) after(scene);
    Image img(width, height);
    for (int y = 0; y < height; ++y) {
        const unsigned char* row = (const unsigned char*)surf->pixels + y * (int)surf->pitch;
        for (int x = 0; x < width; ++x)
            img.SetPixel(x, y, Color::FromBytes(row[x * 4], row[x * 4 + 1], row[x * 4 + 2], row[x * 4 + 3]));
    }
    for (auto& kv : cache) if (kv.second) SDL_DestroyTexture(kv.second);
    SDL_DestroyRenderer(ren);
    SDL_FreeSurface(surf);
    int drawn = 0;
    const unsigned char* px = img.Data();
    for (int i = 0; i < width * height; ++i)
        if (px[i * 4] != 18 || px[i * 4 + 1] != 22 || px[i * 4 + 2] != 32) ++drawn;
    if (drawn < 40) return false;
    return img.SavePNG(path);
}

} // namespace

int main(int argc, char** argv) {
    RUN_SUITE("ui_library2_shots");
    std::string dir = std::string(OKAY_TEST_OUTPUT_DIR) + "/ui_library2";
    if (argc >= 2 && argv[1][0]) dir = argv[1];
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    CHECK(!ec);
    auto at = [&](const char* file) { return dir + "/" + file; };

    CHECK(Render(at("hud-ammo.png"), 520, 120, [](Scene& s) {
        const int cur[3] = {24, 6, 0};
        for (int i = 0; i < 3; ++i) {
            auto* a = s.CreateGameObject(std::string("Ammo") + std::to_string(i))->AddComponent<UIAmmoCounter>();
            a->anchor = UIAnchor::TopLeft; a->position = {14.0f + 170.0f * (float)i, 18}; a->size = {160, 84};
            a->current = cur[i]; a->reserve = 90; a->magazine = 30; a->reloading = false;
            a->showPips = false;
        }
    }));
    CHECK(Render(at("hud-ammo-reloading.png"), 260, 120, [](Scene& s) {
        auto* a = s.CreateGameObject("Ammo")->AddComponent<UIAmmoCounter>();
        a->anchor = UIAnchor::TopLeft; a->position = {20, 18}; a->size = {214, 84};
        a->current = 0; a->reserve = 60; a->magazine = 12; a->reloading = true;
    }));
    CHECK(Render(at("hud-radial.png"), 320, 320, [](Scene& s) {
        auto* w = s.CreateGameObject("Wheel")->AddComponent<UIRadialMenu>();
        w->anchor = UIAnchor::TopLeft; w->position = {10, 10}; w->size = {300, 300};
        w->items = "Attack|Defend|Heal|Run|Talk|Bag"; w->innerRadius = 40;
        w->Open();
        w->PointAt(90, -40);                                        // pointing up and to the right: the second wedge
    }));
    CHECK(Render(at("hud-killfeed.png"), 440, 130, [](Scene& s) {
        auto* f = s.CreateGameObject("Feed")->AddComponent<UIKillFeed>();
        f->anchor = UIAnchor::TopLeft; f->position = {10, 8}; f->size = {420, 110};
        f->maxEntries = 4; f->playerName = "Player";
        f->Push("Sentry", "Wolf");
        f->Push("Raider", "Wolf", "Axe");
        f->Push("Player", "Raider", "Rifle");
    }));
    CHECK(Render(at("hud-prompt.png"), 380, 70, [](Scene& s) {
        auto* p = s.CreateGameObject("Prompt")->AddComponent<UIInteractionPrompt>();
        p->anchor = UIAnchor::TopLeft; p->position = {14, 14}; p->size = {352, 40};
        p->alwaysShow = true; p->key = "E"; p->action = "Open the door";
        p->Settle();
    }));
    CHECK(Render(at("hud-tracker.png"), 340, 190, [](Scene& s) {
        auto* log = s.CreateGameObject("Quests")->AddComponent<QuestLog>();
        log->showTracker = false; log->showNotifications = false;
        QuestDef d; d.id = "wolves"; d.title = "Wolf trouble";
        QuestObjective a; a.description = "Hunt the wolves"; a.count = 3;
        QuestObjective b; b.description = "Tell the mayor";
        QuestObjective c; c.description = "Cross the bridge";
        d.objectives = {a, b, c};
        log->quests.push_back(d);
        auto* t = s.CreateGameObject("Tracker")->AddComponent<UIObjectiveTracker>();
        t->anchor = UIAnchor::TopLeft; t->position = {10, 10}; t->size = {320, 130};
        log->StartQuest("wolves");
        log->AddProgress("wolves", 0, 2);
        log->CompleteObjective("wolves", 1);
    }));
    CHECK(Render(at("hud-compass.png"), 620, 90, [](Scene& s) {
        GameObject* look = s.CreateGameObject("Looker");
        look->transform->SetRotation(Quat::Euler({0, 40, 0}));
        GameObject* tower = s.CreateGameObject("Tower"); tower->transform->SetPosition({20, 0, 60});
        GameObject* hut = s.CreateGameObject("Hut"); hut->transform->SetPosition({90, 0, 10});
        auto* c = s.CreateGameObject("Compass")->AddComponent<UICompass>();
        c->anchor = UIAnchor::TopLeft; c->position = {10, 14}; c->size = {600, 36};
        c->source = "Looker"; c->markers = "Tower|Hut"; c->fieldOfView = 140.0f;
    }));
    CHECK(Render(at("hud-item-card.png"), 320, 230, [](Scene& s) {
        ItemDatabase::Get().Clear();
        ItemDefinition d;
        d.id = "sword_iron"; d.name = "Iron Sword"; d.category = "Weapon"; d.description = "A plain, honest blade.";
        d.rarity = 2; d.rarityColor = Color::FromBytes(90, 150, 255); d.hasRarityColor = true;
        d.weight = 2.4f; d.maxStack = 1; d.equip = EquipSlot::MainHand;
        d.custom.push_back({"damage", "12"}); d.custom.push_back({"speed", "1.1"});
        ItemDatabase::Get().Put(d);
        auto* c = s.CreateGameObject("Card")->AddComponent<UIItemTooltipCard>();
        c->anchor = UIAnchor::TopLeft; c->position = {10, 10}; c->size = {300, 200};
        c->item = "sword_iron";
    }));
    ItemDatabase::Get().Clear();
    CHECK(Render(at("hud-ability.png"), 340, 110, [](Scene& s) {
        const char* names[3] = {"Ready", "Cooling", "Charges"};
        for (int i = 0; i < 3; ++i) {
            GameObject* g = s.CreateGameObject(names[i]);
            auto* ring = g->AddComponent<UIRadialProgress>();
            ring->anchor = UIAnchor::TopLeft; ring->position = {14.0f + 108.0f * (float)i, 14}; ring->size = {84, 84};
            ring->thickness = 7.0f; ring->value = 0.0f;
            ring->fill = Color::FromBytes(14, 16, 24, 240); ring->background = Color::FromBytes(110, 215, 150, 220);
            auto* a = g->AddComponent<UIAbilityIcon>();
            a->label = i == 0 ? "Q" : i == 1 ? "E" : "R"; a->keyText = a->label; a->cooldown = 8.0f;
            if (i == 2) { a->maxCharges = 2; a->Reset(); a->Use(); a->Update(1.0f); }
            if (i == 1) { a->Use(); a->Update(2.5f); }
        }
    }));
    {
        std::string clicked;
        CHECK(Render(at("rich-links-icons.png"), 480, 70, [](Scene& s) {
            ItemDatabase::Get().Clear();
            ItemDefinition d; d.id = "sword_iron"; d.name = "Iron Sword"; d.rarity = 2; d.rarityColor = Color::FromBytes(90, 150, 255); d.hasRarityColor = true;
            ItemDatabase::Get().Put(d);
            GameObject* g = s.CreateGameObject("Text");
            auto* t = g->AddComponent<TextRenderer>();
            t->screenSpace = true; t->richText = true; t->anchor = UIAnchor::TopLeft; t->screenPos = {12, 12}; t->size = {456, 40};
            t->pixelSize = 2.0f; t->color = Color::White; t->vcenter = false;
            t->text = "See the [url=manual]manual[/url] or buy [icon=sword_iron] for 5 [icon=coin]";
            g->AddComponent<UIRichLinks>();
        }, [&](Scene& s) {
            // The renderer recorded where the link was drawn; a click there finds it, a click elsewhere does not.
            GameObject* g = s.Find("Text");
            auto* links = g->GetComponent<UIRichLinks>();
            const auto& rects = ui2d::RichLinksOf(g);
            CHECK(rects.size() == 1);
            if (rects.size() == 1) {
                CHECK(rects[0].target == "manual");
                CHECK(links->Click(rects[0].x + rects[0].w * 0.5f, rects[0].y + rects[0].h * 0.5f));
                CHECK(links->LastLink() == "manual");
                CHECK(Prefs::GetString("ui_link", "") == "manual");
                CHECK(!links->Click(rects[0].x + rects[0].w + 40.0f, rects[0].y + rects[0].h * 0.5f));
            }
        }));
        ItemDatabase::Get().Clear();
    }
    // The whole example scene, at a desktop and a tall window size.
    CHECK(Render(at("example2-1280x720.png"), 1280, 720, [](Scene& s) { UILibrary::BuildSlice2(s); },
                 [](Scene& s) { UpdateUILayouts(s); }));
    CHECK(Render(at("example2-1280x900.png"), 1280, 900, [](Scene& s) { UILibrary::BuildSlice2(s); }));
    TEST_MAIN_RESULT();
}
