#!/usr/bin/env python3
"""Build editor/VisualScriptNodeDocs.inc from the node registry, pin table and help text.

The .inc file is what the editor and the coverage test compile. Re-run this after
adding a hand-written node, then rebuild. Engine-API nodes are documented from
their binding entry at runtime, not here.
"""
import os, re, pathlib

ROOT = pathlib.Path(__file__).resolve().parents[1]

def cpp(s):
    return '"' + (s.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n')) + '"'

def pins_of(blob):
    return re.findall(r'"((?:\\.|[^"\\])*)"', blob)

def mask_quoted_braces(s):
    out, q, esc = [], False, False
    for c in s:
        if esc:
            out.append(c); esc = False; continue
        if c == '\\':
            out.append(c); esc = True; continue
        if c == '"':
            q = not q
        if q and c == '}':
            out.append('\x01')
        elif q and c == '{':
            out.append('\x02')
        else:
            out.append(c)
    return ''.join(out)

def unmask(s):
    return s.replace('\x01', '}').replace('\x02', '{')

def read_inc(path):
    """File text with each #include "generated/x.inc" line replaced by that file."""
    text = pathlib.Path(path).read_text()
    def sub(m):
        inc = ROOT / "editor" / m.group(1)
        if not inc.exists():
            inc = ROOT / "engine/include" / m.group(1)
        return inc.read_text() if inc.exists() else m.group(0)
    return re.sub(r'#include "((?:generated|okay/VisualScript)/[\w.]+\.inc)"', sub, text)

def load_nodes():
    text = read_inc(ROOT / "editor/VisualScriptEditor.hpp")
    a = text.find("VsWithTargetPins({")
    b = text.find("}));", a)
    body = mask_quoted_braces(text[a:b])
    # Hand nodes added next to the text-script builtins (Get Property, Call Method ...).
    c = text.find("inline std::vector<VsDef> VsWithBuiltinDefs")
    if c >= 0:
        d = text.find("return defs;", c)
        body += "\n" + mask_quoted_braces(text[c:d].replace("defs.push_back(", "").replace("});", "},"))
    pat = re.compile(
        r'\{\s*"([^"]+)"\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*(CAT_\w+)\s*,\s*(true|false)\s*,'
        r'\s*\{([^}]*)\}\s*,\s*\{([^}]*)\}\s*,\s*\{([^}]*)\}\s*,\s*(\d+)\s*,\s*(nullptr|"[^"]*")',
        re.S)
    nodes = []
    for typ, title, cat, exin, exout, din, dout, param, hint in pat.findall(body):
        nodes.append({
            "type": typ, "title": unmask(title).replace('\\"', '"'), "cat": cat,
            "execIn": exin == "true",
            "execOut": [unmask(p) for p in pins_of(exout)],
            "dataIn": [unmask(p) for p in pins_of(din)],
            "dataOut": [unmask(p) for p in pins_of(dout)],
            "param": int(param),
            "hint": None if hint == "nullptr" else unmask(hint[1:-1]),
        })
    return nodes

def load_friendly():
    """Plain-English titles and pin names from editor/VisualScriptNames.hpp.

    The registry text keeps the old tokens; the editor renames them at start-up
    (ApplyFriendlyNames), so the docs have to use the renamed pins.
    """
    text = read_inc(ROOT / "editor/VisualScriptNames.hpp")
    arrays = {n: pins_of(b) for n, b in
              re.findall(r'inline const char\* const (\w+)\[\]\s*=\s*\{([^}]*)\};', text)}
    out = {}
    for typ, title, cat_unused, extra_unused, inp, inc, outp, outc in re.findall(
            r'\{"(\w+)",\s*VsNameKind::\w+,\s*"((?:\\.|[^"\\])*)",\s*"((?:\\.|[^"\\])*)",\s*"((?:\\.|[^"\\])*)",'
            r'\s*(\w+),\s*(\d+),\s*(\w+),\s*(\d+)\}', text):
        out[typ] = {
            "title": title,
            "in": arrays.get(inp, [])[:int(inc)],
            "out": arrays.get(outp, [])[:int(outc)],
            "exec": [],
        }
    a = text.find("kVsExecLabels[]")
    b = text.find("};", a)
    for typ, arr, _n in re.findall(r'\{"(\w+)",\s*(\w+),\s*(\d+)\}', text[a:b]):
        out.setdefault(typ, {"title": "", "in": [], "out": [], "exec": []})["exec"] = arrays.get(arr, [])
    return out

def apply_friendly(nodes, friendly):
    """Rename titles and pins in place. node["renames"] maps old pin name to new."""
    for n in nodes:
        f = friendly.get(n["type"])
        n["renames"] = {}
        if not f:
            continue
        if f["title"]:
            n["title"] = f["title"]
        for key, pins in (("dataIn", f["in"]), ("dataOut", f["out"]), ("execOut", f["exec"])):
            for i, new in enumerate(pins):
                if new and i < len(n[key]) and n[key][i] != new:
                    n["renames"][n[key][i]] = new

def load_help():
    text = read_inc(ROOT / "editor/VisualScriptNodeHelp.hpp")
    return dict(re.findall(r'\{\s*"([^"]+)"\s*,\s*"((?:\\.|[^"\\])*)"\s*\}', text))

def load_rows():
    text = read_inc(ROOT / "engine/src/VisualScript/VsTypes.cpp")
    rows = {t: (e == "true", i, o) for t, e, i, o in
            re.findall(r'\{"([^"]+)",\s*(true|false),\s*"([^"]*)",\s*"([^"]*)"\}', text)}
    # Escape-hatch nodes have their signature in code, not in the table.
    for t, e, i, o in re.findall(
            r'type == "(\w+)"\)\s*\{\s*static const NodeSignature s\{(true|false),\s*"([^"]*)",\s*"([^"]*)"\}', text):
        rows.setdefault(t, (e == "true", i, o))
    return rows

def load_targets():
    text = read_inc(ROOT / "engine/src/VisualScript/NodeGraph.cpp")
    a = text.find("static const std::unordered_map<std::string, int> kPins")
    b = text.find("};", a)
    return {t: int(n) for t, n in re.findall(r'\{"([^"]+)",\s*(\d+)\}', text[a:b])}

TYPE_NAME = {"M": "Map", "C": "Color", "N": "Number", "B": "Bool", "T": "Text", "V": "Vector3", "O": "Object", "L": "List", "A": "Any"}
TYPE_DEFAULT = {
    "Number": "0", "Bool": "false", "Text": "empty text", "Vector3": "(0, 0, 0)",
    "Object": "no object", "Map": "an empty map", "List": "an empty list", "Any": "nothing", "Flow": "",
}

# Runtime fallbacks that are not the plain zero/false/empty value. Keyed by type + pin name.
FALLBACK = {
    ("SetVisible", "Visible"): "true (shown)",
    ("PauseGame", "Paused"): "true (paused)",
    ("ShowCursor", "Visible"): "true (cursor shown)",
    ("ChanceBranch", "Chance"): "0.5 (a coin flip)",
    ("EveryN", "N"): "2",
    ("Remap", "In Max"): "1",
    ("Remap", "Out Max"): "1",
    ("SmoothStep", "Edge1"): "1",
    ("Vec3Scale", "Scale"): "1 (unchanged length)",
    ("MoveTowards", "Speed"): "2 units per second",
    ("SetActive", "On"): "true (turned on)",
    ("SetWeather", "Intensity"): "leave the scene's current intensity",
    ("SetFog", "On"): "true (fog on)",
    ("SetFog", "Start"): "leave the scene's fog start",
    ("SetFog", "End"): "leave the scene's fog end",
    ("SetPostEffect", "Value"): "1",
    ("MusicStop", "Fade (s)"): "1 second",
    ("CameraShake", "Strength"): "0.5",
    ("SetFov", "Degrees"): "60",
    ("ParticleBurst", "Count"): "20",
    ("IkLook", "Weight"): "1 (fully looking)",
    ("IkHand", "Weight"): "1 (fully reaching)",
    ("ForLoop", "First"): "0",
    ("ForLoop", "Last"): "9",
    ("Gate", "Open"): "true (the flow passes)",
    ("RandomInt", "Min"): "0",
    ("RandomInt", "Max"): "10",
    ("Vec3Lerp", "T"): "0.5 (halfway)",
    ("SetColor", "R"): "1",
    ("SetColor", "G"): "1",
    ("SetColor", "B"): "1",
    ("SetColor", "A"): "1 (opaque)",
    ("PlayAction", "Blend"): "0.2 seconds",
    ("SetTimeScale", "Scale"): "1 (normal speed)",
    ("SetBlendShape", "Weight"): "1",
    ("Raycast", "Max Dist"): "no practical limit",
    ("AddItem", "Count"): "1",
    ("RemoveItem", "Count"): "1",
    ("HasItem", "Count"): "1",
    ("SpawnPickup", "Count"): "1",
    ("CraftItem", "Times"): "1",
    ("CompleteObjective", "Index"): "0 (the first objective)",
    ("ServerBudget", "Add"): "0 (just read the budget)",
}

EXTRA_DESC = {
    "OnFixedUpdate": "Runs on the physics step, not every drawn frame. Put forces and velocity changes here so they stay steady when the frame rate changes.",
    "OnEnable": "Runs when this object becomes active (including the first time it starts).",
    "OnDisable": "Runs when this object is turned off, before it stops updating.",
    "OnDestroy": "Runs when this object is about to be removed from the scene.",
    "Self": "This object, the one the graph is on. Wire it into any Target or Object input.",
    "Other": "The other object from the collision, trigger or mouse event that started this chain. Wire it into a Target input to act on what you hit.",
    "FindObject": "The object with the name you type. Wire it into a Target input. Empty when no object has that name.",
    "GetName": "The name of this object, or of Target when that pin is wired.",
    "GetTag": "The tag of this object, or of Target when that pin is wired.",
    "FunctionStart": "The start of a function you can call from anywhere in this graph. The text is its signature: Name(param: Type) -> result: Type.",
    "Return": "Sends the flow back to the Call Function that ran this function, along with the return values.",
    "CallFunction": "Runs a function defined by a Function node in this graph, then continues. Pick the function in the node.",
    "SetLocal": "Stores Value in a local variable of the function that is running. Locals exist only while that call runs.",
    "GetLocal": "Reads a local variable of the function that is running.",
}

# component ids the editor can add, separated by |. Empty means "say it in prose only".
REQ = {
    "AddForce": ("Needs a Rigidbody2D or a Rigidbody3D on this object (or on Target). A 2D body is used when both are present. Runs in Play mode.", "Rigidbody3D|Rigidbody2D"),
    "AddImpulse": ("Needs a Rigidbody2D or a Rigidbody3D on this object (or on Target). A 2D body is used when both are present. Runs in Play mode.", "Rigidbody3D|Rigidbody2D"),
    "SetVelocity": ("Needs a Rigidbody2D or a Rigidbody3D on this object (or on Target). A 2D body is used when both are present. Runs in Play mode.", "Rigidbody3D|Rigidbody2D"),
    "VelX": ("Needs a Rigidbody2D or a Rigidbody3D on this object (or on Target). Without one the speed is 0.", "Rigidbody3D|Rigidbody2D"),
    "VelY": ("Needs a Rigidbody2D or a Rigidbody3D on this object (or on Target). Without one the speed is 0.", "Rigidbody3D|Rigidbody2D"),
    "VelZ": ("Needs a Rigidbody2D or a Rigidbody3D on this object (or on Target). Without one the speed is 0.", "Rigidbody3D|Rigidbody2D"),
    "PlaySound": ("Needs an Audio Source on this object (or on Target). Runs in Play mode.", "AudioSource"),
    "AnimTrigger": ("Needs an Animator state machine on this object, a child or a parent (or on Target). Runs in Play mode.", "AnimStateMachine"),
    "AnimSetFloat": ("Needs an Animator state machine on this object, a child or a parent (or on Target). Runs in Play mode.", "AnimStateMachine"),
    "AnimSetBool": ("Needs an Animator state machine on this object, a child or a parent (or on Target). Runs in Play mode.", "AnimStateMachine"),
    "AnimState": ("Needs an Animator state machine on this object, a child or a parent (or on Target).", "AnimStateMachine"),
    "PlayClip": ("Needs an Animator on this object (or on Target) that has the named clip. Runs in Play mode.", "Animator"),
    "NavMoveTo": ("Needs a Nav Mesh Agent on this object (or on Target), and a baked nav mesh in the scene. Runs in Play mode.", "NavMeshAgent"),
    "NavStop": ("Needs a Nav Mesh Agent on this object (or on Target). Runs in Play mode.", "NavMeshAgent"),
    "NavArrived": ("Needs a Nav Mesh Agent on this object (or on Target). False when there is none.", "NavMeshAgent"),
    "AddItem": ("Needs an Inventory on this object (or on its root). Runs in Play mode.", "Inventory"),
    "RemoveItem": ("Needs an Inventory on this object (or on its root). Runs in Play mode.", "Inventory"),
    "HasItem": ("Needs an Inventory on this object (or on its root).", "Inventory"),
    "ItemCount": ("Needs an Inventory on this object (or on its root). The count is 0 when there is none.", "Inventory"),
    "LootRoll": ("Needs an Inventory on this object (or on its root). Runs in Play mode.", "Inventory"),
    "OnItemAdded": ("Runs only if this object has an Inventory that raises the event.", "Inventory"),
    "OnItemRemoved": ("Runs only if this object has an Inventory that raises the event.", "Inventory"),
    "OnInventoryFull": ("Runs only if this object has an Inventory that raises the event.", "Inventory"),
    "OnItemUsed": ("Runs only if this object has an Inventory that raises the event.", "Inventory"),
    "EquipItem": ("Needs an Equipment component, and an Inventory to take the item from. Runs in Play mode.", "Equipment"),
    "OnEquip": ("Runs only if this object has an Equipment component that raises the event.", "Equipment"),
    "CraftItem": ("Needs a Crafting component on this object. Runs in Play mode.", "Crafting"),
    "OnCraft": ("Runs only if this object has a Crafting or Crafting Station component.", "Crafting"),
    "GetSplinePoint": ("Needs a Spline on this object (or on Target).", "Spline"),
    "SplineLength": ("Needs a Spline on this object (or on Target). The length is 0 when there is none.", "Spline"),
    "SetLinePoints": ("Needs a Line Renderer on this object (or on Target). Runs in Play mode.", "LineRenderer"),
    "ClearTrail": ("Needs a Trail Renderer on this object (or on Target). Runs in Play mode.", "TrailRenderer"),
    "ParticlePlay": ("Needs a Particle System on this object (or on Target). Runs in Play mode.", "ParticleSystem"),
    "ParticleStop": ("Needs a Particle System on this object (or on Target). Runs in Play mode.", "ParticleSystem"),
    "ParticleBurst": ("Needs a Particle System on this object (or on Target). Runs in Play mode.", "ParticleSystem"),
    "BtSet": ("Needs a Behavior Tree Runner on this object (or on Target). Runs in Play mode.", "BehaviorTreeRunner"),
    "BtGet": ("Needs a Behavior Tree Runner on this object (or on Target).", "BehaviorTreeRunner"),
    "BtRun": ("Needs a Behavior Tree Runner on this object (or on Target). Runs in Play mode.", "BehaviorTreeRunner"),
    "SyncVarSet": ("Needs a Network Object on this object so the value is published to other players. Runs in Play mode, online.", "NetworkObject"),
    "SyncVarGet": ("Needs a Network Object on this object. Reads the last value the owner published.", "NetworkObject"),
    "OnNetRpc": ("Needs a Network Object on this object. Runs in Play mode when another player calls this object's RPC.", "NetworkObject"),
    "NetObjRpc": ("Needs a Network Object on this object, with the same id on each machine. Runs in Play mode, online.", "NetworkObject"),
    "IkLook": ("Needs a Full Body IK component on this object (or on Target). Runs in Play mode.", "FullBodyIK"),
    "IkHand": ("Needs a Full Body IK component on this object (or on Target). Runs in Play mode.", "FullBodyIK"),
    "SetText": ("Needs a Text renderer on the named object. Runs in Play mode.", "TextRenderer"),
    "StartDialogue": ("Needs a Dialogue Runner on the named object (or on this object when the name is empty). Runs in Play mode.", "DialogueRunner"),
    "MusicPlay": ("Needs a Music Player in the scene. Runs in Play mode.", "MusicPlayer"),
    "MusicStop": ("Needs a Music Player in the scene. Runs in Play mode.", "MusicPlayer"),
    "MusicPause": ("Needs a Music Player in the scene. Runs in Play mode.", "MusicPlayer"),
    "MusicResume": ("Needs a Music Player in the scene. Runs in Play mode.", "MusicPlayer"),
    "MusicNext": ("Needs a Music Player in the scene. Runs in Play mode.", "MusicPlayer"),
    "EnterBuildMode": ("Needs a Build Mode component in the scene (usually on the player). Runs in Play mode.", "BuildMode"),
    "ExitBuildMode": ("Needs a Build Mode component in the scene. Runs in Play mode.", "BuildMode"),
    "InBuildMode": ("Needs a Build Mode component in the scene. False when there is none.", "BuildMode"),
    "SetBuildPalette": ("Needs a Build Mode component in the scene. Runs in Play mode.", "BuildMode"),
    "PlaySequence": ("Needs a Sequence Player in the scene whose file matches the name, or a file Assets/<name>.okayseq. Runs in Play mode.", "SequencePlayer"),
    "StopSequence": ("Stops every Sequence Player in the scene. Runs in Play mode.", "SequencePlayer"),
    "Die": ("Meant for a character with a Health stat. It asks that character to die and play the death direction. Runs in Play mode.", "HealthStat"),
    "Revive": ("Meant for a character with a Health stat or an Animator that is flagged dead. Runs in Play mode.", "HealthStat"),
}

KEYS = {
    "Branch": "if, condition, true false",
    "OnStart": "awake, begin, init, start",
    "OnUpdate": "tick, frame, every frame",
    "OnKey": "keyboard, press, input",
    "Key": "keyboard, held, input",
    "Print": "log, debug, console",
    "Translate": "move, motion, position",
    "Raycast3D": "trace, hit, linecast, physics",
    "Raycast": "trace, hit, linecast, 2d",
    "Spawn": "instantiate, create, clone, prefab",
    "Destroy": "delete, remove, kill object",
    "GetVar": "variable, read, blackboard",
    "SetVar": "variable, write, store",
    "Add": "plus, sum, arithmetic",
    "Compare": "less, greater, equal, if",
    "Lerp": "blend, interpolate, mix",
    "Random": "rng, chance, dice",
    "LoadScene": "level, change scene",
    "PlaySound": "audio, sfx, sound effect",
    "AnimTrigger": "animation, animator, trigger",
}

RELATED = {
    "OnStart": "On Enable, On Update, Print",
    "OnUpdate": "On Late Update, On Fixed Update, Delta Time",
    "Branch": "Compare, Check, Gate, Select",
    "GetVar": "Set Var, Has Var, On Var Changed",
    "SetVar": "Get Var, Add Var, Toggle Var",
    "Add": "Sub, Mul, Add Var",
    "Translate": "Move By, Set Position, Axis X",
    "Raycast3D": "Sphere Cast, Raycast All 3D, Overlap Sphere",
    "Print": "Log Warning, Log Error, To Text",
    "Spawn": "Destroy, Find Object",
    "ForLoop": "Repeat N, While, For Each, Break",
    "CallFunction": "Function, Return, Set Local",
}

IN_ROLE = {
    "A": "The first value.",
    "B": "The second value.",
    "In": "The value to transform.",
    "T": "How far along the blend, from 0 to 1.",
    "Cond": "The condition. True and false take different paths.",
    "Value": "The value being stored, sent or tested.",
    "Amount": "How much to add. Negative numbers subtract.",
    "Target": "The object to act on. Leave it empty to use this object (the one the graph is on).",
    "Object": "An object reference, or its name.",
    "X": "The X part, in world units.",
    "Y": "The Y part, in world units.",
    "Z": "The Z part, in world units.",
    "Min": "The low end of the range.",
    "Max": "The high end of the range.",
    "Vec": "The vector to read or change.",
    "Text": "The text to read or change.",
    "List": "The list to read or change.",
    "Item": "One entry of the list.",
    "Index": "Which entry. The first is 0.",
    "Open": "When this is false the flow stops here.",
    "Reset": "True starts the count again at 0.",
    "Chance": "The odds from 0 to 1. 0.25 is 25 percent.",
    "N": "How many times between each pass.",
    "Speed": "How fast to move, in units per second.",
    "Weight": "How strongly to apply this, from 0 (off) to 1 (fully).",
    "Point": "A point in the world.",
    "Offset": "How far to move from the current position.",
    "Pos": "A position in the world.",
    "Scale": "The new size, or a multiplier.",
    "Deg": "An angle in degrees.",
    "On": "True turns it on, false turns it off.",
    "Visible": "True shows it, false hides it.",
    "Paused": "True pauses, false resumes.",
    "Count": "How many.",
    "First": "The first number of the loop, included.",
    "Last": "The last number of the loop, included.",
    "Origin": "Where the cast starts. Unwired, it starts at this object.",
    "Direction": "Which way the cast points. Unwired, it points along this object's forward.",
    "Max Dist": "How far the cast may travel.",
    "Radius": "The thickness of the ball, in world units.",
    "Center": "The middle of the volume. Unwired, it is this object's position.",
    "Half Size": "Half the box's width, height and depth.",
    "Ignore Triggers": "True skips trigger colliders.",
    "R": "Red, from 0 to 1.",
    "G": "Green, from 0 to 1.",
    "B": "Blue, from 0 to 1.",
    "A": "Opacity, from 0 (invisible) to 1 (solid).",
    "Default": "Used when nothing has been saved under this key yet.",
    "Player": "The player's peer id.",
    "Add": "How much to add before reading. 0 only reads.",
    "Ready": "True marks this player ready, false clears it.",
    "Data": "The payload sent to the other players.",
    "Score": "The score to submit.",
    "Fade (s)": "How long the fade takes, in seconds. 0 cuts immediately.",
    "Strength": "How hard, from 0 to 1.",
    "Degrees": "The field of view in degrees.",
    "Smoothing": "How quickly the camera catches up. Higher is snappier. 0 snaps.",
    "Object name": "The name of the object to follow.",
    "Intensity": "How strong the weather is, from 0 to 1.",
    "Start": "Where the effect begins, in world units from the camera.",
    "End": "Where the effect is complete, in world units from the camera.",
    "Edge0": "The start of the smooth range.",
    "Edge1": "The end of the smooth range.",
    "Length": "The length of one back-and-forth or wrap.",
    "Current": "The value to move from.",
    "Max Step": "The most the value may change.",
    "From": "The angle you are coming from, in degrees.",
    "To": "The angle you are going to, in degrees.",
    "Step": "The grid size to snap to.",
    "Blend": "How many seconds the transition takes.",
    "Heavy": "True plays the heavier reaction.",
    "Slot": "The equipment slot name, such as head or main hand.",
    "Times": "How many times to craft.",
    "Code": "A language code, such as en or fr.",
    "{0}": "The value that replaces {0} in the pattern.",
    "{1}": "The value that replaces {1} in the pattern.",
    "{2}": "The value that replaces {2} in the pattern.",
    "Find": "The text to look for.",
    "With": "The text to put in its place.",
    "Prefix": "The beginning to test.",
    "Suffix": "The ending to test.",
    "Part": "The piece of text to search for.",
    "Left": "How many did not fit.",
    "T (0-1)": "How far along the spline, from 0 (the start) to 1 (the end).",
    "Points": "A list of positions the line passes through.",
    "Origin X": "The X of the ray's start.",
    "Origin Y": "The Y of the ray's start.",
    "Dir X": "The X of the ray's direction.",
    "Dir Y": "The Y of the ray's direction.",
    "X1": "The X of the first point.",
    "Y1": "The Y of the first point.",
    "X2": "The X of the second point.",
    "Y2": "The Y of the second point.",
    "In Min": "The low end of the range you are converting from.",
    "In Max": "The high end of the range you are converting from.",
    "Out Min": "The low end of the range you are converting to.",
    "Out Max": "The high end of the range you are converting to.",
    "Net Id": "The network id of the object to remove.",
}

OUT_ROLE = {
    "Out": "The result.",
    "Value": "The value that was read.",
    "Bool": "True or false.",
    "Object": "The object that was found, or nothing.",
    "Vec3": "The vector result.",
    "Text": "The text result.",
    "Name": "The name.",
    "Tag": "The tag.",
    "Count": "How many.",
    "Index": "The current number, starting at 0.",
    "Item": "The current list entry.",
    "Hit": "True when something was hit.",
    "Point": "Where the hit happened.",
    "Normal": "The direction the surface faces at the hit.",
    "Distance": "How far away the hit is.",
    "Dist": "The distance.",
    "Length": "The length.",
    "List": "The new list.",
    "Objects": "The objects that were found, as a list.",
    "Nearest": "The closest object.",
    "Left": "How many did not fit, or how much budget is left.",
    "Old": "The value before it changed.",
    "State": "The state name.",
    "Prompt": "The words to show the player, or empty when there is nothing to show.",
    "Saved": "True when the save worked.",
    "Loaded": "True when the load worked.",
    "Removed": "True when the items were taken out.",
    "Equipped": "True when the item was equipped.",
    "Crafted": "True when the craft worked.",
    "Rolled": "How many items the roll produced.",
    "In Action": "True while a built-in action is playing.",
    "Sitting": "True while sitting or standing back up.",
    "Hanging": "True while hanging from a ledge.",
    "Down": "True while the button is held.",
    "Pressed": "True only on the frame it is pressed.",
    "Released": "True only on the frame it is released.",
    "t": "Seconds since the game started.",
    "dt": "Seconds since the last frame.",
    "X": "The X value.",
    "Y": "The Y value.",
    "Z": "The Z value.",
    "Pos": "The position.",
    "Scale": "The scale.",
    "Euler": "The rotation as X, Y, Z degrees.",
    "Rad": "The angle in radians.",
    "Deg": "The angle in degrees.",
    "Dot": "How much the two directions agree.",
    "Frame": "How many frames have been drawn.",
    "Delta": "How far the wheel moved this frame.",
    "Id": "The network id.",
    "Yes": "True when the test passes.",
    "Code": "The language code, such as en.",
    "Speed": "How hard the two objects hit.",
}

SETTING = {
    1: "The value typed into the node.",
    2: "The comparison: <, >, <=, >=, == or !=.",
    3: "The action step, chosen in the node.",
    4: "The list of action steps, edited in the node.",
    5: "The check, chosen in the node.",
    6: "Which function, chosen from the functions in this graph.",
}

def fallback(node, name, typ):
    if (node["type"], name) in FALLBACK:
        return FALLBACK[(node["type"], name)]
    if name == "Target":
        return "this object"
    return TYPE_DEFAULT.get(typ, "")

def pin_sentence(node, side, name, typ, desc):
    title = node["title"]
    if side == "ein":
        return f"The flow arrives here. {title} runs when a wire reaches this pin."
    if side == "eout":
        label = name if name else "the next step"
        if name in ("True", "False", "Body", "Done", "A", "B", "Default") or (name.isdigit()):
            return f"The flow continues out of {label}. Wire this to the step that should run for that case."
        return f"After {title} finishes, the flow continues from here to {label}."
    if side == "in":
        role = IN_ROLE.get(name, f"The {name} input of {title}.")
        fb = fallback(node, name, typ)
        extra = f" Unwired, this is {fb}." if fb else ""
        art = "an" if typ[:1] in "AEIOU" else "a"
        return f"{role} It is {art} {typ}.{extra}"
    role = OUT_ROLE.get(name, f"What {title} produces on {name}.")
    art = "an" if typ[:1] in "AEIOU" else "a"
    return f"{role} It is {art} {typ}. Wire it into an input that expects {art} {typ}."

def requirement(node):
    if node["type"] in REQ:
        return REQ[node["type"]]
    cat = node["cat"]
    if cat == "CAT_EVENT":
        return ("No component is required. It runs in Play mode when its event happens on this object.", "")
    if cat == "CAT_MATH" or cat == "CAT_LOGIC" or cat == "CAT_DATA":
        return ("No component is required. It only computes a value from its inputs.", "")
    if cat == "CAT_FLOW":
        return ("No component is required. It changes the order of the flow. It runs in Play mode.", "")
    if cat == "CAT_VAR":
        return ("No extra component is required. Object variables live on the Visual Script. It runs in Play mode.", "")
    if cat == "CAT_INPUT":
        return ("No component is required. It reads the player or the clock while the game is playing.", "")
    if cat == "CAT_XFORM":
        if node["execIn"]:
            return ("No extra component is required. It moves this object's Transform (or Target). It runs in Play mode.", "")
        return ("No extra component is required. It reads this object's Transform (or Target).", "")
    if cat == "CAT_NET":
        return ("No single component is required on this object. Online nodes talk to the session and only do something in Play mode while connected.", "")
    if cat == "CAT_ACTION":
        return ("No single component is required beyond this object's Visual Script, unless the description names one. It runs in Play mode.", "")
    return ("No extra component is required.", "")

def example(node):
    t = node["title"]
    if node["type"] in ("OnStart",):
        return "Wire On Start into Print, and type a sentence into Print, to log it when the scene loads."
    if node["type"] == "OnUpdate":
        return "Wire On Update into Translate, and Axis X times Delta Time into Translate's X, to slide left and right."
    if node["type"] == "Branch":
        return "Wire a Key Held into Cond. Wire the True pin into the step that should run while the key is down."
    if node["execIn"] and node["dataIn"]:
        first = node["dataIn"][0]
        return f"Put {t} after an event such as On Start or On Update, and wire a value into {first}."
    if node["execIn"]:
        return f"Put {t} on the flow after an event such as On Start. The chain runs when that event fires."
    if node["dataOut"]:
        return f"Wire {t}'s {node['dataOut'][0]} output into another node's input. It has no flow of its own."
    return f"Drop {t} into the graph and wire it the way its pins describe."

def related(node):
    if node["type"] in RELATED:
        return RELATED[node["type"]]
    return ""

def keywords(node):
    extra = KEYS.get(node["type"], "")
    words = node["title"].lower()
    return (words + (", " + extra if extra else "")).strip(", ")

def setting_text(node):
    if not node["param"]:
        return "", ""
    hint = node["hint"] or SETTING.get(node["param"], "The node's setting.")
    default = {
        "OnKey": "space", "OnKeyUp": "space", "Key": "space", "KeyDown": "space", "KeyUp": "space",
        "Timer": "1", "Wait": "1", "Delay": "1", "Cooldown": "1", "OnTimer": "1",
        "Compare": ">", "Chance": "0.5", "Const": "0", "GetVar": "x", "SetVar": "x",
        "Repeat": "1", "DoN": "1",
    }.get(node["type"], "")
    meaning = f"{hint}. " + SETTING.get(node["param"], "Typed into the node, not wired.")
    return meaning.strip(), default

def build_pins(node, rows, targets):
    exec_in_row, ins, outs = rows[node["type"]]
    letters_in = list(ins)
    letters_out = list(outs)
    data_in = list(node["dataIn"])
    data_out = list(node["dataOut"])
    tp = targets.get(node["type"], -1)
    if tp == len(data_in):
        data_in.append("Target")
        letters_in.append("O")
    if len(letters_in) != len(data_in) or len(letters_out) != len(data_out):
        raise SystemExit(f"pin mismatch {node['type']}: registry {data_in}/{data_out} types {letters_in}/{letters_out}")
    if exec_in_row != node["execIn"]:
        raise SystemExit(f"exec mismatch {node['type']}")
    pins = []
    if node["execIn"]:
        pins.append(("ein", "", "Flow", pin_sentence(node, "ein", "", "Flow", "")))
    for name in node["execOut"]:
        pins.append(("eout", name, "Flow", pin_sentence(node, "eout", name, "Flow", "")))
    for name, letter in zip(data_in, letters_in):
        typ = TYPE_NAME[letter]
        pins.append(("in", name, typ, pin_sentence(node, "in", name, typ, "")))
    for name, letter in zip(data_out, letters_out):
        typ = TYPE_NAME[letter]
        pins.append(("out", name, typ, pin_sentence(node, "out", name, typ, "")))
    return pins

def ren(text, node):
    """Use the renamed pin words inside a sentence written with the old ones."""
    for old, new in node.get("renames", {}).items():
        if len(old) >= 3 and not old.isdigit():
            text = re.sub(r"\b" + re.escape(old) + r"\b", new, text)
    return text

def emit(nodes, help_map, rows, targets):
    lines = []
    lines.append("// Generated by scripts/gen_vs_node_docs.py. Do not edit by hand.")
    lines.append("inline void VsAddHandDocs(std::unordered_map<std::string, VsNodeDoc>& m) {")
    lines.append("    auto add = [&](VsNodeDoc d) { m.emplace(d.type, std::move(d)); };")
    for node in nodes:
        desc = help_map.get(node["type"]) or EXTRA_DESC.get(node["type"])
        if not desc:
            raise SystemExit("no description for " + node["type"])
        desc = desc.replace('\\"', '"').replace("\\n", " ")
        if len(desc) < 24:
            desc = node["title"] + " — " + desc
            if not desc.endswith("."):
                desc += "."
        req, comp = requirement(node)
        set_mean, set_def = setting_text(node)
        pins = build_pins(node, rows, targets)
        rn = node.get("renames", {})
        pins = [(side, rn.get(name, name), typ, ren(meaning, node)) for side, name, typ, meaning in pins]
        desc = ren(desc, node)
        req = ren(req, node)
        set_mean = ren(set_mean, node)
        lines.append("    {")
        lines.append("        VsNodeDoc d;")
        lines.append(f"        d.type = {cpp(node['type'])};")
        lines.append(f"        d.title = {cpp(node['title'])};")
        lines.append(f"        d.description = {cpp(desc)};")
        lines.append(f"        d.requirements = {cpp(req)};")
        lines.append(f"        d.component = {cpp(comp)};")
        lines.append(f"        d.example = {cpp(ren(example(node), node))};")
        lines.append(f"        d.related = {cpp(related(node))};")
        lines.append(f"        d.keywords = {cpp(keywords(node))};")
        lines.append(f"        d.setting = {cpp(set_mean)};")
        lines.append(f"        d.settingDefault = {cpp(set_def)};")
        for side, name, typ, meaning in pins:
            fb = ""
            if side == "in":
                fb = fallback(node, {v: k for k, v in rn.items()}.get(name, name), typ)
            lines.append(f"        d.pins.push_back({{{'true' if side in ('ein','in') else 'false'}, {'true' if side in ('ein','eout') else 'false'}, {cpp(name)}, {cpp(typ)}, {cpp(meaning)}, {cpp(fb)}}});")
        lines.append("        add(std::move(d));")
        lines.append("    }")
    lines.append("}")
    lines.append("")
    out = ROOT / "editor/VisualScriptNodeDocs.inc"
    out.write_text("\n".join(lines))
    print(f"wrote {out} ({len(nodes)} nodes)")

def main():
    nodes = load_nodes()
    apply_friendly(nodes, load_friendly())
    emit(nodes, load_help(), load_rows(), load_targets())

if __name__ == "__main__":
    main()
