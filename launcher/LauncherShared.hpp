#pragma once
// Functions and state still defined in launcher/main.cpp that the split-out launcher files use (the launcher's
// editor/MainShared.hpp; docs/engine/modularization_plan.md, Part C). This list shrinks as main.cpp is split further;
// each entry moves to the header of the file that ends up owning it. Everything is in namespace okl (the launcher's
// names; main.cpp's anonymous namespace became okl so the split-out files can reach them).
#include "okay/UI/OkayUIImGuiNames.hpp"   // OkayUI, in Dear ImGui's names
#include "okay/Platform/Platform.hpp"   // okay::platform::Window (the launcher's windows)
#include "PainterBridge.hpp"          // okay::pb::Renderer, okay::ui2d::Tex
#include "OkayThreads.hpp"   // okay::thr (the launcher's mutexes and threads)
#include "okay/Net/JoinLink.hpp"            // ChatInviteInfo holds a link
#include "JoinPlan.hpp"                     // okay::joinplan::Candidate (MpJoinProjects)
#include "okay/Platform/Messages.hpp"      // okay::Messages (chat helpers)
#include "okay/Platform/Friends.hpp"       // okay::Friend (the friends list)
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;   // the same alias as main.cpp's
#if !defined(_WIN32)
#include <sys/types.h>   // pid_t (PlayProc)
#endif

// Window calls on the launcher's windows (okay::platform::Window). A null window does nothing.
namespace lw {
inline void Restore(okay::platform::Window* w)  { if (auto* s = w) s->Restore(); }
inline void Raise(okay::platform::Window* w)    { if (auto* s = w) s->Raise(); }
inline void Show(okay::platform::Window* w)     { if (auto* s = w) s->Show(); }
inline void Hide(okay::platform::Window* w)     { if (auto* s = w) s->Hide(); }
inline void Minimize(okay::platform::Window* w) { if (auto* s = w) s->Minimize(); }
inline void GetPos(okay::platform::Window* w, int* x, int* y) { int a = 0, b = 0; if (auto* s = w) s->GetPosition(a, b); if (x) *x = a; if (y) *y = b; }
inline bool IsMinimized(okay::platform::Window* w) { auto* s = w; return s && s->IsMinimized(); }
inline bool IsVisible(okay::platform::Window* w)   { auto* s = w; return s && s->IsVisible(); }
inline bool HasInputFocus(okay::platform::Window* w) { auto* s = w; return s && s->HasInputFocus(); }
inline bool HasMouseFocus(okay::platform::Window* w) { auto* s = w; return s && s->HasMouseFocus(); }
inline bool AnyKeyboardFocus() { auto* b = okay::platform::Default(); return b && b->KeyboardFocusWindowId() != 0; }
inline void* Win32Handle(okay::platform::Window* w) { auto* s = w; return s ? s->Win32Handle() : nullptr; }
inline void GetSize(okay::platform::Window* w, int* x, int* y) { int a = 0, b = 0; if (auto* s = w) s->GetSize(a, b); if (x) *x = a; if (y) *y = b; }
inline void SetSize(okay::platform::Window* w, int x, int y) { if (auto* s = w) s->SetSize(x, y); }
inline void SetPos(okay::platform::Window* w, int x, int y)  { if (auto* s = w) s->SetPosition(x, y); }
inline void SetMinSize(okay::platform::Window* w, int x, int y) { if (auto* s = w) s->SetMinimumSize(x, y); }
inline uint32_t Id(okay::platform::Window* w)   { auto* s = w; return s ? s->Id() : 0; }
inline bool IsFocused(okay::platform::Window* w) { auto* s = w; return s && s->IsFocused(); }
inline void Flash(okay::platform::Window* w, okay::platform::FlashMode m) { if (auto* s = w) s->Flash(m); }
inline bool SetAlwaysOnTop(okay::platform::Window* w, bool on) { auto* s = w; return s && s->SetAlwaysOnTop(on); }
} // namespace lw

namespace okay { struct JoinLink; }
namespace okay { struct MyBanInfo; namespace account { class AccountService; } }
namespace okay { struct PlaytimeEntry; }

// An RGBA8 picture as a texture of `renderer` (main.cpp, global scope); null when it cannot be made.
okay::ui2d::Tex TextureFromRgba(okay::pb::Renderer* renderer, const unsigned char* rgba, int w, int h);

namespace okl {

// Look and messages (main.cpp).
extern ImVec4 kAccent;                          // the accent colour (Settings > Appearance)
ImVec4 NameColor(const std::string& s);         // a stable colour for a user name
void Toast(const std::string& m, int kind);     // a short message in the corner (kind: 0 info, 1 success, 2 error)
// Settings (main.cpp, persisted in the prefs file).
extern bool g_linkPreviews;                     // fetch title / image cards for links in chat
// Opening things and running commands (main.cpp).
bool ShellSafeUrl(const std::string& url);      // a URL that is safe to hand to the shell / browser
void OpenExternal(const std::string& target);   // open a URL or folder with the system's default program
int RunCmd(const std::string& cmd, bool wait);  // run a command without a console window; wait = return its exit code
// Chat media (main.cpp).
void DrawEmojiToken(ImDrawList* dl, ImVec2 c, float s, const std::string& tok);   // any emoji token centred at c
okay::ui2d::Tex ChatImageTexture(const std::string& url, int* w = nullptr, int* h = nullptr);   // a chat image / GIF frame (cached)
std::string ChatMediaPicker(const char* id, char* buf, std::size_t cap,
                            const std::vector<std::string>* recentGifs, bool canSendMedia);   // emoji / GIF / sticker picker
// Shared look helpers (main.cpp).
std::string RelTime(long when);                // "5 min ago"
bool PrimaryButton(const char* label, const ImVec2& size);   // the accent-coloured main button
std::string FormatDate(long t);                // a date for lists
bool ToggleSwitch(const char* id, bool* v);     // the on/off pill switch; true on the frame it flips
std::string FitText(const std::string& s, float maxW);   // s shortened with "..." to fit maxW
void DrawVerifiedBadge(ImDrawList* d, ImVec2 c, float r);   // the blue "verified" tick
// State and config (main.cpp).
extern std::string g_exeDir;                   // the folder the launcher runs from
extern okay::pb::Renderer* g_renderer;               // the renderer textures are uploaded to (switched while the chat window draws)
extern int g_rslot;                            // render slot: 0 = launcher window, 1 = chat window (each texture cache keeps one copy per slot)
bool Download(const std::string& url, const fs::path& out, const std::function<void(uint64_t)>& onBytes = {},
              const std::function<bool()>& cancelled = {});   // fetch a URL to a file (blocking); onBytes = progress; cancelled = stop
extern std::string g_pollCfgDir, g_pollUrl, g_pollKey;   // account config for worker threads (set once at startup)
extern int g_myRank;                           // 3 admin, 2 staff, 1 moderator, 0 player, -1 unknown / not set up
extern okay::MyBanInfo g_myBan;                // my own site ban, if any
extern std::string g_acReportUid, g_acReportName; extern bool g_acReportOpen;   // the "Report cheating" dialog's target
// Public profiles (main.cpp).
void OpenPublicProfile(const std::string& uid, const std::string& username,
                       const std::string& name = std::string(), const std::string& avatar = std::string(),
                       int accent = 0);   // open someone's public profile page
// The official game (main.cpp).
void DrawOfficialAdmin(okay::account::AccountService& account);   // Admin > Official game
// Multiplayer (main.cpp).
void MpLaunch(const okay::JoinLink& in);       // start the game to join a server
// Updates and versions (main.cpp).
void GetNews(std::string& ver, std::string& lines);   // the latest release notes fetched by the update check
int CompareVersions(const std::string& a, const std::string& b);   // <0, 0, >0 like strcmp
// Marketplace listing types (workshop_items.kind).
struct MktKind { const char* key; const char* label; const char* plural; const char* desc; ImVec4 color; };
// theme-allow-begin: the listing type colours (filled tiles and chips, with white text)
static const MktKind kMktKinds[] = {
    {"game",     "Full game",   "Games",       "A finished, playable game (File > Build Game).",   ImVec4(0.26f, 0.55f, 0.85f, 1)},
    {"template", "Template",    "Templates",   "A starter project others build on.",               ImVec4(0.58f, 0.45f, 0.82f, 1)},
    {"model",    "3D model",    "3D models",   "Meshes and characters (.obj, .gltf, .fbx ...).",   ImVec4(0.90f, 0.55f, 0.25f, 1)},
    {"texture",  "Texture",     "Textures",    "Materials, PBR sets, skyboxes.",                   ImVec4(0.30f, 0.70f, 0.55f, 1)},
    {"sprite",   "2D art",      "2D art",      "Sprites, tilesets, UI packs.",                     ImVec4(0.92f, 0.42f, 0.62f, 1)},
    {"audio",    "Audio",       "Audio",       "Music, sound effects, voice.",                     ImVec4(0.95f, 0.75f, 0.25f, 1)},
    {"script",   "Script",      "Scripts",     "OkayScript code, behaviours, visual graphs.",      ImVec4(0.35f, 0.80f, 0.85f, 1)},
    {"tool",     "Tool",        "Tools",       "Editor tools, plugins and utilities.",             ImVec4(0.60f, 0.65f, 0.72f, 1)},
    {"scene",    "Scene",       "Scenes",      "A single scene or level.",                         ImVec4(0.30f, 0.62f, 0.40f, 1)},
};
// theme-allow-end
static const int kMktKindN = (int)(sizeof(kMktKinds) / sizeof(kMktKinds[0]));
int MktKindIndex(const std::string& k);       // the kMktKinds row of a kind key (unknown -> game)
// Selectable accent colors (name, main, dim) — mirror the editor's accent presets.
struct AccentPreset { const char* name; ImVec4 col; ImVec4 dim; };
// theme-allow-begin: the accent colours people pick
const AccentPreset kAccentPresets[] = {
    {"Classic", ImVec4(0.235f, 0.475f, 0.800f, 1), ImVec4(0.172f, 0.360f, 0.600f, 1)},  // muted selection blue (default)
    {"Blue",   ImVec4(0.26f, 0.59f, 0.98f, 1), ImVec4(0.19f, 0.43f, 0.72f, 1)},
    {"Teal",   ImVec4(0.18f, 0.78f, 0.74f, 1), ImVec4(0.13f, 0.56f, 0.53f, 1)},
    {"Violet", ImVec4(0.56f, 0.46f, 0.96f, 1), ImVec4(0.40f, 0.33f, 0.70f, 1)},
    {"Green",  ImVec4(0.36f, 0.80f, 0.46f, 1), ImVec4(0.26f, 0.58f, 0.33f, 1)},
    {"Amber",  ImVec4(0.95f, 0.66f, 0.26f, 1), ImVec4(0.70f, 0.48f, 0.19f, 1)},
    {"Rose",   ImVec4(0.96f, 0.42f, 0.56f, 1), ImVec4(0.70f, 0.30f, 0.41f, 1)},
};
// theme-allow-end
// Multiplayer (main.cpp): joining and inviting, used by the chat and the profile page.
void MpJoin(const std::string& link, const std::string& from);   // Multiplayer, below
bool MpCanInvite();                                             // in a joinable match right now
std::string MpGameTitle();                                      // the game I'm playing ("" = none)
void MpInviteUser(const std::string& uid, const std::string& name);
void MpJoinFriend(const std::string& sessionCode, const std::string& name);
// For a join that opens a project in the editor (MpLaunch, shared/JoinPlan.hpp; main.cpp): your editor projects with
// their engine_version stamps, the editor versions you have (this launcher's own and those under Versions/), and
// opening a project's scene with one of them.
std::vector<okay::joinplan::Candidate> MpJoinProjects();
std::vector<std::string> MpInstalledEditors();
bool MpOpenProjectForJoin(const std::string& editorVersion, const std::string& scene);
void MpInviteGroup(long gid, const std::string& name);         // a whole group chat
std::string MpMyJoin();                                        // my session's public link ("" = none)
void MpPartyInvite(const std::string& uid, const std::string& name);
void DrawRecentPlayersPanel(bool signedIn);                     // Multiplayer's recent players (Friends > Add friend shows it too)   // (makes a party if needed)
void MpPartyJoinFromInvite(const std::string& body);
std::string MpPartyId();                                       // my party ("" = none)
// Playtime and state (main.cpp).
extern std::vector<okay::PlaytimeEntry> g_playtime;   // local playtime totals
void LoadPlaytime();
bool PlaytimeRunning(const std::string& key);   // the game is being played right now
extern bool g_raiseMainReq;                    // something in the chat window wants the launcher in front
extern bool g_openPublishForm;                 // request the Market tab to open its publish form
extern bool g_chatInOwnWindow;                 // true while drawing the chat window's own frame
void OpenChatWith(const std::string& uid, const std::string& name, const std::string& avatar);   // open a chat with someone
// Chat (main.cpp): the message types and helpers the chat screens share.
struct StarredMsg { std::string uid; std::string preview; long when = 0; };
struct SchedMsg { long id = 0; std::string toUid, toName; long toGroup = 0; std::string body; long at = 0; };
struct ChatFileInfo { std::string name, size, url; };
struct ChatPollInfo { std::string question; std::vector<std::string> options; };
struct ChatInviteInfo { std::string title; okay::JoinLink link; };
// Who an invite card is with (the other person in a 1:1 chat): their public name, and their presence join link when the
// friend list has been read (`peerKnown`), so a card can say the match is over or full.
struct ChatInviteCtx { std::string peerUid, peerName; bool peerKnown = false; std::string peerLink; };
struct ChatFwd { bool ok = false; std::string from, body; };
struct ChatVoiceInfo { int secs = 0; std::string url; };
struct ChatPinRef { long id = 0; long when = 0; std::string preview, who; };
struct ChatTbBtn { char icon; const char* tip; };
static constexpr int kChatThemeCount = 8;
void ChatDateSeparator(long when);
bool ChatDemo();
float ChatFileCardWidth(const ChatFileInfo& f, float maxW);
void ChatForward(const std::string& body, const std::string& from);
void ChatForwardedLabel(const std::string& from);
bool ChatHeaderIconButton(const char* id, ImVec2 pos, bool active, const char* tip, ImVec2* centre);
float ChatInviteWidth(float maxW);
bool ChatIsPinned(bool group, long id);
int ChatMsgToolbar(long msgId, ImVec2 rowMin, ImVec2 rowMax, const ChatTbBtn* btns, int nb, bool liked);
void ChatPinToggle(bool group, long id);
float ChatPollWidth(float maxW);
std::string ChatPreview(const std::string& body, std::size_t n);
std::string ChatPreviewSmart(const std::string& body, std::size_t n);
bool ChatSaveEdit(okay::Messages& dm, bool group, long id, const std::string& oldBody, const std::string& newBody);
bool ChatSlashCommand(const std::string& text, const std::string& uid, const std::string& name, long gid);
ImVec4 ChatThemeColor(int idx);
ImVec4 ChatThemeFor(const std::string& uid);
float ChatVoiceWidth(float maxW);
void ComposerPlusButton(const char* id, float size, const std::string& toUid, long toGroup, long toChannel = 0);
long DayKey(long when);
void DrawDotsIcon(ImDrawList* d, ImVec2 c, ImU32 col);
void DrawPinnedBar(const char* id, std::vector<ChatPinRef> pins, bool group, long& jumpTo);
void DrawScheduledStrip(const std::string& toUid, long toGroup);
void DrawVoiceMicButton(const char* id, ImVec2 size, ImVec4 col, const std::string& toUid, long toGroup, long toChannel = 0);
void DrawVoiceRecorderBar(float h, ImVec4 sendCol);
std::string ExpandCustomEmoji(const std::string& text);
void FinishVoiceRecord(bool send);
std::string FirstImageUrl(const std::string& body);
bool HasVisibleReactions(const std::map<std::string, long>& m);
bool IsPollReaction(const std::string& k);
void OpenEditHistory(okay::Messages& dm, bool group, long id, const std::string& current);
bool ParseChatFile(const std::string& body, ChatFileInfo& out);
bool ParseChatInvite(const std::string& body, ChatInviteInfo& out);
bool ParseChatPoll(const std::string& body, ChatPollInfo& p);
bool ParseChatVoice(const std::string& body, ChatVoiceInfo& out);
void RenderChatFileCard(const ChatFileInfo& f, float width);
void RenderChatInvite(const ChatInviteInfo& inv, bool mine, long when, float width, const ChatInviteCtx* ctx = nullptr);
void OpenChatLink(const std::string& url);       // a click on a link in a message (okay::chat::ClickLink: confirm, join or refuse)
void DrawChatLinkConfirm();                       // the "Open link?" question, in the window where the link was clicked
void RenderChatVoice(const ChatVoiceInfo& v, float width, bool mine, ImVec4 accent);
bool VoiceRecordingFor(const std::string& uid, long gid, long channel = 0);
extern volatile bool g_attBusy;
extern std::string   g_attName;
extern std::map<std::string,int>          g_chatColor;
extern volatile bool g_chatForceLog;
extern long g_chatJumpGroup;
extern std::string g_chatLightboxReq;
extern volatile long g_chatSelGroup;
extern std::vector<okay::Messages::GroupMemberInfo> g_groupMembers;
extern long g_groupMembersFor;
extern std::set<std::string>              g_mutedChats;
extern std::set<std::string>              g_pinnedChats;
extern bool g_schedOpenReq;
extern std::vector<SchedMsg>              g_scheduled;
extern std::map<long, StarredMsg>         g_starred;
int DrawOutbox(const std::string& uid, long gid, float avW, float baseX, ImVec4 bubbleCol,
               const std::function<bool(const std::string&, long)>& inLog);   // the not-yet-sent messages of a chat
void DrawScheduleDialog(const char* popupId, char* text, std::size_t cap,
                        const std::string& toUid, const std::string& toName, long toGroup);
void RenderChatPoll(const ChatPollInfo& p, bool group, long id, const std::map<std::string, long>* counts,
                    const std::set<std::string>* mine, float width, bool mineBubble, ImVec4 accent);
void DrawStar(ImDrawList* dl, ImVec2 c, float r, ImU32 col);
bool VoiceRecording();                         // a voice message is being recorded
void OutboxSend(const std::string& toUid, long toGroup, const std::string& body, long replyTo = 0, long toChannel = 0);   // send through the outbox
void OutboxSendInvite(const std::string& toUid, const std::string& body);   // a game invite card to a friend, with its game_invites record
void SavePrefs();                              // write the launcher's preferences file
ChatFwd ParseChatFwd(const std::string& b);    // a forwarded message: who from, and the body
extern std::map<long, long> g_pinsDm, g_pinsGroup;   // message id -> pinned at (unix)
extern ImVec4 kAccentDim;                      // the darker accent
extern const char* kChatThemeNames[];          // the chat colour themes (kChatThemeCount)
extern const ImVec4& kChatWinBg;
void ChatBubbleTextPush(bool mine);   // white text in my chat bubbles (main.cpp)
void ChatBubbleTextPop(bool mine);
bool ReactionPill(ImDrawList* dl, const std::string& key, const std::string& cnt, bool mine);   // a chat reaction pill (main.cpp)                // the chat window's background
// Games, friends and presence (main.cpp): what the multiplayer code shares with the rest.
struct PlayProc {
    std::string key, title; std::uint32_t lastCredit = 0;
#if defined(_WIN32)
    HANDLE h = nullptr;
#else
    pid_t pid = -1;
#endif
};
struct PendingInvite { long id = 0; std::string from, fromName, body; long when = 0; };

std::string AgoText(long when);
bool AntiCheatAllowsSceneLaunch(const std::string& scene);
void DrawGamepadIcon(ImDrawList* d, ImVec2 c, float s, ImU32 col);
std::string FindExe(std::initializer_list<const char*> names);
std::vector<fs::path> FindScenes();
int FriendsListWorker(void*);
std::string GameTitleFromPath(const std::string& path);
std::string InviteMessageBody(const std::string& title, const std::string& link);
bool IsBuiltGame(const fs::path& scene);
std::string LastOnline(long when);
void PlayChatChime();
int MyOnlineStatus();   // your status: okayfriends::kOnline / kAway / kInvisible / kDoNotDisturb (launcher/FriendsList.hpp)
std::string PresenceText();
void RecordPlayed(const std::string& p);
bool SpawnTracked(const std::string& exe, const std::string& args, const std::string& key, const std::string& title);
extern volatile bool g_frFetching;
extern std::vector<okay::Friend> g_frList;
extern okay::thr::Mutex* g_frMutex;
extern volatile long g_inviteSeenId;
extern int g_onlineStatus;
extern bool g_partyFollow;
extern std::vector<PlayProc> g_playProcs;
extern std::vector<PendingInvite> g_pollInvites;
extern okay::thr::Mutex*  g_pollMutex;
extern okay::thr::Mutex* g_presMutex;
extern okay::account::AccountService* g_presenceAccount;
extern bool g_sharePresence;
extern int g_themeIndex;
bool UpsertNotif(const std::string& key, const std::string& title,
                 const std::string& body, int kind, int gotoTab);   // add or update a notification (true = new or changed)
extern std::string g_presText, g_presJoin;     // (guarded by g_presMutex) "" = no game running
} // namespace okl
