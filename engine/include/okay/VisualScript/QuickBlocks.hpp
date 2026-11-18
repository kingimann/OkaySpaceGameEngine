#pragma once
// Quick blocks: one-block ready-made actions for beginners ("Move With Keys", "Collect On Touch", ...).
// A quick block is ONE node in the editor and in the saved file (its type token plus the values typed on it).
// When a graph is loaded, ExpandQuickBlocks turns each block into the ordinary low-level nodes and wires it stands
// for, using the data table below and one generic expander. Nothing here runs by itself: the low-level nodes do the
// work, so a block can never behave differently from the nodes it is built from.
// Written for this project (no outside code). Block text language, one line each:
//   N a Type args   a low-level node (a..z are local names)         D a pin value   a typed value on a pin
//   X a pin b       a flow wire                                      W c inPin p outPin   a data wire
//   E entry a       register node a as an event entry               IN a            where incoming flow enters
//   OUT a pin       which flow pin of which node is the block's "Then do"
// Field placeholders: {0} the field as a word, {v0} as a number or quoted text, {q0} always quoted.
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace okay::vs {

/// What a field is, so the editor can draw the right widget: a number you can drag or type, text, a key to press, an
/// object to pick, a fixed list of choices, a colour swatch, or the presets list (a field that fills the others in).
enum class QbKind { Text, Number, Key, Object, Choice, Colour, Preset, Name };   // Name: a saved value's name (typed, or picked from the names already used)
struct QbField {
    const char* name;
    const char* def;
    QbKind kind = QbKind::Text;
    const char* choices = "";   // Choice and Preset: "Label~value|Label~value" (a Preset's value is its name)
};
struct QbPreset { const char* name; std::vector<const char*> values; };   // one value per field; "" leaves a field alone

struct QuickBlock {
    const char* type;          // the saved token
    const char* title;         // plain display name
    const char* group;         // Movement, Game rules, World, Feedback, Flow
    const char* description;   // one sentence
    const char* example;       // a tiny example
    bool flowIn;               // has a flow input (put it after an event); false: it starts on its own event
    bool thenOut;              // has a "Then do" flow output
    std::vector<QbField> fields;
    const char* body;
    std::vector<QbPreset> presets;   // ready-made settings ("Coin pickup"); empty: no Presets field
};


// ---- The Rule block: WHEN <event> [ONLY IF <condition>] DO <up to four actions>. Plain data, one generic runner.
// An entry's body is the same little language as a block body; {0} is the row's own value (typed beside the dropdown).
struct QbRuleEntry { const char* value; const char* label; const char* body; };
inline const std::vector<QbRuleEntry>& QbRuleEvents() {
    static const std::vector<QbRuleEntry> t = {
        {"start", "the game starts", "N a OnStart\nE OnStart a\nIN a\nOUT a 0\n"},
        {"update", "every frame", "N a OnUpdate\nE OnUpdate a\nIN a\nOUT a 0\n"},
        {"key", "a key is pressed (type the key)", "N a OnKey {0}\nE OnKey:{0} a\nIN a\nOUT a 0\n"},
        {"touched", "something touches this", "N a OnTrigger\nE OnTrigger a\nIN a\nOUT a 0\n"},
        {"click", "this is clicked", "N a OnClick\nE OnClick a\nIN a\nOUT a 0\n"},
        {"timer", "every few seconds (type the seconds)", "N a OnTimer {0}\nE OnTimer:{0} a\nIN a\nOUT a 0\n"},
        {"dead", "health reaches 0", "N a OnDeath\nE OnDeath a\nIN a\nOUT a 0\n"},
        {"score", "the score reaches (type the score)", "N a OnUpdate\nE OnUpdate a\nN s ScoreGet\nN c Compare >=\nW c 0 s 0\nD c 1 {v0}\nN b Branch\nW b 0 c 0\nX a 0 b\nIN a\nOUT b 0\n"},
    };
    return t;
}
inline const std::vector<QbRuleEntry>& QbRuleConditions() {
    static const std::vector<QbRuleEntry> t = {
        {"none", "always", ""},
        {"key", "a key is held (type the key)", "N k Key {0}\nN b Branch\nW b 0 k 0\nIN b\nOUT b 0\n"},
        {"score", "the score is at least (type the score)", "N s ScoreGet\nN c Compare >=\nW c 0 s 0\nD c 1 {v0}\nN b Branch\nW b 0 c 0\nIN b\nOUT b 0\n"},
        {"chance", "by chance (type 0 to 1)", "N k Chance {0}\nN b Branch\nW b 0 k 0\nIN b\nOUT b 0\n"},
    };
    return t;
}
inline const std::vector<QbRuleEntry>& QbRuleActions() {
    static const std::vector<QbRuleEntry> t = {
        {"none", "(nothing)", ""},
        {"score", "add to the score (type the points)", "N a ScoreAdd\nD a 0 {v0}\nIN a\nOUT a 0\n"},
        {"hurt", "take health away (type the amount)", "N a Damage\nD a 0 {v0}\nIN a\nOUT a 0\n"},
        {"heal", "give health back (type the amount)", "N a Heal\nD a 0 {v0}\nIN a\nOUT a 0\n"},
        {"text", "show text (type the words)", "N a SetText self\nD a 0 {q0}\nIN a\nOUT a 0\n"},
        {"say", "say text in the Console (type the words)", "N a Print\nD a 0 {q0}\nIN a\nOUT a 0\n"},
        {"sound", "play this object's sound", "N a PlaySound\nIN a\nOUT a 0\n"},
        {"spawn", "make a copy of a saved object (type its file)", "N a Spawn {0}\nIN a\nOUT a 0\n"},
        {"remove", "remove this object", "N a Destroy\nIN a\nOUT a 0\n"},
        {"hide", "hide this object", "N a SetActive self\nD a 0 0\nIN a\nOUT a 0\n"},
        {"show", "show this object", "N a SetActive self\nD a 0 1\nIN a\nOUT a 0\n"},
        {"scene", "go to another scene (type its name)", "N a LoadScene {0}\nIN a\n"},
        {"wait", "wait (type the seconds)", "N a Delay {0}\nIN a\nOUT a 0\n"},
        {"shake", "shake the camera (type the strength)", "N a CameraShake\nD a 0 {v0}\nIN a\nOUT a 0\n"},
        {"colour", "flash a colour (type #rrggbb)", "N a SetColor\nD a 0 {r0}\nD a 1 {g0}\nD a 2 {b0}\nD a 3 1\nIN a\nOUT a 0\n"},
        {"move", "move by (type right, up, forward)", "N a Translate\nD a 0 {vp0.0}\nD a 1 {vp0.1}\nD a 2 {vp0.2}\nIN a\nOUT a 0\n"},
        {"turn", "turn by degrees (type the degrees)", "N a Rotate\nD a 0 {v0}\nIN a\nOUT a 0\n"},
        {"setvalue", "set a saved number (type name, value)", "N a SetVar {p0.0}\nD a 0 {vp0.1}\nIN a\nOUT a 0\n"},
        {"checkpoint", "save a checkpoint", "N a CheckpointSave\nIN a\nOUT a 0\n"},
        {"win", "win the game", "N a Win\nIN a\n"},
        {"lose", "lose the game", "N a Lose\nIN a\n"},
    };
    return t;
}
/// "Label~value|Label~value" for a dropdown (0 events, 1 conditions, 2 actions).
inline const char* QbRuleChoices(int which) {
    static const std::string c[3] = {
        [] { std::string r; for (const auto& e : QbRuleEvents()) r += (r.empty() ? "" : "|") + std::string(e.label) + "~" + e.value; return r; }(),
        [] { std::string r; for (const auto& e : QbRuleConditions()) r += (r.empty() ? "" : "|") + std::string(e.label) + "~" + e.value; return r; }(),
        [] { std::string r; for (const auto& e : QbRuleActions()) r += (r.empty() ? "" : "|") + std::string(e.label) + "~" + e.value; return r; }()};
    return c[which].c_str();
}

inline const std::vector<QuickBlock>& QuickBlocks() {
    static const std::vector<QuickBlock> all = {
        // ---- Movement
        {"QbMoveWithKeys", "Move With Keys", "Movement",
         "Slides this object while you hold the keys, all the time.", "Hold the right arrow to go right.", false, false,
         {{"Keys", "arrows", QbKind::Choice, "arrow keys~arrows|W A S D~wasd|both~both"}, {"Speed", "5", QbKind::Number}},
         "N u OnUpdate\nE OnUpdate u\n"
         "N ra Key {kr1:0}\nN rb Key {kr2:0}\nN ro Or\nW ro 0 ra 0\nW ro 1 rb 0\n"
         "N la Key {kl1:0}\nN lb Key {kl2:0}\nN lo Or\nW lo 0 la 0\nW lo 1 lb 0\n"
         "N c Sub\nW c 0 ro 0\nW c 1 lo 0\nN d Mul\nD d 1 {v1}\nW d 0 c 0\nN e DeltaTime\nN f Mul\nW f 0 d 0\nW f 1 e 0\n"
         "N ua Key {ku1:0}\nN ub Key {ku2:0}\nN uo Or\nW uo 0 ua 0\nW uo 1 ub 0\n"
         "N da Key {kd1:0}\nN db Key {kd2:0}\nN do Or\nW do 0 da 0\nW do 1 db 0\n"
         "N i Sub\nW i 0 uo 0\nW i 1 do 0\nN j Mul\nD j 1 {v1}\nW j 0 i 0\nN k Mul\nW k 0 j 0\nW k 1 e 0\n"
         "N t Translate\nW t 0 f 0\nW t 1 k 0\nD t 2 0\nX u 0 t\nIN u\n"},
        {"QbMoveToward", "Move To Spot", "Movement",
         "Walks this object toward a spot (right, up, forward), all the time.", "Walk to the spot 5 steps right.", false, false,
         {{"Spot", "5,0,0"}, {"Speed", "2", QbKind::Number}},
         "N u OnUpdate\nE OnUpdate u\nN a GlMoveToPosition\nD a 0 {vp0.0}\nD a 1 {vp0.1}\nD a 2 {vp0.2}\nD a 3 {v1}\nX u 0 a\nIN u\n"},
        {"QbFollowObject", "Follow Object", "Movement",
         "Walks toward the object with this name, all the time.", "A ghost follows the object named Player.", false, false,
         {{"Who", "Player", QbKind::Object}, {"Speed", "3", QbKind::Number}},
         "N u OnUpdate\nE OnUpdate u\nN a MoveTowards {0}\nD a 0 {v1}\nX u 0 a\nIN u\n", {{"Enemy chases", {"Player", "3"}}, {"Slow ghost", {"Player", "1"}}}},
        {"QbPatrol", "Walk Back And Forth", "Movement",
         "Swings this object left and right of where it started, all the time.", "A guard walks 3 steps each way, or a platform slides.", false, false,
         {{"Distance", "3", QbKind::Number}, {"Speed", "1.5", QbKind::Number}},
         "N a OnUpdate\nE OnUpdate a\nN b GlMoveBackAndForth\nD b 0 {v0}\nD b 1 {v1}\nX a 0 b\nIN a\n", {{"Guard walks", {"3", "1.5"}}, {"Moving platform", {"4", "1"}}}},
        {"QbJumpOnKey", "Jump On Key", "Movement",
         "Jumps up when you press the key.", "Press Space to jump.", false, true,
         {{"Key", "space", QbKind::Key}, {"Strength", "7", QbKind::Number}},
         "N a OnKey {0}\nE OnKey:{0} a\nN b AddImpulse\nD b 0 0\nD b 1 {v1}\nD b 2 0\nX a 0 b\nIN a\nOUT b 0\n", {{"Jump pad", {"space", "14"}}, {"Small hop", {"space", "4"}}}},
        {"QbTurnToFace", "Turn To Face", "Movement",
         "Turns this object to look at the object with this name.", "A turret turns to face the Player.", true, true,
         {{"Who", "Player", QbKind::Object}},
         "N a LookAt {0}\nIN a\nOUT a 0\n"},
        // ---- Game rules
        {"QbAddToScore", "Add To Score", "Game rules",
         "Adds points to the score.", "Add 1 point for each coin.", true, true,
         {{"Points", "1", QbKind::Number}},
         "N a ScoreAdd\nD a 0 {v0}\nIN a\nOUT a 0\n"},
        {"QbShowScore", "Show Score", "Game rules",
         "Writes the score on this object's text, all the time.", "Show Score: 3 on the screen.", false, false,
         {{"Label", "Score: "}},
         "N a OnUpdate\nE OnUpdate a\nN s ScoreGet\nN x GlShowNumberInText\nD x 0 {q0}\nW x 1 s 0\nD x 2 0\nX a 0 x\nIN a\n"},
        {"QbLoseHealth", "Lose Health", "Game rules",
         "Takes health away from this object.", "A spike takes 10 health.", true, true,
         {{"Amount", "10", QbKind::Number}},
         "N a Damage\nD a 0 {v0}\nIN a\nOUT a 0\n", {{"Lava hurts", {"10"}}, {"Spikes hurt", {"25"}}}},
        {"QbGameOverAtZeroHealth", "Game Over When Health Is 0", "Game rules",
         "Ends the game and says You lose when this object has no health left.", "The hero dies and the game is over.", false, false,
         {},
         "N a OnDeath\nE OnDeath a\nN b Lose\nX a 0 b\nIN a\n"},
        {"QbGameOverOnTouch", "Game Over When Touched", "Game rules",
         "Ends the game and says You lose when something touches this object.", "Touch the enemy and the game is over.", false, false,
         {},
         "N a OnTrigger\nE OnTrigger a\nN b Lose\nX a 0 b\nIN a\n"},
        {"QbWinAtScore", "Win When Score Reaches", "Game rules",
         "Ends the game and says You win when the score gets this high.", "Win at 5 coins.", false, false,
         {{"Score", "5", QbKind::Number}},
         "N a OnUpdate\nE OnUpdate a\nN s ScoreGet\nN c Compare >=\nW c 0 s 0\nD c 1 {v0}\nN b Branch\nW b 0 c 0\nX a 0 b\n"
         "N w Win\nX b 0 w\nIN a\n"},
        {"QbEverySeconds", "Do Every Few Seconds", "Game rules",
         "Does the next thing again and again, after this many seconds.", "Do something every 2 seconds.", false, true,
         {{"Seconds", "2", QbKind::Number}},
         "N a OnTimer {0}\nE OnTimer:{0} a\nIN a\nOUT a 0\n"},
        {"QbRespawnAtCheckpoint", "Go Back To Checkpoint", "Game rules",
         "Puts this object back at the last saved checkpoint.", "After falling, return to the flag.", true, true,
         {},
         "N a CheckpointLoad\nIN a\nOUT a 0\n"},
        // ---- Things in the world
        {"QbCollectOnTouch", "Collect Coin On Touch", "World",
         "When touched, like a coin: adds points, plays this object's sound and hides it.", "A coin that gives 1 point.", false, false,
         {{"Points", "1", QbKind::Number}},
         "N a OnTrigger\nE OnTrigger a\nN b ScoreAdd\nD b 0 {v0}\nX a 0 b\nN p PlaySound\nX b 0 p\nN c SetActive self\nD c 0 0\nX p 0 c\nIN a\n", {{"Coin pickup", {"1"}}, {"Big gem", {"10"}}}},
        {"QbOpenDoorWhenTouched", "Open Door When Touched", "World",
         "When touched, this object slides up out of the way.", "A door that opens when you walk into it.", false, false,
         {{"Lift", "2", QbKind::Number}},
         "N a OnTrigger\nE OnTrigger a\nN b Translate\nD b 0 0\nD b 1 {v0}\nD b 2 0\nX a 0 b\nIN a\n", {{"Door opens", {"3"}}, {"Small hatch", {"1"}}}},
        {"QbSpinningObject", "Spinning Object", "World",
         "Turns this object around, all the time.", "A fan that spins.", false, false,
         {{"Speed", "90", QbKind::Number}},
         "N a OnUpdate\nE OnUpdate a\nN b Spin\nD b 0 {v0}\nX a 0 b\nIN a\n"},
        {"QbShootOnKey", "Shoot On Key", "World",
         "Fires a copy of a saved object forward when you press the key.", "Press Space to fire a bullet.", false, true,
         {{"Key", "space", QbKind::Key}, {"What", "Assets/bullet.okayprefab"}, {"Speed", "10", QbKind::Number}},
         "N a OnKey {0}\nE OnKey:{0} a\nN b SpawnProjectile {1}\nD b 0 {v2}\nX a 0 b\nIN a\nOUT b 0\n"},
        {"QbSpawnEvery", "Spawn Copy Every Few Seconds", "World",
         "Makes a new copy of a saved object again and again.", "A cannon that makes a ball every 2 seconds.", false, false,
         {{"What", "Assets/ball.okayprefab"}, {"Seconds", "2", QbKind::Number}},
         "N a OnUpdate\nE OnUpdate a\nN b SpawnEvery {0}\nD b 0 {v1}\nX a 0 b\nIN a\n"},
        {"QbSpawnCopyAt", "Spawn Copy At", "World",
         "Makes a copy of a saved object at a spot (right, up, forward).", "Drop a coin 2 steps up.", true, true,
         {{"What", "Assets/coin.okayprefab"}, {"Spot", "0,0,0"}},
         "N a Spawn {0}\nD a 0 {vp1.0}\nD a 1 {vp1.1}\nD a 2 {vp1.2}\nIN a\nOUT a 0\n"},
        {"QbRemoveAfter", "Remove After A While", "World",
         "Takes this object away after some seconds.", "A bullet disappears after 3 seconds.", false, false,
         {{"Seconds", "3", QbKind::Number}},
         "N a OnStart\nE OnStart a\nN b DestroyAfter\nD b 0 {v0}\nX a 0 b\nIN a\n"},
        // ---- Feedback
        {"QbShowText", "Show Text On Screen", "Feedback",
         "Shows a message on this object's text for a few seconds.", "Show Hello! for 2 seconds.", true, true,
         {{"Text", "Hello!"}, {"Seconds", "2", QbKind::Number}},
         "N a SetText self\nD a 0 {q0}\nN w Delay {1}\nX a 0 w\nN c SetText self\nD c 0 \"\"\nX w 0 c\nIN a\nOUT a 0\n"},
        {"QbPlaySoundOnKey", "Play Sound On Key", "Feedback",
         "Plays this object's sound when you press the key.", "Press Space to hear a beep.", false, true,
         {{"Key", "space", QbKind::Key}},
         "N a OnKey {0}\nE OnKey:{0} a\nN b PlaySound\nX a 0 b\nIN a\nOUT b 0\n"},
        {"QbFlashColour", "Flash Colour", "Feedback",
         "Turns this object a colour for a moment, then back to white.", "A hit flashes red.", true, true,
         {{"Colour", "#ff0000", QbKind::Colour}, {"Seconds", "0.2", QbKind::Number}},
         "N a SetColor\nD a 0 {r0}\nD a 1 {g0}\nD a 2 {b0}\nD a 3 1\nN w Delay {1}\nX a 0 w\nN c SetColor\nD c 0 1\nD c 1 1\nD c 2 1\nD c 3 1\nX w 0 c\nIN a\nOUT a 0\n"},
        {"QbFollowWithCamera", "Follow With Camera", "Feedback",
         "Makes the camera follow the object with this name.", "The camera follows the Player.", true, true,
         {{"Who", "Player", QbKind::Object}, {"Smoothness", "5", QbKind::Number}},
         "N f FindObject {0}\nN a SetCameraTarget\nW a 0 f 0\nD a 1 {v1}\nIN a\nOUT a 0\n"},
        // ---- Flow
        {"QbWaitThenDo", "Wait Then Do", "Flow",
         "Waits, then does the next thing.", "Wait 2 seconds, then open the door.", true, true,
         {{"Seconds", "1", QbKind::Number}},
         "N a Delay {0}\nIN a\nOUT a 0\n"},
        {"QbRepeat", "Repeat Times", "Flow",
         "Does the next thing again and again, this many times.", "Spawn 5 coins.", true, true,
         {{"Times", "3", QbKind::Number}},
         "N a Repeat {0}\nIN a\nOUT a 0\n"},
        {"QbDoForever", "Do Forever", "Flow",
         "Does the next thing on every frame, all the time.", "Spin all the time.", false, true,
         {},
         "N a OnUpdate\nE OnUpdate a\nIN a\nOUT a 0\n"},
        {"QbIfKeyThenDo", "If Key Pressed Do", "Flow",
         "Does the next thing only while the key is held down.", "While Shift is held, run.", true, true,
         {{"Key", "space", QbKind::Key}},
         "N k Key {0}\nN b Branch\nW b 0 k 0\nIN b\nOUT b 0\n"},
        {"QbWhenTouchedDo", "When Touched Do", "Flow",
         "Does the next thing when something touches this object.", "When touched, add a point.", false, true,
         {},
         "N a OnTrigger\nE OnTrigger a\nIN a\nOUT a 0\n"},
        // ---- Merged blocks: one block with a dropdown instead of several nodes
        {"QbChangeNumber", "Change Number", "Game rules",
         "Changes a saved number: add, subtract, multiply or divide.", "Take 1 off the number named lives.", true, true,
         {{"Name", "points", QbKind::Name}, {"Change", "Add", QbKind::Choice, "add~Add|subtract~Sub|multiply~Mul|divide~Div"}, {"Amount", "1", QbKind::Number}},
         "N g {G0} {n0}\nN o {1}\nW o 0 g 0\nD o 1 {v2}\nN s {S0} {n0}\nW s 0 o 0\nIN s\nOUT s 0\n", {{"Add to score", {"score", "Add", "1"}}, {"Take away 1 life", {"lives", "Sub", "1"}}, {"Add 1 coin", {"coins", "Add", "1"}}, {"Count down 1", {"time", "Sub", "1"}}}},
        {"QbIfNumberIs", "If Number Is", "Flow",
         "Does the next thing only when a saved number matches.", "If lives is 0, then end the game.", true, true,
         {{"Name", "points", QbKind::Name}, {"Is", ">=", QbKind::Choice, "equal to~==|not equal to~!=|less than~<|more than~>|at least~>=|at most~<="}, {"Number", "5", QbKind::Number}},
         "N g {G0} {n0}\nN c Compare {1}\nW c 0 g 0\nD c 1 {v2}\nN b Branch\nW b 0 c 0\nIN b\nOUT b 0\n"},
        {"QbPlayOrStopSound", "Play Or Stop Sound", "Feedback",
         "Starts or stops this object's sound.", "Stop the music when the game is over.", true, true,
         {{"Do", "PlaySound", QbKind::Choice, "play~PlaySound|stop~StopSound"}},
         "N a {0}\nIN a\nOUT a 0\n"},
        // ---- The Rule block: WHEN ... ONLY IF ... DO ... (up to four actions), no wires.
        {"QbRule", "Rule", "Flow",
         "Reads like a sentence: when this happens, only if that is true, do these things.", "When touched, add a point, play a sound and hide.", false, false,
         {{"When", "touched", QbKind::Choice, QbRuleChoices(0)}, {"When value", ""},
          {"Only if", "none", QbKind::Choice, QbRuleChoices(1)}, {"If value", ""},
          {"Do 1", "score", QbKind::Choice, QbRuleChoices(2)}, {"Value 1", "1"},
          {"Do 2", "sound", QbKind::Choice, QbRuleChoices(2)}, {"Value 2", ""},
          {"Do 3", "hide", QbKind::Choice, QbRuleChoices(2)}, {"Value 3", ""},
          {"Do 4", "none", QbKind::Choice, QbRuleChoices(2)}, {"Value 4", ""}},
         "",
         {{"Coin pickup", {"touched", "", "none", "", "score", "1", "sound", "", "hide", "", "none", ""}},
          {"Door opens", {"touched", "", "none", "", "move", "0,2,0", "none", "", "none", "", "none", ""}},
          {"Lava hurts", {"touched", "", "none", "", "hurt", "10", "none", "", "none", "", "none", ""}},
          {"Checkpoint", {"touched", "", "none", "", "checkpoint", "", "none", "", "none", "", "none", ""}},
          {"Jump pad", {"touched", "", "none", "", "move", "0,3,0", "sound", "", "none", "", "none", ""}},
          {"Game over", {"dead", "", "none", "", "lose", "", "none", "", "none", "", "none", ""}}}},
        // ---- A bigger wave: more everyday blocks, each one job, typed fields only
        {"QbWhenKeyDo", "When Key Pressed Do", "Flow",
         "Does the next thing when you press the key.", "When you press E, open the chest.", false, true,
         {{"Key", "space", QbKind::Key}},
         "N a OnKey {0}\nE OnKey:{0} a\nIN a\nOUT a 0\n"},
        {"QbWhenKeyReleasedDo", "When Key Let Go Do", "Flow",
         "Does the next thing when you let go of the key.", "When you let go of Space, stop the engine.", false, true,
         {{"Key", "space", QbKind::Key}},
         "N a OnKeyUp {0}\nE OnKeyUp:{0} a\nIN a\nOUT a 0\n"},
        {"QbWhenClickedDo", "When Clicked Do", "Flow",
         "Does the next thing when you click this object.", "When you click the button, play a sound.", false, true,
         {},
         "N a OnClick\nE OnClick a\nIN a\nOUT a 0\n"},
        {"QbWhenBumpedDo", "When Bumped Do", "Flow",
         "Does the next thing when this object bumps into something.", "When the ball bumps the wall, play a sound.", false, true,
         {},
         "N a OnCollision\nE OnCollision a\nIN a\nOUT a 0\n"},
        {"QbWhenStartDo", "When The Game Starts Do", "Flow",
         "Does the next thing once, when the game starts.", "When the game starts, say hello.", false, true,
         {},
         "N a OnStart\nE OnStart a\nIN a\nOUT a 0\n"},
        {"QbRunAwayFrom", "Run Away From", "Movement",
         "Walks away from the object with this name, all the time.", "A rabbit runs away from the Player.", false, false,
         {{"Who", "Player", QbKind::Object}, {"Speed", "3", QbKind::Number}},
         "N u OnUpdate\nE OnUpdate u\nN f FindObject {0}\nN a Flee\nW a 0 f 0\nD a 1 {v1}\nX u 0 a\nIN u\n"},
        {"QbGoRound", "Go Round In A Circle", "Movement",
         "Moves this object round in a circle, all the time.", "A planet circles the sun.", false, false,
         {{"Radius", "2", QbKind::Number}, {"Speed", "1.5", QbKind::Number}},
         "N u OnUpdate\nE OnUpdate u\nN a GlMoveInCircle\nD a 0 {v0}\nD a 1 {v1}\nX u 0 a\nIN u\n"},
        {"QbFloatUpDown", "Bob Up And Down", "Movement",
         "Bobs this object up and down, all the time.", "A coin bobs above the ground.", false, false,
         {{"Height", "0.5", QbKind::Number}, {"Speed", "2", QbKind::Number}},
         "N u OnUpdate\nE OnUpdate u\nN a Bob\nD a 0 {v0}\nD a 1 {v1}\nX u 0 a\nIN u\n"},
        {"QbTeleportTo", "Teleport To Spot", "Movement",
         "Puts this object at a spot (right, up, forward) straight away.", "Teleport the player back to the start.", true, true,
         {{"Spot", "0,0,0"}},
         "N a Teleport self\nD a 0 {vp0.0}\nD a 1 {vp0.1}\nD a 2 {vp0.2}\nIN a\nOUT a 0\n"},
        {"QbLaunch", "Launch This Object", "Movement",
         "Gives this object a sudden push (right, up, forward).", "A spring flings the player up.", true, true,
         {{"Push", "0,8,0"}},
         "N a AddImpulse\nD a 0 {vp0.0}\nD a 1 {vp0.1}\nD a 2 {vp0.2}\nIN a\nOUT a 0\n"},
        {"QbCountdownThenDo", "Count Down Then Do", "Flow",
         "Counts down, then does the next thing.", "Count down 3 seconds, then start the race.", true, true,
         {{"Seconds", "3", QbKind::Number}},
         "N a Countdown {0}\nIN a\nOUT a 0\n"},
        {"QbOnlyEvery", "Only Every Few Seconds", "Flow",
         "Lets the next thing happen at most once every few seconds.", "The gun can fire once every second.", true, true,
         {{"Seconds", "1", QbKind::Number}},
         "N a Cooldown {0}\nIN a\nOUT a 0\n"},
        {"QbDoOnce", "Do Only Once", "Flow",
         "Lets the next thing happen only the first time.", "Show the tip only once.", true, true,
         {},
         "N a Once\nIN a\nOUT a 0\n"},
        {"QbDoByChance", "Do By Chance", "Flow",
         "Does the next thing only some of the time.", "A chest drops a gem half the time.", true, true,
         {{"Chance (0 to 1)", "0.5", QbKind::Number}},
         "N k Chance {0}\nN b Branch\nW b 0 k 0\nIN b\nOUT b 0\n"},
        {"QbToggleSwitch", "Switch On Or Off", "Flow",
         "Flips a switch: on becomes off and off becomes on.", "Press a button to switch the light.", true, true,
         {{"Name", "switch", QbKind::Name}},
         "N a Toggle {n0}\nIN a\nOUT a 0\n"},
        {"QbIfSwitchOn", "If Switch Is On", "Flow",
         "Does the next thing only when the switch is on.", "If the lever is on, open the gate.", true, true,
         {{"Name", "switch", QbKind::Name}},
         "N g {G0} {n0}\nN b Branch\nW b 0 g 0\nIN b\nOUT b 0\n"},
        {"QbSaveNumber", "Save A Number", "Game rules",
         "Keeps a saved number so it is still there next time you play.", "Save the best score.", true, true,
         {{"Name", "score", QbKind::Name}, {"Save as", "best"}},
         "N g {G0} {n0}\nN a SaveValue {1}\nW a 0 g 0\nIN a\nOUT a 0\n"},
        {"QbLoadNumber", "Load A Saved Number", "Game rules",
         "Reads a number you saved before back into a saved value.", "Load the best score when the game starts.", true, true,
         {{"Name", "score", QbKind::Name}, {"Saved as", "best"}},
         "N l LoadValue {1}\nD l 0 0\nN a {S0} {n0}\nW a 0 l 0\nIN a\nOUT a 0\n"},
        {"QbGiveItem", "Give Item", "Game rules",
         "Puts items in the bag.", "Give the player 2 keys.", true, true,
         {{"Item", "key"}, {"How many", "1", QbKind::Number}},
         "N a AddItem {0}\nD a 0 {v1}\nIN a\nOUT a 0\n"},
        {"QbIfHasItem", "If Has Item", "Game rules",
         "Does the next thing only if the bag has enough of the item.", "If the player has a key, open the door.", true, true,
         {{"Item", "key"}, {"At least", "1", QbKind::Number}},
         "N h HasItem {0}\nD h 0 {v1}\nN b Branch\nW b 0 h 0\nIN b\nOUT b 0\n"},
        {"QbRestartGame", "Restart The Game", "Game rules",
         "Starts the scene again from the beginning.", "Press R to try again.", true, false,
         {},
         "N a RestartScene\nIN a\n"},
        {"QbHealFully", "Heal Fully", "Game rules",
         "Gives all the health back.", "A potion fills the health.", true, true,
         {},
         "N a GlHealToFull\nIN a\nOUT a 0\n"},
        {"QbIfHealthLow", "If Health Is Low", "Game rules",
         "Does the next thing only when health is low.", "When health is low, play a warning.", true, true,
         {{"Share (0 to 1)", "0.25", QbKind::Number}},
         "N h GlIsHealthLow\nD h 0 {v0}\nN b Branch\nW b 0 h 0\nIN b\nOUT b 0\n"},
        {"QbDefeatThis", "Defeat This Object", "Game rules",
         "Takes all the health away.", "A trap defeats whatever falls in.", true, true,
         {},
         "N a GlKill\nIN a\nOUT a 0\n"},
        {"QbRespawnThis", "Respawn This Object", "Game rules",
         "Brings this object back to life.", "The hero comes back after falling.", true, true,
         {},
         "N a Respawn\nIN a\nOUT a 0\n"},
        {"QbHurtOnTouch", "Hurt Whatever Touches", "Game rules",
         "When something touches this, it loses health.", "Lava that hurts the player.", false, false,
         {{"Amount", "10", QbKind::Number}},
         "N a OnTrigger\nE OnTrigger a\nN o Other\nN d Damage\nD d 0 {v0}\nW d 1 o 0\nX a 0 d\nIN a\n", {{"Lava hurts", {"10"}}, {"Sharp spikes", {"25"}}}},
        {"QbHealOnTouch", "Heal Whatever Touches", "Game rules",
         "When something touches this, it gets health back.", "A heart that heals the player.", false, false,
         {{"Amount", "25", QbKind::Number}},
         "N a OnTrigger\nE OnTrigger a\nN o Other\nN d Heal\nD d 0 {v0}\nW d 1 o 0\nX a 0 d\nIN a\n"},
        {"QbRemoveOnTouch", "Remove When Touched", "World",
         "Takes this object out of the game when something touches it.", "A balloon that pops.", false, false,
         {},
         "N a OnTrigger\nE OnTrigger a\nN b Destroy\nX a 0 b\nIN a\n"},
        {"QbRandomSpot", "Jump To A Random Spot", "World",
         "Puts this object at a random spot left or right and forward or back.", "A coin hops to a new place.", true, true,
         {{"Smallest", "-5", QbKind::Number}, {"Largest", "5", QbKind::Number}},
         "N r1 RandomRange\nD r1 0 {v0}\nD r1 1 {v1}\nN r2 RandomRange\nD r2 0 {v0}\nD r2 1 {v1}\nN p SetPosition\nW p 0 r1 0\nD p 1 0\nW p 2 r2 0\nIN p\nOUT p 0\n"},
        {"QbSayWhenTouched", "Show Text When Touched", "Feedback",
         "Shows a message on this object's text when something touches it.", "A sign that says Welcome.", false, false,
         {{"Text", "Welcome!"}},
         "N a OnTrigger\nE OnTrigger a\nN b SetText self\nD b 0 {q0}\nX a 0 b\nIN a\n"},
        {"QbShowValue", "Show Value On Screen", "Game rules",
         "Writes a saved value on this object's text, all the time.", "Show Score: 3 on the screen.", false, false,
         {{"Name", "score", QbKind::Name}, {"Label", "Score: "}},
         "N a OnUpdate\nE OnUpdate a\nN s {G0} {n0}\nN x GlShowNumberInText\nD x 0 {q1}\nW x 1 s 0\nD x 2 0\nX a 0 x\nIN a\n"},
        {"QbCountEverySecond", "Count Every Second", "Game rules",
         "Counts a saved value up or down once every second.", "A clock that counts up.", false, false,
         {{"Name", "time", QbKind::Name}, {"Count", "Add", QbKind::Choice, "up~Add|down~Sub"}},
         "N a OnTimer 1\nE OnTimer:1 a\nN g {G0} {n0}\nN o {1}\nW o 0 g 0\nD o 1 1\nN s {S0} {n0}\nW s 0 o 0\nX a 0 s\nIN a\n"},
        {"QbSetValue", "Set Value To", "Game rules",
         "Sets a saved value to a number, a word or yes or no.", "Set health to 100.", true, true,
         {{"Name", "health", QbKind::Name}, {"Value", "100"}},
         "N a {S0} {n0}\nD a 0 {v1}\nIN a\nOUT a 0\n",
         {{"Set health to 100", {"health", "100"}}, {"Set lives to 3", {"lives", "3"}}, {"Set score to 0", {"score", "0"}}, {"Set time to 60", {"time", "60"}}}},
        {"QbMoveTurnResize", "Move Turn Or Resize", "Movement",
         "Moves, turns or resizes this object by an amount (right, up, forward). For Turn, the first number is the degrees.", "Move up 2, or turn 90 degrees.", true, true,
         {{"Do", "Translate", QbKind::Choice, "move~Translate|turn~Rotate|resize~SetScale"}, {"Amount", "0,1,0"}},
         "N a {0}\nD a 0 {vp1.0}\n!Rotate D a 1 {vp1.1}\n!Rotate D a 2 {vp1.2}\nIN a\nOUT a 0\n"},
        {"QbShowHide", "Show Or Hide This", "World",
         "Shows or hides this object.", "Hide the coin after it is picked up.", true, true,
         {{"Do", "1", QbKind::Choice, "show~1|hide~0"}},
         "N a SetActive self\nD a 0 {0}\nIN a\nOUT a 0\n"},
    };
    return all;
}

inline const QuickBlock* FindQuickBlock(const std::string& type) {
    static const std::unordered_map<std::string, const QuickBlock*> idx = [] {
        std::unordered_map<std::string, const QuickBlock*> m;
        for (const QuickBlock& b : QuickBlocks()) m[b.type] = &b;
        return m;
    }();
    auto it = idx.find(type);
    return it == idx.end() ? nullptr : it->second;
}

namespace qb_detail {
inline std::string Trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}
inline std::string Unquote(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        std::string out;
        for (std::size_t i = 1; i + 1 < s.size(); ++i) {
            if (s[i] == '\\' && i + 2 < s.size()) ++i;
            out += s[i];
        }
        return out;
    }
    return s;
}
inline std::string Quote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) { if (c == '"' || c == '\\') out += '\\'; out += c; }
    return out + "\"";
}
inline bool IsNumberOrBool(const std::string& v) {
    if (v == "true" || v == "false") return true;
    if (v.empty()) return false;
    char* end = nullptr;
    std::strtod(v.c_str(), &end);
    return end && *end == '\0';
}
inline std::string Literal(const std::string& v) { return IsNumberOrBool(v) ? v : Quote(v); }
inline std::string Word(const std::string& v) {
    bool plain = !v.empty();
    for (char c : v) if (std::isspace((unsigned char)c) || c == '"' || c == '#') plain = false;
    return plain ? v : Quote(v);
}
// Splits off the first `n` space-separated words; `rest` gets the text after them.
inline std::vector<std::string> Words(const std::string& line, std::size_t n, std::string* rest) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (out.size() < n) {
        while (i < line.size() && line[i] == ' ') ++i;
        if (i >= line.size()) break;
        std::size_t j = i;
        while (j < line.size() && line[j] != ' ') ++j;
        out.push_back(line.substr(i, j - i));
        i = j;
    }
    while (i < line.size() && line[i] == ' ') ++i;
    if (rest) *rest = i < line.size() ? line.substr(i) : std::string();
    return out;
}
inline std::string Part(const std::string& v, int index) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : v) { if (c == ',') { parts.push_back(Trim(cur)); cur.clear(); } else cur += c; }
    parts.push_back(Trim(cur));
    return index >= 0 && index < (int)parts.size() ? parts[(std::size_t)index] : std::string();
}
/// "#rrggbb" -> one colour component (0 red, 1 green, 2 blue) as a number text; anything else is 1 (white).
inline std::string ColourPart(const std::string& v, int which) {
    if (v.size() != 7 || v[0] != '#') return "1";
    for (std::size_t i = 1; i < 7; ++i) if (!std::isxdigit((unsigned char)v[i])) return "1";
    const int n = (int)std::strtol(v.substr(1 + (std::size_t)which * 2, 2).c_str(), nullptr, 16);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%.3f", n / 255.0);
    return buf;
}
// Placeholders: {0} word, {v0} number-or-quoted, {q0} quoted, {r0}/{g0}/{b0} colour parts, {p0.1} part of a comma list,
// {vp0.1} the same as a number or quoted text.
/// The key for a direction (l r u d) from the Keys dropdown: arrows, W A S D, or both (the second key is the other set).
inline std::string MoveKey(const std::string& choice, char dir, bool second) {
    const bool arrows = choice != "wasd";
    const bool both = choice == "both";
    const bool useArrows = both ? !second : arrows;
    switch (dir) {
        case 'l': return useArrows ? "left" : "a";
        case 'r': return useArrows ? "right" : "d";
        case 'u': return useArrows ? "up" : "w";
        default:  return useArrows ? "down" : "s";
    }
}
inline std::string Fill(const std::string& tmpl, const std::vector<std::string>& f) {
    std::string out;
    for (std::size_t i = 0; i < tmpl.size(); ++i) {
        if (tmpl[i] != '{') { out += tmpl[i]; continue; }
        const std::size_t e = tmpl.find('}', i);
        if (e == std::string::npos) { out += tmpl[i]; continue; }
        std::string key = tmpl.substr(i + 1, e - i - 1);
        if (key.size() > 4 && key[0] == 'k' && key[3] == ':') {   // {kl1:0}: a movement key, from the Keys dropdown in field 0
            const std::size_t fk = (std::size_t)std::atoi(key.c_str() + 4);
            out += MoveKey(fk < f.size() ? f[fk] : std::string("arrows"), key[1], key[2] == '2');
            i = e;
            continue;
        }
        if (key.size() >= 2 && (key[0] == 'G' || key[0] == 'S' || key[0] == 'n') && std::isdigit((unsigned char)key[1])) {
            // A saved-value name: {n0} the name, {G0} the node that reads it, {S0} the node that sets it.
            const std::size_t fk = (std::size_t)std::atoi(key.c_str() + 1);
            const std::string nm = fk < f.size() ? f[fk] : std::string();
            const bool shared = !nm.empty() && nm[0] == '*';
            if (key[0] == 'G') out += shared ? "GetSceneVar" : "GetVar";
            else if (key[0] == 'S') out += shared ? "SetSceneVar" : "SetVar";
            else out += Word(shared ? nm.substr(1) : nm);
            i = e;
            continue;
        }
        char mode = 'w';
        if (!key.empty() && (key[0] == 'v' || key[0] == 'q' || key[0] == 'r' || key[0] == 'g' || key[0] == 'b')) { mode = key[0]; key = key.substr(1); }
        int part = -1;
        if (!key.empty() && key[0] == 'p') {
            key = key.substr(1);
            const std::size_t dot = key.find('.');
            if (dot != std::string::npos) { part = std::atoi(key.c_str() + dot + 1); key = key.substr(0, dot); }
        }
        const std::size_t k = (std::size_t)std::atoi(key.c_str());
        std::string v = k < f.size() ? f[k] : std::string();
        if (part >= 0) v = Part(v, part);
        if (mode == 'r') out += ColourPart(v, 0);
        else if (mode == 'g') out += ColourPart(v, 1);
        else if (mode == 'b') out += ColourPart(v, 2);
        else out += mode == 'v' ? Literal(v) : mode == 'q' ? Quote(v) : Word(v);
        i = e;
    }
    return out;
}
struct Ends { int in = -1; int out = -1; int outPin = 0; };
/// Writes one body (a block's, or one row of a Rule) as saved-graph lines. Local names get `prefix`; ids come from `base`.
inline Ends Emit(const std::string& body, const std::vector<std::string>& fields, const std::string& prefix, int base,
                 std::unordered_map<std::string, int>& names, std::string& out) {
    auto idOf = [&](const std::string& name) {
        const std::string key = prefix + name;
        auto it = names.find(key);
        if (it != names.end()) return it->second;
        const int id = base + 1 + (int)names.size();
        names[key] = id;
        return id;
    };
    Ends e;
    std::istringstream in(body);
    std::string l;
    while (std::getline(in, l)) {
        l = Fill(l, fields);
        if (!l.empty() && l[0] == '!') {   // "!Value line": left out when the first field is Value (a dropdown choice)
            std::string tail;
            const auto head = Words(l.substr(1), 1, &tail);
            if (head.empty() || (!fields.empty() && fields[0] == head[0])) continue;
            l = tail;
        }
        std::string rest;
        const auto w = Words(l, 1, &rest);
        if (w.empty()) continue;
        const std::string& k = w[0];
        if (k == "N") {
            std::string args;
            const auto n = Words(rest, 2, &args);
            out += "node " + std::to_string(idOf(n[0])) + " " + n[1] + (args.empty() ? "" : " " + args) + "\n";
        } else if (k == "D") {
            std::string val;
            const auto n = Words(rest, 2, &val);
            out += "default " + std::to_string(idOf(n[0])) + " " + n[1] + " " + val + "\n";
        } else if (k == "X") {
            const auto n = Words(rest, 3, nullptr);
            out += "exec " + std::to_string(idOf(n[0])) + " " + n[1] + " " + std::to_string(idOf(n[2])) + "\n";
        } else if (k == "W") {
            const auto n = Words(rest, 4, nullptr);
            out += "data " + std::to_string(idOf(n[0])) + " " + n[1] + " " + std::to_string(idOf(n[2])) + " " + n[3] + "\n";
        } else if (k == "E") {
            const auto n = Words(rest, 2, nullptr);
            out += "entry " + Word(n[0]) + " " + std::to_string(idOf(n[1])) + "\n";
        } else if (k == "IN") {
            e.in = idOf(Trim(rest));
        } else if (k == "OUT") {
            const auto n = Words(rest, 2, nullptr);
            e.out = idOf(n[0]);
            e.outPin = n.size() > 1 ? std::atoi(n[1].c_str()) : 0;
        }
    }
    return e;
}
inline const QbRuleEntry* FindRuleEntry(const std::vector<QbRuleEntry>& t, const std::string& value) {
    for (const QbRuleEntry& e : t) if (value == e.value) return &e;
    return nullptr;
}
} // namespace qb_detail

/// Every field of a block as the editor shows it: the typed fields, then a Presets dropdown when the block has presets.
inline std::vector<QbField> QbAllFields(const QuickBlock& b) {
    std::vector<QbField> f = b.fields;
    if (!b.presets.empty()) {
        static std::unordered_map<std::string, std::string> keep;
        std::string& c = keep[b.type];
        if (c.empty()) for (const QbPreset& p : b.presets) c += (c.empty() ? "" : "|") + std::string(p.name) + "~" + p.name;
        f.push_back(QbField{"Presets", "", QbKind::Preset, c.c_str()});
    }
    return f;
}

/// "Label~value|Label~value" -> the pairs. A part without "=" is both label and value.
inline std::vector<std::pair<std::string, std::string>> QbChoices(const char* choices) {
    std::vector<std::pair<std::string, std::string>> out;
    std::string c = choices ? choices : "", cur;
    c += '|';
    for (char ch : c) {
        if (ch != '|') { cur += ch; continue; }
        if (!cur.empty()) {
            const std::size_t t = cur.find('~');
            if (t == std::string::npos) out.push_back({cur, cur});
            else out.push_back({cur.substr(0, t), cur.substr(t + 1)});
        }
        cur.clear();
    }
    return out;
}

/// Fills `values` (one per field) from the preset with this name. False when there is no such preset.
inline bool QbApplyPreset(const QuickBlock& b, const std::string& name, std::vector<std::string>& values) {
    for (const QbPreset& p : b.presets) {
        if (name != p.name) continue;
        for (std::size_t i = 0; i < p.values.size() && i < b.fields.size(); ++i) {
            if (!p.values[i] || !p.values[i][0]) { if (i < values.size() && b.fields[i].kind != QbKind::Text) continue; }
            if (i >= values.size()) values.resize(i + 1);
            values[i] = p.values[i] ? p.values[i] : "";
        }
        return true;
    }
    return false;
}

/// True when `value` is acceptable for the kind of field (the editor refuses to store anything else).
inline bool QbValidate(QbKind kind, const std::string& value, const char* choices = "") {
    switch (kind) {
        case QbKind::Number: {
            if (qb_detail::Trim(value).empty()) return false;
            char* end = nullptr;
            const double d = std::strtod(value.c_str(), &end);
            return end && *end == '\0' && d == d && d > -1e12 && d < 1e12;
        }
        case QbKind::Key: return value.size() == 1 || (!value.empty() && value.size() <= 12 && std::isalpha((unsigned char)value[0]));
        case QbKind::Choice:
            for (const auto& c : QbChoices(choices)) if (c.second == value) return true;
            return false;
        case QbKind::Colour: {
            if (value.size() != 7 || value[0] != '#') return false;
            for (std::size_t i = 1; i < 7; ++i) if (!std::isxdigit((unsigned char)value[i])) return false;
            return true;
        }
        case QbKind::Name: {
            const std::string bare = (!value.empty() && value[0] == '*') ? value.substr(1) : value;   // a leading * means "everyone can see this"
            return !qb_detail::Trim(bare).empty() && bare.find_first_of("\"# \t*") == std::string::npos;
        }
        case QbKind::Preset: case QbKind::Object: case QbKind::Text: return true;
    }
    return true;
}

/// The tidy form of a typed value: numbers trimmed, keys and colours lower-case.
inline std::string QbNormalize(QbKind kind, const std::string& value) {
    std::string v = qb_detail::Trim(value);
    if (kind == QbKind::Key || kind == QbKind::Colour) for (char& c : v) c = (char)std::tolower((unsigned char)c);
    return v;
}

/// A saved value's name as typed: spaces and odd marks become "_", so it is one word the graph can keep.
inline std::string QbNormalizeName(const std::string& typed) {
    std::string t = qb_detail::Trim(typed), out;
    const bool shared = !t.empty() && t[0] == '*';
    if (shared) t = t.substr(1);
    for (char c : t) out += (std::isalnum((unsigned char)c) || c == '_') ? c : '_';
    return shared ? "*" + out : out;
}
/// A saved value is "only this object" by default; a leading * in its name means "everyone can see this" (every graph
/// in the scene). These three keep that in one place.
inline bool QbNameShared(const std::string& name) { return !name.empty() && name[0] == '*'; }
inline std::string QbNameBare(const std::string& name) { return QbNameShared(name) ? name.substr(1) : name; }
inline std::string QbNameWithScope(const std::string& bare, bool shared) { return (shared ? "*" : "") + QbNameBare(bare); }

/// What the editor knows when it fills a new block in.
struct QbContext {
    std::string lastKey;                    // the key the person picked most recently ("" none)
    std::string selfName;                   // the selected object
    std::vector<std::string> objectNames;   // objects in the scene
};
/// A starting value from context: the last key used for a key field; for an object field the default name when that
/// object exists, else the first other object in the scene.
inline std::string QbSmartDefault(const QbField& f, const QbContext& ctx) {
    if (f.kind == QbKind::Key && !ctx.lastKey.empty()) return ctx.lastKey;
    if (f.kind == QbKind::Object && !ctx.objectNames.empty()) {
        for (const std::string& n : ctx.objectNames) if (n == f.def) return f.def;
        for (const std::string& n : ctx.objectNames) if (n != ctx.selfName) return n;
    }
    return f.def;
}

/// An old node that a Quick block now covers: which block, which field holds the choice, and its value.
struct QbAlias { const char* oldType; const char* block; int field; const char* value; };
inline const QbAlias* QuickBlockForOldNode(const std::string& type) {
    static const QbAlias t[] = {
        {"AddVar", "QbChangeNumber", 1, "Add"},
        {"PlaySound", "QbPlayOrStopSound", 0, "PlaySound"},
        {"StopSound", "QbPlayOrStopSound", 0, "StopSound"},
    };
    for (const QbAlias& a : t) if (type == a.oldType) return &a;
    return nullptr;
}

/// Graphs saved by release 2.885.0 hold the first fields these blocks had. This turns such a set of typed values (pin ->
/// text) into the current fields, so an old graph loads and behaves the same. A set that is already current is untouched.
inline bool QbUpgradeFields(const std::string& type, std::map<int, std::string>& v) {
    auto get = [&](int pin, const char* def) { auto it = v.find(pin); return it != v.end() && !it->second.empty() ? it->second : std::string(def); };
    auto hasHigh = [&](int pin) { return v.lower_bound(pin) != v.end(); };
    if (type == "QbMoveWithKeys" && hasHigh(2)) {   // was: Left key, Right key, Up key, Down key, Speed
        const std::string l = get(0, "left"), r = get(1, "right");
        const std::string speed = get(4, "5");
        v.clear();
        v[0] = (l == "a" && r == "d") ? "wasd" : "arrows";
        v[1] = speed;
        return true;
    }
    if (type == "QbMoveToward" && hasHigh(2)) {    // was: Right, Up, Forward, Speed
        const std::string spot = get(0, "5") + "," + get(1, "0") + "," + get(2, "0"), speed = get(3, "2");
        v.clear(); v[0] = spot; v[1] = speed;
        return true;
    }
    if (type == "QbSpawnCopyAt" && hasHigh(2)) {   // was: What, Right, Up, Forward
        const std::string what = get(0, "Assets/coin.okayprefab"), spot = get(1, "0") + "," + get(2, "0") + "," + get(3, "0");
        v.clear(); v[0] = what; v[1] = spot;
        return true;
    }
    if (type == "QbFlashColour" && hasHigh(2)) {   // was: Red, Green, Blue, Seconds (0 to 1 each)
        auto hex = [&](int pin, const char* def) {
            const double d = std::atof(get(pin, def).c_str());
            const int n = d <= 0 ? 0 : d >= 1 ? 255 : (int)(d * 255.0 + 0.5);
            char b[4]; std::snprintf(b, sizeof b, "%02x", n);
            return std::string(b);
        };
        const std::string colour = "#" + hex(0, "1") + hex(1, "0") + hex(2, "0"), seconds = get(3, "0.2");
        v.clear(); v[0] = colour; v[1] = seconds;
        return true;
    }
    return false;
}

/// An old plain node that one Quick block now covers, when it is safe to swap (nothing wired into it): returns the block
/// and its typed values. `pins` is the node's typed values (pin -> text). The editor does this when it loads a graph;
/// the running graph does not need it, because the old nodes keep working as they are.
inline bool QbConvertOldNode(const std::string& type, const std::string& arg, const std::map<int, std::string>& pins,
                             std::string& newType, std::map<int, std::string>& newPins) {
    auto numeric = [](const std::string& v) { return qb_detail::IsNumberOrBool(v) && v != "true" && v != "false"; };
    newPins.clear();
    if (type == "SetActive" && (arg.empty() || arg == "self") && pins.size() == 1 && pins.count(0)) {
        const std::string& v = pins.at(0);
        if (v == "0" || v == "false") { newType = "QbShowHide"; newPins[0] = "0"; return true; }
        if (v == "1" || v == "true")  { newType = "QbShowHide"; newPins[0] = "1"; return true; }
        return false;
    }
    if (type == "Translate" && arg.empty() && !pins.empty()) {
        std::string parts[3] = {"0", "0", "0"};
        for (const auto& kv : pins) {
            if (kv.first < 0 || kv.first > 2 || !numeric(kv.second)) return false;
            parts[kv.first] = kv.second;
        }
        newType = "QbMoveTurnResize"; newPins[0] = "Translate"; newPins[1] = parts[0] + "," + parts[1] + "," + parts[2];
        return true;
    }
    if (type == "Rotate" && arg.empty() && pins.size() == 1 && pins.count(0) && numeric(pins.at(0))) {
        newType = "QbMoveTurnResize"; newPins[0] = "Rotate"; newPins[1] = pins.at(0) + ",0,0";
        return true;
    }
    return false;
}

/// Turns every quick block in a saved graph into the low-level nodes and wires it stands for. Graphs without a
/// quick block come back unchanged.
/// `ranges` (optional) gets one {first expanded id, the block's own id} per block: every expanded node of a block has an id in
/// [first, first + 199]. The editor uses it to light up the block while its nodes run in Play.
inline std::string ExpandQuickBlocks(const std::string& text, std::vector<std::pair<int, int>>* ranges = nullptr) {
    using namespace qb_detail;
    if (text.find("node ") == std::string::npos || text.find(" Qb") == std::string::npos) return text;
    struct Inst { const QuickBlock* b; std::vector<std::string> fields; int base; };
    std::unordered_map<int, Inst> blocks;
    std::vector<std::string> lines;
    { std::istringstream in(text); std::string l; while (std::getline(in, l)) lines.push_back(l); }
    int counter = 0;
    for (const std::string& l : lines) {
        if (l.compare(0, 5, "node ") != 0) continue;
        std::string rest;
        const auto w = Words(l, 3, &rest);
        if (w.size() < 3) continue;
        const QuickBlock* b = FindQuickBlock(w[2]);
        if (!b) continue;
        Inst inst{b, {}, 1000000 + 200 * counter++};
        for (const QbField& f : b->fields) inst.fields.push_back(f.def);
        blocks[std::atoi(w[1].c_str())] = inst;
        if (ranges) ranges->push_back({inst.base, std::atoi(w[1].c_str())});
    }
    if (blocks.empty()) return text;
    // Typed field values: "default <id> <pin> <value>".
    for (const std::string& l : lines) {
        if (l.compare(0, 8, "default ") != 0) continue;
        std::string rest;
        const auto w = Words(l, 3, &rest);
        if (w.size() < 3) continue;
        auto it = blocks.find(std::atoi(w[1].c_str()));
        if (it == blocks.end()) continue;
        const std::size_t pin = (std::size_t)std::atoi(w[2].c_str());
        if (pin < it->second.fields.size()) it->second.fields[pin] = Unquote(Trim(rest));
    }
    // Graphs saved by an earlier release: bring the typed values up to the current fields.
    {
        std::unordered_map<int, std::map<int, std::string>> typed;
        for (const std::string& l : lines) {
            if (l.compare(0, 8, "default ") != 0) continue;
            std::string rest;
            const auto w = Words(l, 3, &rest);
            if (w.size() < 3) continue;
            const int id = std::atoi(w[1].c_str());
            if (blocks.count(id)) typed[id][std::atoi(w[2].c_str())] = Unquote(Trim(rest));
        }
        for (auto& kv : typed) {
            Inst& inst = blocks[kv.first];
            if (!QbUpgradeFields(inst.b->type, kv.second)) continue;
            inst.fields.clear();
            for (const QbField& f : inst.b->fields) inst.fields.push_back(f.def);
            for (const auto& pv : kv.second) if ((std::size_t)pv.first < inst.fields.size()) inst.fields[(std::size_t)pv.first] = pv.second;
        }
    }
    // A dropdown that holds something that is not one of its choices (a hand-edited file) falls back to its default.
    for (auto& kv : blocks)
        for (std::size_t i = 0; i < kv.second.fields.size() && i < kv.second.b->fields.size(); ++i) {
            const QbField& f = kv.second.b->fields[i];
            if (f.kind == QbKind::Choice && !QbValidate(QbKind::Choice, kv.second.fields[i], f.choices)) kv.second.fields[i] = f.def;
        }
    std::unordered_map<int, Ends> ends;
    std::string extra;
    for (auto& kv : blocks) {
        Inst& inst = kv.second;
        std::unordered_map<std::string, int> names;
        if (std::strcmp(inst.b->type, "QbRule") != 0) {
            ends[kv.first] = Emit(inst.b->body, inst.fields, "", inst.base, names, extra);
            continue;
        }
        // The Rule: event, optional condition, then each action in order, chained with flow wires.
        const std::vector<std::string>& f = inst.fields;
        auto field = [&](std::size_t i) { return i < f.size() ? f[i] : std::string(); };
        Ends prev, first;
        auto chain = [&](const QbRuleEntry* e, const std::string& value, const std::string& prefix) {
            if (!e || !e->body[0]) return;
            const Ends cur = Emit(e->body, {value}, prefix, inst.base, names, extra);
            if (first.in < 0) first = cur;
            if (prev.out >= 0 && cur.in >= 0)
                extra += "exec " + std::to_string(prev.out) + " " + std::to_string(prev.outPin) + " " + std::to_string(cur.in) + "\n";
            prev = cur;
        };
        chain(FindRuleEntry(QbRuleEvents(), field(0)), field(1), "w_");
        chain(FindRuleEntry(QbRuleConditions(), field(2)), field(3), "c_");
        for (int k = 0; k < 4; ++k)
            chain(FindRuleEntry(QbRuleActions(), field(4 + 2 * (std::size_t)k)), field(5 + 2 * (std::size_t)k), "a" + std::to_string(k) + "_");
        ends[kv.first] = Ends{};   // a Rule starts on its own event and has no flow in or out
    }
    std::string out;
    for (const std::string& l : lines) {
        std::string rest;
        const auto w = Words(l, 4, &rest);
        auto isBlock = [&](const std::string& s) { return blocks.count(std::atoi(s.c_str())) > 0; };
        if (w.size() >= 3 && w[0] == "node" && isBlock(w[1]) && FindQuickBlock(w[2])) continue;
        if (w.size() >= 2 && (w[0] == "default" || w[0] == "pos") && isBlock(w[1])) continue;
        if (w.size() >= 4 && w[0] == "exec") {
            std::string from = w[1], to = w[3], pin = w[2];
            if (isBlock(to)) { const int in = ends[std::atoi(to.c_str())].in; if (in < 0) continue; to = std::to_string(in); }
            if (isBlock(from)) {
                const Ends& e = ends[std::atoi(from.c_str())];
                if (e.out < 0 || std::atoi(pin.c_str()) != 0) continue;
                from = std::to_string(e.out); pin = std::to_string(e.outPin);
            }
            out += "exec " + from + " " + pin + " " + to + "\n";
            continue;
        }
        if (w.size() >= 3 && w[0] == "entry") {   // the block registers its own entries
            const std::size_t sp = l.find_last_of(' ');
            if (sp != std::string::npos && isBlock(l.substr(sp + 1))) continue;
        }
        if (w.size() >= 4 && w[0] == "data" && (isBlock(w[1]) || isBlock(w[3]))) continue;   // a block takes typed fields only
        out += l + "\n";
    }
    return out + extra;
}

} // namespace okay::vs
