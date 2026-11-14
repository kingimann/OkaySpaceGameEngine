# 10. Story & cinematics

Timeline cutscenes, branching dialogue graphs, quests, global story variables and the built-in boxes that show them. Code is
five components in `Components/` with their `.cpp` files in `engine/src/Components/` (about 2.8k lines) and two editor windows
(Timeline, Dialogue and Quests). Docs: `docs/cutscenes.md`, `docs/dialogue_quests.md`. Merged in #574: a caption manager
(`Core/Subtitles`, area 32.2) that the cutscene and dialogue code do not use yet. Camera rigs and dolly paths are area 4.8.

## 10. Story & cinematics

### 10.1 Cutscene sequences and Sequence Player
- **What:** A `.okayseq` timeline of tracks (camera shots, object transforms, animation and audio cues, events, fades and subtitles, letterbox bars, camera shake, a coloured screen fade, nested sequences) played by a Sequence Player component, authored in the Timeline window with live preview in the Scene and Game views.
- **Parts:**
  - 10.1.1 Sequence data and file - `Components/Sequence` - tracks, keys, shots, cues and events; evaluators `CameraAt`, `ChannelAt`, `OverlayAt`; text save and load
  - 10.1.2 Sequence Player - `Components/SequencePlayer` - Play, Pause, Resume, Stop, Skip, Seek; play on start, loop, time scale, skip key, lock player; restores the main camera when done
  - 10.1.3 Camera, transform, animation and audio tracks - `Components/Sequence`, `Components/SequencePlayer` - shots that cut or blend between camera objects or inline poses; eased keys; Character, model and Animator cues; one-shot audio through an Audio Source
  - 10.1.4 Event track actions - `Components/Sequence` - named events to scripts, set active, teleport, fade to and from black, subtitle, time scale, wait for input, end
  - 10.1.5 Timeline window - `editor/TimelineWindow.*` - tracks, ruler with scrub and snap, key editing, Record and Key, Shot from Scene view, preview outside Play, Play in Game, own undo
  - 10.1.6 Letterbox, shake, screen fade and nested sequences - `Components/Sequence`, `Components/SequencePlayer` - keyed bar height, keyed shake (off when reduce motion is on), keyed colour and opacity, and a cue that plays another `.okayseq`; bars and the colour fade are overlay rectangles, so every renderer draws them
- **Code:** `Components/Sequence`, `Components/SequencePlayer`, `editor/TimelineWindow.*`
- **API:** `Sequence`, `SequencePlayer`, `SeqShot`, `SeqKey`, `SeqEvent`, `SeqOverlay`, script `play_sequence`, `stop_sequence`, `sequence_playing`, `sequence_time`, `on_sequence_event`, `on_sequence_started`, `on_sequence_finished`, visual-script On Sequence Event
- **Editor:** View > Editors & Tools > Timeline; Add Component > Gameplay > Sequence Player (cutscene); double-click a `.okayseq` in the Project panel
- **Docs:** `docs/cutscenes.md`
- **Tests:** `sequence`
- **Status:** Works - shots, keys, events, fades, letterbox bars, camera shake, coloured screen fade and nested sequences. The `sequence` test asserts each new track's effect in a scene. `examples/cutscene` is a cutscene you can play.
- **Open:** per docs, playback state is not saved and in multiplayer every client plays its own copy (start from an RPC); subtitles are drawn by the sequence itself, not by the caption manager that #574 added (`Core/Subtitles`)
- **Related:** 4.8, 7.3, 5.1, 10.4, 32.2, 8.5
- **Words:** cutscene, sequence, timeline, Sequence Player, .okayseq, cinematic, camera shot, track, keyframe, fade to black, subtitle, intro, skip cutscene, Timeline window, letterbox, camera shake, screen fade, nested sequence
- **Seen as:** the camera does not return after a cutscene; the player can still walk during it; the cutscene plays twice or from the middle after loading; an object stays where the cutscene left it; a subtitle never appears; skip does not fire the last events
- **Ask me like:** "make an intro cutscene with two camera shots and a fade"; "let the player skip the cutscene with Escape"

### 10.2 Dialogue graphs and Dialogue Runner
- **What:** Branching conversations authored as node graphs (`.okaydialog`) and run by a Dialogue Runner on an NPC: lines with portraits and sound, choices with conditions, story variable and quest hooks, items and script events.
- **Parts:**
  - 10.2.1 Graph and session - `Components/DialogueGraph` - node kinds Line, Choice, Condition, Set, Event, Give Item, Take Item, Start Quest, Complete Objective, Jump, End; `DialogueSession` walks it through a `DialogueHost`
  - 10.2.2 Dialogue Runner - `Components/DialogueRunner` - interact-key or script start, pause player, typewriter, auto advance, cancel with Esc, `StartDialogue`, `Advance`, `Choose`, events to scripts
  - 10.2.3 Dialogue window - `editor/DialogueWindow.*` (Dialogue tab) - node canvas, palette, inspector, validation, Test panel that plays the graph, copy and paste, own undo
  - 10.2.4 Text format - `engine/src/Components/StoryKv.hpp` - order-free `key=value` lines with quoted strings, shared by the dialogue and quest files
- **Code:** `Components/DialogueGraph`, `Components/DialogueRunner`, `engine/src/Components/StoryKv.hpp`, `editor/DialogueWindow.*`
- **API:** `DialogueGraph`, `DialogueSession`, `DialogueHost`, `DialogueRunner`, `DialogueNode::Kind`, script `start_dialogue`, `dialogue_active`, `stop_dialogue`, `on_dialogue_event`, `on_dialogue_end`, C# `Dialogue.Start`, visual-script On Dialogue Event
- **Editor:** View > Editors & Tools > Dialogue & Quests; Add Component > Gameplay > Dialogue Runner; behavior tree task Start Dialogue starts one
- **Docs:** `docs/dialogue_quests.md`
- **Tests:** `dialogue`
- **Status:** Works - DialogueGraph.hpp and DialogueRunner.hpp read in full and the function layout of DialogueGraph.cpp (parse, write, validate, session run); node walking, conditions and runner behaviour covered by the 372-line `dialogue` test
- **Open:** per docs, dialogues are local to each client in multiplayer and a conversation in progress is not saved; the speaker dropdown list is kept per graph; the interact key is shared with pickups through the Hold Prompt line (see 8.5)
- **Related:** 10.3, 10.4, 9.4, 11.1, 1.8, 8.5
- **Words:** dialogue, conversation, NPC talk, DialogueRunner, .okaydialog, choice, branching, speaker, portrait, typewriter, Start Dialogue, dialogue graph, Event node, Give Item node
- **Seen as:** pressing E does nothing near the NPC; the player keeps moving while talking; a choice never shows; text appears all at once; the conversation does not end; a condition node always takes the false path
- **Ask me like:** "make a shopkeeper who gives a quest if I have a key"; "add a choice that only shows when gold is 10 or more"

### 10.3 Quests and story variables
- **What:** A Quest Log tracks each quest's state and progress (talk, collect, kill, reach and custom objectives, prerequisites, rewards, repeatable quests), and one Story State per scene holds the global variables and flags that dialogues, quests and scripts share.
- **Parts:**
  - 10.3.1 Quest definitions and file - `Components/QuestLog` - `QuestDef`, objectives, rewards, prerequisites, `.okayquest` parse, write, validate and localization keys
  - 10.3.2 Quest Log runtime - `Components/QuestLog` - Start, Complete, Fail, Reset, `AddProgress`, `CompleteObjective`; collect counted from the player's inventory; kill hook on death; events to scripts
  - 10.3.3 Quest Trigger zone - `Components/QuestLog` - `QuestTrigger` completes reach objectives on entry
  - 10.3.4 Story State - `Components/StoryState` - named number, text and bool variables plus flags, one per scene on an object named Story, saved with the scene
  - 10.3.5 Quests tab - `editor/DialogueWindow.*` (Quests tab) - quest list, objectives, rewards, problems list, `.okayquest` New, Open and Save
- **Code:** `Components/QuestLog`, `Components/StoryState`, `engine/src/Components/StoryKv.hpp`, `editor/DialogueWindow.*`
- **API:** `QuestLog`, `QuestTrigger`, `QuestDef`, `QuestObjective`, `QuestStatus`, `StoryState`, script `start_quest`, `complete_objective`, `add_quest_progress`, `quest_state`, `set_story`, `get_story`, `set_story_flag`, `story_flag`, C# `Quest.Start`, `Story.Set`, visual-script Set Story Var, Get Story Var
- **Editor:** Quests tab of the Dialogue window; Add Component > Gameplay > Quest Log and Quest Trigger; Story State inspector
- **Docs:** `docs/dialogue_quests.md`
- **Tests:** `quests`, `dialogue`
- **Status:** Works - QuestLog.hpp and StoryState.hpp read in full and the function layout of QuestLog.cpp; objectives, prerequisites, rewards and state covered by the 323-line `quests` test
- **Open:** per docs, quests are per client in multiplayer (no host-validated shared state); per docs a Kill objective counts deaths through the victim's Health Stat (see 11.4)
- **Related:** 10.2, 10.4, 11.1, 11.4, 1.7, 35
- **Words:** quest, quest log, objective, reward, prerequisite, journal, story state, story variable, flag, Quest Trigger, .okayquest, mission, collect quest, kill quest, reach zone
- **Seen as:** a quest never completes after collecting the items; the reward is not given; a quest cannot be restarted; the story variable resets after loading; the tracker shows the wrong quest
- **Ask me like:** "add a quest to bring 3 ore to the smith"; "unlock the gate when the quest is completed"

### 10.4 Story UI: dialogue box, quest tracker and journal
- **What:** The built-in screens: the dialogue box with typewriter text, portrait and numbered choices, the quest tracker overlay, the notification toasts and the quest journal window. Each can be turned off to draw your own.
- **Parts:**
  - 10.4.1 Dialogue box - `Components/DialogueRunner` - drawn through the host's OkayUI bridge; speaker as title, portrait or avatar, continue hint; off with `drawUI`
  - 10.4.2 Quest tracker, notifications and journal - `Components/QuestLog` - top-right tracker, toasts on start, progress and completion, journal on J with Active, Completed and All tabs; off with `drawUI`
  - 10.4.3 Modal ownership - `Components/DialogueRunner` - `StoryUIModal` tells the hosts a story overlay owns the pointer so the cursor is freed
  - 10.4.4 Localized words - `Components/DialogueRunner`, `Components/QuestLog` - `LocKeys` lists the string-table keys of the built-in words
- **Code:** `Components/DialogueRunner`, `Components/QuestLog`
- **API:** `DialogueRunner::drawUI`, `DialogueRunner::CurrentLine`, `QuestLog::showTracker`, `QuestLog::allowJournal`, `QuestLog::journalKey`, `QuestLog::onNotify`, `StoryUIModal`, script `toggle_journal`
- **Editor:** Dialogue Runner and Quest Log inspectors (draw UI, tracker, journal key, notifications)
- **Docs:** `docs/dialogue_quests.md`
- **Tests:** `dialogue`, `quests`
- **Status:** Works for the boxes as drawn through the host UI bridge (DialogueRunner.hpp and QuestLog.hpp read; behaviour tested through the runner and log states, not pixel output)
- **Open:** per docs the boxes are drawn in one fixed launcher look through the OkayUI bridge; restyling means turning `drawUI` off and drawing your own from the events and records
- **Related:** 12.7, 12.8, 10.2, 10.3, 1.8, 32
- **Words:** dialogue box, subtitle box, quest tracker, journal, quest log UI, notification toast, portrait, choice buttons, typewriter, OkayUI, drawUI
- **Seen as:** the dialogue box covers the screen or is too small; the tracker overlaps the minimap; the journal does not open; toasts appear in English only; text overflows the box
- **Ask me like:** "move the quest tracker to the left"; "draw my own dialogue box instead of the built-in one"
