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
};
struct FollowRow {
    std::string id, username, display, seen;   // seen: "seen 3h ago", or empty
    bool online = false;
    unsigned char avatar[3] = {80, 80, 110};
};
struct FeedRow {
    std::string who, text, ago;          // text: the whole sentence ("Maya Chen is playing Neon Drift")
    unsigned char avatar[3] = {80, 80, 110};
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

} // namespace okayuiscreens
