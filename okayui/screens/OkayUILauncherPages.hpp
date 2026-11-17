#pragma once
// Three more launcher screens drawn with OkayUI (docs/own_ui_toolkit.md section 8): the Downloads page, the Notifications panel
// and the Achievements page. Same shape as OkayUIScreens.hpp and OkayUIPanels.hpp: a plain function takes the data to show and a
// rectangle, draws into an OkayUI window that fills it, and reports what the person did in its result, so the launcher runs the
// same actions as its Dear ImGui version. No SDL, no Dear ImGui. Written from the launcher's own Dear ImGui pages (what they show
// and do), not from any other project's code.
#include "okay/UI/OkayUI.hpp"

#include <string>
#include <vector>

namespace okayuiscreens {

// ---- small text helpers shared by the three screens -------------------------------------------------------------------------

/// `s` cut to fit `maxW` pixels in the current font and text scale, ending in an ellipsis when cut (never splits a UTF-8
/// character). An empty string for maxW <= 0.
std::string FitTextWidth(const std::string& s, float maxW);
/// The first line of `s` that fits `maxW` (broken at a space), and the rest fitted onto a second line with an ellipsis.
/// A text with no space that fits nothing is fitted onto the first line alone.
void WrapTwoLines(const std::string& s, float maxW, std::string& line1, std::string& line2);

// ---- Downloads (the launcher's page of that name) ---------------------------------------------------------------------------

struct DownloadEntry {
    int id = 0;                          // the launcher's download id (stable while the row exists)
    std::string title, dest;             // dest: where the file is saved (a .zip gets "Install to Community")
    long long received = 0, total = 0;   // bytes; total 0 = unknown
    int status = 0;                      // 0 queued, 1 downloading, 2 completed, 3 failed, 4 canceled (as the launcher's DlItem)
};

struct DownloadsModel { std::vector<DownloadEntry> items; };

struct DownloadsResult {
    OkayUI::Rect openFolderButton, clearFinishedButton, browseButton;   // browseButton: only in the empty state
    std::vector<OkayUI::Rect> rows;      // one per item, in model order
    // Per item (model order); a zero rectangle where the row has no such button in its state.
    std::vector<OkayUI::Rect> cancelButtons, openButtons, installButtons, removeButtons;
    bool clearEnabled = false;           // some download has finished (completed, failed or canceled)
    bool empty = false;                  // "No downloads yet" is showing
    // What the person did this frame (the host runs it):
    bool openFolder = false, clearFinished = false, browseMarketplace = false;
    int cancelId = -1, openItemFolderId = -1, installId = -1, removeId = -1;   // a download id, -1 = none
};

/// "--", "512 B", "1.5 KB", "3.2 MB", "1.25 GB" (as the Dear ImGui page).
std::string HumanBytes(long long bytes);
/// "Queued", "Downloading", "Completed", "Failed", "Canceled".
const char* DownloadStatusText(int status);
/// True for a destination ending in .zip or .ZIP.
bool DownloadIsZip(const std::string& dest);
/// The bar's fill 0..1: received / total, full for a completed download of unknown size.
float DownloadFraction(const DownloadEntry& d);

DownloadsResult DrawDownloads(const DownloadsModel& model, float x, float y, float w, float h);

// ---- Notifications (the launcher's flyout panel) ----------------------------------------------------------------------------

struct NotifEntry {
    std::string title, body;
    int kind = 0;                        // 0 system, 1 friend, 2 message, 3 update (anything else counts as system)
    bool unread = false;                 // unread, or new since the panel opened
    unsigned ageSeconds = 0;             // how long ago it arrived
    int day = 2;                         // 0 today, 1 yesterday, 2 earlier (the host knows the calendar)
};

/// The cleared notifications are left out by the host; `items` is the feed in the host's order.
struct NotificationsModel {
    std::vector<NotifEntry> items;
    bool drawFrame = true;               // the window's own background, border and shadow; false when the host's window already draws them
};

struct NotificationsState { int filter = 0; };   // 0 All, 1 Unread, 2 Friends, 3 Messages, 4 Updates, 5 System

struct NotificationsResult {
    OkayUI::Rect markAllButton, clearButton, closeButton, list;
    std::vector<OkayUI::Rect> chips;     // the six filter chips
    std::vector<OkayUI::Rect> rows;      // the rows laid out, in display order (parallel to `shown`)
    std::vector<int> shown;              // model indexes that pass the filter, newest first
    OkayUI::Rect dismissButton;          // on the hovered row only (zero otherwise)
    int total = 0, unread = 0;
    bool empty = false;                  // the empty state is showing
    std::string emptyTitle, emptyHint;   // its two lines
    // What the person did this frame:
    bool markAllRead = false, clearAll = false, close = false;
    int clicked = -1, dismissed = -1;    // model indexes, -1 = none (a click on the dismiss button is not also a row click)
};

/// The filter the chips apply.
bool NotifPassesFilter(const NotifEntry& n, int filter);
/// "now", "5m", "3h", "2d".
std::string NotifAgeText(unsigned ageSeconds);
/// The model indexes that pass `filter`, newest first (stable for equal ages).
std::vector<int> NotifOrder(const NotificationsModel& model, int filter);
/// The empty state's title and hint for the counts and the filter (as the Dear ImGui panel words them).
void NotifEmptyText(int total, int filter, std::string& title, std::string& hint);

/// Draw the panel as a window at (x, y, w, h): a floating window (not bare), so it stays in front of a hosted page and
/// takes the clicks over it.
NotificationsResult DrawNotifications(const NotificationsModel& model, NotificationsState& state, float x, float y, float w, float h);

// ---- Achievements (the launcher's page of that name) ------------------------------------------------------------------------

struct AchEntry {
    std::string id, label;               // label empty: the id is shown prettified ("first_win" -> "First Win")
    long when = 0;                       // unix time it was unlocked
    int points = 10;
    std::string date, rel;               // "Mar 04, 2026" and "2d ago", formatted by the host (local time)
    unsigned char color[3] = {150, 150, 200};   // the medal colour (the launcher's name colour for the id)
    bool showcased = false;
};

struct AchievementsModel {
    std::vector<AchEntry> items;
    long now = 0;                        // unix time now
    int points = 0, level = 1;           // total points and the level they make (1-based)
    float levelProgress = 0.0f;          // 0..1 toward the next level
    int showcaseCount = 0, showcaseMax = 6;
};

struct AchievementsState {
    int view = 0;                        // 0 all, 1 this month, 2 showcased
    int sort = 0;                        // 0 newest first, 1 oldest first, 2 A to Z
    char filter[64] = "";
};

struct AchievementsResult {
    OkayUI::Rect profileButton, levelBadge, sortCombo, searchField, browseButton;
    std::vector<OkayUI::Rect> chips;     // All, This month, Showcased
    std::vector<OkayUI::Rect> tiles;     // parallel to `shown`
    OkayUI::Rect menuShowcase, menuCopyId;   // the open right-click menu's entries (zero while it is closed)
    std::vector<int> shown;              // model indexes after view, search and sort
    int columns = 1, monthCount = 0;
    bool empty = false;
    std::string emptyTitle;
    // What the person did this frame:
    bool viewProfile = false, browseGames = false;
    std::string addShowcase, removeShowcase, copyId;   // an achievement id, empty = none
};

/// "first_win" -> "First Win".
std::string AchPrettyId(const std::string& id);
/// The model indexes the page shows for a view, sort and search text (case-insensitive, label and id).
std::vector<int> AchList(const AchievementsModel& model, int view, int sort, const std::string& filter);
/// How many were unlocked in the last 30 days.
int AchMonthCount(const AchievementsModel& model);

AchievementsResult DrawAchievements(const AchievementsModel& model, AchievementsState& state, float x, float y, float w, float h);

} // namespace okayuiscreens
