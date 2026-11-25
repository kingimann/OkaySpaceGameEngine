#pragma once
// Reports and the Admin page (AdminPage.cpp). Moved out of launcher/main.cpp unchanged
// (docs/engine/modularization_plan.md, Part C section 20, batch L5).
#include "LauncherShared.hpp"
#include "UserReports.hpp"   // okayrep::Target

namespace okl {

void OpenReport(okayrep::Target t, const std::string& id, const std::string& label);   // every "Report ..." control opens this
void DrawReportPopup();      // the "Report ..." dialog (once per frame)
void DrawAcReportPopup();    // "Report cheating" (drawn from the chat window)
void DrawAdminPage(okay::account::AccountService& account);   // the Admin page (moderators, staff, admins)
void DrawBanBanner();                                          // the banner for a banned player, at the top of every page
} // namespace okl
