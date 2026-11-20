# gameplay/: Things a game is made of

> **What this is:** the list of pages in `docs/gameplay/`, one line each. Things a game is made of: animation, characters and cameras, AI and navigation, dialogue, cutscenes, vehicles, weapons, survival.
>
> **Status:** index, made 2026-11-20 when the docs were sorted into folders. Back to the [start page](../README.md).
>
> **Last checked:** 2026-11-20.

- [Custom character animations (keyframe clips)](animation.md): Custom character animations: keyframe clips, how they are made, played and blended.
- [Behavior trees](behavior_trees.md): NPC AI you author visually: a **behavior tree** decides what an NPC does every tick, a **blackboard** holds what it knows (its target, where it last saw it, its health), and the **Behavior Tree Runner** component plays the tree on an object...
- [Build mode and logic links: let players build in your game](build_mode.md): Build mode lets players build while they play.
- [Cameras, layers and tags](cameras.md): The camera upgrade, and layers, tags and the camera stack.
- [Cutscenes: sequences and the Timeline window](cutscenes.md): A **sequence** is a timeline of tracks that drives a cutscene without scripting: camera shots, object moves, animation and audio cues, events and subtitles, letterbox bars, camera shake, a coloured screen fade, and nested sequences.
- [Dialogue and quests](dialogue_quests.md): Conversations are **dialogue graphs** (`.okaydialog` files in the project's Assets folder), edited in the **Dialogue** window and played by a **Dialogue Runner** on an NPC or object.
- [The first-person arm (hand, forearm, presets)](fp_hand.md): What you see of your own body in first person, and what other players see.
- [Inverse kinematics (IK)](ik.md): IK bends bones so a hand, foot, head or tail tip reaches a goal, on top of whatever the animation did this frame.
- [Inventory: what exists, what is missing](inventory.md): A short, honest status of survival-game inventory in the engine.
- [Make your first game](making_a_game.md): This walks you from an empty editor to a shipped, double-clickable game in a few minutes.
- [Minimap and turn order](minimap.md): Two ready-made pieces that need no script to put on screen: a map in the corner, and a list that says whose turn it is.
- [Navigation meshes](navigation.md): NPCs, click-to-move players and `NavMeshAgent`s path over a **navigation mesh**: convex polygons baked from the scene's static colliders, joined by portals.
- [NPCs & Crafting](npcs_and_crafting.md): Two native components that round out the gameplay kit.
- [Top-Down Shooter and 2.5D Platformer controllers](shooter_and_25d_controllers.md): There are two ready-made player controllers for two popular kinds of game:
- [Survival Kit — native gameplay components](survival_kit.md): The Survival Kit is a set of built-in native components for common survival-game mechanics.
- [Vehicles](vehicles.md): The Vehicle Controller: the open-world driving model and the older arcade model.
- [Sandbox digging in first and third person](voxel_sandbox_dig.md): Written for the voxel Sandbox (`Components/VoxelDigger`, `Components/VoxelAim`, `Scene/SceneTemplates.hpp` `VoxelSandbox`).
- [Walk versus run (the natural walk and run)](walk_vs_run.md): The owner's words: "the character walking is basically running.
- [Weapons](weapons.md): A weapon is a row in a `.okayweapon` file, not a set of numbers typed onto every fighter.
- [Wind](wind.md): Wind blows trees, grass, flags, cables, particles and physics bodies, and it has a sound.
