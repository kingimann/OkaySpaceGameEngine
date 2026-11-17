---
id: five-templates-removed
group: appendix
---

## Five starter templates removed from New Project

Coin Collector, Snake, Brick Breaker, Inventory and Humanoid are no longer offered when you create a project. Projects and scenes
already made from them still open and play, and the scene builders stay in the engine (still tested).

1. Windows launcher: open the **Templates** page (or **Create**, then the New Project wizard). The list now has 17 templates:
   Coin Collector, Snake, Brick Breaker, Inventory and Humanoid are gone; Main Menu, Platformer, First Person and the rest are still there.
2. Pick Main Menu (or any other) and create a project: the editor opens with the starter scene.
3. In the editor, **File > New Project** shows the same list.
4. Test: `ctest -R templates_work` checks the launcher list, the editor table and the builders agree.
