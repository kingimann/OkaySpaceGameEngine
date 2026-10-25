#include "test_framework.hpp"
#include "AntiCheatBackend.hpp"
#include "AntiCheatHardware.hpp"

// Anti-cheat P4: the salted hardware hash, and the requests the launcher's
// Admin > Anti-cheat page and the player's appeal flow send to Supabase
// (docs/sql/anticheat.sql), plus parsing what comes back. No network here.
namespace {
using namespace okayac;
namespace be = okayac::backend;

bool Has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }

HardwareInfo Sample() {
    HardwareInfo h;
    h.parts = {{"machine_guid", "4C4C4544-0042-3510-8058-B2C04F4E3432"},
               {"volume_serial", "A1B2C3D4"},
               {"cpu", "GenuineIntel Intel(R) Core(TM) i7-9700K CPU @ 3.60GHz"}};
    return h;
}

void TestHardwareHash() {
    RUN_SUITE("salted hardware hash");
    const HardwareInfo a = Sample();
    const std::string h1 = HardwareHash(a, "salt-one");
    CHECK(h1.size() == 64 && h1.find_first_not_of("0123456789abcdef") == std::string::npos);
    // Stable: same machine, same salt, same hash (and order / case / spacing don't matter).
    CHECK(HardwareHash(a, "salt-one") == h1);
    HardwareInfo b;
    b.parts = {{"CPU", "  genuineintel intel(r) core(tm) i7-9700k cpu @ 3.60ghz "},
               {"volume_serial", "a1b2c3d4"},
               {"machine_guid", "4c4c4544-0042-3510-8058-b2c04f4e3432"}};
    CHECK(HardwareHash(b, "salt-one") == h1);
    // Salted: another deployment's salt gives an unrelated hash.
    const std::string h2 = HardwareHash(a, "salt-two");
    CHECK(h2.size() == 64 && h2 != h1);
    CHECK(HardwareHash(a, "") != h1);
    // Another machine: another hash.
    HardwareInfo c = a;
    c.parts[1].second = "FFFF0000";
    CHECK(HardwareHash(c, "salt-one") != h1);
    // The raw IDs never appear in what leaves the machine.
    for (const auto& p : a.parts) CHECK(!Has(h1, og::Lower(p.second)) && !Has(h1, p.second));
    // Empty values are left out; nothing to go on = no hash at all.
    HardwareInfo d = a;
    d.parts.push_back({"disk", "   "});
    CHECK(HardwareHash(d, "salt-one") == h1);
    CHECK(HardwareHash(HardwareInfo{}, "salt-one").empty());
    // The canonical form is what's documented: sorted name=value lines.
    CHECK(CanonicalHardware(a) == "cpu=genuineintel intel(r) core(tm) i7-9700k cpu @ 3.60ghz\n"
                                  "machine_guid=4c4c4544-0042-3510-8058-b2c04f4e3432\nvolume_serial=a1b2c3d4\n");
    // Collected twice on this machine: the same (whatever the platform offers).
    const HardwareInfo here = CollectHardware();
    CHECK(HardwareHash(here, "x") == HardwareHash(CollectHardware(), "x"));
    CHECK(!DeploymentSalt().empty());
    CHECK(MachineHash() == MachineHash());
}

void TestTimes() {
    RUN_SUITE("timestamps");
    CHECK(be::IsoTime(0) == "1970-01-01T00:00:00Z");
    CHECK(be::IsoTime(1792929600) == "2026-10-25T12:00:00Z");
    CHECK(be::ParseTime("2026-10-25T12:00:00+00:00") == 1792929600);
    CHECK(be::ParseTime("2026-10-25T12:00:00.123456+00:00") == 1792929600);
    CHECK(be::ParseTime("2026-10-25T12:00:00Z") == 1792929600);
    CHECK(be::ParseTime("2026-10-25T14:00:00+02:00") == 1792929600);
    CHECK(be::ParseTime("2026-10-25 12:00:00") == 1792929600);
    CHECK(be::ParseTime("") == 0 && be::ParseTime("garbage") == 0);
    CHECK(be::ExpiryFor(0, 1000) == 0);                  // permanent
    CHECK(be::ExpiryFor(1, 1000) == 1000 + 86400);       // 1 day
    CHECK(be::ExpiryFor(2, 1000) == 1000 + 7 * 86400);
    CHECK(be::ExpiryFor(3, 1000) == 1000 + 30 * 86400);
}

const std::string kUid = "0b5a4c1e-2f3d-4e5f-8a9b-0c1d2e3f4a5b";
const std::string kHw = std::string(64, 'a');

void TestPlayerRequests() {
    RUN_SUITE("player requests: report, my bans, appeal");
    be::Request r;
    std::string err;
    CHECK(be::FileReport(kUid, "Neon Drift", "Aimbot", "snapped to heads all match", "", r, &err));
    CHECK(r.method == "POST" && r.path == "/rest/v1/ac_reports" && Has(r.header, "return=minimal"));
    CHECK(Has(r.body, "\"reported\":\"" + kUid + "\"") && Has(r.body, "\"kind\":\"player\"") && Has(r.body, "\"reason\":\"Aimbot\""));
    CHECK(!Has(r.body, "reporter") && !Has(r.body, "status"));   // the server fills those (RLS)
    // Bad input is refused before anything is sent.
    CHECK(!be::FileReport("not-a-uuid", "g", "x", "", "", r, &err) && !err.empty());
    CHECK(!be::FileReport(kUid, "g", "   ", "", "", r, &err));
    CHECK(!be::FileReport(kUid, "g", "x", "", "nothex", r, &err));
    // Quotes, newlines and control characters are escaped / dropped; long text is capped.
    CHECK(be::FileReport(kUid, "g", "say \"hi\"\n\x01", std::string(9000, 'd'), "", r));
    CHECK(Has(r.body, "say \\\"hi\\\"") && !Has(r.body, "\x01"));
    CHECK(r.body.size() < 5000);

    // A client check the player's own game filed (no reported player, with the hw hash).
    CHECK(be::FileClientCheck("Neon Drift", "module", "unexpected module loaded: C:\\x.dll", kHw, r));
    CHECK(Has(r.body, "\"kind\":\"client_check\"") && Has(r.body, "\"hw_hash\":\"" + kHw + "\"") && !Has(r.body, "\"reported\""));
    CHECK(Has(r.body, "C:\\\\x.dll"));

    r = be::MyBans(kHw);
    CHECK(r.method == "POST" && r.path == "/rest/v1/rpc/ac_my_bans" && r.body == "{\"p_hw_hash\":\"" + kHw + "\"}");
    r = be::MyBans("");
    CHECK(r.body == "{\"p_hw_hash\":null}");

    CHECK(be::FileAppeal(42, "I was using a screen reader", kHw, r, &err));
    CHECK(r.path == "/rest/v1/rpc/ac_file_appeal" && Has(r.body, "\"p_ban\":42") && Has(r.body, "\"p_message\":\"I was using a screen reader\""));
    CHECK(!be::FileAppeal(0, "x", "", r, &err));
    CHECK(!be::FileAppeal(42, "  ", "", r, &err));
}

void TestAdminRequests() {
    RUN_SUITE("admin requests: reports, ban / unban, appeals");
    be::Request r = be::ListReports("open", 50);
    CHECK(r.method == "GET" && Has(r.path, "/rest/v1/ac_reports?select=") && Has(r.path, "status=eq.open") &&
          Has(r.path, "order=created_at.desc") && Has(r.path, "limit=50"));
    r = be::ListReports("", 500);
    CHECK(!Has(r.path, "status=eq.") && Has(r.path, "limit=200"));   // all, capped
    r = be::ListReports("open&delete=1", 10);                       // not a status: no filter smuggled in
    CHECK(!Has(r.path, "delete"));

    std::string err;
    CHECK(be::SetReportStatus(7, "dismissed", "duplicate", r, &err));
    CHECK(r.method == "PATCH" && r.path == "/rest/v1/ac_reports?id=eq.7" && Has(r.body, "\"status\":\"dismissed\"") &&
          Has(r.body, "\"review_note\":\"duplicate\""));
    CHECK(!be::SetReportStatus(7, "deleted", "", r, &err));

    // Ban: reason required, expiry optional (0 = permanent) and in the future.
    const long now = 1792929600;
    CHECK(be::BanPlayer(kUid, kHw, "Speedhack (clock 40% fast)", now + 86400, 7, now, r, &err));
    CHECK(r.method == "POST" && r.path == "/rest/v1/ac_bans" && Has(r.body, "\"user_id\":\"" + kUid + "\"") &&
          Has(r.body, "\"hw_hash\":\"" + kHw + "\"") && Has(r.body, "\"until\":\"2026-10-26T12:00:00Z\"") &&
          Has(r.body, "\"report_id\":7"));
    CHECK(be::BanPlayer(kUid, "", "Cheating", 0, 0, now, r, &err));
    CHECK(Has(r.body, "\"until\":null") && Has(r.body, "\"hw_hash\":null") && Has(r.body, "\"report_id\":null"));
    CHECK(!be::BanPlayer(kUid, "", "", 0, 0, now, r, &err) && !err.empty());        // no reason
    CHECK(!be::BanPlayer(kUid, "", "x", now - 5, 0, now, r, &err));                 // expiry in the past
    CHECK(!be::BanPlayer("", "", "x", 0, 0, now, r, &err));                         // nobody
    CHECK(be::BanPlayer("", kHw, "ban evasion", 0, 0, now, r, &err));               // hardware only
    CHECK(!be::BanPlayer("'; drop table--", "", "x", 0, 0, now, r, &err));

    CHECK(be::Unban(9, "appeal accepted", r, &err));
    CHECK(r.method == "PATCH" && r.path == "/rest/v1/ac_bans?id=eq.9" && Has(r.body, "\"lifted\":true") &&
          Has(r.body, "\"lift_reason\":\"appeal accepted\""));
    CHECK(!be::Unban(0, "x", r, &err));

    r = be::ListBans(true, now);
    CHECK(r.method == "GET" && Has(r.path, "/rest/v1/ac_bans?select=") && Has(r.path, "lifted=is.false") &&
          Has(r.path, "or=(until.is.null,until.gt.2026-10-25T12:00:00Z)"));
    r = be::ListBans(false, now);
    CHECK(!Has(r.path, "lifted=is.false"));

    r = be::ListAppeals("open");
    CHECK(Has(r.path, "/rest/v1/ac_appeals?select=") && Has(r.path, "status=eq.open"));
    CHECK(be::DecideAppeal(3, true, "false positive", r, &err));
    CHECK(r.method == "PATCH" && r.path == "/rest/v1/ac_appeals?id=eq.3" && Has(r.body, "\"status\":\"accepted\"") &&
          Has(r.body, "\"decision_note\":\"false positive\""));
    CHECK(be::DecideAppeal(3, false, "", r, &err) && Has(r.body, "\"status\":\"rejected\""));

    r = be::UserByName(" maya&id=eq.x ");
    CHECK(r.path == "/rest/v1/profiles?select=id,username&limit=1&username=eq.maya%26id%3Deq.x");
    r = be::Usernames({kUid, "bad id", "1b5a4c1e-2f3d-4e5f-8a9b-0c1d2e3f4a5b"});
    CHECK(Has(r.path, "/rest/v1/profiles?select=id,username&id=in.(" + kUid + ",1b5a4c1e-2f3d-4e5f-8a9b-0c1d2e3f4a5b)"));
}

void TestParsing() {
    RUN_SUITE("parsing what Supabase answers");
    std::vector<be::Report> reps;
    CHECK(be::ParseReports(
        "[{\"id\":7,\"reporter\":\"" + kUid + "\",\"reported\":null,\"kind\":\"client_check\",\"game\":\"Neon Drift\","
        "\"reason\":\"clock\",\"details\":\"the game clock runs 40.0% faster\",\"hw_hash\":\"" + kHw + "\","
        "\"status\":\"open\",\"review_note\":null,\"created_at\":\"2026-10-25T12:00:00.5+00:00\"},"
        "{\"id\":8,\"reporter\":\"" + kUid + "\",\"reported\":\"" + kUid + "\",\"kind\":\"player\",\"game\":null,"
        "\"reason\":\"Aimbot \\\"obvious\\\"\",\"details\":null,\"hw_hash\":null,\"status\":\"reviewing\",\"review_note\":\"looking\","
        "\"created_at\":\"2026-10-24T12:00:00+00:00\"}]", reps));
    CHECK(reps.size() == 2);
    CHECK(reps[0].id == 7 && reps[0].kind == "client_check" && reps[0].reported.empty() && reps[0].hwHash == kHw &&
          reps[0].created == 1792929600 && reps[0].status == "open");
    CHECK(reps[1].reason == "Aimbot \"obvious\"" && reps[1].reviewNote == "looking" && reps[1].game.empty());
    CHECK(!be::ParseReports("{\"message\":\"permission denied\"}", reps));   // an error object, not rows
    CHECK(!be::ParseReports("not json", reps));
    CHECK(be::ParseReports("[]", reps) && reps.empty());

    std::vector<be::Ban> bans;
    const long now = 1792929600;
    CHECK(be::ParseBans(
        "[{\"id\":9,\"user_id\":\"" + kUid + "\",\"hw_hash\":null,\"reason\":\"Speedhack\",\"until\":null,"
        "\"report_id\":7,\"created_at\":\"2026-10-25T10:00:00+00:00\",\"lifted\":false,\"lifted_at\":null,\"lift_reason\":null},"
        "{\"id\":10,\"user_id\":\"" + kUid + "\",\"hw_hash\":\"" + kHw + "\",\"reason\":\"x\",\"until\":\"2026-10-25T11:00:00+00:00\","
        "\"report_id\":null,\"created_at\":\"2026-10-24T10:00:00+00:00\",\"lifted\":false,\"lifted_at\":null,\"lift_reason\":null},"
        "{\"id\":11,\"user_id\":\"" + kUid + "\",\"hw_hash\":null,\"reason\":\"y\",\"until\":null,"
        "\"report_id\":null,\"created_at\":\"2026-10-24T10:00:00+00:00\",\"lifted\":true,\"lifted_at\":\"2026-10-25T09:00:00+00:00\",\"lift_reason\":\"oops\"}]",
        bans));
    CHECK(bans.size() == 3);
    CHECK(bans[0].id == 9 && bans[0].Active(now) && bans[0].until == 0 && bans[0].reportId == 7);
    CHECK(!bans[1].Active(now) && bans[1].until == now - 3600);   // expired
    CHECK(!bans[2].Active(now) && bans[2].lifted > 0 && bans[2].liftReason == "oops");

    std::vector<be::Appeal> aps;
    CHECK(be::ParseAppeals("[{\"id\":3,\"ban_id\":9,\"user_id\":\"" + kUid + "\",\"message\":\"please\\nlook\",\"status\":\"open\","
                           "\"decision_note\":null,\"created_at\":\"2026-10-25T12:00:00+00:00\",\"decided_at\":null}]", aps));
    CHECK(aps.size() == 1 && aps[0].banId == 9 && aps[0].message == "please\nlook" && aps[0].status == "open" && aps[0].decided == 0);

    std::vector<be::MyBan> mine;
    CHECK(be::ParseMyBans("[{\"id\":9,\"reason\":\"Speedhack\",\"until\":null,\"created_at\":\"2026-10-25T10:00:00+00:00\","
                          "\"appeal_status\":\"rejected\",\"appeal_note\":\"clear evidence\",\"appeals_left\":1}]", mine));
    CHECK(mine.size() == 1 && mine[0].id == 9 && mine[0].appealStatus == "rejected" && mine[0].appealNote == "clear evidence" &&
          mine[0].appealsLeft == 1 && mine[0].CanAppeal());
    CHECK(be::ParseMyBans("[{\"id\":9,\"reason\":\"x\",\"until\":null,\"created_at\":null,\"appeal_status\":\"open\",\"appeals_left\":2}]", mine));
    CHECK(!mine[0].CanAppeal());   // one already waiting

    std::map<std::string, std::string> names;
    CHECK(be::ParseUsernames("[{\"id\":\"" + kUid + "\",\"username\":\"maya\"}]", names) && names[kUid] == "maya");

    // Errors PostgREST sends back, in words for a toast.
    CHECK(Has(be::ErrorText(403, "{\"message\":\"new row violates row-level security policy\"}"), "permission"));
    CHECK(Has(be::ErrorText(404, "{\"message\":\"relation \\\"public.ac_bans\\\" does not exist\"}"), "anticheat.sql"));
    CHECK(Has(be::ErrorText(400, "{\"message\":\"Too many reports; try again later\"}"), "Too many reports"));
    CHECK(Has(be::ErrorText(0, ""), "reach"));
}
} // namespace

int main() {
    TestHardwareHash();
    TestTimes();
    TestPlayerRequests();
    TestAdminRequests();
    TestParsing();
    TEST_MAIN_RESULT();
}
