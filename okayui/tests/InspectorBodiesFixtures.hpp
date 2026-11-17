#pragma once
// Component bodies as field rows for the Inspector's stage 2 tests (docs/own_ui_toolkit.md section 15): stand-ins for the values of a
// Spot light, a camera, a Rigidbody 3D, a Box Collider 3D, a script's variables, the rest of a Mesh Renderer and a component drawn from
// the property registry, and the rows the editor's builders make of them (editor/main.cpp DrawLightWithOkayUI and the others,
// editor/InspectorPanel.cpp DrawFallbackOkayUI): the same kinds, labels, ranges, formats and layout. Used by the OkayUI test
// (okayui/tests/test_okayui_inspector.cpp) and the comparison with Dear ImGui (tests/test_okayui_inspector_imgui.cpp). Std and the
// model only; the hooks answer from small tables and count what they were asked.
#include "EditorPanelModels.hpp"

#include <string>
#include <vector>

namespace inspbody {

namespace P = okaypanels;

// stage 3 (section 16): a Sprite Renderer, an Audio Source (3D, with its rule), a Box Collider 2D, an Edge Collider 2D (the rule and the
// point list), a Particle System (the preset list's own text), a Character Controller 3D (the crouch fold-out's 96-wide key lists) and an
// Animator (the tracks as bullets)
enum Body : int { BodyLight = 0, BodyCamera, BodyRigidbody, BodyBoxCollider, BodyScript, BodyMeshRest, BodyGeneric,
                  BodySprite, BodyAudio, BodyBox2D, BodyEdge2D, BodyParticles, BodyController3D, BodyAnimator,
                  // stage 4 (section 17): Text (screen space, its style bar), a Canvas, a UI Button, a UI Image, a UI Slider, a UI Toggle,
                  // a UI Input Field, a UI Layout Group (flex), a Nav Mesh Agent, a Terrain (its layers and scatter sections) and a
                  // Particle System with a gradient and two curves
                  BodyText, BodyCanvas, BodyButton, BodyImage, BodySlider, BodyToggle, BodyInput, BodyLayout, BodyNavAgent, BodyTerrain,
                  BodyCurves, BodyCount };
inline const char* const kBodyNames[BodyCount] = {"light", "camera", "rigidbody", "boxcollider", "script", "meshrest", "generic",
                                                  "sprite", "audio", "box2d", "edge2d", "particles", "controller3d", "animator",
                                                  "text", "canvas", "uibutton", "uiimage", "uislider", "uitoggle", "uiinput", "layout",
                                                  "navagent", "terrain", "curves"};

struct Values {
    // Light (a Spot light, colour mode)
    int lightType = 2; bool useTemp = false; float temperature = 6500.0f; float color[4] = {1.0f, 0.95f, 0.8f, 1.0f};
    float intensity = 1.2f, ambient = 0.2f, ambientTint[3] = {0.5f, 0.55f, 0.65f}, range = 12.0f; int falloff = 1;
    float spotAngle = 45.0f, softness = 0.3f; bool castShadows = true; float shStrength = 0.8f, shBias = 1.0f;
    float flicker = 0.3f, flickerSpeed = 9.0f;
    // Camera (perspective, a solid colour background)
    int projection = 1; bool physical = false; float fov = 60.0f; int fovAxis = 0; int clearFlags = 1; float bg[4] = {0.16f, 0.12f, 0.24f, 1.0f};
    float nearClip = 0.1f, farClip = 500.0f, zoom = 1.0f, dutch = 0.0f, shift[2] = {0.0f, 0.0f};
    float shakeAmount = 0.5f, shakeAngle = 2.0f, shakeFreq = 25.0f, shakeRecovery = 3.0f, rect[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    int cullingMask = ~0; bool mainCam = true; float depth = 0.0f; std::string renderTarget;
    // Rigidbody 3D
    int bodyType = 0; float gravity = 1.0f, mass = 1.0f, drag = 0.0f, bounce = 0.2f, friction = 0.5f, maxFall = 0.0f;
    bool fx = false, fy = false, fz = false, freezeRot = false; float angDrag = 0.05f; bool sleep = true, ccd = false; float rolling = 0.03f;
    bool children = false;
    // Box Collider 3D
    float size[3] = {1.0f, 1.0f, 1.0f}, offset[3] = {0.0f, 0.5f, 0.0f}; bool trigger = true; int layer = 0;
    bool ownF = true, ownB = false; float colFriction = 0.6f; int fmix = 0, bmix = 0; std::string onlyTag = "Player", onlyLayers;
    bool autoFit = false, editCollider = false;
    // a script's variables
    bool vAlive = true; float vSpeed = 5.0f, vJump = 0.4f, vTarget[3] = {1.0f, 2.0f, 0.0f}; std::string vName = "Hero", vFollow = "Player";
    // the rest of a Mesh Renderer
    std::string meshFile, texture = "Assets/stone.png", normalMap, specMap, aoMap, mrMap, emisMap, matPath = "Assets/Material.okaymat", procPick;
    float tiling[2] = {1.0f, 1.0f}, texOffset[2] = {0.0f, 0.0f}, uvScroll[2] = {0.0f, 0.0f}; int texFilter = 0; bool triplanar = false;
    float reflect = 0.0f, metallic = 0.0f;
    // a component from the property registry (an Audio Source)
    std::string clip = "Assets/Audio/step.wav", follow;   // the follow field holds an object's guid
    bool playOnAwake = true, loop = false; float volume = 0.8f; int priority = 128; float pos3[3] = {0.0f, 1.0f, 0.0f};
    float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f}; int rolloff = 1; std::string note = "footsteps";

    // stage 3: a Sprite Renderer
    float spColor[4] = {1.0f, 1.0f, 1.0f, 1.0f}, spSize[2] = {1.0f, 1.0f}; std::string spTex = "Assets/hero.png";
    int spLayer = 0, spOrder = 0; bool spFlipX = false, spFlipY = false; int spFilter = 1;
    // an Audio Source (3D)
    float auVol = 0.8f, auPitch = 1.0f, auPan = 0.0f; int auGroup = 0; float auLowPass = 22000.0f; bool auLoop = true, auAwake = true, auStream = false;
    std::string auPath = "Assets/Audio/wind.ogg"; bool auSpatial = true; float auMin = 1.0f, auMax = 30.0f; int auRoll = 1;
    bool auDoppler = false, auOcclude = true; float auOccVol = 0.4f, auOccLp = 900.0f; bool auDistLp = false;
    // a Box Collider 2D and an Edge Collider 2D
    float b2Size[2] = {1.0f, 1.0f}, b2Off[2] = {0.0f, 0.0f}; bool b2Trigger = false; int b2Layer = 0; bool b2OwnF = false, b2OwnB = false, b2Auto = true;
    float e2Off[2] = {0.0f, 0.0f}; bool e2OneWay = true; float e2Normal[2] = {0.0f, 1.0f}; float e2Pts[3][2] = {{-2.0f, 0.0f}, {0.0f, 0.5f}, {2.0f, 0.0f}};
    // a Particle System
    int psPreset = -1; bool psPlaying = true; float psRate = 20.0f; int psMax = 500, psShape = 4; float psAngle = 25.0f, psLife = 2.0f, psSize = 0.3f;
    float psStartCol[4] = {1.0f, 0.6f, 0.2f, 1.0f}, psVel[3] = {0.0f, 2.0f, 0.0f}; bool psFade = true, psColLife = true; int psCMode = 0;
    float psEndCol[4] = {0.3f, 0.3f, 0.3f, 0.0f}; int psSeed = 7;
    // a Character Controller 3D (its crouch fold-out open)
    int c3Crouch = 2, c3Prone = 3; bool c3Toggle = false, c3Prone2 = true, c3Resize = true; float c3CrouchSpeed = 2.0f, c3CrouchH = 0.6f;
    float c3Speed = 5.0f; bool c3Jump = true; float c3JumpF = 6.0f;
    // an Animator
    std::string anClip = "Bounce"; bool anLoop = true, anPlaying = false; float anSpeed = 1.0f, anTime = 0.25f;

    // stage 4: Text (screen space)
    std::string txText = "Score: 120\nBest: 400"; float txColor[4] = {1.0f, 0.84f, 0.3f, 1.0f}, txSize = 3.0f, txBox[2] = {240.0f, 64.0f}, txOff[2] = {12.0f, 8.0f};
    bool txLoc = false, txFit = false, txScreen = true, txBg = true, txShadow = true, txOutline = false, txBold = true, txItalic = false, txUpper = false, txGrad = false;
    int txAnchor = 0, txAlign = 1, txVAlign = 0, txVisible = -1; float txBgCol[4] = {0.0f, 0.0f, 0.0f, 0.5f}, txShCol[4] = {0.0f, 0.0f, 0.0f, 0.8f}, txShOff[2] = {1.0f, -1.0f};
    float txType = 0.0f, txLetter = 0.0f, txLine = 0.0f; bool txWrap = false, txRich = true; std::string txFont;
    // a Canvas (scale with screen size, match)
    bool cvVisible = true; float cvOpacity = 1.0f; int cvMode = 1, cvMatch = 0; float cvRef[2] = {1920.0f, 1080.0f}, cvMatchWH = 0.5f, cvScale = 1.0f;
    int cvSort = 0; bool cvWorld = false;
    // a UI Button (no child text)
    std::string btLabel = "Play", btTarget, btIcon; int btFn = 0; float btPos[2] = {0.0f, -40.0f}, btSize[2] = {200.0f, 56.0f};
    float btColor[4] = {0.24f, 0.47f, 0.8f, 1.0f}, btHover[4] = {0.3f, 0.55f, 0.9f, 1.0f}, btPressed[4] = {0.18f, 0.36f, 0.62f, 1.0f}, btDisabled[4] = {0.4f, 0.4f, 0.4f, 1.0f};
    bool btInter = true, btFocus = true, btShadow = false, btFocusCol = false, btIconRight = false, btToggle = false, btRepeat = false; int btAnchor = 4, btShape = 1, btEase = 1;
    float btRadius = 12.0f, btFontScale = 2.0f, btBorder = 0.0f, btGrow = 1.05f, btHoverText[4] = {1, 1, 1, 1}, btSpeed = 10.0f, btTime = 0.12f, btPress = 2.0f, btIconSize = 0.0f;
    std::string btFont;
    // a UI Image (stretched: four margins)
    int imAnchor = 11; float imPos[2] = {16.0f, 16.0f}, imSize[2] = {16.0f, 16.0f}; std::string imTex; float imTint[4] = {0.2f, 0.25f, 0.35f, 0.9f};
    bool imFlipX = false, imFlipY = false, imAspect = false, imNine = false, imShadow = false; int imShape = 1; float imRadius = 8.0f, imFrame = 0.0f;
    bool imTL = true, imTR = true, imBL = true, imBR = true;
    // a UI Slider
    float slPos[2] = {0.0f, 0.0f}, slSize[2] = {300.0f, 24.0f}, slMin = 0.0f, slMax = 100.0f, slValue = 40.0f, slTrack[4] = {0.2f, 0.2f, 0.25f, 1.0f}, slFill[4] = {0.3f, 0.6f, 1.0f, 1.0f};
    float slKnob[4] = {1, 1, 1, 1}; int slAnchor = 4, slShape = 1, slHandle = 0; float slRadius = 6.0f, slKnobSize = 1.0f, slStep = 0.0f;
    bool slRound = true, slShow = true, slWhole = false, slInter = true, slVert = false, slGrad = false; float slText[4] = {1, 1, 1, 1}; std::string slFmt = "{v}%";
    // a UI Toggle (switch)
    std::string tgLabel = "Music"; bool tgOn = true, tgInter = true; float tgPos[2] = {0, 80}, tgSize[2] = {180, 32}, tgBox[4] = {0.2f, 0.2f, 0.25f, 1}, tgCheck[4] = {0.3f, 0.8f, 0.4f, 1};
    float tgText[4] = {1, 1, 1, 1}, tgKnob[4] = {1, 1, 1, 1}, tgRadius = 6.0f, tgAnim = 12.0f; int tgAnchor = 4, tgStyle = 1;
    // a UI Input Field
    std::string inText, inPlace = "Your name"; float inPos[2] = {0, 0}, inSize[2] = {260, 40}, inColor[4] = {0.15f, 0.15f, 0.2f, 1}, inRing = 2.0f, inRingCol[4] = {0.3f, 0.6f, 1, 1};
    int inMax = 24, inType = 0, inAnchor = 4, inShape = 1; float inRadius = 8.0f; bool inMulti = false;
    // a UI Layout Group (flex row)
    int lgDir = 3, lgJustify = 3, lgAlign = 1; float lgBox[2] = {600, 80}, lgOrigin[2] = {0, 0}, lgSpacing = 8.0f, lgPadding = 4.0f; bool lgWrap = true; int lgAnchor = 0;
    // a Nav Mesh Agent
    float naRadius = 0.5f, naHeight = 2.0f, naSpeed = 3.5f, naAccel = 8.0f, naAngular = 120.0f, naStop = 0.1f, naRepath = 0.5f, naRange = 2.0f, naOffset = 0.0f;
    bool naRepathOn = true, naBrake = true, naFace = true, naDrive = false; int naPriority = 50; int naMask = (int)0xFFFFFFFDu;
    // a Terrain (two texture layers)
    float teColor[4] = {1, 1, 1, 1}, teSize = 100.0f, teTiling = 8.0f; int teRes = 129; std::string teTex = "Assets/grass.png", teNrm; bool teTri = false, teAuto = true;
    bool teSculpt = false; int teBrush = 0, teGen = 0, teOct = 5, teDrops = 30000, teTherm = 25, teTerrace = 5; float teRadius = 6.0f, teStrength = 4.0f, teHard = 0.4f;
    float teAmp = 12.0f, teFreq = 3.0f, teErode = 0.4f, teTalus = 1.2f, teNorm[2] = {0.0f, 12.0f}, teHm[2] = {0.0f, 25.0f};
    float teWater[4] = {0.2f, 0.4f, 0.7f, 1}, teSand[4] = {0.8f, 0.75f, 0.5f, 1}, teGrass[4] = {0.3f, 0.6f, 0.25f, 1}, teRock[4] = {0.45f, 0.42f, 0.4f, 1}, teSnow[4] = {0.95f, 0.95f, 1, 1};
    float teWaterL = 1.0f, teSnowL = 30.0f, teRockS = 0.6f;
    int teLayer = 1; std::string teLName[2] = {"Grass", "Dirt"}, teLTex[2] = {"Assets/grass.png", ""}; float teLTint[2][4] = {{1, 1, 1, 1}, {0.51f, 0.38f, 0.26f, 1}}, teLTile[2] = {8.0f, 6.0f};
    int teDetail = 1; bool tePaint = false; float teLRadius = 6.0f, teLStrength = 1.0f, teLHard = 0.5f;
    int teProp = 0, teCount = 80; float teScale[2] = {0.7f, 1.5f}, teSlope = 35.0f, teBand[2] = {-100.0f, 100.0f};
    // a Particle System's over-life editors: a three-stop gradient, a size curve, a speed curve
    float gStops[8 * 5] = {0.0f, 1.0f, 0.9f, 0.3f, 1.0f,  0.5f, 1.0f, 0.3f, 0.1f, 0.8f,  1.0f, 0.2f, 0.2f, 0.2f, 0.0f};
    int gCount = 3, gSel = 1; float gCol[4] = {1.0f, 0.3f, 0.1f, 0.8f}, gTime = 0.5f;
    float cKeys[8 * 2] = {0.0f, 1.0f,  0.4f, 1.6f,  1.0f, 0.2f}; int cCount = 3, cSel = 1; bool cSmooth = true; float cKey[2] = {0.4f, 1.6f};
    float sKeys[8 * 2] = {0.0f, 1.0f,  1.0f, 0.25f}; int sCount = 2, sSel = 0; bool sSmooth = false; float sKey[2] = {0.0f, 1.0f};

    // what the rows asked for
    int tipAsked = 0, choicesAsked = 0, prefabMenuAsked = 0;
    int renamedLayer = -1; std::string renamed;   // a layer renamed from the mask's right-click menu
    std::vector<std::string> clicked;   // the buttons pressed, by label
};

inline const char* const kLightTypes[] = {"Directional", "Point", "Spot", "Area"};
inline const char* const kFalls[] = {"Linear", "Inverse Square"};
inline const char* const kProjs[] = {"Orthographic", "Perspective"};
inline const char* const kAxes[] = {"Vertical", "Horizontal"};
inline const char* const kClears[] = {"Skybox", "Solid Color", "Depth Only", "Don't Clear"};
inline const char* const kBodyTypes[] = {"Dynamic", "Kinematic", "Static"};
inline const char* const kModes[] = {"Default", "Average", "Minimum", "Multiply", "Maximum"};
inline const char* const kFilters[] = {"Smooth", "Pixel"};
inline const char* const kRolloffs[] = {"Linear", "Logarithmic", "None"};
inline const char* const kGroups[] = {"SFX", "Music", "UI", "Voice"};
inline const char* const kRolls3[] = {"Linear", "Inverse", "Logarithmic"};
inline const char* const kPresets[] = {"Fire", "Smoke", "Sparks", "Explosion", "Rain", "Snow", "Magic", "Dust"};
inline const char* const kShapes[] = {"Point", "Circle", "Sphere", "Box", "Cone", "Edge"};
inline const char* const kCModes[] = {"Start -> End", "Gradient"};
inline const char* const kKeys[] = {"(none)", "Shift", "Ctrl", "C", "Z", "X", "V", "F", "B"};
inline const char* const kBgIds[] = {"##bgp0", "##bgp1", "##bgp2", "##bgp3", "##bgp4"};
inline const char* const kAligns[] = {"Left", "Center", "Right"};
inline const char* const kVAligns[] = {"Top", "Middle", "Bottom"};
inline const char* const kTextLooks[] = {"Title", "Heading", "Body", "Caption", "Shadowed", "Outlined"};
inline const char* const kButtonLooks[] = {"Primary", "Secondary", "Ghost", "Danger", "Pill", "Flat"};
inline const float kTxtCols[6][3] = {{1, 1, 1}, {0, 0, 0}, {1.0f, 0.84f, 0.30f}, {0.95f, 0.30f, 0.28f}, {0.35f, 0.85f, 0.42f}, {0.35f, 0.62f, 0.98f}};
inline const char* const kTxtColNames[] = {"White", "Black", "Gold", "Red", "Green", "Blue"};
inline const char* const kCvModes[] = {"Constant Pixel Size", "Scale With Screen Size"};
inline const char* const kCvMatches[] = {"Match Width or Height", "Expand (always fits)", "Shrink (always fills)"};
inline const char* const kUIShapes[] = {"Rectangle", "Rounded", "Circle", "Pill", "Diamond", "Tab", "Arrow Left", "Arrow Right", "Hexagon"};
inline const char* const kEases[] = {"Linear", "Smooth", "Ease Out", "Ease In"};
inline const char* const kBtnFns[] = {"(none)", "Heal", "OpenDoor", "start_game", "show_credits"};
inline const char* const kBtnFnHeads[] = {nullptr, "Native (gameplay)", nullptr, "Script", nullptr};
inline const char* const kHandles[] = {"From Round Knob", "Circle", "Rounded", "Square", "Pill", "Diamond"};
inline const char* const kTgStyles[] = {"Checkbox", "Switch", "Radio"};
inline const char* const kInTypes[] = {"Standard", "Integer", "Decimal", "Password"};
inline const char* const kLgDirs[] = {"Vertical", "Horizontal", "Grid", "Flex Row", "Flex Column"};
inline const char* const kLgJustify[] = {"Start", "Center", "End", "Space Between", "Space Around", "Space Evenly"};
inline const char* const kLgAlign[] = {"Start", "Center", "End", "Stretch"};
inline const char* const kAreaNames[] = {"0 Walkable", "1 Not Walkable", "2 Jump", "3 Water", "4 Road", "5 Grass", "6 Area 6", "7 Area 7"};
inline const char* const kAreaPresets[] = {"All but Not Walkable", "Everything"};
inline const int kAreaPresetBits[] = {(int)0xFFFFFFFDu, (int)0xFFFFFFFFu};
inline const char* const kBrushes[] = {"Raise / Lower", "Smooth", "Flatten", "Set Height", "Noise", "Erode"};
inline const char* const kGens[] = {"Mountains", "Hills", "Plains", "Plateau", "Islands", "Ridged Mountains", "Canyons"};
inline const char* const kSplat[] = {"256", "512", "1024", "2048"};
inline const char* const kProps[] = {"Tree", "Pine", "PalmTree", "DeadTree", "Bush", "Rock", "Mushroom", "Cactus", "Crystal"};
inline const char* const kSModes[] = {"End Size", "Curve"};
inline const float kBgCols[5][3] = {{0.36f, 0.58f, 0.85f}, {0.16f, 0.12f, 0.24f}, {0.02f, 0.03f, 0.07f}, {1, 1, 1}, {0, 0, 0}};

/// The hooks the views ask: tooltips, the prefab menu, and what pickers list (counted in `v`).
inline P::InspFieldHooks Hooks(Values& v) {
    P::InspFieldHooks h;
    h.tipFor = [&v](const char* label) { ++v.tipAsked; return std::string("Tip for ") + std::string(label, P::InspVisibleLen(label)); };
    h.prefabMenu = [&v](const char* label, std::vector<P::PanelMenuEntry>& out) {
        ++v.prefabMenuAsked;
        P::PanelMenuEntry e; e.kind = P::PanelMenuHeading; e.label = "Overridden from Crate.okayprefab"; e.color = P::HierCol(115, 179, 255); out.push_back(e);
        e.color = 0;
        e.label = std::string(label, P::InspVisibleLen(label)) + ": 1 -> 2"; out.push_back(e);
        P::PanelMenuEntry sep; sep.kind = P::PanelMenuSeparator; out.push_back(sep);
        P::PanelMenuEntry r; r.id = P::kInspPrefabRevert; r.label = "Revert Property"; out.push_back(r);
        r.id = P::kInspPrefabApply; r.label = "Apply Property to Prefab"; out.push_back(r);
    };
    h.choices = [&v](const P::InspRow& row, std::vector<std::string>& values, std::vector<std::string>& names, std::vector<void*>& thumbs) {
        ++v.choicesAsked;
        const std::string k = row.pickKind ? row.pickKind : "";
        if (k == "layers") {
            const char* named[] = {"Default", "TransparentFX", "Ignore Raycast", "Layer 3", "Water", "UI"};
            for (int i = 0; i < 32; ++i) names.push_back(i < 6 ? named[i] : "Layer " + std::to_string(i));
        } else if (k == "proc") {
            for (const char* n : {"brick", "wood", "stone", "grass", "metal", "tiles"}) { values.push_back(std::string("proc:") + n); names.push_back(n); thumbs.push_back(nullptr); }
        } else if (k == "object") {
            const char* objs[][2] = {{"g-1", "Player"}, {"g-2", "Main Camera"}, {"g-3", "Door  (in House)"}};
            for (auto& o : objs) { values.push_back(o[0]); names.push_back(o[1]); }
        } else {
            for (const char* f : {"Assets/stone.png", "Assets/wood.png", "Assets/ui/icon.png", "Assets/Audio/step.wav", "Assets/Audio/jump.wav"}) {
                values.push_back(f); names.push_back(std::string(f).substr(7));
            }
        }
    };
    h.layerName = [](int i, std::string& text, std::string& hint, std::string& def) {
        const char* named[] = {"Default", "TransparentFX", "Ignore Raycast", "", "Water", "UI"};
        def = i < 6 ? named[i] : "";
        text = def;
        hint = def.empty() ? "Layer " + std::to_string(i) : def;
    };
    h.renameLayer = [&v](int i, const std::string& name, std::string& err) {
        if (name.find('!') != std::string::npos) { err = "Use letters, digits and spaces."; return false; }
        v.renamedLayer = i; v.renamed = name;
        return true;
    };
    return h;
}

/// The rows of one body (built every frame, as the editor's builders are), pointing at `v`.
/// The editor's style bar as rows (AddStyleBarRows): "Look", the presets wrapping at the right edge, Copy style, Paste style (off: nothing
/// copied).
inline void StyleBar(P::InspFieldList& L, const char* const* names, int n) {
    L.Note("Look"); L.Last().alignFrame = true;
    for (int i = 0; i < n; ++i) { L.SameLineOrWrap(); L.SmallButton(names[i]); }
    L.SmallButton("Copy style"); L.SameLine(); L.SmallButton("Paste style"); L.Last().disabled = true;
    L.Spacing();
}

inline P::InspFieldList Rows(Body b, Values& v) {
    P::InspFieldList L;
    L.hooks = Hooks(v);
    switch (b) {
    case BodyLight: {
        L.Combo("Type##light", &v.lightType, kLightTypes, 4); L.Tip("Directional = sun (parallel).\nPoint = bulb (all directions).");
        L.Check("Use Temperature##light", &v.useTemp); L.TipLookup();
        L.Color("Color##light", v.color, 4); L.TipLookup();
        L.Drag("Intensity##light", &v.intensity, 1, 0.02f, 0.0f, 8.0f); L.TipLookup();
        L.Section("Ambient (scene floor)");
        L.Slider("Ambient##light", &v.ambient, 0.0f, 1.0f); L.Tip("Unlit floor brightness (taken from the first light)");
        L.Color("Ambient Tint##light", v.ambientTint, 3); L.TipLookup();
        if (v.lightType != 0) {
            L.Section(v.lightType == 2 ? "Spot" : v.lightType == 3 ? "Area" : "Point");
            L.Drag("Range##light", &v.range, 1, 0.2f, 0.1f, 500.0f); L.TipLookup();
            L.Combo("Falloff##light", &v.falloff, kFalls, 2);
        }
        if (v.lightType == 2) {
            L.Slider("Spot Angle##light", &v.spotAngle, 5.0f, 170.0f); L.TipLookup();
            L.Slider("Softness##light", &v.softness, 0.0f, 1.0f);
        }
        L.Section("Shadows");
        L.Check("Cast Shadows##light", &v.castShadows);
        if (v.castShadows) { L.Slider("Strength##lightsh", &v.shStrength, 0.0f, 1.0f); L.Slider("Bias##lightsh", &v.shBias, 0.0f, 4.0f); }
        L.Section("Flicker");
        L.Slider("Amount##lightfl", &v.flicker, 0.0f, 1.0f);
        if (v.flicker > 0.0f) {
            L.Slider("Speed##lightfl", &v.flickerSpeed, 0.5f, 30.0f, "%.1f /s"); L.TipLookup();
            L.SmallButton("Candle"); L.SameLine(4.0f); L.SmallButton("Torch"); L.SameLine(4.0f); L.SmallButton("Faulty bulb");
            L.Note("Flicker plays in Play mode and in the built game.");
        }
        L.Note("Rotate this object to aim the light (its +Z is the direction).");
        L.SmallButton("Remove##light");
        break;
    }
    case BodyCamera: {
        L.Combo("Projection", &v.projection, kProjs, 2); L.TipLookup();
        L.Check("Physical Camera", &v.physical);
        L.Slider("Field of View", &v.fov, 10.0f, 170.0f, "%.0f deg"); L.TipLookup();
        L.SmallButton("60"); L.SameLine(4.0f); L.SmallButton("75"); L.SameLine(4.0f); L.SmallButton("90"); L.SameLine(4.0f); L.SmallButton("110");
        L.Combo("FOV Axis", &v.fovAxis, kAxes, 2);
        L.Section("Background");
        L.Combo("Clear Flags", &v.clearFlags, kClears, 4);
        L.Color("Background", v.bg, 4);
        for (int i = 0; i < 5; ++i) { if (i) L.SameLine(4.0f); L.Swatch(kBgIds[i], kBgCols[i][0], kBgCols[i][1], kBgCols[i][2], 1.0f, 22.0f, 18.0f); }
        L.Section("Clipping Planes");
        L.Drag("Near", &v.nearClip, 1, 0.01f, 0.001f, 100.0f);
        L.Drag("Far", &v.farClip, 1, 1.0f, 1.0f, 100000.0f);
        L.Section("Zoom & Framing");
        L.Slider("Zoom", &v.zoom, 0.25f, 8.0f, "%.2fx", true); L.SameLine(); L.SmallButton("1x##zoomreset");
        L.Slider("Dutch Angle", &v.dutch, -45.0f, 45.0f, "%.1f deg");
        L.Drag("Lens Shift", v.shift, 2, 0.005f, -1.0f, 1.0f, "%.3f");
        L.Section("Camera Shake");
        L.Slider("Amount", &v.shakeAmount, 0.0f, 3.0f, "%.2f units");
        L.Slider("Angle", &v.shakeAngle, 0.0f, 15.0f, "%.1f deg");
        L.Button("Test Shake"); L.Last().disabled = true;
        L.Section("Viewport Rect");
        L.Drag("X / Y / W / H", v.rect, 4, 0.01f, 0.0f, 1.0f);
        L.Section("Culling Mask");
        L.Mask("Layers", &v.cullingMask);
        L.Spacing();
        L.Check("Main", &v.mainCam); L.SameLine(); L.Drag("Depth", &v.depth, 1, 0.1f); L.Last().width = 90.0f;
        L.Text("Render Target", &v.renderTarget);
        L.Button("Add overlay camera");
        L.SmallButton("Remove##cam");
        break;
    }
    case BodyRigidbody: {
        L.Combo("Body Type##rb3", &v.bodyType, kBodyTypes, 3); L.TipLookup();
        L.Drag("Gravity Scale##rb3", &v.gravity, 1, 0.05f);
        L.Drag("Mass##rb3", &v.mass, 1, 0.05f, 0.01f, 1000.0f);
        L.Drag("Drag##rb3", &v.drag, 1, 0.01f, 0.0f, 100.0f);
        L.Drag("Bounciness##rb3", &v.bounce, 1, 0.01f, 0.0f, 1.0f);
        L.Drag("Friction##rb3", &v.friction, 1, 0.01f, 0.0f, 2.0f);
        L.Drag("Max Fall Speed##rb3", &v.maxFall, 1, 0.1f, 0.0f, 200.0f);
        L.Note("Freeze position");
        L.SameLine(); L.Check("X##fz", &v.fx); L.SameLine(); L.Check("Y##fz", &v.fy); L.SameLine(); L.Check("Z##fz", &v.fz);
        L.Check("Freeze Rotation##rb3", &v.freezeRot);
        if (!v.freezeRot) L.Drag("Angular Drag##rb3", &v.angDrag, 1, 0.01f, 0.0f, 10.0f);
        L.Check("Allow Sleep##rb3", &v.sleep);
        L.Check("Continuous Collision##rb3", &v.ccd);
        L.Drag("Rolling Resistance##rb3", &v.rolling, 1, 0.005f, 0.0f, 1.0f, "%.3f");
        L.Check("Use Child Colliders##rb3", &v.children);
        L.Label("This body follows its parent. Moving or turning the parent carries it, and gravity still pulls straight down.");
        L.Last().wrap = true;
        L.SmallButton("Remove##rb3");
        break;
    }
    case BodyBoxCollider: {
        L.Drag("Size##bc3", v.size, 3, 0.05f, 0.0f, 0.0f, "%.3f"); L.Last().axisBars = true;
        L.Drag("Offset##bc3", v.offset, 3, 0.05f, 0.0f, 0.0f, "%.3f"); L.Last().axisBars = true;
        L.Check("Trigger##bc3", &v.trigger); L.SameLine(); L.DragInt("Layer##bc3", &v.layer, 0.1f, 0, 31); L.Last().width = 80.0f;
        L.Tree("Physics Material##pmbc3"); L.Last().on = true;
        L.Check("Own Friction##pmbc3", &v.ownF);
        if (v.ownF) { L.Drag("Friction##pmbc3", &v.colFriction, 1, 0.01f, 0.0f, 10.0f); L.Combo("Friction Mix##pmbc3", &v.fmix, kModes, 5); }
        L.Check("Own Bounciness##pmbc3", &v.ownB);
        L.TreeEnd();
        if (v.trigger) {
            L.Text("Only Tag##tfbc3", &v.onlyTag, "any tag (e.g. Player)");
            L.Text("Only Layers##tfbc3", &v.onlyLayers, "all layers");
        }
        L.SmallButton("Fit to mesh##bc3"); L.SameLine(); L.Check("Auto Fit##bc3", &v.autoFit); L.TipLookup();
        L.Button("Edit Collider##bc3", v.editCollider);
        if (v.editCollider) L.Note("Drag the colored dots on each face in the Scene view.");
        L.SmallButton("Remove##bc3");
        break;
    }
    case BodyScript: {
        L.SmallButton("Open in Script Editor");
        L.Section("Variables");
        const float nameCol = 200.0f;
        L.Label("alive", 0, true); L.SameLine(-1.0f, nameCol); L.Check("##v", &v.vAlive);
        L.Label("speed", P::kInspOverriddenVar, true); L.SameLine(-1.0f, nameCol); L.Drag("##v", &v.vSpeed, 1, 0.05f); L.Last().width = -28.0f;
        L.SameLine(); L.SmallButton("o");
        L.Label("jump", 0, true); L.SameLine(-1.0f, nameCol); L.Slider("##v", &v.vJump, 0.0f, 1.0f); L.Last().width = -28.0f;
        L.Label("target", 0, true); L.SameLine(-1.0f, nameCol); L.Drag("##v", v.vTarget, 3, 0.05f); L.Last().width = -28.0f;
        L.Label("title", 0, true); L.SameLine(-1.0f, nameCol); L.Text("##v", &v.vName); L.Last().width = -28.0f;
        L.Label("follow", 0, true); L.SameLine(-1.0f, nameCol); L.Button("Player##objslot", false, -28.0f); L.Last().dropKind = "objectname";
        break;
    }
    case BodyMeshRest: {
        L.Text("Mesh File##mesh", &v.meshFile); L.SameLine(); L.SmallButton("Load##obj");
        L.Text("##meshtex", &v.texture); L.Last().pick = P::InspPickList; L.Last().dropKind = ""; L.Last().width = 210.0f;
        L.SameLine(4.0f); L.Button("...##meshtexbrowse"); L.SameLine(4.0f); L.Label("Texture");
        L.SameLineOrWrap(); L.SmallButton("Procedural##tex"); L.Last().pick = P::InspPickGrid; L.Last().pickKind = "proc"; L.Last().s = &v.procPick;
        if (!v.texture.empty()) {
            L.SameLineOrWrap(); L.SmallButton("Clear##tex");
            L.Drag("Tiling##mesh", v.tiling, 2, 0.05f, 0.01f, 64.0f);
            L.Drag("Offset##mesh", v.texOffset, 2, 0.01f);
            L.Combo("Filter##mesh", &v.texFilter, kFilters, 2);
            L.Drag("UV Scroll##mesh", v.uvScroll, 2, 0.01f);
            L.Check("Triplanar##mesh", &v.triplanar);
        }
        L.Text("Normal Map##mesh", &v.normalMap); L.Last().pick = P::InspPickList; L.Last().dropKind = "";
        L.Slider("Reflectivity##mesh", &v.reflect, 0.0f, 1.0f);
        L.Slider("Metallic##mesh", &v.metallic, 0.0f, 1.0f);
        L.Text("Specular Map##mesh", &v.specMap); L.Last().pick = P::InspPickList;
        L.Text("Occlusion Map##mesh", &v.aoMap); L.Last().pick = P::InspPickList;
        L.Text("Metal / Rough##mesh", &v.mrMap); L.Last().pick = P::InspPickList;
        L.Text("Emissive Map##mesh", &v.emisMap); L.Last().pick = P::InspPickList;
        L.Note("117 verts, 192 triangles");
        L.SmallButton("Subdivide##mesh"); L.SameLine(); L.SmallButton("Subdiv+Smooth##mesh"); L.SameLine(); L.SmallButton("Smooth##mesh");
        L.SameLine(); L.SmallButton("Weld##mesh");
        L.SmallButton("Recenter##mesh"); L.SameLine(); L.SmallButton("Ground##mesh"); L.SameLine(); L.SmallButton("Fit 1u##mesh");
        L.SameLine(); L.SmallButton("Remove##mesh");
        L.Section("Material preset");
        L.Text("##matpath", &v.matPath); L.Last().pick = P::InspPickList; L.Last().dropKind = "";
        L.SameLine(); L.SmallButton("Save##mat"); L.SameLine(); L.SmallButton("Load##mat");
        L.SmallButton("Add Physics (Rigidbody + Box)##mesh");
        break;
    }
    case BodyGeneric: {
        L.Group("Playback");
        L.Asset("Clip##clip", &v.clip, "audio", "audio"); L.TipLookup();
        L.Check("Play On Awake##playOnAwake", &v.playOnAwake); L.TipLookup();
        L.Check("Loop##loop", &v.loop); L.TipLookup();
        L.Drag("Volume##volume", &v.volume, 1, 1.0f / 300.0f, 0.0f, 1.0f); L.TipLookup();
        L.DragInt("Priority##priority", &v.priority, 0.2f, 0, 256); L.TipLookup();
        L.Group("Space");
        L.Drag("Position##pos3", v.pos3, 3, 0.01f); L.Last().axisTags = true; L.Last().mixedAxes = 2u; L.TipLookup();
        L.Color("Tint##tint", v.tint, 4); L.TipLookup();
        L.Combo("Rolloff##rolloff", &v.rolloff, kRolloffs, 3); L.TipLookup();
        L.Asset("Follow##follow", &v.follow, "object", "object");
        L.Text("Note##note", &v.note); L.TipLookup();
        break;
    }
    case BodySprite: {
        L.Color("Color##sprite", v.spColor, 4);
        L.Drag("Size", v.spSize, 2, 0.05f, 0.0f, 1000.0f);
        L.Text("##spritetex", &v.spTex); L.Last().pick = P::InspPickList; L.Last().dropKind = ""; L.Last().width = 210.0f; L.TipLookup();
        L.SameLine(4.0f); L.Button("...##spritetexbrowse");
        L.SameLine(4.0f); L.Label("Texture");
        L.DragInt("Sorting Layer##sprite", &v.spLayer, 0.05f, -100, 100); L.TipLookup();
        L.DragInt("Sort Order##sprite", &v.spOrder, 0.1f, -1000, 1000); L.TipLookup();
        L.Check("Flip X##sprite", &v.spFlipX); L.SameLine(); L.Check("Flip Y##sprite", &v.spFlipY);
        L.Combo("Filter##sprite", &v.spFilter, kFilters, 2);
        L.Note("image file (PNG/JPG); higher Sorting Layer always on top, then Sort Order", false, true);
        L.SmallButton("Remove##sprite");
        break;
    }
    case BodyAudio: {
        L.Drag("Volume", &v.auVol, 1, 0.01f, 0.0f, 1.0f); L.TipLookup();
        L.Drag("Pitch##audio", &v.auPitch, 1, 0.01f, 0.1f, 3.0f);
        L.Slider("Pan##audio", &v.auPan, -1.0f, 1.0f, "%.2f");
        L.Combo("Group##audio", &v.auGroup, kGroups, 4);
        L.Slider("Low Pass##audio", &v.auLowPass, 200.0f, 22000.0f, "%.0f Hz", true);
        L.Check("Loop", &v.auLoop); L.SameLine(); L.Check("Play On Awake", &v.auAwake); L.SameLine(); L.Check("Stream##audio", &v.auStream);
        L.Button("Beep"); L.SameLine(); L.Button("Noise"); L.SameLine(); L.Button("Play"); L.SameLine(); L.Button("Stop##audio");
        L.Text("##audiowav", &v.auPath); L.Last().pick = P::InspPickList; L.Last().dropKind = ""; L.Last().width = 210.0f;
        L.SameLine(4.0f); L.Button("...##audiobrowse"); L.SameLine(4.0f); L.Label("Audio File");
        L.SameLine(); L.SmallButton("Load##audiowavload");
        L.Note("WAV, OGG, MP3 or FLAC; loads on Play and in the built game; 12.50s clip, stereo", false, true);
        L.Check("3D (spatial)", &v.auSpatial);
        L.Drag("Min Distance##audio", &v.auMin, 1, 0.1f, 0.0f, 1000.0f);
        L.Drag("Max Distance##audio", &v.auMax, 1, 0.1f, 0.0f, 5000.0f);
        L.Combo("Rolloff##audio", &v.auRoll, kRolls3, 3);
        L.Separator();
        L.Check("Pitch follows speed", &v.auDoppler);
        L.Check("Quieter behind walls", &v.auOcclude);
        if (v.auOcclude) {
            L.Slider("Loudness behind a wall", &v.auOccVol, 0.0f, 1.0f, "%.2f");
            L.Slider("Muffle behind a wall", &v.auOccLp, 80.0f, 8000.0f, "%.0f Hz", true);
        }
        L.Check("Muffle with distance", &v.auDistLp);
        L.SmallButton("Remove##audio");
        break;
    }
    case BodyBox2D: {
        L.Drag("Size##bc", v.b2Size, 2, 0.05f, 0.0f, 1000.0f);
        L.Drag("Offset##bc", v.b2Off, 2, 0.05f);
        L.Check("Trigger##bc", &v.b2Trigger); L.SameLine(); L.DragInt("Layer##bc", &v.b2Layer, 0.1f, 0, 31); L.Last().width = 80.0f;
        L.Tree("Physics Material##pmbc");
        L.Check("Own Friction##pmbc", &v.b2OwnF);
        L.Check("Own Bounciness##pmbc", &v.b2OwnB);
        L.TreeEnd();
        L.SmallButton("Fit to Object##bc"); L.SameLine(); L.Check("Auto Fit##bc", &v.b2Auto); L.TipLookup();
        L.SmallButton("Remove##bc");
        break;
    }
    case BodyEdge2D: {
        L.Note("Open polyline: ground, slopes, walls. Local-space points.");
        L.Drag("Offset##ec2", v.e2Off, 2, 0.05f);
        L.Check("Trigger##ec2", &v.trigger); L.SameLine(); L.DragInt("Layer##ec2", &v.layer, 0.1f, 0, 31); L.Last().width = 80.0f;
        L.Tree("Physics Material##pmec2");
        L.Check("Own Friction##pmec2", &v.b2OwnF);
        L.Check("Own Bounciness##pmec2", &v.b2OwnB);
        L.TreeEnd();
        if (v.trigger) {
            L.Text("Only Tag##tfec2", &v.onlyTag, "any tag (e.g. Player)");
            L.Text("Only Layers##tfec2", &v.onlyLayers, "all layers");
        }
        L.Check("One-Way##ec2", &v.e2OneWay);
        if (v.e2OneWay) L.Drag("Block Normal##ec2", v.e2Normal, 2, 0.05f);
        L.Separator();
        L.Label("Points (3)");
        for (int i = 0; i < 3; ++i) { L.Drag("##pt", v.e2Pts[i], 2, 0.05f); L.SameLine(); L.SmallButton("x"); }
        L.SmallButton("+ Point##ec2");
        L.SmallButton("Remove##ec2");
        break;
    }
    case BodyParticles: {
        L.Section("Presets");
        L.Combo("Preset##ps", &v.psPreset, kPresets, 8); L.Last().preview = "Choose a preset...";
        L.Note("Starting points \xE2\x80\x94 every value below stays editable.");
        L.Section("Emission");
        L.Check("Playing##ps", &v.psPlaying); L.SameLine(); L.Note("(37 live)");
        L.Drag("Rate / sec##ps", &v.psRate, 1, 0.5f, 0.0f, 10000.0f);
        L.DragInt("Max Particles##ps", &v.psMax, 1.0f, 0, 100000);
        L.Section("Shape");
        L.Combo("Shape##ps", &v.psShape, kShapes, 6);
        if (v.psShape == 4) L.Slider("Cone Angle##ps", &v.psAngle, 0.0f, 180.0f);
        L.Section("Start");
        L.Drag("Lifetime##ps", &v.psLife, 1, 0.05f, 0.01f, 1000.0f);
        L.Drag("Size##ps", &v.psSize, 1, 0.01f, 0.0f, 1000.0f);
        L.Color("Start Color##ps", v.psStartCol, 4);
        L.Drag("Velocity##ps", v.psVel, 3, 0.05f);
        L.Section("Over Lifetime");
        L.Check("Fade Out##ps", &v.psFade);
        L.Check("Color Over Life##ps", &v.psColLife);
        if (v.psColLife) { L.Combo("Color Mode##ps", &v.psCMode, kCModes, 2); L.Color("End Color##ps", v.psEndCol, 4); }
        L.Section("Bursts & Duration");
        L.Check("Loop##ps", &v.loop); L.SameLine(); L.SmallButton("Restart##ps"); L.SameLine(); L.SmallButton("Burst##ps");
        L.DragInt("Seed##ps", &v.psSeed, 1.0f, 0, 2000000000);
        L.SmallButton("Remove##ps");
        break;
    }
    case BodyController3D: {
        L.Tree("Crouch / prone##cc3"); L.Last().on = true;
        L.Combo("Crouch Key##stcc3", &v.c3Crouch, kKeys, 9); L.Last().width = 96.0f;
        L.Combo("Prone Key##stcc3", &v.c3Prone, kKeys, 9); L.Last().width = 96.0f;
        L.Check("Toggle (vs hold)##stcc3", &v.c3Toggle);
        L.Check("Allow Prone##stcc3", &v.c3Prone2);
        L.Check("Resize Body##stcc3", &v.c3Resize);
        L.Drag("Crouch Speed##stcc3", &v.c3CrouchSpeed, 1, 0.05f, 0.0f, 20.0f);
        L.Slider("Crouch Height##stcc3", &v.c3CrouchH, 0.2f, 1.0f, "%.2f");
        L.Note("Scripts: SetStance(0 stand / 1 crouch / 2 prone).", false, true);
        L.Note("How long changes take: the Animator's Stance changes.", false, true);
        L.Note("Line one.\nLine two (a note with a line break).");
        L.TreeEnd();
        L.Drag("Speed##cc3", &v.c3Speed, 1, 0.1f, 0.0f, 200.0f);
        L.Check("Can Jump##cc3", &v.c3Jump);
        if (v.c3Jump) L.Drag("Jump Force##cc3", &v.c3JumpF, 1, 0.1f, 0.0f, 200.0f);
        L.Note("WASD / arrows on XZ. Uses a Rigidbody3D if present.");
        L.SmallButton("Remove##cc3");
        break;
    }
    case BodyAnimator: {
        L.Text("Clip##anm", &v.anClip);
        L.Check("Loop##anm", &v.anLoop); L.SameLine(); L.Check("Playing##anm", &v.anPlaying);
        L.Drag("Speed##anm", &v.anSpeed, 1, 0.02f, -4.0f, 4.0f);
        L.Slider("Time##anm", &v.anTime, 0.0f, 1.0f, "%.2fs"); L.Last().width = -165.0f;   // the editor's AnimTimeRoom at this face
        L.SameLine(); L.SmallButton(v.anPlaying ? "Pause" : "Play"); L.SameLine(); L.SmallButton("|<");
        L.Section("Keyframes");
        L.Note("Scrub to a time, pose the object, then Record.");
        L.Button("Record Key (transform @ time)");
        L.Bullet("position.x  (3 keys)"); L.SameLine(); L.SmallButton("Clear");
        L.Bullet("position.y  (3 keys)"); L.SameLine(); L.SmallButton("Clear");
        L.Bullet("rotation.z  (2 keys)"); L.SameLine(); L.SmallButton("Clear");
        L.Spacing();
        L.SmallButton("Remove##anm-comp");
        break;
    }
    case BodyText: {
        StyleBar(L, kTextLooks, 6);
        L.Check("Localized##txtloc", &v.txLoc);
        L.Text("##txtbody", &v.txText); L.Last().wrap = true; L.Last().width = -1.0f; L.Last().texH = 3.0f;
        L.Color("Color##txt", v.txColor, 4); L.TipLookup();
        for (int i = 0; i < 6; ++i) { if (i) L.SameLine(4.0f); L.Swatch("##txtc", kTxtCols[i][0], kTxtCols[i][1], kTxtCols[i][2], 1.0f, 20.0f, 16.0f); L.Tip(kTxtColNames[i]); }
        L.Drag("Font Size##txt", &v.txSize, 1, 0.1f, 0.001f, 100.0f);
        L.Check("Best Fit (auto-size to box)##txt", &v.txFit);
        L.Check("Screen Space (UI)##txt", &v.txScreen); L.TipLookup();
        L.Drag("Box Size##txt", v.txBox, 2, 1.0f, 1.0f, 4000.0f);
        L.SmallButton("Fit to Text##txt");
        L.Drag("Offset##txt", v.txOff, 2, 1.0f);
        L.Anchor("Anchor##txt", &v.txAnchor);
        L.Combo("Align##txt", &v.txAlign, kAligns, 3);
        L.Combo("V-Align##txt", &v.txVAlign, kVAligns, 3);
        L.Check("Background##txt", &v.txBg);
        if (v.txBg) L.Color("BG Color##txt", v.txBgCol, 4);
        L.Check("Shadow##txt", &v.txShadow);
        if (v.txShadow) { L.Color("Shadow Color##txt", v.txShCol, 4); L.Drag("Shadow Offset##txt", v.txShOff, 2, 0.1f); }
        L.Check("Outline##txt", &v.txOutline);
        L.Check("Bold##txt", &v.txBold); L.SameLine(); L.Check("Italic##txt", &v.txItalic); L.SameLine(); L.Check("UPPERCASE##txt", &v.txUpper);
        L.Check("Gradient##txt", &v.txGrad);
        L.DragInt("Visible Chars##txt", &v.txVisible, 0.5f, -1, 100000);
        L.Drag("Type Speed##txt", &v.txType, 1, 0.5f, 0.0f, 200.0f, "%.0f cps");
        L.SameLine(); L.Check("Wrap##txt", &v.txWrap);
        L.Check("Rich Text##txt", &v.txRich);
        L.Drag("Letter Spacing##txt", &v.txLetter, 1, 0.1f, -4.0f, 32.0f);
        L.Drag("Line Spacing##txt", &v.txLine, 1, 0.1f, -4.0f, 32.0f);
        L.Label("Font", 0, true); L.SameLine(); L.Slot("txtfont", &v.txFont, "Font", "font");
        L.Note("Built-in 8x8 bitmap font.");
        L.SmallButton("Remove##txt");
        break;
    }
    case BodyCanvas: {
        L.Check("Visible##cv", &v.cvVisible); L.SameLine(); L.Slider("Opacity##cv", &v.cvOpacity, 0.0f, 1.0f); L.Last().width = 150.0f;
        L.Separator();
        L.Combo("UI Scale Mode##cv", &v.cvMode, kCvModes, 2);
        L.Drag("Reference Res##cv", v.cvRef, 2, 1.0f, 1.0f, 8000.0f);
        L.Note("Presets:"); for (const char* n : {"1080p", "720p", "1440p"}) { L.SameLine(); L.SmallButton(n); }
        L.Note("Portrait:"); for (const char* n : {"9:16", "phone"}) { L.SameLine(); L.SmallButton(n); }
        L.Combo("Screen Match##cv", &v.cvMatch, kCvMatches, 3);
        L.Slider("Match W/H##cv", &v.cvMatchWH, 0.0f, 1.0f, "%.2f");
        L.SmallButton("Width##cvm"); L.SameLine(); L.SmallButton("Match##cvm"); L.SameLine(); L.SmallButton("Height##cvm");
        L.Drag("Scale Factor##cv", &v.cvScale, 1, 0.01f, 0.05f, 10.0f, "%.2fx");
        L.DragInt("Sort Order##cv", &v.cvSort, 1.0f);
        L.Section("World Space (3D)");
        L.Check("World Space##cv", &v.cvWorld);
        L.Section("Phones");
        L.Button("Add Safe Area##cv");
        L.Note("Pick a phone in the Game view to preview it.");
        L.Note("Current scale: 0.67x  (canvas 1280 x 720)");
        L.SmallButton("Remove##cv");
        break;
    }
    case BodyButton: {
        StyleBar(L, kButtonLooks, 6);
        L.Check("Localized##uibloc", &v.txLoc);
        L.Text("Label##uib", &v.btLabel); L.SameLine(); L.SmallButton("Text as Child##uib");
        L.Drag("Pos (px)##uib", v.btPos, 2, 1.0f); L.Drag("Size (px)##uib", v.btSize, 2, 1.0f, 1.0f, 4000.0f);
        L.Color("Color##uib", v.btColor, 4); L.Color("Hover##uib", v.btHover, 4); L.Color("Pressed##uib", v.btPressed, 4); L.Color("Disabled##uib", v.btDisabled, 4);
        L.Check("Interactable##uib", &v.btInter); L.SameLine(); L.Check("Focusable##uib", &v.btFocus);
        L.Note("calls the script's on_click(); disabled buttons are greyed out", false, true);
        L.Section("On Click (assign a function)");
        L.Text("Target Object##uibclk", &v.btTarget); L.Last().dropKind = "objectname";
        L.Combo("Function##uibclk", &v.btFn, kBtnFns, 5); L.Last().heads = kBtnFnHeads;
        L.Anchor("Anchor##uib", &v.btAnchor);
        L.Section("Style");
        L.Combo("Shape##uib", &v.btShape, kUIShapes, 9);
        L.Drag("Corner Radius##uib", &v.btRadius, 1, 0.2f, 0.0f, 64.0f);
        L.Drag("Font Scale##uib", &v.btFontScale, 1, 0.05f, 0.5f, 16.0f);
        L.Label("Font", 0, true); L.SameLine(); L.Slot("uibfont", &v.btFont, "Font", "font");
        L.Check("Drop Shadow##uib", &v.btShadow);
        L.Drag("Border Width##uib", &v.btBorder, 1, 0.1f, 0.0f, 16.0f);
        L.Drag("Hover Grow##uib", &v.btGrow, 1, 0.01f, 1.0f, 2.0f);
        L.Color("Hover Text##uib", v.btHoverText, 4);
        L.Drag("Transition Speed##uib", &v.btSpeed, 1, 0.2f, 0.0f, 30.0f);
        L.Combo("Ease##uib", &v.btEase, kEases, 4);
        L.Drag("Transition Time##uib", &v.btTime, 1, 0.01f, 0.0f, 3.0f);
        L.Check("Own Focus Color##uib", &v.btFocusCol);
        L.Drag("Press Offset##uib", &v.btPress, 1, 0.1f, 0.0f, 16.0f);
        L.Text("Icon##uib", &v.btIcon);
        L.Drag("Icon Size##uib", &v.btIconSize, 1, 0.5f, 0.0f, 256.0f);
        L.Check("Icon on Right##uib", &v.btIconRight);
        L.Section("Behavior");
        L.Check("Toggle Button##uib", &v.btToggle);
        L.Check("Hold to Repeat##uib", &v.btRepeat);
        L.SmallButton("Remove##uib");
        break;
    }
    case BodyImage: {
        L.Anchor("Anchor##uim", &v.imAnchor);
        L.Drag("Left##uim_px", &v.imPos[0], 1, 1.0f); L.Drag("Top##uim_py", &v.imPos[1], 1, 1.0f);
        L.Drag("Right##uim_sx", &v.imSize[0], 1, 1.0f); L.Drag("Bottom##uim_sy", &v.imSize[1], 1, 1.0f);
        L.Note("Margins from the canvas edges (px)");
        L.Label("Image", 0, true); L.SameLine(-1.0f, 64.0f); L.Slot("uimtex", &v.imTex, "Image", "image");
        L.Color("Tint##uim", v.imTint, 4);
        L.Note("No image: it draws as a coloured shape.");
        L.Section("Texture");
        L.Check("Flip X##uim", &v.imFlipX); L.SameLine(); L.Check("Flip Y##uim", &v.imFlipY);
        L.Check("Preserve Aspect##uim", &v.imAspect);
        L.Check("Nine-slice##uim", &v.imNine);
        L.Section("Shape & Frame");
        L.Combo("Shape##uim", &v.imShape, kUIShapes, 9);
        L.Drag("Corner Radius##uim", &v.imRadius, 1, 0.2f, 0.0f, 64.0f);
        L.Note("Rounded corners");
        L.Check("TL##uim_c1", &v.imTL); L.SameLine(); L.Check("TR##uim_c2", &v.imTR);
        L.Check("BL##uim_c8", &v.imBL); L.SameLine(); L.Check("BR##uim_c4", &v.imBR); L.SameLine(); L.SmallButton("All##uim_call");
        L.Drag("Frame Width##uim", &v.imFrame, 1, 0.1f, 0.0f, 24.0f);
        L.Check("Drop Shadow##uim", &v.imShadow);
        L.SmallButton("Remove##uim");
        break;
    }
    case BodySlider: {
        StyleBar(L, kButtonLooks, 4);
        L.Drag("Pos (px)##usl", v.slPos, 2, 1.0f); L.Drag("Size (px)##usl", v.slSize, 2, 1.0f, 1.0f, 8000.0f);
        L.Drag("Min##usl", &v.slMin, 1, 0.1f); L.Drag("Max##usl", &v.slMax, 1, 0.1f);
        L.Slider("Value##usl", &v.slValue, v.slMin, v.slMax);
        L.Color("Track##usl", v.slTrack, 4); L.Color("Fill##usl", v.slFill, 4); L.Color("Knob##usl", v.slKnob, 4);
        L.Note("drag in the built game; calls script on_change()");
        L.Anchor("Anchor##usl", &v.slAnchor);
        L.Combo("Track Shape##usl", &v.slShape, kUIShapes, 9);
        L.Drag("Corner Radius##usl", &v.slRadius, 1, 0.2f, 0.0f, 64.0f);
        L.Check("Round Knob##usl", &v.slRound);
        L.Drag("Knob Size##usl", &v.slKnobSize, 1, 0.02f, 0.1f, 3.0f);
        L.Check("Show Value##usl", &v.slShow); L.SameLine(); L.Check("Whole Numbers##usl", &v.slWhole);
        L.Color("Text Color##usl", v.slText, 4);
        L.Check("Interactable##usl", &v.slInter); L.SameLine(); L.Check("Vertical##usl", &v.slVert);
        L.Drag("Step##usl", &v.slStep, 1, 0.01f, 0.0f, 1000.0f);
        L.Text("Value Format##usl", &v.slFmt);
        L.Check("Fill Gradient##usl", &v.slGrad);
        L.Combo("Handle##usl", &v.slHandle, kHandles, 6);
        L.SmallButton("Remove##usl");
        break;
    }
    case BodyToggle: {
        StyleBar(L, kButtonLooks, 3);
        L.Check("Localized##utgloc", &v.txLoc);
        L.Text("Label##utg", &v.tgLabel);
        L.Check("On##utg", &v.tgOn);
        L.Drag("Pos (px)##utg", v.tgPos, 2, 1.0f); L.Drag("Size (px)##utg", v.tgSize, 2, 1.0f, 1.0f, 4000.0f);
        L.Color("Box##utg", v.tgBox, 4); L.Color("Check##utg", v.tgCheck, 4); L.Color("Label Color##utg", v.tgText, 4);
        L.Note("click in the built game; calls script on_toggle()");
        L.Anchor("Anchor##utg", &v.tgAnchor);
        L.Drag("Corner Radius##utg", &v.tgRadius, 1, 0.2f, 0.0f, 64.0f);
        L.Combo("Style##utg", &v.tgStyle, kTgStyles, 3);
        if (v.tgStyle == 1) L.Color("Knob##utg", v.tgKnob, 4);
        L.Drag("Anim Speed##utg", &v.tgAnim, 1, 0.2f, 0.0f, 40.0f);
        L.Check("Interactable##utg", &v.tgInter);
        L.SmallButton("Remove##utg");
        break;
    }
    case BodyInput: {
        StyleBar(L, kButtonLooks, 4);
        L.Text("Text##uif", &v.inText);
        L.Check("Localized##uifloc", &v.txLoc);
        L.Text("Placeholder##uif", &v.inPlace);
        L.Drag("Pos (px)##uif", v.inPos, 2, 1.0f); L.Drag("Size (px)##uif", v.inSize, 2, 1.0f, 8.0f, 4000.0f);
        L.Color("Color##uif", v.inColor, 4);
        L.DragInt("Max Length##uif", &v.inMax, 1.0f, 1, 1024);
        L.Combo("Content Type##uif", &v.inType, kInTypes, 4);
        L.Anchor("Anchor##uif", &v.inAnchor);
        L.Combo("Shape##uif", &v.inShape, kUIShapes, 9);
        L.Drag("Corner Radius##uif", &v.inRadius, 1, 0.2f, 0.0f, 64.0f);
        L.Drag("Focus Ring##uif", &v.inRing, 1, 0.1f, 0.0f, 12.0f);
        L.Color("Ring Color##uif", v.inRingCol, 4);
        L.Note("Click to focus + type at runtime; Enter calls on_submit().");
        L.Check("Multiline##uif", &v.inMulti);
        L.SmallButton("Remove##uif");
        break;
    }
    case BodyLayout: {
        L.Combo("Direction##lg", &v.lgDir, kLgDirs, 5);
        L.Drag("Box Size##lgf", v.lgBox, 2, 1.0f, 1.0f, 8000.0f);
        L.Combo("Justify##lgf", &v.lgJustify, kLgJustify, 6);
        L.Combo("Align##lgf", &v.lgAlign, kLgAlign, 4);
        L.Check("Wrap##lgf", &v.lgWrap);
        L.Note("Children with a UI Flex Item grow and shrink to fill the box.");
        L.Drag("Origin##lg", v.lgOrigin, 2, 1.0f);
        L.Drag("Spacing##lg", &v.lgSpacing, 1, 0.5f, 0.0f, 400.0f);
        L.Drag("Padding##lg", &v.lgPadding, 1, 0.5f, 0.0f, 400.0f);
        L.Anchor("Anchor##lg", &v.lgAnchor);
        L.SmallButton("Arrange Now##lg"); L.SameLine(); L.Note("content: 412 px");
        L.SmallButton("Remove##lg");
        break;
    }
    case BodyNavAgent: {
        L.Drag("Radius##nava", &v.naRadius, 1, 0.01f, 0.05f, 5.0f, "%.2f");
        L.Drag("Height##nava", &v.naHeight, 1, 0.05f, 0.1f, 10.0f, "%.2f");
        L.Drag("Speed##nava", &v.naSpeed, 1, 0.1f, 0.0f, 50.0f, "%.2f");
        L.Drag("Acceleration##nava", &v.naAccel, 1, 0.1f, 0.0f, 100.0f, "%.1f");
        L.Drag("Angular Speed##nava", &v.naAngular, 1, 1.0f, 0.0f, 3600.0f, "%.0f deg/s");
        L.Drag("Stopping Distance##nava", &v.naStop, 1, 0.01f, 0.0f, 10.0f, "%.2f");
        L.Check("Auto Repath##nava", &v.naRepathOn);
        if (v.naRepathOn) L.Drag("Repath Interval##nava", &v.naRepath, 1, 0.05f, 0.05f, 10.0f, "%.2f s");
        L.SliderInt("Avoidance Priority##nava", &v.naPriority, 0, 99);
        L.Drag("Avoidance Range##nava", &v.naRange, 1, 0.1f, 0.0f, 20.0f, "%.1f");
        L.Check("Auto Braking##nava", &v.naBrake);
        L.Check("Face Movement##nava", &v.naFace);
        L.Check("Drive Animation##nava", &v.naDrive);
        L.Mask("Area Mask##nava", &v.naMask);
        { P::InspRow& r = L.Last(); r.count = 8; r.items = kAreaNames; r.presetNames = kAreaPresets; r.presetBits = kAreaPresetBits; r.presetCount = 2;
          r.preview = v.naMask == (int)0xFFFFFFFDu ? "All but Not Walkable" : v.naMask == -1 ? "Everything" : "Some areas"; }
        L.Drag("Base Offset##nava", &v.naOffset, 1, 0.01f, -5.0f, 5.0f, "%.2f");
        L.SmallButton("Send To Click (Scene view)##nava");
        L.SmallButton("Remove##navagent");
        break;
    }
    case BodyTerrain: {
        L.Color("Color##terr", v.teColor, 4);
        L.DragInt("Resolution##terr", &v.teRes, 1.0f, 4, 2048);
        L.Drag("Size##terr", &v.teSize, 1, 1.0f, 2.0f, 1000000.0f);
        L.Section("Texturing");
        L.Text("Ground Texture##terr", &v.teTex); L.Last().pick = P::InspPickList; L.Last().dropKind = "";
        L.SameLine(); L.SmallButton("Clear##terrtex");
        L.Drag("Tiling##terr", &v.teTiling, 1, 0.1f, 0.1f, 200.0f);
        L.Check("Triplanar (no cliff smear)##terr", &v.teTri);
        L.Text("Normal Map##terr", &v.teNrm); L.Last().pick = P::InspPickList; L.Last().dropKind = "";
        L.Check("Auto-color layers##terr", &v.teAuto);
        L.Section("Sculpt brush (drag in the 3D view)");
        L.Check("Sculpt##terr", &v.teSculpt); L.SameLine(); L.Note("(Shift = lower/invert)");
        L.Combo("Mode##terrbr", &v.teBrush, kBrushes, 6); L.Last().width = 160.0f;
        L.Slider("Radius##terr", &v.teRadius, 0.5f, 30.0f);
        L.Slider("Strength##terr", &v.teStrength, 0.1f, 20.0f);
        L.Slider("Hardness##terr", &v.teHard, 0.0f, 1.0f);
        L.Section("Generate (procedural noise)");
        L.Combo("Type##terrgen", &v.teGen, kGens, 7); L.Last().width = 160.0f;
        L.Slider("Amplitude##terrgen", &v.teAmp, 1.0f, 60.0f);
        L.Slider("Frequency##terrgen", &v.teFreq, 0.5f, 12.0f);
        L.SliderInt("Detail##terrgen", &v.teOct, 1, 8);
        L.Button("Generate##terrgen"); L.SameLine(); L.Button("Flatten##terr"); L.SameLine(); L.Button("Smooth All##terr");
        L.Section("Erosion (geological realism)");
        L.Note("Carve valleys & ridgelines into generated terrain.");
        L.SliderInt("Rain drops##tero", &v.teDrops, 1000, 200000);
        L.Slider("Erode strength##tero", &v.teErode, 0.05f, 1.0f);
        L.Button("Hydraulic Erode##tero");
        L.SliderInt("Thermal passes##tero", &v.teTherm, 1, 100);
        L.Slider("Talus angle##tero", &v.teTalus, 0.2f, 6.0f);
        L.Button("Thermal Erode##tero");
        L.SliderInt("Terrace bands##tero", &v.teTerrace, 2, 20); L.SameLineOrWrap(); L.Button("Terrace##tero");
        L.Range("Range##ternorm", v.teNorm, 0.1f, -100.0f, 100.0f, "Low %.1f", "High %.1f");
        L.SameLineOrWrap(); L.Button("Normalize##ternorm"); L.SameLineOrWrap(); L.Button("Invert##terinv");
        L.Section("Heightmap (PNG import / export)");
        L.Button("Import PNG...##terhm"); L.SameLine(); L.Button("Export PNG...##terhm");
        L.Range("Import Low/High##terhm", v.teHm, 0.2f, -100.0f, 200.0f, "%.1f");
        L.Section("Layers (auto-color by height & slope)");
        L.Check("Auto Color##terr", &v.teAuto);
        L.Color("Water##terr", v.teWater, 3); L.Last().noInputs = true; L.SameLine();
        L.Color("Sand##terr", v.teSand, 3); L.Last().noInputs = true; L.SameLine();
        L.Color("Grass##terr", v.teGrass, 3); L.Last().noInputs = true;
        L.Color("Rock##terr", v.teRock, 3); L.Last().noInputs = true; L.SameLine();
        L.Color("Snow##terr", v.teSnow, 3); L.Last().noInputs = true;
        L.Slider("Water Level##terr", &v.teWaterL, -20.0f, 40.0f);
        L.Slider("Snow Level##terr", &v.teSnowL, 0.0f, 80.0f);
        L.Slider("Rock Slope##terr", &v.teRockS, 0.1f, 1.0f);
        L.Group("Texture layers (paint in the 3D view)");
        for (int i = 0; i < 2; ++i) {
            L.Radio("##paintlayer", &v.teLayer, i);
            L.SameLine(); L.Text("##lname", &v.teLName[i]); L.Last().width = 110.0f;
            L.SameLine(); L.Color("##ltint", v.teLTint[i], 3); L.Last().noInputs = true;
            L.SameLine(); L.Drag("##ltile", &v.teLTile[i], 1, 0.1f, 0.1f, 400.0f, "x%.1f"); L.Last().width = 70.0f;
            L.SameLine(); L.SmallButton("Remove");
            L.Text("Texture##ltex", &v.teLTex[i]); L.Last().pick = P::InspPickList; L.Last().pickKind = "image"; L.Last().dropKind = "image";
        }
        L.Button("Add Layer##tl"); L.SameLine(); L.Button("Fill With Selected##tl");
        L.Button("Remove All Layers##tl");
        L.Combo("Detail (px)##tl", &v.teDetail, kSplat, 4); L.Last().width = 100.0f;
        L.Check("Paint layers in the 3D view##tl", &v.tePaint);
        L.Slider("Radius##tl", &v.teLRadius, 0.5f, 60.0f, "%.1f");
        L.Slider("Strength##tl", &v.teLStrength, 0.05f, 4.0f, "%.2f");
        L.Slider("Hardness##tl", &v.teLHard, 0.0f, 1.0f, "%.2f");
        L.Note("The layers replace the Ground Texture and the auto-colours above while any exist.", false, true);
        L.Group("Scatter props (trees, rocks, bushes)");
        L.Combo("Prop##scat", &v.teProp, kProps, 9); L.Last().width = 170.0f;
        L.SliderInt("Count##scat", &v.teCount, 1, 5000);
        L.Range("Scale##scat", v.teScale, 0.02f, 0.1f, 6.0f, "%.2f", "%.2f");
        L.Slider("Max Slope##scat", &v.teSlope, 0.0f, 89.0f, "%.0f deg");
        L.Range("Height Band##scat", v.teBand, 0.2f, -100.0f, 100.0f, "%.1f", "%.1f");
        L.Button("Scatter##scat"); L.SameLine(); L.Button("Clear Scattered##scat");
        L.Button("Add Foliage Layer##scat");
        L.SmallButton("Remove##terr");
        break;
    }
    case BodyCurves: {
        L.Section("Over Lifetime");
        L.Check("Color Over Life##ps", &v.psColLife);
        int cm = 1; L.Combo("Color Mode##ps", &cm, kCModes, 2);
        L.Gradient("psgrad", v.gStops, &v.gCount, 8, &v.gSel);
        if (v.gSel >= 0 && v.gSel < v.gCount) {
            for (int c = 0; c < 4; ++c) v.gCol[c] = v.gStops[v.gSel * 5 + 1 + c];
            v.gTime = v.gStops[v.gSel * 5];
            L.Color("Stop Color", v.gCol, 4); L.Slider("Stop Time", &v.gTime, 0.0f, 1.0f);
        }
        L.Note(v.gCount == 3 ? "3 / 8 stops" : "other / 8 stops");
        bool on = true; int sm = 1;
        L.Check("Size Over Life##ps", &on);
        L.Combo("Size Mode##ps", &sm, kSModes, 2);
        L.Note("Size x start size over life");
        L.Curve("pssize", v.cKeys, &v.cCount, 8, &v.cSmooth, &v.cSel);
        if (v.cSel >= 0 && v.cSel < v.cCount) { v.cKey[0] = v.cKeys[v.cSel * 2]; v.cKey[1] = v.cKeys[v.cSel * 2 + 1]; L.Drag("Key (time, x)", v.cKey, 2, 0.01f, 0.0f, 100.0f); }
        L.Check("Smooth", &v.cSmooth); L.SameLine(); L.Note("3 / 8 keys");
        L.Check("Speed Over Life##ps", &on);
        L.Note("Speed x over life");
        L.Curve("psspeed", v.sKeys, &v.sCount, 8, &v.sSmooth, &v.sSel);
        if (v.sSel >= 0 && v.sSel < v.sCount) { v.sKey[0] = v.sKeys[v.sSel * 2]; v.sKey[1] = v.sKeys[v.sSel * 2 + 1]; L.Drag("Key (time, x)##sp", v.sKey, 2, 0.01f, 0.0f, 100.0f); }
        L.Check("Smooth##sp", &v.sSmooth); L.SameLine(); L.Note("2 / 8 keys");
        break;
    }
    default: break;
    }
    return L;
}

/// After a frame: the buttons pressed (by their visible label) go into v.clicked; a Mesh Renderer pick from the procedural grid becomes
/// the texture, as the editor's builder does.
inline void Apply(Body b, Values& v, P::InspFieldList& L) {
    for (P::InspRow& r : L.rows)
        if (r.clicked) v.clicked.push_back(std::string(r.label, P::InspVisibleLen(r.label)));
    if (b == BodyMeshRest)
        for (P::InspRow& r : L.rows)
            if (r.changed && r.kind == P::InspRowSmallButton && r.pick == P::InspPickGrid) v.texture = v.procPick;
}

} // namespace inspbody
