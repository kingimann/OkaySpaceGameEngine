#pragma once
// Three more launcher pages drawn with OkayUI (docs/own_ui_toolkit.md section 8.8): Friends, Community and Versions. Same shape
// as OkayUILauncherPages.hpp: a plain function takes the data to show and a rectangle, draws into an OkayUI window that fills it,
// and reports what the person did in its result, so the launcher runs the same actions as its Dear ImGui version. No SDL, no
// Dear ImGui. Written from the launcher's own Dear ImGui pages (what they show and do, their sizes and colours), not from any
// other project's code.
#include "okay/UI/OkayUI.hpp"

#include <string>
#include <vector>

namespace okayuiscreens {

/// `text` broken into lines no wider than `maxW` at spaces (a word wider than the line is cut where it no longer fits), in the
/// current font and text scale. Newlines start a new line. What the launcher's wrapped text does.
std::vector<std::string> WrapLines(const std::string& text, float maxW);

// ---- Friends (the launcher's page of that name) ----------------------------------------------------------------------------

struct FriendRow {
    long rowId = 0;                      // the friendship row (stable while it exists)
    std::string id, username, name;      // name: the nickname, else the display name, else the username (the host decides)
    int kind = 0;                        // 0 friend, 1 incoming request, 2 sent request
    bool online = false, favorite = false, joinable = false, hasNickname = false;
    std::string playing, customStatus, lastOnline;   // lastOnline: "Last online 3h ago", or empty
    long lastSeen = 0;                   // unix time last online (orders the offline friends, most recent first)
    unsigned char avatar[3] = {80, 80, 110};         // the initial's disc colour
    SDL_Texture* picture = nullptr;      // the profile picture (drawn round instead of the disc), or null
};
struct FollowRow {
    std::string id, username, display, seen;   // seen: "seen 3h ago", or empty
    bool online = false;
    unsigned char avatar[3] = {80, 80, 110};
    SDL_Texture* picture = nullptr;      // the profile picture, or null
};
struct FeedRow {
    std::string who, text, ago;          // text: the whole sentence ("Maya Chen is playing Neon Drift")
    unsigned char avatar[3] = {80, 80, 110};
    SDL_Texture* picture = nullptr;      // the profile picture, or null
};

struct FriendsModel {
    std::vector<FriendRow> friends;
    std::vector<FollowRow> following, followers;
    std::vector<FeedRow> feed;           // newest first; the page shows 8 at most
    std::string code;                    // my friend code ("" while it is being read)
    int presence = 0;                    // 0 online, 1 away, 2 invisible
    bool followSupported = true, followBusy = false;
    bool canInvite = false;              // a game is running that friends can be invited to
    std::string inviteLabel = "Invite to game", partyLabel = "Start a party with them";
    std::string addError, addOk;         // the Add friend card's last message
    unsigned revision = 0;               // the host bumps it whenever it rebuilds the model (0 = never reuse last frame's sorting)
};

/// What both versions of the page share (the host keeps one and hands it to whichever draws).
struct FriendsState {
    int view = 0;                        // 0 Online, 1 All, 2 Pending, 3 Add friend, 4 Following, 5 Followers
    char filter[64] = "";
    char addBuf[48] = "";
    char followBuf[48] = "";
};

/// One shown section ("ONLINE", "FAVORITES", ...) of the list views, with the model indexes of its rows.
struct FriendSection { const char* title = ""; int kind = 0; std::vector<int> rows; };
/// The sections a view shows for a search text: Online = favourites who are online then the rest online; All = favourites,
/// online, offline; Pending = incoming then sent. Online first then name within a section, offline by last seen.
std::vector<FriendSection> FriendSections(const FriendsModel& model, int view, const std::string& filter);
/// The status line under a friend's name and whether it is in the accent colour (1), green (2) or dim (0).
std::string FriendStatusLine(const FriendRow& f, int* colour = nullptr);

struct FriendsResult {
    std::vector<OkayUI::Rect> chips;     // Online, All, Pending, Following, Followers, Add friend
    OkayUI::Rect presenceButton, searchField, copyCodeButton, nudge, addField, sendButton, followField, followButton;
    std::vector<int> shown;              // model indexes of the friend rows laid out, in order
    std::vector<OkayUI::Rect> rows, messageButtons, joinButtons, moreButtons, acceptButtons, declineButtons;   // parallel to `shown`
    std::vector<OkayUI::Rect> followRows, followButtons, followBackButtons;   // the follow rows and their buttons (zero where none)
    std::vector<OkayUI::Rect> presenceItems, menuItems;   // the open presence menu's three rows; the open friend menu's entries
    std::vector<std::string> menuLabels;
    bool empty = false;
    std::string emptyTitle;
    // What the person did this frame (indexes into model.friends, -1 = none):
    bool viewChanged = false, copyCode = false;
    int presence = -1;
    int openProfile = -1, message = -1, join = -1, accept = -1, remove = -1, invite = -1, party = -1, toggleFavorite = -1,
        setNickname = -1, clearNickname = -1, copyUsername = -1;
    std::string addRequest;              // Send request (the text typed)
    std::string follow;                  // follow by username (the text typed, without a leading @)
    std::string unfollow, removeFollower, followBack;   // a user id
    int openFollowProfile = -1;          // index into the list shown (following or followers)
};

FriendsResult DrawFriends(const FriendsModel& model, FriendsState& state, float x, float y, float w, float h);

// ---- Community (the launcher's page of that name) --------------------------------------------------------------------------

struct CommunityItem {
    std::string title;                   // the item's folder name
    std::string file;                    // the scene's file name (searched, and shown with the age)
    std::string meta;                    // "Game  ·  1.2 MB"
    std::string ago;                     // "3 d ago", or empty
    unsigned char tile[3] = {100, 110, 130};   // the initial tile's colour (the launcher's muted name colour)
    SDL_Texture* cover = nullptr;        // the game's cover art (drawn with rounded corners instead of the tile), or null
};
struct CommunityTemplate {
    std::string name, desc;
    unsigned char tile[3] = {100, 110, 130};
};
struct CommunityModel {
    std::vector<CommunityItem> items;    // everything in the community library
    std::vector<CommunityTemplate> templates;
    bool havePlayer = true;              // the player runtime is next to the launcher (Play works)
    bool haveEditor = true;              // the editor is next to the launcher (a template opens New Project)
};
/// The search texts live in the host (the launcher keeps them for Ctrl+F and other pages); the page edits them in place.
struct CommunityState {
    char* libFilter = nullptr; int libCap = 0;
    char* tplFilter = nullptr; int tplCap = 0;
    bool focusLibrarySearch = false;     // Ctrl+F: put the keyboard in the library search (cleared when done)
};

/// The library items a search text shows (case-insensitive over the file name and the title), in model order.
std::vector<int> CommunityFilter(const CommunityModel& model, const std::string& filter);
/// The templates a search text shows (over the name and the description).
std::vector<int> CommunityTemplateFilter(const CommunityModel& model, const std::string& filter);

struct CommunityResult {
    OkayUI::Rect publishButton, folderButton, refreshButton, dropZone, librarySearch, templateSearch, shareFolderButton, sharePublishButton;
    std::vector<int> shown;              // library items shown
    std::vector<OkayUI::Rect> cards, playButtons, revealButtons, moreButtons;   // parallel to `shown`
    std::vector<int> templatesShown;
    std::vector<OkayUI::Rect> templateCards;   // parallel to `templatesShown`
    std::vector<OkayUI::Rect> menuItems;   // the open item menu's entries
    std::vector<std::string> menuLabels;
    bool libraryEmpty = false;
    std::string emptyTitle;
    // What the person did this frame:
    bool publish = false, openFolder = false, refresh = false, revealGames = false;   // revealGames: the launcher's own folder
    int play = -1, reveal = -1, copyPath = -1, remove = -1;   // indexes into model.items
    int useTemplate = -1;                // index into model.templates
};

CommunityResult DrawCommunity(const CommunityModel& model, CommunityState& state, float x, float y, float w, float h);

// ---- Versions (the launcher's page of that name) ---------------------------------------------------------------------------
// The launcher's Versions module (launcher/VersionsUI.cpp) decides everything the page says (it holds the rules and the jobs) and
// hands it over ready to show; this page only lays it out and reports clicks.

struct VersionRow {
    std::string version, title, sub, size;   // title: "v2.880.0"; size: installed rows only
    bool latest = false, lts = false, isDefault = false, highlight = false;
    // installed rows
    bool makeDefaultDisabled = false;
    // available rows
    bool downloading = false, cancelDisabled = false, installDisabled = false;
    float progress = 0.0f;
    std::string overlay;                 // the progress bar's text
    std::string note;                    // the last message about this version (a failed install), or empty
    bool noteBad = false;
};

struct VersionsModel {
    std::string subtitle;                // under the page title
    bool banner = false, bannerGreen = false;
    std::string bannerText;
    std::string runningTitle, runningSub;
    bool runningLatest = false, runningLts = false, checking = false;
    std::string status;                  // under "You're running": the version list's state
    int statusKind = 0;                  // 0 none, 1 dim, 2 red, 3 amber (wrapped)
    int channel = 0;                     // 0 Latest, 1 Long-term support
    std::string channelText, channelDim; // the channel's explanation (channelDim: the second, dim paragraph for LTS)
    // LTS: what the channel asks for now
    int ltsKind = -1;                    // -1 none, 0 a sentence and an accent button, 1 a sentence and "Update the launcher", 2 an amber note, 3 a dim note
    std::string ltsSentence, ltsButton;
    bool ltsButtonDisabled = false;
    int ltsDefault = -1;                 // -1 none, 0 "<v> is your default editor." and its undo button, 1 the accent "Use ... as my default editor"
    std::string ltsDefaultText, ltsDefaultButton;
    bool defaultRow = false;             // "Default editor" with its combo
    std::vector<std::string> defaultOptions;   // [0] this version, then the installed ones
    int defaultSelected = 0;
    std::vector<VersionRow> installed;
    std::string installedEmpty;          // the text when nothing is installed
    int keep = 1;                        // "Keep previous versions:" 0..9
    std::string usage;                   // "The Versions folder uses 0 B."
    std::string prune;                   // "2 kept copies are over the limit ...", or empty
    int availableState = 0;              // 0 looking it up, 1 could not be read, 2 the list
    std::string availableError;
    bool retryDisabled = false;
    std::vector<VersionRow> available;
    std::string moreButton;              // "Show 4 older versions" / "Show fewer", or empty
};

struct VersionsResult {
    OkayUI::Rect dismissButton, checkButton, folderButton, channelCombo, ltsButton, ltsDefaultButton, defaultCombo, keepCombo,
        pruneButton, retryButton, moreButton;
    std::vector<OkayUI::Rect> installedRows, makeDefaultButtons, removeButtons;   // parallel to model.installed
    std::vector<OkayUI::Rect> availableRows, installButtons, cancelButtons;       // parallel to model.available
    float contentHeight = 0.0f;          // the whole page, from y (the host makes its scroll range this tall)
    // What the person did this frame:
    bool dismissBanner = false, checkAgain = false, openFolder = false, ltsAction = false, ltsDefaultAction = false, pruneNow = false,
         retry = false, cancelDownload = false, toggleShowAll = false;
    int channel = -1, defaultChoice = -1, keep = -1;      // a new value, -1 = unchanged
    int makeDefault = -1, remove = -1, install = -1;      // a row index, -1 = none
};

/// The page laid out from (x, y), `w` wide, as tall as it needs (contentHeight); only what falls inside the visible band
/// viewY0..viewY1 is drawn and takes clicks. The host scrolls it (y moves up) the way it scrolls its own page.
VersionsResult DrawVersions(const VersionsModel& model, float x, float y, float w, float viewY0, float viewY1);

} // namespace okayuiscreens
