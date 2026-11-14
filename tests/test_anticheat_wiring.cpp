#include "test_framework.hpp"
#include "AntiCheatAdminTools.hpp"
#include "okay/Net/DedicatedServer.hpp"
#include "okay/Net/DetectInspector.hpp"

// Anti-cheat wiring: (1) a kick / ban request's evidence bundle rides on the
// server_kick report (KickReporter + AntiCheatBackend), (2) the launcher admin
// helpers ac_linked_accounts / ac_ban_with_devices (requests and row parsing), and
// (3) the editor inspector's detect.* model (field table, observe mode, text round
// trip). No network, no UI, no database here; the SQL side is tests/sql/test_anticheat.sql.
namespace {
using namespace okay;
namespace be = okayac::backend;

bool Has(const std::string& s, const std::string& part) { return s.find(part) != std::string::npos; }
const std::string kUid = "6f94da84-5a9b-48c6-a2d0-90059bd6bf79";
const std::string kUid2 = "0b5a4c1e-0000-4000-8000-000000000002";

EvidenceBundle SampleBundle() {
    EvidenceBundle b;
    b.game = "Arena"; b.server = "arena-3"; b.peer = 4; b.risk = 95.5f; b.action = "kick"; b.t = 812.5;
    b.account = kUid; b.address = "203.0.113.9:45000";
    b.counts = {{"teleport", 2}, {"speed_window", 4}};
    b.thresholds = {{"maxSpeed", 8.0f}};
    Evidence e; e.kind = DetectKind::Teleport; e.t = 801.2; e.value = 91.4f; e.threshold = 24.5f; e.weight = 30;
    e.detail = "moved 91 units in 50 ms token=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    b.evidence.push_back(e);
    return b;
}

// ---- (1) evidence on the report -----------------------------------------------------
void TestBackendEvidence() {
    RUN_SUITE("server kick report: evidence in the request");
    be::Request r;
    const std::string ev = "{\"v\":1,\"risk\":95.5}";
    CHECK(be::FileServerKick(kUid, "Arena", "[server check] kicked", "player: zed", r, nullptr, ev));
    CHECK(Has(r.body, ",\"evidence\":{\"v\":1,\"risk\":95.5}"));
    og::json::Value root;
    CHECK(og::json::Reader(r.body).Parse(root) && root.kind == og::json::Value::Object);   // the whole body is still valid JSON
    const og::json::Value* e = root.get("evidence");
    CHECK(e && e->kind == og::json::Value::Object);
    // No evidence: the request is what it was before.
    CHECK(be::FileServerKick(kUid, "Arena", "[server check] kicked", "d", r));
    CHECK(!Has(r.body, "evidence"));
    // Not an object / not JSON / too big: left off, the report is still built.
    for (const std::string bad : {std::string("[1,2]"), std::string("\"x\""), std::string("{oops"), std::string("{\"a\":1} extra"),
                                  "{\"pad\":\"" + std::string(be::kMaxEvidenceBytes, 'x') + "\"}"}) {
        CHECK(!be::ValidEvidence(bad));
        CHECK(be::FileServerKick(kUid, "Arena", "[server check] kicked", "d", r, nullptr, bad));
        CHECK(!Has(r.body, "evidence"));
    }
    // A player report can't carry one (no parameter for it).
    CHECK(be::FileReport(kUid, "Arena", "cheating", "d", std::string(), r));
    CHECK(!Has(r.body, "evidence"));
}

void TestKickReporterEvidence() {
    RUN_SUITE("KickReporter attaches the bundle, without addresses or tokens");
    std::vector<std::string> sent;
    KickReporter rep("Arena", [&](const std::string&, const std::string&, const std::string& body, const std::string&) { sent.push_back(body); });
    NetworkManager::AntiCheatKick k;
    k.peer = 4; k.name = "zed"; k.userId = kUid; k.risk = 95.5f; k.banRequested = false;
    k.evidence = SampleBundle().ToJson(12 * 1024);
    CHECK(Has(k.evidence, "203.0.113.9"));   // the input does hold them (a caller that opted in)
    CHECK(rep.File(k, 0.0));
    CHECK(sent.size() == 1);
    CHECK(Has(sent[0], "\"evidence\":{"));
    CHECK(!Has(sent[0], "203.0.113.9"));                                   // address cleared by default
    CHECK(!Has(sent[0], "\\\"" + kUid + "\\\"") && sent[0].find(kUid, sent[0].find("\"evidence\"")) == std::string::npos);   // account cleared too
    CHECK(!Has(sent[0], "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"));   // a token-like run is redacted
    CHECK(Has(sent[0], "teleport") && Has(sent[0], "\"risk\""));
    CHECK(Has(sent[0], "kicked by the server's anti-cheat"));
    og::json::Value root;
    CHECK(og::json::Reader(sent[0]).Parse(root));

    // Opted in: the bundle's identifiers stay.
    sent.clear();
    KickReporter rep2("Arena", [&](const std::string&, const std::string&, const std::string& body, const std::string&) { sent.push_back(body); });
    rep2.includeIdentifiers = true;
    CHECK(rep2.File(k, 0.0));
    CHECK(Has(sent[0], "203.0.113.9"));

    // A ban request says so (nothing is banned: an admin decides).
    sent.clear();
    k.banRequested = true;
    CHECK(rep.File(k, 5000.0));
    CHECK(Has(sent[0], "ban requested by the server's anti-cheat"));

    // No engine, or a bundle that doesn't parse: filed without it.
    sent.clear();
    NetworkManager::AntiCheatKick plain = k; plain.userId = kUid2; plain.evidence.clear();
    CHECK(rep.File(plain, 6000.0));
    CHECK(!Has(sent[0], "evidence"));
    NetworkManager::AntiCheatKick junk = k; junk.userId = "00000000-0000-4000-8000-000000000009"; junk.evidence = "{not json";
    CHECK(rep.File(junk, 6001.0));
    CHECK(sent.size() == 2 && !Has(sent[1], "evidence"));

    // Size cap: a huge bundle is cut to fit, and still valid JSON.
    EvidenceBundle big = SampleBundle();
    for (int i = 0; i < 200; ++i) { Evidence e = big.evidence[0]; e.detail = std::string(100, 'a' + i % 26); big.evidence.push_back(e); }
    NetworkManager::AntiCheatKick bk = k; bk.userId = "00000000-0000-4000-8000-000000000010";
    bk.evidence = big.ToJson(0);
    CHECK(bk.evidence.size() > be::kMaxEvidenceBytes / 2);
    const std::string capped = rep.EvidenceFor(bk);
    CHECK(!capped.empty() && capped.size() <= be::kMaxEvidenceBytes && be::ValidEvidence(capped));
}

// ---- (2) launcher admin helpers --------------------------------------------------------
void TestAdminTools() {
    RUN_SUITE("linked accounts and ban with devices");
    be::Request r;
    std::string err;
    CHECK(be::LinkedAccounts(kUid, r, &err));
    CHECK(r.method == "POST" && r.path == "/rest/v1/rpc/ac_linked_accounts" && r.body == "{\"p_user\":\"" + kUid + "\"}");
    CHECK(!be::LinkedAccounts("nope", r, &err) && !err.empty());
    CHECK(!be::LinkedAccounts("", r));

    std::vector<be::LinkedAccount> v;
    const std::string rows =
        "[{\"other_user\":\"" + kUid2 + "\",\"shared_machines\":2,\"last_seen\":\"2026-10-25T12:00:00.123456+00:00\",\"other_banned\":true},"
        "{\"other_user\":\"not-a-uuid\",\"shared_machines\":1},"
        "{\"other_user\":\"" + kUid + "\",\"shared_machines\":1,\"last_seen\":\"2026-10-26T01:02:03Z\",\"other_banned\":false}]";
    CHECK(be::ParseLinkedAccounts(rows, v));
    CHECK(v.size() == 2);   // the malformed id is skipped
    CHECK(v[0].userId == kUid2 && v[0].sharedMachines == 2 && v[0].banned && v[0].lastSeen == be::ParseTime("2026-10-25T12:00:00Z"));
    CHECK(v[1].userId == kUid && !v[1].banned && v[1].lastSeen > v[0].lastSeen);
    CHECK(be::LinkedSummary(v[0]) == "2 shared machines, already banned" && be::LinkedSummary(v[1]) == "1 shared machine");
    CHECK(be::ParseLinkedAccounts("[]", v) && v.empty());
    CHECK(!be::ParseLinkedAccounts("{\"message\":\"Only admins can look up linked accounts\"}", v));
    CHECK(!be::ParseLinkedAccounts("garbage", v));

    const long now = 1800000000;
    CHECK(be::BanWithDevices(kUid, "  speedhack  ", 0, 0, now, r, &err));
    CHECK(r.path == "/rest/v1/rpc/ac_ban_with_devices");
    CHECK(Has(r.body, "\"p_user\":\"" + kUid + "\"") && Has(r.body, "\"p_reason\":\"speedhack\"") &&
          Has(r.body, "\"p_until\":null") && Has(r.body, "\"p_report\":null"));
    CHECK(be::BanWithDevices(kUid, "x\"y", now + 86400, 31, now, r));
    CHECK(Has(r.body, "x\\\"y") && Has(r.body, "\"p_until\":\"" + be::IsoTime(now + 86400) + "\"") && Has(r.body, "\"p_report\":31"));
    og::json::Value root;
    CHECK(og::json::Reader(r.body).Parse(root));
    CHECK(!be::BanWithDevices("bad", "why", 0, 0, now, r, &err));
    CHECK(!be::BanWithDevices(kUid, "   ", 0, 0, now, r, &err) && Has(err, "reason"));
    CHECK(!be::BanWithDevices(kUid, "why", now - 5, 0, now, r, &err) && Has(err, "over"));
    CHECK(be::BanWithDevices(kUid, std::string(1000, 'x'), 0, 0, now, r));
    CHECK(r.body.size() < 600);   // the reason is capped at 300

    int n = -1;
    CHECK(be::ParseBanCount("3", n) && n == 3);
    CHECK(be::ParseBanCount("1\n", n) && n == 1);
    CHECK(!be::ParseBanCount("[1]", n) && !be::ParseBanCount("-1", n) && !be::ParseBanCount("{\"message\":\"x\"}", n) && !be::ParseBanCount("", n));
    CHECK(!be::ParseBanCount("99999", n));

    // Evidence lookup for one report.
    CHECK(be::ReportEvidence(12, r) && r.method == "GET" && Has(r.path, "select=evidence&id=eq.12"));
    CHECK(!be::ReportEvidence(0, r, &err));
    CHECK(be::ParseReportEvidence("[{\"evidence\":{\"v\":1,\"d\":\"a}b\\\"}\"}}]") == "{\"v\":1,\"d\":\"a}b\\\"}\"}");
    CHECK(be::ParseReportEvidence("[{\"evidence\":null}]").empty());
    CHECK(be::ParseReportEvidence("[]").empty() && be::ParseReportEvidence("{\"message\":\"no\"}").empty());
}

// ---- (3) editor inspector model -------------------------------------------------------------
void TestInspectorModel() {
    RUN_SUITE("detect inspector: fields, observe mode, text");
    using namespace okay::detectui;
    // Every field has a tooltip and a key the parser knows; the member it points at round-trips through the text.
    for (const Field& f : Fields()) {
        CHECK((f.f != nullptr) != (f.i != nullptr));
        CHECK(f.tip && std::string(f.tip).size() > 20 && f.label && f.group);
        DetectSettings d;
        std::string err;
        const std::string v = f.i ? "7" : "3";
        CHECK(SetField(d, f.key, v, &err));
        if (f.i) CHECK(d.*f.i == 7); else CHECK_NEAR(d.*f.f, 3.0f, 1e-6);
        CHECK(FieldValueText(d, f) == v);
        CHECK(Has(d.ToText(), std::string("detect.") + f.key + " " + v) || FieldValueText(DetectSettings{}, f) == v);
        CHECK(f.lo <= f.hi);
    }
    CHECK(FindField("kickAt") && !FindField("nope"));
    DetectSettings x; std::string err;
    CHECK(!SetField(x, "nope", "1", &err) && !SetField(x, "kickAt", "-3", &err) && !SetField(x, "kickAt", "abc", &err));

    // Observe mode: the responding rungs go to 0 and come back as they were.
    DetectSettings d;
    d.kickAt = 77; d.banAt = 123; d.shadowAt = 55; d.rateLimitAt = 33;
    CHECK(!IsObserveMode(d));
    ObserveMemory mem;
    SetObserveMode(d, true, mem);
    CHECK(IsObserveMode(d) && d.kickAt == 0 && d.banAt == 0 && d.shadowAt == 0 && d.rateLimitAt == 0);
    CHECK(d.logAt == DetectSettings{}.logAt && d.flagAt == DetectSettings{}.flagAt);   // recording stays on
    SetObserveMode(d, true, mem);   // twice: the memory isn't overwritten with zeros
    SetObserveMode(d, false, mem);
    CHECK(!IsObserveMode(d) && d.kickAt == 77 && d.banAt == 123 && d.shadowAt == 55 && d.rateLimitAt == 33);
    DetectSettings fromFile; fromFile.kickAt = 0; fromFile.banAt = 0; fromFile.shadowAt = 0; fromFile.rateLimitAt = 0;
    ObserveMemory none;
    SetObserveMode(fromFile, false, none);   // nothing remembered: the defaults
    CHECK(fromFile.kickAt == DetectSettings{}.kickAt && fromFile.banAt == DetectSettings{}.banAt);
    // The observe state survives the .okayac text (that is how scenes and files store it).
    DetectSettings o; o.enabled = true; ObserveMemory m2; SetObserveMode(o, true, m2);
    DetectSettings back; std::string e2;
    CHECK(FromText(back, ToText(o), &e2) && IsObserveMode(back) && back.enabled);

    // Text round trip, including lists and weights.
    DetectSettings a;
    a.enabled = true; a.maxSpeed = 8.5f; a.gravity = 20; a.maxJumpHeight = 1.2f; a.kickAt = 120; a.accuracyWindow = 50;
    std::string p;
    CHECK(a.ParseLine("weapon", "rifle 10 30 2 300", &p) && a.ParseLine("ability", "dash 5", &p) &&
          a.ParseLine("economy", "coins 12", &p) && a.ParseLine("weight.teleport", "31", &p));
    const std::string text = ToText(a);
    CHECK(text.rfind("detect.enabled true\n", 0) == 0 && Has(text, "detect.maxSpeed 8.5") && Has(text, "detect.weapon rifle"));
    DetectSettings b;
    CHECK(FromText(b, text, &p));
    CHECK(b.enabled && b.maxSpeed == 8.5f && b.gravity == 20 && b.kickAt == 120 && b.accuracyWindow == 50);
    CHECK(b.weapons.size() == 1 && b.weapons[0].name == "rifle" && b.abilities.size() == 1 && b.economy.size() == 1);
    CHECK(ToText(b) == text);   // stable
    // Comments and blank lines are fine; a key not in the text goes back to its default.
    DetectSettings c; c.maxSpeed = 99;
    CHECK(FromText(c, "# hi\n\ndetect.enabled true   # on\ndetect.gravity 9.8\n", &p) && c.enabled && c.gravity == 9.8f && c.maxSpeed == 0.0f);
    // A bad line changes nothing and names the line.
    DetectSettings keep; keep.maxSpeed = 5;
    CHECK(!FromText(keep, "detect.gravity 9\ndetect.bogus 1\n", &p) && Has(p, "line 2") && keep.maxSpeed == 5 && keep.gravity == 0);
    CHECK(!FromText(keep, "gravity 9\n", &p) && Has(p, "line 1"));
    CHECK(!FromText(keep, "detect.kickAt -4\n", &p));
    CHECK(FromText(keep, "", &p) && !keep.enabled);
}
} // namespace

int main() {
    TestBackendEvidence();
    TestKickReporterEvidence();
    TestAdminTools();
    TestInspectorModel();
    TEST_MAIN_RESULT();
}
