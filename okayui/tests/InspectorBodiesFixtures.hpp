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
                  BodyCurves,
                  // stage 5 (section 18): Procedural Object Holding, Holdable Object, Character, First-person settings, View toggle and the
                  // Animator state machine (two states, the first one's transitions open)
                  BodyObjHolding, BodyHoldable, BodyCharacter, BodyFirstPerson, BodyPlayerView, BodyAnimSM,
                  // stage 6 (section 19): Item Pickup (an unknown item id), Hold Prompt, Water, Flashlight, Pause Menu, Hand (legacy),
                  // Voxel Digger, Voxel Terrain (materials on)
                  BodyItemPickup, BodyHoldPrompt, BodyWater, BodyFlashlight, BodyPauseMenu, BodyFpHand, BodyVoxelDigger, BodyVoxelTerrain,
                  // stage 7 (section 20): Stats, Inventory (two slots, a weight limit), Camera Follow, World UI, Vehicle Seat, Vehicle
                  // Controller (Car), Joint 3D (a hinge with motor and limits)
                  BodyStats, BodyInventory, BodyCameraFollow, BodyWorldUI, BodyVehicleSeat, BodyVehicle, BodyJoint3D, BodyWorldSpaceUI, BodyCount };
inline const char* const kBodyNames[BodyCount] = {"light", "camera", "rigidbody", "boxcollider", "script", "meshrest", "generic",
                                                  "sprite", "audio", "box2d", "edge2d", "particles", "controller3d", "animator",
                                                  "text", "canvas", "uibutton", "uiimage", "uislider", "uitoggle", "uiinput", "layout",
                                                  "navagent", "terrain", "curves",
                                                  "objholding", "holdable", "character", "firstperson", "playerview", "animsm",
                                                  "itempickup", "holdprompt", "water", "flashlight", "pausemenu", "fphand", "voxeldigger",
                                                  "voxelterrain", "stats", "inventory", "camerafollow", "worldui", "vehicleseat", "vehicle",
                                                  "joint3d", "worldspaceui"};

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

    // stage 5: Procedural Object Holding
    int ohHand = 1, ohStyle = 0, ohCarry = 0, ohMouse = 1, ohPad = -1; bool ohIK = true, ohBody = true; float ohTrans = 0.25f;
    std::string ohKey = "f"; float ohCharge = 0.8f, ohMin = 4.0f, ohMax = 14.0f, ohRange = 2.0f;
    // Holdable Object (Automatic: the resolved style shown)
    int hoStyle = 0; bool hoSupport = false, hoTwo = false, hoLeft = true, hoRight = false; float hoWeight = 2.0f; std::string hoSocket;
    float hoOff[3] = {0.2f, -0.1f, 0.4f}, hoRot[3] = {0, 0, 0}, hoScale = 1.0f, hoFpOff[3] = {0, 0, 0}, hoFpRot[3] = {0, 0, 0}, hoFpScale = 1.0f;
    float hoTpOff[3] = {0, 0, 0}, hoTpRot[3] = {0, 0, 0}, hoGripPos[3] = {-0.1f, 0.0f, 0.0f}, hoGripRot[3] = {0, 90, 0};
    // Character (no custom model, two clips)
    float chHeight = 1.2f, chBlend = 0.2f, chAnimSpeed = 1.0f, chTurn = 8.0f; int chIdle = 1, chWalk = 0, chRun = 2, chShirt = 1, chLegs = 0, chHair = 0, chBeard = 0;
    int chGlasses = 0, chAnim = 0; bool chParts = false, chBelt = true, chJacket = false, chGloves = false, chHasHair = true, chHat = false, chMask = false;
    bool chScarf = false, chPads = false, chPack = false, chCape = false, chLook = false; std::string chLookAt;
    float chSkin[3] = {0.9f, 0.7f, 0.55f}, chShirtC[3] = {0.2f, 0.4f, 0.8f}, chPants[3] = {0.2f, 0.2f, 0.25f}, chShoes[3] = {0.1f, 0.1f, 0.1f};
    float chHairC[3] = {0.3f, 0.2f, 0.1f}, chEyes[3] = {0.1f, 0.3f, 0.6f};
    float chJump = 0.12f, chBend = 20.0f, chStepA = 60.0f, chStepT = 0.25f, chLift = 30.0f, chStill = 0.8f, chLean = 1.0f, chLeanMax = 8.0f;
    // First-person settings (owned by a Player Controller: no model tools, no natural-speeds button)
    float fpWalk = 1.4f, fpRun = 5.5f, fpJump = 5.0f, fpWalkA = 22.0f, fpRunA = 30.0f, fpDecel = 35.0f, fpTurn = 90.0f, fpAir = 0.2f, fpCoyote = 0.08f;
    float fpBuffer = 0.1f, fpSens = 0.15f, fpCapSens = 0.05f, fpCap = 40.0f; bool fpInvY = false, fpCanJump = true, fpDrive = true, fpFootIK = true, fpLock = true;
    int fpMaxJumps = 1, fpSprint = 1, fpCrouch = 3, fpProne = 4, fpPad = 0; bool fpToggleRun = false, fpToggleStance = false, fpAllowProne = true, fpResize = true;
    float fpCrouchS = 2.0f, fpProneS = 1.0f, fpCrouchH = 0.6f, fpProneH = 0.3f, fpHold = 0.4f, fpEyeS = 1.6f, fpEyeC = 1.0f, fpEyeP = 0.4f, fpEase = 10.0f;
    // View toggle (owned)
    bool pvAllow = true, pvSnap = false; int pvView = 1, pvKey = 6, pvPad = 0, pvEase = 1; float pvBlend = 0.35f, pvEye[3] = {0, 0, 0}, pvDist = -1.0f, pvShoulder = 0.0f;
    // the Animator state machine
    std::string smS0 = "Idle", smS1 = "Walk", smParam = "speed"; int smEntry = 0, smClip0 = 0, smClip1 = 1, smTo = 1, smCond = 1; bool smLoop0 = true, smLoop1 = true;
    float smSpd0 = 1.0f, smSpd1 = 1.0f, smVal = 0.1f, smBlendT = 0.2f, smSC = 0.3f, smCP = 0.5f, smPS = 0.8f, smIW = 0.2f, smWR = 0.2f, smGA = 0.1f, smAG = 0.1f;

    // stage 6
    std::string ipTyped;   // an id typed in the item list's search and used as it is
    int ipItem = -1, ipCount = 1; std::string ipTag = "Player", ipKey = "e"; bool ipTouch = false, ipHold = true; float ipAuto = 0.0f, ipRange = 3.0f, ipMagnet = 0.0f;
    std::string hpDrop = "e", hpAlt = "g", hpPrompt = "Hold Prompt"; bool hpLoc = false;
    float waCol[4] = {0.15f, 0.4f, 0.6f, 0.8f}, waSize = 50.0f, waDepth = 3.0f, waDens = 1000.0f, waDrag = 1.0f, waH = 0.3f, waL = 8.0f, waS = 1.0f, waRefl = 0.6f, waGlint = 0.5f;
    float waFlow[2] = {0.02f, 0.0f}; int waDetail = 32;
    std::string flKey = "f"; bool flOn = true; float flRange = 25.0f, flAngle = 40.0f, flInt = 2.0f, flCol[3] = {1.0f, 0.95f, 0.85f};
    bool pmLoc = false, pmResume = true, pmQuit = true; std::string pmTitle = "Paused", pmScene; int pmKey = 0; float pmDim[4] = {0, 0, 0, 0.5f}, pmPanel[4] = {0.1f, 0.1f, 0.15f, 0.9f};
    int fhBtn = 0, fhSide = 0; bool fhHold = true, fhFollow = true, fhBob = true, fhSteady = false, fhRun = false; float fhRaise = -96.0f, fhElbow = 16.0f;
    int vdMode = 0, vdBtn = 0, vdAbtn = 1, vdPreset = 0, vdCollect = 2, vdShape = 0, vdSpan = 6, vdPasses = 4, vdDebris = 120; float vdRad = 2.0f, vdStr = 4.0f, vdReach = 40.0f;
    bool vdBrush = true, vdNeeds = true, vdCreative = false, vdSnap = false, vdSlump = true, vdCollapse = true, vdGive = true, vdTools = false, vdBuild = false;
    float vdPower = 3.0f, vdHard = 1.0f, vdDepth = -1.0f, vdBed = -1.0f, vdLife = 20.0f; std::string vdPour = "f", vdHud;
    float vtCol[4] = {0.5f, 0.45f, 0.4f, 1.0f}, vtDims[3] = {64, 32, 64}, vtSize = 1.0f, vtSurf = 0.5f, vtAmp = 8.0f, vtCaves = 0.3f, vtTop = 0.3f, vtDirt = 3.0f, vtSlope = 0.6f, vtOre = 0.1f;
    bool vtAuto = true, vtCatch = true, vtFoot = false, vtFluid = false; std::string vtTex; int vtSeed = 1234, vtVeins = 12; float vtCatchD = 4.0f;
    std::string vtMName[2] = {"Grass", "Stone"}, vtMDrop[2] = {"dirt", "stone"}; float vtMCol[2][4] = {{0.3f, 0.6f, 0.25f, 1}, {0.5f, 0.5f, 0.5f, 1}};
    float vtMHard[2] = {1.0f, 4.0f}, vtMDens[2] = {1500.0f, 2600.0f}, vtMPer[2] = {1.0f, 1.0f}; bool vtMLoose[2] = {true, false};

    // stage 7
    float stHp = 75.0f, stHpMax = 100.0f, stMp = 20.0f, stMpMax = 50.0f; int stLvl = 3, stXp = 120, stNext = 300, stStr = 5, stDef = 4, stInt = 6, stAgi = 7;
    int ivCap = 20, ivStack = 64, ivCount[2] = {3, 1}, ivItem[2] = {0, 1}; float ivWeight = 40.0f, ivOver = 0.5f; bool ivSlow = true, ivKeep = false;
    std::vector<int> ivOrder = {0, 1};   // the slots' order after a drag
    std::string cfTarget = "Player"; float cfOff[3] = {0, 3, -6}, cfSmooth = 5.0f;
    std::string wuText = "Guard"; bool wuLoc = false, wuScale = true; float wuCol[4] = {1, 1, 1, 1}, wuBg[4] = {0, 0, 0, 0.5f}, wuOff[3] = {0, 2, 0}, wuSize = 2.0f, wuMax = 30.0f, wuBar = 0.7f, wuBarCol[4] = {0.2f, 0.9f, 0.3f, 1};
    bool vsDriver = true; float vsSit[3] = {0, 0.5f, 0}, vsExit[3] = {-1.5f, 0, 0}, vsRadius = 2.5f; std::string vsPrompt = "Press E to drive", vsKey = "e";
    float vcF[20] = {30, 8, 12, 25, 0.5f, 120, 6, 1, 0.6f, 8, 3, 4, 0.5f, 60, 6, 0.3f, 2.6f, 1.6f, 1, 8}; float vcTilt = 6; bool vcFollow = true, vcSusp = true;
    int jtMode = 3; std::string jtBody; float jtAnchor[3] = {0, 2, 0}, jtAxis[3] = {0, 1, 0}, jtSpeed = 90, jtTorque = 50, jtMin = -45, jtMax = 45, jtBreak = 10;
    bool jtMotor = true, jtLimits = true, jtBreakable = false;
    float w3Ppu = 200.0f, w3Scale = 1.0f; bool w3Bill = true, w3Const = true;

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

    case BodyObjHolding: {
        static const char* const kH[] = {"Left", "Right", "Both"};
        static const char* const kS[] = {"Automatic", "Custom"};
        static const char* const kM[] = {"Left Mouse", "Right Mouse", "Middle Mouse", "None"};
        L.Note("Hold objects with the hands: IK onto each object's grips.");
        L.Combo("Primary Hand##poh", &v.ohHand, kH, 3);
        L.Combo("Holding Style##poh", &v.ohStyle, kS, 2);
        L.Check("Arm IK##poh", &v.ohIK);
        L.Drag("Transition (s)##poh", &v.ohTrans, 1, 0.01f, 0.0f, 2.0f);
        L.Check("Body Adjustment##poh", &v.ohBody);
        L.Combo("Carry Animations##poh", &v.ohCarry, kS, 2);
        L.Section("Throw");
        L.Text("Throw Key##poh", &v.ohKey);
        L.Combo("Throw Mouse##poh", &v.ohMouse, kM, 4);
        L.DragInt("Throw Pad Button##poh", &v.ohPad, 0.1f, -1, 11);
        L.Drag("Charge Time##poh", &v.ohCharge, 1, 0.01f, 0.05f, 3.0f);
        L.Drag("Min Speed##poh", &v.ohMin, 1, 0.1f, 0.0f, 40.0f);
        L.Drag("Max Speed##poh", &v.ohMax, 1, 0.1f, 0.0f, 60.0f);
        L.Drag("Pickup Range##poh", &v.ohRange, 1, 0.05f, 0.0f, 8.0f);
        L.SmallButton("Remove##poh");
        break;
    }
    case BodyHoldable: {
        static const char* const kSt[] = {"Automatic", "Custom", "Pistol", "Rifle", "Box", "Flashlight", "Sword", "Cup", "Heavy",
                                          "One hand right", "One hand left", "Two handed", "Carry in front", "Over the shoulder"};
        L.Note("A character's Procedural Object Holding can pick this up.");
        L.Combo("Style##hold", &v.hoStyle, kSt, 14);
        L.SmallButton("Preset: block##hold"); L.SameLine(); L.SmallButton("Preset: crate##hold");
        L.Note("  -> Box");
        L.Check("Support Hand##hold", &v.hoSupport);
        L.Check("Two Handed##hold", &v.hoTwo);
        L.Drag("Weight (kg)##hold", &v.hoWeight, 1, 0.1f, 0.0f, 500.0f);
        L.Text("Socket##hold", &v.hoSocket);
        L.Drag("Hold Offset##hold", v.hoOff, 3, 0.01f);
        L.Drag("Hold Rotation##hold", v.hoRot, 3, 1.0f);
        L.Drag("In-hand scale##hold", &v.hoScale, 1, 0.01f, 0.1f, 3.0f);
        L.Drag("FP offset##hold", v.hoFpOff, 3, 0.005f);
        L.Drag("FP rotation##hold", v.hoFpRot, 3, 1.0f);
        L.Drag("FP scale##hold", &v.hoFpScale, 1, 0.01f, 0.1f, 3.0f);
        L.Drag("TP offset##hold", v.hoTpOff, 3, 0.005f);
        L.Drag("TP rotation##hold", v.hoTpRot, 3, 1.0f);
        L.Check("Left Grip", &v.hoLeft);
        if (v.hoLeft) { L.Drag("Position##grip", v.hoGripPos, 3, 0.005f); L.Drag("Rotation##grip", v.hoGripRot, 3, 1.0f); }
        L.Check("Right Grip", &v.hoRight);
        L.Note("Grip rotation 0 = palm down, the hand points along -Z.");
        L.SmallButton("Remove##hold");
        break;
    }
    case BodyCharacter: {
        static const char* const kClips[] = {"(built-in)", "Idle Loop", "Sprint"};
        static const char* const kShirts[] = {"Tank (bare arms)", "Short sleeve", "Long sleeve"};
        static const char* const kLegs[] = {"Trousers", "Shorts"};
        static const char* const kHairs[] = {"Short", "Long", "Mohawk", "Bun", "Spiky", "Afro", "Ponytail", "Buzz"};
        static const char* const kBeards[] = {"None", "Full", "Goatee", "Mustache"};
        static const char* const kGl[] = {"None", "Glasses", "Sunglasses"};
        static const char* const kAn[] = {"None", "Idle", "Walk", "Run", "Wave"};
        L.Button("Set Character Model...##char");
        L.ColorText(P::HierCol(242, 191, 89), "No character model.");
        L.Label("Set Character Model, or right-click a model file and choose Make Playable Character. A new player starts in third person. V switches to the eyes. Player Controller, View, changes that.");
        L.Last().wrap = true;
        L.Button("Wire animation and IK##char"); L.SameLine(); L.Button("Open Animator##char");
        L.Slider("Height##char", &v.chHeight, 0.6f, 1.8f);
        L.Section("Rig");
        L.Check("Separate Into Parts (editable rig)##char", &v.chParts);
        L.Section("Movement animations");
        L.Note("Play YOUR authored clips as the character moves (no scripting).", false, true);
        L.Combo("Idle##charstate", &v.chIdle, kClips, 3); L.Last().width = 150.0f;
        L.Combo("Walk##charstate", &v.chWalk, kClips, 3); L.Last().width = 150.0f;
        L.Combo("Run##charstate", &v.chRun, kClips, 3); L.Last().width = 150.0f;
        L.Slider("Blend (s)##charblend", &v.chBlend, 0.0f, 0.6f, "%.2f"); L.Last().width = 150.0f;
        L.Section("Clothing");
        L.Combo("Shirt##char", &v.chShirt, kShirts, 3); L.Last().width = 150.0f;
        L.Combo("Legs##char", &v.chLegs, kLegs, 2); L.Last().width = 150.0f;
        L.Check("Belt##char", &v.chBelt); L.SameLine(); L.Check("Jacket##char", &v.chJacket); L.SameLine(); L.Check("Gloves##char", &v.chGloves);
        L.Section("Hair");
        L.Check("Hair##char", &v.chHasHair); L.SameLine(); L.Combo("##hairstyle", &v.chHair, kHairs, 8); L.Last().width = 120.0f;
        L.Combo("Beard##char", &v.chBeard, kBeards, 4); L.Last().width = 150.0f;
        L.Section("Accessories");
        L.Check("Hat##char", &v.chHat);
        L.Combo("Eyewear##char", &v.chGlasses, kGl, 3); L.Last().width = 150.0f;
        L.Check("Mask##char", &v.chMask); L.SameLine(); L.Check("Scarf##char", &v.chScarf);
        L.Check("Shoulder Pads##char", &v.chPads);
        L.Check("Backpack##char", &v.chPack); L.SameLine(); L.Check("Cape##char", &v.chCape);
        L.Section("Colors");
        L.Color("Skin##char", v.chSkin, 3); L.Color("Shirt##char", v.chShirtC, 3); L.Color("Pants##char", v.chPants, 3);
        L.Color("Shoes##char", v.chShoes, 3); L.Color("Hair##char", v.chHairC, 3); L.Color("Eyes##char", v.chEyes, 3);
        L.Section("Animation (plays in Play mode)");
        L.Combo("Animation##char", &v.chAnim, kAn, 5); L.Last().width = 150.0f;
        L.Tree("Clips (2)##charprev");
        L.Label("Idle Loop"); L.SameLine(); L.SmallButton("Preview##apcl");
        L.Label("Sprint"); L.SameLine(); L.SmallButton("Preview##apcl");
        L.TreeEnd();
        L.Section("Head look");
        L.Check("Look at Camera##char", &v.chLook);
        L.Text("Look at Object##char", &v.chLookAt);
        L.Slider("Head Turn Speed##char", &v.chTurn, 0.0f, 20.0f);
        L.Section("Feel");
        L.Slider("Jump Crouch (s)##char", &v.chJump, 0.0f, 0.4f, "%.2f");
        L.Slider("Jump Crouch Bend##char", &v.chBend, 0.0f, 60.0f, "%.0f deg");
        L.Slider("Turn Step Angle##char", &v.chStepA, 0.0f, 120.0f, "%.0f deg");
        L.Slider("Turn Step Time##char", &v.chStepT, 0.05f, 0.6f, "%.2f");
        L.Slider("Turn Step Lift##char", &v.chLift, 0.0f, 70.0f, "%.0f deg");
        L.Slider("Turn Still Speed##char", &v.chStill, 0.0f, 2.0f, "%.2f");
        L.Slider("Motion Lean##char", &v.chLean, 0.0f, 2.0f, "%.2f");
        L.Slider("Motion Lean Max##char", &v.chLeanMax, 0.0f, 15.0f, "%.0f deg");
        break;
    }
    case BodyFirstPerson: {
        static const char* const kPad[] = {"None", "A", "B", "X", "Y", "Back", "Start", "L", "R", "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right"};
        L.Note("Feel:");
        L.SameLine(); L.SmallButton("Tactical"); L.SameLine(4.0f); L.SmallButton("Arcade"); L.SameLine(4.0f); L.SmallButton("Floaty");
        L.Drag("Walk Speed##fp", &v.fpWalk, 1, 0.1f, 0.0f, 50.0f);
        L.Drag("Run Speed##fp", &v.fpRun, 1, 0.1f, 0.0f, 50.0f);
        L.Drag("Jump Force##fp", &v.fpJump, 1, 0.1f, 0.0f, 50.0f);
        L.Drag("Walk Accel##fp", &v.fpWalkA, 1, 1.0f, 1.0f, 300.0f);
        L.Drag("Run Accel##fp", &v.fpRunA, 1, 1.0f, 1.0f, 300.0f);
        L.Drag("Decel##fp", &v.fpDecel, 1, 1.0f, 1.0f, 300.0f);
        L.Drag("Turn Rate##fp", &v.fpTurn, 1, 1.0f, 1.0f, 600.0f);
        L.Drag("Air Control##fp", &v.fpAir, 1, 0.01f, 0.0f, 1.0f);
        L.Drag("Coyote Time##fp", &v.fpCoyote, 1, 0.005f, 0.0f, 0.5f, "%.3f s");
        L.Drag("Jump Buffer##fp", &v.fpBuffer, 1, 0.005f, 0.0f, 0.5f, "%.3f s");
        L.Drag("Mouse Sensitivity##fp", &v.fpSens, 1, 0.01f, 0.0f, 2.0f);
        L.Drag("Captured Sensitivity##fp", &v.fpCapSens, 1, 0.005f, 0.0f, 1.0f);
        L.Drag("Captured Pixel Cap##fp", &v.fpCap, 1, 1.0f, 4.0f, 120.0f, "%.0f px");
        L.Check("Invert Y##fp", &v.fpInvY);
        L.Check("Can Jump##fp", &v.fpCanJump); L.SameLine(); L.DragInt("Max Jumps##fp", &v.fpMaxJumps, 0.1f, 1, 5); L.Last().width = 90.0f;
        L.Check("Drive Animation##fp", &v.fpDrive);
        L.Check("Foot IK##fp", &v.fpFootIK);
        L.Check("Lock + Hide Cursor##fp", &v.fpLock);
        L.Section("Run / Stance");
        L.Combo("Sprint Key##fp", &v.fpSprint, kKeys, 9); L.Last().width = 96.0f;
        L.SameLine(); L.Check("Toggle Run##fp", &v.fpToggleRun);
        L.Combo("Crouch Key##fp", &v.fpCrouch, kKeys, 9); L.Last().width = 96.0f;
        L.SameLine(); L.Combo("Prone Key##fp", &v.fpProne, kKeys, 9); L.Last().width = 96.0f;
        L.Check("Toggle Stance##fp", &v.fpToggleStance);
        L.Drag("Crouch Speed##fp", &v.fpCrouchS, 1, 0.1f, 0.0f, 20.0f);
        L.Drag("Prone Speed##fp", &v.fpProneS, 1, 0.1f, 0.0f, 20.0f);
        L.Check("Allow Prone##fp", &v.fpAllowProne); L.SameLine(); L.Check("Resize Body##fp", &v.fpResize);
        L.Slider("Crouch Height##fp", &v.fpCrouchH, 0.3f, 1.0f, "%.2f x standing");
        L.Slider("Prone Height##fp", &v.fpProneH, 0.15f, 0.8f, "%.2f x standing");
        L.Combo("Stance Pad Button##fp", &v.fpPad, kPad, 13); L.Last().width = 130.0f;
        L.SameLineOrWrap(); L.Drag("Hold For Prone##fp", &v.fpHold, 1, 0.02f, 0.1f, 2.0f, "%.2fs"); L.Last().width = 90.0f;
        L.Drag("Stand Eye Height##fp", &v.fpEyeS, 1, 0.05f, 0.0f, 3.0f);
        L.Drag("Crouch Eye Height##fp", &v.fpEyeC, 1, 0.05f, 0.0f, 3.0f);
        L.Drag("Prone Eye Height##fp", &v.fpEyeP, 1, 0.05f, 0.0f, 3.0f);
        L.Drag("Stance Ease##fp", &v.fpEase, 1, 0.5f, 0.0f, 40.0f);
        break;
    }
    case BodyPlayerView: {
        static const char* const kViews[] = {"First person", "Third person"};
        static const char* const kPad[] = {"None", "A", "B", "X", "Y", "Back", "Start", "L", "R", "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right"};
        static const char* const kE[] = {"Linear", "Smooth", "Ease In", "Ease Out"};
        L.Check("Allow Toggle##pv", &v.pvAllow);
        L.Combo("Default View##pv", &v.pvView, kViews, 2);
        L.Combo("Toggle Key##pv", &v.pvKey, kKeys, 9); L.Last().width = 96.0f;
        L.Combo("Toggle Pad Button##pv", &v.pvPad, kPad, 13); L.Last().width = 130.0f;
        L.Drag("Switch Blend Time##pv", &v.pvBlend, 1, 0.01f, 0.0f, 3.0f, "%.2f s");
        L.Combo("Easing##pv", &v.pvEase, kE, 4);
        L.Check("Snap##pv", &v.pvSnap);
        L.Drag("Eye Offset##pv", v.pvEye, 3, 0.01f);
        L.Drag("Third Person Distance##pv", &v.pvDist, 1, 0.05f, -1.0f, 40.0f);
        L.Drag("Shoulder Offset##pv", &v.pvShoulder, 1, 0.01f, -3.0f, 3.0f);
        L.Note("V eases between the eyes and the orbit.");
        break;
    }
    case BodyAnimSM: {
        static const char* const kEntry[] = {"(first state)", "Idle", "Walk"};
        static const char* const kStates[] = {"Idle", "Walk"};
        static const char* const kSmClips[] = {"Idle", "Walk", "Run", "Jump"};
        static const char* const kConds[] = {"On Clip End", "Float >", "Float <", "Bool true", "Bool false", "Trigger"};
        L.Button("Open Animator Graph##asm");
        L.SameLineOrWrap(); L.Button("Reset to default##asm");
        L.SameLineOrWrap(); L.Button("Add built-in actions##asm");
        L.Combo("Entry##asm", &v.smEntry, kEntry, 3); L.Last().width = 200.0f;
        L.Tree("Stance changes##asm");
        L.Drag("Stand <-> crouch##asm", &v.smSC, 1, 0.01f, 0.05f, 3.0f, "%.2fs");
        L.Drag("Crouch <-> prone##asm", &v.smCP, 1, 0.01f, 0.05f, 3.0f, "%.2fs");
        L.Drag("Prone -> stand##asm", &v.smPS, 1, 0.01f, 0.05f, 4.0f, "%.2fs");
        L.TreeEnd();
        L.Tree("Gait crossfades##asm");
        L.TreeEnd();
        std::string* names[2] = {&v.smS0, &v.smS1};
        int* clips[2] = {&v.smClip0, &v.smClip1};
        float* spd[2] = {&v.smSpd0, &v.smSpd1};
        bool* loops[2] = {&v.smLoop0, &v.smLoop1};
        for (int si = 0; si < 2; ++si) {
            L.Separator();
            L.Text("State##asm", names[si]); L.Last().width = 140.0f;
            L.SameLineOrWrap(); L.Combo("##asmclip", clips[si], kSmClips, 4); L.Last().width = 150.0f;
            L.SameLineOrWrap(); L.Drag("##asmspd", spd[si], 1, 0.02f, 0.05f, 5.0f, "x%.2f"); L.Last().width = 70.0f;
            L.SameLineOrWrap(); L.Check("loop##asm", loops[si]);
            L.SameLineOrWrap(); L.SmallButton("x##asmst");
            L.SmallButton("Preview##apst"); L.SameLine(); L.SmallButton("Edit clip##asm");
            L.Tree(si == 0 ? "1 transition(s)###asmtr0" : "0 transition(s)###asmtr1"); if (si == 0) L.Last().on = true;
            if (si == 0) {
                L.Note("  ->"); L.SameLine();
                L.Combo("##trto", &v.smTo, kStates, 2); L.Last().width = 120.0f;
                L.SameLineOrWrap(); L.Combo("##trcond", &v.smCond, kConds, 6); L.Last().width = 110.0f;
                L.SameLineOrWrap(); L.Text("##trparam", &v.smParam, "param"); L.Last().width = 90.0f;
                L.SameLineOrWrap(); L.Drag("##trval", &v.smVal, 1, 0.05f); L.Last().width = 60.0f;
                L.SameLineOrWrap(); L.Drag("##trblend", &v.smBlendT, 1, 0.01f, 0.0f, 2.0f, "%.2fs"); L.Last().width = 60.0f;
                L.SameLineOrWrap(); L.SmallButton("x##tr");
            }
            L.SmallButton("+ Transition##asm");
            L.TreeEnd();
        }
        L.Separator();
        L.SmallButton("+ Add State##asm");
        L.Note("Set parameters from a script: anim_set_float(\"speed\", v),\nanim_set_bool(\"armed\", 1), anim_trigger(\"attack\"); anim_state() reads back.", false, true);
        break;
    }

    case BodyItemPickup: {
        static const char* const kItems[] = {"Apple", "Crate", "Wood Plank", "Stone"};
        L.Note("An item in the world. Walk into it (trigger), come close, or press the key.", false, true);
        static const char* const kItemIds[] = {"apple", "crate", "plank", "stone"};
        L.Combo("Item##pk", &v.ipItem, kItems, 4); L.Last().width = 160.0f; L.Last().preview = "crate_heavy_wooden_box"; L.Last().color = P::HierCol(255, 153, 76);
        L.Last().search = true; L.Last().searchHint = "Search or type an id"; L.Last().searchKeys = kItemIds;
        L.DragInt("Count##pk", &v.ipCount, 0.2f, 1, 99999);
        L.Text("Collector Tag##pk", &v.ipTag);
        L.Check("Pick Up On Touch##pk", &v.ipTouch);
        L.Drag("Auto Radius##pk", &v.ipAuto, 1, 0.05f, 0.0f, 50.0f);
        L.Text("Interact Key##pk", &v.ipKey);
        L.Drag("Interact Range##pk", &v.ipRange, 1, 0.05f, 0.1f, 50.0f);
        L.Check("Hold In Hands##pk", &v.ipHold);
        L.Note("The key picks it up with Procedural Object Holding (if the player has it).", false, true);
        L.Drag("Magnet Radius##pk", &v.ipMagnet, 1, 0.05f, 0.0f, 50.0f);
        break;
    }
    case BodyHoldPrompt:
        L.Note("E picks a hold-in-hands item up. E again, or G, puts it down.", false, true);
        L.Text("Drop Key##hp", &v.hpDrop); L.Text("Alt Drop Key##hp", &v.hpAlt); L.Text("Prompt Object##hp", &v.hpPrompt);
        L.Check("Localized##hp", &v.hpLoc);
        L.SmallButton("Remove##hp");
        break;
    case BodyWater:
        L.Note("A living, reflective water surface with rolling waves.", false, true);
        L.Color("Color##wat", v.waCol, 4); L.Drag("Size##wat", &v.waSize, 1, 1.0f, 1.0f, 1000000.0f);
        L.Drag("Depth##wat", &v.waDepth, 1, 0.1f, 0.0f, 200.0f); L.Drag("Density##wat", &v.waDens, 1, 0.01f, 0.0f, 20.0f);
        L.Drag("Drag##wat", &v.waDrag, 1, 0.05f, 0.0f, 20.0f); L.SliderInt("Detail##wat", &v.waDetail, 4, 128);
        L.Section("Waves");
        L.Drag("Height##wat", &v.waH, 1, 0.01f, 0.0f, 5.0f); L.Drag("Length##wat", &v.waL, 1, 0.1f, 0.5f, 60.0f); L.Drag("Speed##wat", &v.waS, 1, 0.05f, 0.0f, 8.0f);
        L.Section("Surface");
        L.Slider("Reflectivity##wat", &v.waRefl, 0.0f, 1.0f); L.Slider("Glint##wat", &v.waGlint, 0.0f, 1.0f);
        L.Drag("Flow##wat", v.waFlow, 2, 0.005f, -1.0f, 1.0f);
        L.SmallButton("Remove##wat");
        break;
    case BodyFlashlight:
        L.Note("A spot light that follows your view \xE2\x80\x94 see inside caves. Toggle in Play.", false, true);
        L.Text("Toggle Key##fl", &v.flKey); L.Check("On##fl", &v.flOn);
        L.Drag("Range##fl", &v.flRange, 1, 0.5f, 1.0f, 200.0f); L.Slider("Cone Angle##fl", &v.flAngle, 10.0f, 120.0f, "%.0f deg");
        L.Drag("Intensity##fl", &v.flInt, 1, 0.05f, 0.0f, 8.0f); L.Color("Color##fl", v.flCol, 3);
        L.Note("Mount on the player or its camera; the beam aims where you look.", false, true);
        L.SmallButton("Remove##fl");
        break;
    case BodyPauseMenu: {
        static const char* const kPk[] = {"Escape", "P", "Tab"};
        L.Note("Editable UI is under 'PauseMenu UI' \xE2\x80\x94 select its parts to restyle.", false, true);
        L.Note("Press the toggle key in Play to pause; Resume / Quit buttons appear.", false, true);
        L.Check("Localized##pm", &v.pmLoc); L.Text("Title##pm", &v.pmTitle);
        L.Combo("Toggle Key##pm", &v.pmKey, kPk, 3);
        L.Check("Resume button##pm", &v.pmResume); L.Check("Quit button##pm", &v.pmQuit);
        L.Text("Main Menu Scene##pm", &v.pmScene);
        L.Color("Backdrop##pm", v.pmDim, 4); L.Color("Panel##pm", v.pmPanel, 4);
        L.SmallButton("Remove##pm");
        break;
    }
    case BodyFpHand: {
        static const char* const kSides[] = {"Right arm", "Left arm"};
        L.Note("Shows your character's OWN arm in first person. Your body is hidden from your camera only (others see all of it) and never duplicated. Click to punch.", false, true);
        L.SliderInt("Attack Button##fh", &v.fhBtn, 0, 2); L.Check("Swing while held##fh", &v.fhHold);
        L.Combo("Arm side##fh", &v.fhSide, kSides, 2); L.Check("Follow camera look##fh", &v.fhFollow); L.Check("Bobbing##fh", &v.fhBob);
        L.Check("Steady arm (ignore lean/crouch/prone)##fh", &v.fhSteady); L.Check("Show While Running##fh", &v.fhRun);
        L.Section("Manual adjust");
        L.Drag("Raise##fh", &v.fhRaise, 1, 0.5f, -180.0f, 180.0f, "%.0f deg"); L.Drag("Elbow##fh", &v.fhElbow, 1, 0.5f, -180.0f, 180.0f, "%.0f deg");
        L.SmallButton("Reset arm pose##fh"); L.SmallButton("Remove##fh");
        break;
    }
    case BodyVoxelDigger: {
        static const char* const kM[] = {"Dig (remove)", "Add (build)", "Smooth", "Box (rectangular)"};
        static const char* const kB[] = {"Left Mouse", "Right Mouse", "Middle Mouse", "None"};
        static const char* const kPr[] = {"Pick a preset...", "Realistic", "Arcade", "Creative"};
        static const char* const kCo[] = {"Vanish (old behaviour)", "Store (into the count)", "Debris (falls, lands back)", "Both (debris, pick up to store)"};
        static const char* const kSh[] = {"Sphere", "Cube", "Cylinder", "Plane (flat disc)"};
        L.Note("Aim with the camera and hold to carve caves/tunnels (or add material).", false, true);
        L.Combo("Primary##vdg", &v.vdMode, kM, 4); L.Combo("Primary Button##vdg", &v.vdBtn, kB, 4); L.Combo("Opposite Button##vdg", &v.vdAbtn, kB, 4);
        L.Drag("Brush Radius##vdg", &v.vdRad, 1, 0.1f, 0.25f, 30.0f); L.Drag("Strength / sec##vdg", &v.vdStr, 1, 0.1f, 0.1f, 100.0f);
        L.Drag("Reach##vdg", &v.vdReach, 1, 0.5f, 1.0f, 500.0f); L.Check("Show brush marker##vdg", &v.vdBrush);
        L.Section("Real life");
        L.Combo("Preset##vdg", &v.vdPreset, kPr, 4); L.Combo("Dug Ground##vdg", &v.vdCollect, kCo, 4);
        L.Check("Building needs material##vdg", &v.vdNeeds); L.SameLineOrWrap(); L.Check("Creative (unlimited)##vdg", &v.vdCreative);
        L.Drag("Tool Power##vdg", &v.vdPower, 1, 0.1f, 0.0f, 1.0e9f, "%.1f"); L.Last().logarithmic = true; L.Drag("Hardness Scale##vdg", &v.vdHard, 1, 0.05f, 0.1f, 20.0f);
        L.Combo("Brush Shape##vdg", &v.vdShape, kSh, 4);
        L.Drag("Depth Limit##vdg", &v.vdDepth, 1, 0.1f, -1.0f, 200.0f, "%.1f"); L.Drag("Bedrock Height##vdg", &v.vdBed, 1, 0.1f, -1.0f, 500.0f, "%.1f");
        L.Check("Snap to grid##vdg", &v.vdSnap); L.SameLineOrWrap(); L.Check("Slump loose ground##vdg", &v.vdSlump);
        L.Text("Pour Key##vdg", &v.vdPour); L.SliderInt("Cave-in Span##vdg", &v.vdSpan, 0, 24);
        L.SliderInt("Slump Passes##vdg", &v.vdPasses, 1, 32); L.SameLineOrWrap(); L.Check("Collapse##vdg", &v.vdCollapse);
        L.SliderInt("Debris Limit##vdg", &v.vdDebris, 0, 300); L.Drag("Debris Life (s)##vdg", &v.vdLife, 1, 0.5f, 1.0f, 120.0f);
        L.Check("Give inventory items##vdg", &v.vdGive); L.Check("Tools from inventory##vdg", &v.vdTools); L.Check("Build from items##vdg", &v.vdBuild);
        L.Text("Count Text Object##vdg", &v.vdHud);
        L.Note("In Play: hold the mouse to dig, right mouse builds, Ctrl+Z takes back the last stroke.", false, true);
        L.SmallButton("Remove##vdg");
        break;
    }
    case BodyVoxelTerrain:
        L.Note("Smooth marching-cubes voxels: real caves, tunnels & overhangs.", false, true);
        L.Color("Color##vox", v.vtCol, 4); L.Check("Auto-color (height/slope)##vox", &v.vtAuto);
        L.Drag("Grid (X,Y,Z)##vox", v.vtDims, 3, 1.0f, 4.0f, 512.0f, "%.0f"); L.Drag("Voxel Size##vox", &v.vtSize, 1, 0.05f, 0.1f, 10.0f);
        L.Text("Texture##vox", &v.vtTex); L.Last().pick = P::InspPickList; L.Last().dropKind = "";
        L.Section("Generate");
        L.Drag("Surface##vox", &v.vtSurf, 1, 0.01f, 0.1f, 0.95f); L.Drag("Relief##vox", &v.vtAmp, 1, 0.2f, 0.0f, 40.0f); L.Slider("Caves##vox", &v.vtCaves, 0.0f, 1.0f);
        L.InputInt("Seed##vox", &v.vtSeed); L.SameLineOrWrap(); L.SmallButton("Random##voxseed");
        L.Button("Generate##voxgen"); L.SameLineOrWrap(); L.Button("Solid Slab##voxgen"); L.SameLineOrWrap(); L.Button("Clear##voxgen");
        L.Note("Add a Voxel Digger to carve it live in Play.", false, true);
        L.Section("Materials");
        L.Drag("Topsoil (m)##voxlay", &v.vtTop, 1, 0.05f, 0.0f, 10.0f); L.Drag("Dirt Depth (m)##voxlay", &v.vtDirt, 1, 0.1f, 0.0f, 40.0f);
        L.Slider("Rock Slope##voxlay", &v.vtSlope, 0.1f, 1.0f); L.Slider("Ore Pockets##voxlay", &v.vtOre, 0.0f, 0.5f); L.SliderInt("Ore Veins##voxlay", &v.vtVeins, 0, 80);
        for (int mi = 0; mi < 2; ++mi) {
            L.Text("##mname", &v.vtMName[mi]); L.Last().width = 90.0f;
            L.SameLineOrWrap(); L.Color("##mcol", v.vtMCol[mi], 4); L.Last().noInputs = true;
            L.SameLineOrWrap(); L.Drag("hard##mh", &v.vtMHard[mi], 1, 0.05f, 0.1f, 50.0f, "%.1f"); L.Last().width = 60.0f;
            L.SameLineOrWrap(); L.Drag("kg/m3##md", &v.vtMDens[mi], 1, 10.0f, 1.0f, 30000.0f, "%.0f"); L.Last().width = 70.0f;
            L.SameLineOrWrap(); L.Check("loose##ml", &v.vtMLoose[mi]);
            L.Text("drop item##mdr", &v.vtMDrop[mi]); L.Last().width = 90.0f;
            L.SameLineOrWrap(); L.Drag("per m3##mpm", &v.vtMPer[mi], 1, 0.5f, 0.0f, 1000.0f, "%.1f"); L.Last().width = 60.0f;
        }
        L.SmallButton("Regenerate layers##voxmat"); L.SameLineOrWrap(); L.SmallButton("Materials off##voxmat");
        L.Note("Ground volume: 41230 m3. Dug ground keeps its material; built ground uses the digger's Build Material.", false, true);
        L.Check("Catch bodies that fall out of the world##vox", &v.vtCatch); L.Drag("Catch Depth##vox", &v.vtCatchD, 1, 0.1f, 0.0f, 100.0f);
        L.Check("Footprints and tracks##voxft", &v.vtFoot); L.Check("Water / lava flow in holes##voxfl", &v.vtFluid);
        L.Note("Drawn as one mesh. Last build: 64 of 64 chunks, 12.40 ms.", false, true);
        L.SmallButton("Remove##vox");
        break;

    case BodyStats:
        L.Section("Vitals");
        L.Drag("Health##st", &v.stHp, 1, 1.0f, 0.0f, 100000.0f); L.SameLineOrWrap(); L.Drag("Max##sthp", &v.stHpMax, 1, 1.0f, 1.0f, 100000.0f); L.Last().width = 90.0f;
        L.Drag("Mana##st", &v.stMp, 1, 1.0f, 0.0f, 100000.0f); L.SameLineOrWrap(); L.Drag("Max##stmp", &v.stMpMax, 1, 1.0f, 0.0f, 100000.0f); L.Last().width = 90.0f;
        L.Progress("HP", v.stHp / v.stHpMax);
        L.Section("Progression");
        L.DragInt("Level##st", &v.stLvl, 0.1f, 1, 999); L.DragInt("XP##st", &v.stXp, 1.0f, 0, 1000000);
        L.SameLineOrWrap(); L.DragInt("To Next##st", &v.stNext, 1.0f, 1, 1000000); L.Last().width = 90.0f;
        L.Section("Attributes");
        L.DragInt("Strength##st", &v.stStr, 0.1f, 0, 999); L.DragInt("Defense##st", &v.stDef, 0.1f, 0, 999);
        L.DragInt("Intelligence##st", &v.stInt, 0.1f, 0, 999); L.DragInt("Agility##st", &v.stAgi, 0.1f, 0, 999);
        L.Note("scripts: take_damage / heal / add_xp; fires on_death, on_level_up", false, true);
        L.SmallButton("Remove##st");
        break;
    case BodyInventory: {
        static const char* const kItems[] = {"Apple", "Wood Plank"};
        static const char* const kIds[] = {"apple", "plank"};
        L.DragInt("Capacity##inv", &v.ivCap, 0.2f, 1, 999); L.DragInt("Max Stack##inv", &v.ivStack, 0.2f, 0, 9999);
        L.Drag("Weight Limit##inv", &v.ivWeight, 1, 0.5f, 0.0f, 100000.0f);
        L.Check("Slow When Over##inv", &v.ivSlow); L.Slider("Over-weight Speed##inv", &v.ivOver, 0.1f, 1.0f);
        L.Label("Weight: 4.5 / 40.0"); L.Check("Keep On Scene Load##inv", &v.ivKeep); L.Label("Slots: 2 / 20");
        for (int i = 0; i < 2; ++i) {
            const int s = v.ivOrder[(std::size_t)i];
            L.SmallButton("::"); L.Last().dragType = "INV_SLOT"; L.Last().tag = i; L.Last().hint = kItems[s];
            L.SameLine(); L.Combo("##invit", &v.ivItem[s], kItems, 2); L.Last().width = 130.0f; L.Last().search = true; L.Last().searchKeys = kIds;
            L.SameLine(); L.DragInt("##invn", &v.ivCount[s], 0.2f, 0, 9999); L.Last().width = 70.0f;
            L.SameLine(); L.SmallButton("X");
        }
        L.SmallButton("Add Item##inv"); L.SmallButton("Remove##inv");
        break;
    }
    case BodyCameraFollow:
        L.Text("Target##cf", &v.cfTarget); L.Drag("Offset##cf", v.cfOff, 3, 0.05f, 0.0f, 0.0f, "%.3f"); L.Last().axisBars = true;
        L.Drag("Smoothing##cf", &v.cfSmooth, 1, 0.1f, 0.0f, 50.0f);
        L.Note("follows the named GameObject (0 = instant snap)", false, true);
        L.SmallButton("Remove##cf");
        break;
    case BodyWorldUI:
        L.Check("Localized##wuloc", &v.wuLoc); L.Text("Text##wu", &v.wuText);
        L.Color("Text Color##wu", v.wuCol, 4); L.Color("Background##wu", v.wuBg, 4);
        L.Drag("World Offset##wu", v.wuOff, 3, 0.05f, 0.0f, 0.0f, "%.3f"); L.Last().axisBars = true;
        L.Drag("Size##wu", &v.wuSize, 1, 0.05f, 0.2f, 16.0f); L.Check("Scale With Distance##wu", &v.wuScale);
        L.Drag("Max Distance##wu", &v.wuMax, 1, 0.5f, 0.0f, 1000.0f); L.Drag("Bar (0..1, <0 = none)##wu", &v.wuBar, 1, 0.01f, -1.0f, 1.0f);
        L.Color("Bar Color##wu", v.wuBarCol, 4);
        L.Note("Floats over a 3D point (nameplates, health bars). Shows in the Game view + built game.", false, true);
        L.SmallButton("Remove##wu");
        break;
    case BodyVehicleSeat:
        L.Check("Driver Seat##vseat", &v.vsDriver); L.Drag("Sit Point##vseat", v.vsSit, 3, 0.01f); L.Drag("Exit Point##vseat", v.vsExit, 3, 0.01f);
        L.Drag("Use Radius##vseat", &v.vsRadius, 1, 0.05f, 0.1f, 20.0f); L.Text("Prompt##vseat", &v.vsPrompt); L.Text("Enter / Exit Key##vseat", &v.vsKey);
        L.Note("Needs a Vehicle Controller on this object or a parent.", false, true);
        L.SmallButton("Remove##vseat");
        break;
    case BodyVehicle: {
        static const char* const kN[] = {"Max Speed##veh", "Reverse Speed##veh", "Acceleration##veh", "Brake Force##veh", "Drag (coast)##veh"};
        for (int i = 0; i < 5; ++i) L.Drag(kN[i], &v.vcF[i], 1, 0.5f, 0.0f, 300.0f);
        L.Drag("Turn Speed##veh", &v.vcF[5], 1, 1.0f, 0.0f, 400.0f, "%.0f deg/s");
        L.Drag("Grip##veh", &v.vcF[6], 1, 0.1f, 0.0f, 30.0f); L.Drag("Handbrake Grip##veh", &v.vcF[7], 1, 0.05f, 0.0f, 30.0f);
        L.Drag("Ground Check##veh", &v.vcF[8], 1, 0.05f, 0.0f, 10.0f); L.Check("Follow Camera##veh", &v.vcFollow);
        L.Drag("Cam Distance##veh", &v.vcF[9], 1, 0.1f, 0.0f, 50.0f); L.Drag("Cam Height##veh", &v.vcF[10], 1, 0.1f, 0.0f, 50.0f);
        L.Drag("Cam Smoothing##veh", &v.vcF[11], 1, 0.1f, 0.0f, 30.0f);
        L.Separator(); L.Check("Raycast Suspension##veh", &v.vcSusp);
        static const char* const kS[] = {"Ride Height##veh", "Spring Strength##veh", "Spring Damping##veh", "Suspension Travel##veh", "Wheel Base##veh",
                                         "Track Width##veh", "Body Lean##veh"};
        for (int i = 0; i < 7; ++i) L.Drag(kS[i], &v.vcF[12 + i], 1, 0.05f, 0.0f, 400.0f);
        L.Drag("Max Tilt##veh", &v.vcTilt, 1, 0.5f, 0.0f, 45.0f, "%.0f deg"); L.Drag("Tilt Smoothing##veh", &v.vcF[19], 1, 0.2f, 0.0f, 30.0f);
        L.Separator();
        L.Note("Drive: W/S or arrows = gas/reverse, A/D = steer, Space = handbrake. Needs a Rigidbody3D + collider.", false, true);
        L.SmallButton("Remove##veh");
        break;
    }
    case BodyJoint3D: {
        static const char* const kModes[] = {"Distance (rigid)", "Spring", "Pin (weld)", "Hinge (revolute)", "Slider (prismatic)"};
        L.Note("Constrains this body to an anchor or another body.", false, true);
        L.Combo("Mode##jt", &v.jtMode, kModes, 5); L.Text("Connected Body##jt", &v.jtBody); L.Drag("Anchor##jt", v.jtAnchor, 3, 0.1f);
        L.Note("Pins a point, spins about the axis. Needs Freeze Rotation OFF.", false, true);
        L.Drag("Axis##jt", v.jtAxis, 3, 0.05f); L.Check("Use Motor##jt", &v.jtMotor);
        L.Drag("Motor Speed (deg/s)##jt", &v.jtSpeed, 1, 1.0f); L.Drag("Max Motor Torque##jt", &v.jtTorque, 1, 1.0f, 0.0f, 100000.0f);
        L.Check("Use Limits##jt", &v.jtLimits);
        L.Drag("Min Angle##jt", &v.jtMin, 1, 1.0f, -360.0f, 360.0f); L.Drag("Max Angle##jt", &v.jtMax, 1, 1.0f, -360.0f, 360.0f);
        L.Check("Breakable##jt", &v.jtBreakable);
        L.SmallButton("Remove##jt");
        break;
    }
    case BodyWorldSpaceUI:
        L.Drag("Pixels / Unit##w3", &v.w3Ppu, 1, 1.0f, 1.0f, 4000.0f, "%.0f"); L.Check("Billboard (face camera)##w3", &v.w3Bill);
        L.Check("Constant size (don't resize with distance)##w3", &v.w3Const); L.Drag("Size Scale##w3", &v.w3Scale, 1, 0.01f, 0.05f, 20.0f, "%.2f");
        L.Note("This UI widget renders in the 3D world at this object's position. Move it with the transform gizmo.", false, true);
        L.SmallButton("Remove##w3");
        break;
    default: break;
    }
    return L;
}

/// After a frame: the buttons pressed (by their visible label) go into v.clicked; a Mesh Renderer pick from the procedural grid becomes
/// the texture, as the editor's builder does.
inline void Apply(Body b, Values& v, P::InspFieldList& L) {
    for (P::InspRow& r : L.rows)
        if (r.clicked) v.clicked.push_back(std::string(r.label, P::InspVisibleLen(r.label)));
    if (b == BodyInventory)   // a slot's handle dropped on another's: the dragged slot goes there (as the editor's Inventory body)
        for (P::InspRow& r : L.rows)
            if (r.dragType && r.dropped >= 0 && r.dropped != r.tag) {
                const int from = r.dropped, at = r.tag;
                const int moved = v.ivOrder[(std::size_t)from];
                v.ivOrder.erase(v.ivOrder.begin() + from);
                v.ivOrder.insert(v.ivOrder.begin() + (from < at ? at - 1 : at), moved);
                break;
            }
    if (b == BodyItemPickup)
        for (P::InspRow& r : L.rows)
            if (r.changed && r.search && *r.I() == -1) v.ipTyped = r.sv;
    if (b == BodyMeshRest)
        for (P::InspRow& r : L.rows)
            if (r.changed && r.kind == P::InspRowSmallButton && r.pick == P::InspPickGrid) v.texture = v.procPick;
}

} // namespace inspbody
