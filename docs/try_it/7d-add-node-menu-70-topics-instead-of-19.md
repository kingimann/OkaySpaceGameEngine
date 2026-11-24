---
id: 7d
row: | 7d | Visual script Add Node menu: 70 topics instead of 19 | Visual Script window, right-click the canvas or press Space | New in this change; the list and search are tested, the drawing is unverified |
---
## 7d. Add Node menu: 70 topics instead of 19

**What it is.** The panel that opens when you add a node to a visual script (right-click the canvas, press Space, press
Add node, or drop a wire on empty space). It had 19 topics, and one of them, Advanced, held 515 nodes. Now it has 70
specific topics in 14 sections, and every node is in the topic you would look in. Nothing was removed and your saved
graphs are unchanged (a graph stores a node's type, never its topic). Code: `editor/VisualScriptCategories.hpp`,
`editor/VisualScriptPlaceRules.hpp`; the list and the rules are in [visual_scripting_naming.md](../visual_scripting_naming.md),
every node under its topic in [visual_scripting_topics.md](../visual_scripting_topics.md), and the before-and-after
measurement in [visual_script_category_audit.md](../visual_script_category_audit.md).

**How to reach it.**

1. Open the editor (`Tools\OkayEngine.exe`, or the launcher), make or open any project, select an object (for example
   the cube of a new project) and add a **Visual Script** component in the Inspector.
2. In the Inspector's Visual Script section press **Open in node editor** (or open **View > Editors & Tools > Visual
   Script**).
3. Right-click the empty canvas, or press **Space**. The Add Node panel opens.

**What you should see.**

1. On the left, under Starter, Recently used, All and Favourites, the topics in small groups under grey headings:
   Events, Flow, Variables / Lists & Saving, Math, Text, Objects, Movement & Physics, Gameplay, Characters & AI,
   Sound / Camera & Screen, Scenes & Environment, Input, Online, Debugging. Each topic shows how many nodes it holds.
   **Click a heading to fold its topics** (click again to open them). Hover a topic for a one-line description.
2. Click a topic with many nodes (for example Numbers, Tweens or Multiplayer): the subcategories appear under it
   (Rounding & Snapping, Limits & Wrapping, ... ). Hover one for its description. Click one to see only its nodes.
3. Twelve nodes and where to find each (the name on the node, then topic / subcategory):

   | Node | Where it is |
   |---|---|
   | On Start, On Every Frame | Game Events |
   | On Key Pressed | Input Events (it used to be under Input / Keys) |
   | On Collision Start | Collision & Trigger Events |
   | Wait Seconds | Waiting & Timers |
   | Spawn | Create & Destroy |
   | Move Towards | Move & Follow |
   | Add Force | Forces & Velocity |
   | Set Weather | Weather & Fog (it used to be under Scene / Atmosphere) |
   | Play Sound | Sound Effects |
   | Start Quest | Quests & Dialogue / Quests |
   | Host Game | Multiplayer / Hosting & Joining |

4. **Search still finds everything.** Type `weather`: Set Weather and its twin Weather Set are at the top, and both sit in
   Weather & Fog. Type `scene` or
   `atmosphere`: the nodes that used to be under Scene (Set Weather, Set Fog, Set Post Effect, ...) are in the list too,
   because the old topic name is kept as a search word. Type `advanced`: the text-script functions that used to be in
   the Advanced bucket are listed.
5. **Favourites keep working.** Click the star on On Key Pressed, close the panel and open it again: the star is still
   there (it is stored by node name in `okay_vsnodes.txt`, never by topic).

Screenshots (headless renders: the real Add Node panel code drawn with ImGui's software renderer and its default font, so
the font and window frame differ from the editor):


Status: the topic list, the rule that every node has exactly one place, the size limits, the search words for old topic
names and the favourites keys are checked by `tests/test_vs_categories.cpp` (compiled and run with g++ on Linux). The
counts above come from that test's dump. The three screenshots show the panel working with the new topics. **Unverified:**
how the sidebar looks and feels in the real editor window (font, dot colours, scrolling); nothing was run on Windows.
