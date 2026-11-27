#include "OkayScriptVMInternal.hpp"
#include "okay/Net/ServerQuery.hpp"
#include "okay/Net/Rollback.hpp"

namespace okay {

void OkayScriptVM::Impl::RegisterNetworkBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        // Relay (NAT traversal): host/join through a shared relay + session code,
        // so peers behind routers connect without port-forwarding.
        b["net_host_relay"] = [this](std::vector<Value>& a) {
            NetworkManager* n = EnsureNet();
            if (!n || a.size() < 3) return Value{0.0f};
            return Value{n->HostViaRelay(a[0].AsString(), (std::uint16_t)SafeInt(a[1].AsFloat()),
                                         a[2].AsString()) ? 1.0f : 0.0f};
        };
        b["net_join_relay"] = [this](std::vector<Value>& a) {
            NetworkManager* n = EnsureNet();
            if (!n || a.size() < 3) return Value{0.0f};
            return Value{n->JoinViaRelay(a[0].AsString(), (std::uint16_t)SafeInt(a[1].AsFloat()),
                                         a[2].AsString()) ? 1.0f : 0.0f};
        };
        // Launcher integration (Steam-style): was this run started to join a friend
        // / invite / server? (Skip your menu and go straight to the match.)
        b["net_launch_join"] = [](std::vector<Value>&) {
            return Value{NetworkManager::LaunchedToJoin() ? 1.0f : 0.0f};
        };
        // Join from an okay://join link (e.g. one a player pasted).
        b["net_join_link"] = [this](std::vector<Value>& a) {
            NetworkManager* n = EnsureNet();
            if (!n || a.empty()) return Value{0.0f};
            return Value{n->JoinByLink(a[0].AsString()) ? 1.0f : 0.0f};
        };
        // In-game server browser (filtered to this game; refreshes in the background).
        // net_browser_open() shows the built-in "Find a server" window; the rest let a
        // game draw its own list.
        b["net_browser_open"]  = [](std::vector<Value>&) { ServerBrowser::Shared().windowOpen = true; return Value{1.0f}; };
        b["net_browser_close"] = [](std::vector<Value>&) { ServerBrowser::Shared().windowOpen = false; return Value{1.0f}; };
        b["net_browser_is_open"] = [](std::vector<Value>&) { return Value{ServerBrowser::Shared().windowOpen ? 1.0f : 0.0f}; };
        b["net_browser_refresh"] = [](std::vector<Value>& a) {   // ["all" | "internet" | "lan"]
            const std::string w = a.empty() ? std::string("all") : a[0].AsString();
            ServerBrowser::Shared().Refresh(w == "internet" ? ServerBrowser::kInternet : w == "lan" ? ServerBrowser::kLan : ServerBrowser::kAll);
            return Value{1.0f};
        };
        b["net_browser_busy"]  = [](std::vector<Value>&) { return Value{ServerBrowser::Shared().Busy() ? 1.0f : 0.0f}; };
        b["net_browser_count"] = [](std::vector<Value>&) { return Value{(float)ServerBrowser::Shared().Results().size()}; };
        auto browserAt = [](std::vector<Value>& a, ServerBrowser::Entry& out) {
            if (a.empty()) return false;
            const auto r = ServerBrowser::Shared().Results(); const int i = SafeInt(a[0].AsFloat());
            if (i < 0 || i >= (int)r.size()) return false;
            out = r[i]; return true;
        };
        b["net_browser_name"] = [browserAt](std::vector<Value>& a) { ServerBrowser::Entry e; return Value{browserAt(a, e) ? (e.info.name.empty() ? e.info.Address() : e.info.name) : std::string{}}; };
        b["net_browser_map"]  = [browserAt](std::vector<Value>& a) { ServerBrowser::Entry e; return Value{browserAt(a, e) ? e.info.map : std::string{}}; };
        b["net_browser_address"] = [browserAt](std::vector<Value>& a) { ServerBrowser::Entry e; return Value{browserAt(a, e) ? e.info.Address() : std::string{}}; };
        b["net_browser_players"] = [browserAt](std::vector<Value>& a) { ServerBrowser::Entry e; return Value{browserAt(a, e) ? (float)e.info.players : 0.0f}; };
        b["net_browser_max"]  = [browserAt](std::vector<Value>& a) { ServerBrowser::Entry e; return Value{browserAt(a, e) ? (float)e.info.maxPlayers : 0.0f}; };
        b["net_browser_ping"] = [browserAt](std::vector<Value>& a) { ServerBrowser::Entry e; return Value{browserAt(a, e) ? (float)e.info.pingMs : -1.0f}; };
        b["net_browser_locked"] = [browserAt](std::vector<Value>& a) { ServerBrowser::Entry e; return Value{browserAt(a, e) && e.info.password ? 1.0f : 0.0f}; };
        b["net_browser_join"] = [this, browserAt](std::vector<Value>& a) {   // (index[, password])
            ServerBrowser::Entry e;
            NetworkManager* n = EnsureNet();
            if (!n || !browserAt(a, e)) return Value{0.0f};
            JoinLink l = e.Link();
            if (a.size() > 1) l.password = a[1].AsString();
            ServerBrowser::Shared().windowOpen = false;
            return Value{n->JoinByLink(l.Format()) ? 1.0f : 0.0f};
        };

        // Scores (server-owned, replicated to everyone in the room) + the roster.
        b["net_set_score"] = [this](std::vector<Value>& a) {   // server only: (id, score)
            NetworkManager* n = Net();
            if (!n || a.size() < 2 || !n->IsServer()) return Value{0.0f};
            n->SetPeerScore(SafeUInt32(a[0].AsFloat()), SafeInt(a[1].AsFloat()));
            return Value{1.0f};
        };
        b["net_score"] = [this](std::vector<Value>& a) {
            NetworkManager* n = Net();
            return Value{(n && !a.empty()) ? (float)n->PeerScore(SafeUInt32(a[0].AsFloat())) : 0.0f};
        };
        b["net_peer_name"] = [this](std::vector<Value>& a) {
            NetworkManager* n = Net();
            return Value{(n && !a.empty()) ? n->PeerName(SafeUInt32(a[0].AsFloat())) : std::string{}};
        };
        b["net_roster_count"] = [this](std::vector<Value>&) {
            NetworkManager* n = Net();
            return Value{n ? (float)n->Roster().size() : 0.0f};
        };
        b["net_roster_id"] = [this](std::vector<Value>& a) {   // (index) -> peer id, -1 if out of range
            NetworkManager* n = Net();
            if (!n || a.empty()) return Value{-1.0f};
            const auto r = n->Roster(); const int i = SafeInt(a[0].AsFloat());
            return Value{(i >= 0 && i < (int)r.size()) ? (float)r[i].id : -1.0f};
        };

        // ---- net.*: the easy layer (NetworkManagerEvents.cpp), one call per action ----
        //   net.host()  net.join("192.168.1.20")  net.host_or_join()  net.leave()
        //   net.on("hit", "on_hit")  or  net.on("hit", (data, from) => { ... })
        //   net.send("hit", "10")  net.send_to(id, "hit", "10")
        //   net.on("player_joined", fn) / net.on("player_left", fn), or define
        //   on_player_joined(id, name) / on_player_left(id) / on_network_event(name, data, from)
        // A handler for a named event gets (data, from, name).
        auto port0 = [](std::vector<Value>& a, std::size_t i) {
            return (std::uint16_t)(a.size() > i ? SafeInt(a[i].AsFloat()) : (int)NetworkManager::kDefaultPort);
        };
        b["net.host"] = [this, port0](std::vector<Value>& a) {
            NetworkManager* n = EnsureNet();
            return Value{n && n->StartServer(port0(a, 0))};
        };
        b["net.join"] = [this](std::vector<Value>& a) {   // ("host" | "host:port" [, port])
            NetworkManager* n = EnsureNet();
            if (!n) return Value{false};
            std::string host; std::uint16_t port = NetworkManager::kDefaultPort;
            const std::string addr = a.empty() ? std::string("127.0.0.1") : a[0].AsString();
            if (!SplitHostPort(addr, host, port, NetworkManager::kDefaultPort)) {
                Log::Warning("OkayScript net.join: \"", addr, "\" is not an address (write 192.168.1.20 or 192.168.1.20:45000)");
                return Value{false};
            }
            if (a.size() > 1) port = (std::uint16_t)SafeInt(a[1].AsFloat());
            return Value{n->StartClient(host, port)};
        };
        b["net.host_or_join"] = [this, port0](std::vector<Value>& a) {
            NetworkManager* n = EnsureNet();
            return Value{n && n->HostOrJoin(port0(a, 0))};
        };
        b["net.join_lan"] = [this](std::vector<Value>& a) {   // join the first game on the local network
            NetworkManager* n = EnsureNet();
            if (!n) return Value{false};
            for (const ServerInfo& s : QueryServers({}, a.empty() ? 1000 : SafeInt(a[0].AsFloat()), true))
                if (s.responded) return Value{n->StartClient(s.host, s.port)};
            return Value{false};
        };
        // Online play through the relay by a short room code: net.host_online() -> the code to give
        // friends ("" and net.error() when it could not start); net.join_code("K7Q29X") -> true while it tries.
        b["net.host_online"] = [this](std::vector<Value>& a) {
            NetworkManager* n = EnsureNet();
            if (!n || !n->HostOnline(a.empty() ? std::string() : a[0].AsString())) return Value{std::string()};
            return Value{n->RoomCode()};
        };
        b["net.join_code"] = [this](std::vector<Value>& a) {
            NetworkManager* n = EnsureNet();
            return Value{n && n->JoinCode(a.empty() ? std::string() : a[0].AsString())};
        };
        b["net.relay"] = [this](std::vector<Value>& a) {   // ("host:port") sets the relay online play uses; returns it
            NetworkManager* n = EnsureNet();
            if (!n) return Value{std::string()};
            if (!a.empty()) n->relayServer = a[0].AsString();
            return Value{n->OnlineRelay()};
        };
        b["net.room_code"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{n ? n->RoomCode() : std::string()}; };
        // How this player's traffic travels: "direct", "direct (punched)", "relayed", "relayed (trying direct)", "" offline.
        b["net.path"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{n ? n->PathText() : std::string()}; };
        b["net.leave"] = [this](std::vector<Value>&) { if (NetworkManager* n = Net()) n->Stop(); return Value{}; };
        b["net.is_host"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{n && n->IsServer()}; };
        b["net.connected"] = [this](std::vector<Value>&) {
            NetworkManager* n = Net(); return Value{n && (n->IsServer() || (n->IsClient() && n->Joined()))};
        };
        b["net.id"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{n ? (float)n->LocalId() : 0.0f}; };
        b["net.players"] = [this](std::vector<Value>&) {   // the ids of everyone in the game, you included
            Value out = Value::MakeArray();
            if (NetworkManager* n = Net())
                for (const auto& p : n->Players()) out.AsArray()->push_back(Value{(float)p.id});
            return out;
        };
        b["net.player_count"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{n ? (float)n->Players().size() : 0.0f}; };
        b["net.player_name"] = [this](std::vector<Value>& a) {
            NetworkManager* n = Net();
            if (n && !a.empty())
                for (const auto& p : n->Players())
                    if ((float)p.id == a[0].AsFloat()) return Value{p.name};
            return Value{std::string{}};
        };
        b["net.send"] = [this](std::vector<Value>& a) {   // (event [, data]): every player, this one too
            NetworkManager* n = Net();
            if (!n) { Log::Warning("OkayScript net.send: not in a network game yet (net.host or net.join first)"); return Value{false}; }
            return Value{!a.empty() && n->SendEvent(a[0].AsString(), a.size() > 1 ? a[1].AsString() : std::string())};
        };
        b["net.send_to"] = [this](std::vector<Value>& a) {   // (player id, event [, data])
            NetworkManager* n = Net();
            if (!n || a.size() < 2) return Value{false};
            return Value{n->SendEventTo(SafeUInt32(a[0].AsFloat()), a[1].AsString(), a.size() > 2 ? a[2].AsString() : std::string())};
        };
        // net.on(event, fn): fn is a function's name ("on_hit") or a lambda. "player_joined" and
        // "player_left" are the join / leave events; any other name is a network event.
        auto netEventKey = [](const std::string& ev) {
            if (ev == "player_joined" || ev == "player_left" || ev == "host_migrated") return "on_" + ev;
            return "net_event:" + ev;
        };
        auto netHandler = [](const Value& f) {
            if (f.IsString()) { Value fn = Value::MakeMap(); (*fn.AsMap())["__fn"] = f; return fn; }
            return f;
        };
        b["net.on"] = [this, netEventKey, netHandler](std::vector<Value>& a) {
            if (a.size() < 2) throw ScriptError("net.on needs an event name and a function: net.on(\"hit\", \"on_hit\")");
            const Value fn = netHandler(a[1]);
            if (fn.IsString() || !(Runtime::IsLambda(fn) || (fn.AsMap() && fn.AsMap()->count("__fn"))))
                throw ScriptError("net.on(\"" + a[0].AsString() + "\", ...): the second value must be a function name in quotes or a lambda");
            if (fn.AsMap()->count("__fn") && !rt.functions.count((*fn.AsMap())["__fn"].AsString()))
                throw ScriptError("net.on(\"" + a[0].AsString() + "\", ...): there is no function named '" + (*fn.AsMap())["__fn"].AsString() + "' in this script");
            rt.AddSub(netEventKey(a[0].AsString()), fn);
            return Value{true};
        };
        b["net.off"] = [this, netEventKey, netHandler](std::vector<Value>& a) {
            if (a.size() >= 2) rt.RemoveSub(netEventKey(a[0].AsString()), netHandler(a[1]));
            return Value{};
        };
        // The same calls as the older names (one body each): net.spawn = net_spawn_owned, net.set = net_set, net.get = net_get.
        auto forward = [this](const char* to) {
            return [this, to](std::vector<Value>& a) { return rt.Call(to, a); };
        };
        b["net.spawn"] = forward("net_spawn_owned");   // (prefab [, x, y, z]) -> the object's network id
        b["net.set"] = forward("net_set");
        b["net.get"] = forward("net_get");
        b["net.time"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{n ? (float)n->NetworkTime() : 0.0f}; };
        b["net.ping"] = forward("net_ping");
        b["net.error"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{n ? n->LastError() : std::string{}}; };
        // Ownership and prediction: netId defaults to this object's Network Sync id.
        auto myNetId = [this](std::vector<Value>& a, std::size_t i) {
            if (a.size() > i && !a[i].AsString().empty()) return a[i].AsString();
            GameObject* me = rt.host ? rt.host->gameObject : nullptr;
            auto* ns = me ? me->GetComponent<NetworkSync>() : nullptr;
            return ns ? (ns->netId.empty() ? me->name : ns->netId) : std::string();
        };
        b["net.is_mine"] = [this, myNetId](std::vector<Value>& a) {
            NetworkManager* n = Net(); const std::string id = myNetId(a, 0);
            return Value{!n || id.empty() || n->IsMine(id)};
        };
        b["net.is_predicted"] = [this, myNetId](std::vector<Value>& a) { NetworkManager* n = Net(); return Value{n && n->Predicted(myNetId(a, 0))}; };
        b["net.give"] = [this, myNetId](std::vector<Value>& a) {   // (netId or "", player id): host
            NetworkManager* n = Net();
            return Value{n && a.size() >= 2 && n->GiveOwnership(myNetId(a, 0), SafeUInt32(a[1].AsFloat()))};
        };
        b["net.request"] = [this, myNetId](std::vector<Value>& a) { if (NetworkManager* n = Net()) n->RequestOwnership(myNetId(a, 0)); return Value{}; };
        b["net.owner"] = [this, myNetId](std::vector<Value>& a) { NetworkManager* n = Net(); return Value{n ? (float)n->OwnerOf(myNetId(a, 0)) : -1.0f}; };
        b["net.simulate"] = [this](std::vector<Value>& a) {   // (latency ms, jitter ms, loss %): all 0 = off
            if (NetworkManager* n = EnsureNet()) {
                n->simulateLatencyMs = a.size() > 0 ? a[0].AsFloat() : 0.0f;
                n->simulateJitterMs = a.size() > 1 ? a[1].AsFloat() : 0.0f;
                n->simulateLossPercent = a.size() > 2 ? a[2].AsFloat() : 0.0f;
            }
            return Value{};
        };
        b["net.loss"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{n ? n->Stats().lossPercent : 0.0f}; };
        b["net.bytes_in"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{n ? n->Stats().bytesInPerSec : 0.0f}; };
        b["net.bytes_out"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{n ? n->Stats().bytesOutPerSec : 0.0f}; };
        // The in-game stats overlay: net.show_stats() / net.show_stats(false); net.stats_text() is its text.
        b["net.show_stats"] = [this](std::vector<Value>& a) {
            NetworkManager* n = EnsureNet();
            if (n) n->showStatsOverlay = a.empty() || a[0].AsBool();
            return Value{n && n->showStatsOverlay};
        };
        b["net.stats_text"] = [this](std::vector<Value>&) {
            std::string t;
            if (NetworkManager* n = Net()) for (const std::string& l : n->StatsLines()) t += l + "\n";
            return Value{t};
        };
        // Host migration: net.migration("off" | "lan" | "always") sets it; with no value it says which.
        // on_host_migrated(previous_id), or net.on("host_migrated", fn), runs once there is a new host.
        b["net.migration"] = [this](std::vector<Value>& a) {
            NetworkManager* n = EnsureNet();
            if (!n) return Value{std::string()};
            if (!a.empty()) {
                const std::string m = a[0].IsString() ? a[0].AsString() : (a[0].AsBool() ? "lan" : "off");
                if (m == "off") n->hostMigration = NetworkManager::HostMigration::Off;
                else if (m == "lan") n->hostMigration = NetworkManager::HostMigration::Lan;
                else if (m == "always") n->hostMigration = NetworkManager::HostMigration::Always;
                else throw ScriptError("net.migration(\"" + m + "\"): use \"off\", \"lan\" or \"always\"");
            }
            const auto h = n->hostMigration;
            return Value{std::string(h == NetworkManager::HostMigration::Off ? "off" : h == NetworkManager::HostMigration::Lan ? "lan" : "always")};
        };
        b["net.migrations"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{n ? (float)n->HostMigrations() : 0.0f}; };
        b["net.migrating"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{n && n->Migrating()}; };
        // Rollback (deterministic games): net.rollback_start([input delay [, tick rate [, max predicted frames]]]).
        // The script defines rollback_save() -> text, rollback_load(text), rollback_input() -> text and
        // rollback_step(frame, inputs), where inputs is an array of every player's input in
        // net.rollback_players() order. Every player calls it (after net.players() lists everyone).
        b["net.rollback_start"] = [this](std::vector<Value>& a) {
            NetworkManager* n = Net();
            if (!n) { Log::Warning("OkayScript net.rollback_start: not in a network game yet (net.host or net.join first)"); return Value{false}; }
            for (const char* fn : {"rollback_save", "rollback_load", "rollback_input", "rollback_step"})
                if (!rt.functions.count(fn))
                    throw ScriptError(std::string("net.rollback_start needs a function ") + fn + " in this script");
            net::Rollback::Settings st;
            if (a.size() > 0) st.inputDelay = SafeInt(a[0].AsFloat());
            if (a.size() > 1) st.tickRate = a[1].AsFloat();
            if (a.size() > 2) st.maxPrediction = SafeInt(a[2].AsFloat());
            std::weak_ptr<int> life = this->life;   // the callbacks do nothing once this script is gone
            auto call = [this, life](const char* fn, std::vector<Value> args) -> Value {
                if (life.expired()) return Value{};
                try { return rt.Call(fn, args); }
                catch (const std::exception& e) { Log::Error("OkayScript ", fn, "(): ", e.what()); return Value{}; }
            };
            const bool ok = n->Rollback().Start(st,
                [call] { return call("rollback_save", {}).AsString(); },
                [call](const std::string& state) { call("rollback_load", {Value{state}}); },
                [call] { return call("rollback_input", {}).AsString(); },
                [call](std::uint32_t frame, const std::vector<net::Rollback::FrameInput>& in) {
                    Value arr = Value::MakeArray();
                    for (const auto& i : in) arr.AsArray()->push_back(Value{i.input});
                    call("rollback_step", {Value{(float)frame}, arr});
                });
            if (!ok) Log::Warning("OkayScript net.rollback_start: ", n->Rollback().LastError());
            return Value{ok};
        };
        b["net.rollback_stop"] = [this](std::vector<Value>&) { if (NetworkManager* n = Net()) n->Rollback().Stop(); return Value{}; };
        b["net.rollback_frame"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{n ? (float)n->Rollback().Frame() : 0.0f}; };
        b["net.rollback_confirmed"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{n ? (float)n->Rollback().ConfirmedFrame() : 0.0f}; };
        b["net.rollback_resimulating"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{n && n->Rollback().Resimulating()}; };
        b["net.rollback_players"] = [this](std::vector<Value>&) {
            Value out = Value::MakeArray();
            if (NetworkManager* n = Net()) for (std::uint32_t id : n->Rollback().Players()) out.AsArray()->push_back(Value{(float)id});
            return out;
        };
        b["net.rollback_running"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{n && n->Rollback().Running()}; };
}

void OkayScriptVM::Impl::RegisterMatchmakingQueueBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        // ---- Matchmaking queues (docs/sql/matchmaking.sql) ----
        // net_queue_join("duel", 2[, minPlayers[, "EU"]]) puts this player (and their
        // party, if they lead one) in the queue; when a match is found the game joins
        // it — a dedicated server's room, or a player who hosts. The player shows a
        // "Finding a match" banner with Cancel; scripts can draw their own with these.
        b["net_queue_join"] = [this](std::vector<Value>& a) {
            EnsureNet();   // the found match needs a NetworkManager to connect
            MatchRequest r;
            r.mode = a.size() > 0 ? a[0].AsString() : std::string();
            r.players = a.size() > 1 ? SafeInt(a[1].AsFloat()) : 2;
            r.minPlayers = a.size() > 2 ? SafeInt(a[2].AsFloat()) : 0;
            r.region = a.size() > 3 ? a[3].AsString() : std::string();
            return Value{Matchmaker::Shared().Start(r) ? 1.0f : 0.0f};
        };
        b["net_queue_cancel"] = [](std::vector<Value>&) { Matchmaker::Shared().Cancel(); return Value{1.0f}; };
        // "idle" | "searching" | "waiting" (found; the host is starting) | "found" | "failed"
        b["net_queue_state"] = [](std::vector<Value>&) {
            return Value{std::string(Matchmaker::StateName(Matchmaker::Shared().GetState()))};
        };
        b["net_queue_time"]  = [](std::vector<Value>&) { return Value{Matchmaker::Shared().Elapsed()}; };
        b["net_queue_count"] = [](std::vector<Value>&) { return Value{(float)Matchmaker::Shared().Searching()}; };
        b["net_queue_error"] = [](std::vector<Value>&) { return Value{Matchmaker::Shared().Error()}; };
        b["net_queue_is_host"] = [](std::vector<Value>&) { return Value{Matchmaker::Shared().Assignment().isHost ? 1.0f : 0.0f}; };
        // Turn off auto-connect to show "Match found — Accept"; net_queue_accept() then connects.
        b["net_queue_auto"] = [](std::vector<Value>& a) {
            if (!a.empty()) Matchmaker::Shared().autoConnect = a[0].AsFloat() != 0.0f;
            return Value{Matchmaker::Shared().autoConnect ? 1.0f : 0.0f};
        };
        b["net_queue_accept"] = [this](std::vector<Value>&) {
            NetworkManager* n = EnsureNet();
            return Value{(n && Matchmaker::Shared().Connect(n)) ? 1.0f : 0.0f};
        };

}

void OkayScriptVM::Impl::RegisterMatchResultBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto go = c.go;
        // ---- Matches: results -> stats + leaderboards (docs/sql/matches.sql) ----
        // Server: net_match_start() zeroes scores and starts the clock;
        // net_match_end([winner ids...]) reports everyone's score / place / win
        // (winners default to first place) when the server is signed in.
        b["net_match_start"] = [this](std::vector<Value>&) {
            NetworkManager* n = Net();
            if (!n || !n->IsServer()) return Value{0.0f};
            n->BeginMatch();
            return Value{1.0f};
        };
        b["net_match_end"] = [this](std::vector<Value>& a) {
            NetworkManager* n = Net();
            if (!n || !n->IsServer()) return Value{0.0f};
            std::vector<std::uint32_t> winners;
            for (auto& v : a) {
                if (auto arr = v.AsArray()) { for (auto& w : *arr) winners.push_back(SafeUInt32(w.AsFloat())); }
                else winners.push_back(SafeUInt32(v.AsFloat()));
            }
            Scene* sc = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            return Value{EndMatchAndReport(*n, winners, std::string(), sc ? sc->Name() : std::string()) ? 1.0f : 0.0f};
        };
        b["net_match_seconds"] = [this](std::vector<Value>&) {
            NetworkManager* n = Net();
            return Value{n ? n->MatchSeconds() : 0.0f};
        };
        // "" until a report finished, then "ok" or why it failed.
        b["net_match_status"] = [](std::vector<Value>&) { return Value{LastMatchReportStatus()}; };
        // Leaderboards from match results: net_stats_top("wins"|"score"|"best"|"matches"|"time", n)
        // -> ["1,maya,42", ...]; net_stats_mine(stat) -> my value. Optional last arg: another game's title.
        b["net_stats_top"] = [this](std::vector<Value>& a) {
            Value v = Value::MakeArray();
            auto arr = v.AsArray();
            if (!arr || !Account::Exists() || !Account::IsLoggedIn()) return v;
            const std::string stat = a.empty() ? std::string("wins") : a[0].AsString();
            const int count = a.size() > 1 ? std::max(1, SafeInt(a[1].AsFloat())) : 10;
            std::string game = a.size() > 2 ? a[2].AsString() : std::string();
            if (game.empty()) { NetworkManager* n = Net(); game = n && !n->gameId.empty() ? n->gameId : ServerBrowser::Shared().Game(); }
            for (const auto& e : MatchResults(Account::Get()).Leaderboard(game, stat, count)) {
                const long val = stat == "score" ? e.totalScore : stat == "best" ? e.bestScore : stat == "matches" ? e.matches
                               : stat == "time" ? e.seconds : e.wins;
                arr->push_back(Value{std::to_string(e.rank) + "," + e.Name() + "," + std::to_string(val)});
            }
            return v;
        };
        b["net_stats_mine"] = [this](std::vector<Value>& a) {
            if (!Account::Exists() || !Account::IsLoggedIn()) return Value{0.0f};
            const std::string stat = a.empty() ? std::string("wins") : a[0].AsString();
            std::string game = a.size() > 1 ? a[1].AsString() : std::string();
            if (game.empty()) { NetworkManager* n = Net(); game = n && !n->gameId.empty() ? n->gameId : ServerBrowser::Shared().Game(); }
            PlayerStats e;
            if (!MatchResults(Account::Get()).MyStats(game, e)) return Value{0.0f};
            if (stat == "rank") return Value{(float)e.rank};
            const long val = stat == "score" ? e.totalScore : stat == "best" ? e.bestScore : stat == "matches" ? e.matches
                           : stat == "time" ? e.seconds : e.wins;
            return Value{(float)val};
        };
        // Server only: move a player (respawn / teleport) — passes the speed check.
        b["net_teleport"] = [this](std::vector<Value>& a) {
            NetworkManager* n = Net();
            if (!n || a.size() < 3 || !n->IsServer()) return Value{0.0f};
            n->ServerTeleport(SafeUInt32(a[0].AsFloat()),
                              Vec3(a[1].AsFloat(), a[2].AsFloat(), a.size() > 3 ? a[3].AsFloat() : 0.0f));
            return Value{1.0f};
        };
        // Server only: anti-cheat basics. (max units/second, 0 = off) / (messages per second, 0 = off).
        b["net_max_speed"] = [this](std::vector<Value>& a) {
            NetworkManager* n = EnsureNet(); if (n && !a.empty()) n->maxMoveSpeed = a[0].AsFloat();
            return Value{n ? n->maxMoveSpeed : 0.0f};
        };
        b["net_rate_limit"] = [this](std::vector<Value>& a) {
            NetworkManager* n = EnsureNet(); if (n && !a.empty()) n->maxMessagesPerSecond = SafeInt(a[0].AsFloat());
            return Value{n ? (float)n->maxMessagesPerSecond : 0.0f};
        };
        // Server only: the anti-cheat (see okay/Net/AntiCheat.hpp). net_anticheat([on])
        // reads / sets whether it's on; net_anticheat_load(path) applies an .okayac
        // file from the game's folders (true if it loaded); net_violations(peer)
        // is that peer's refusals this minute.
        b["net_anticheat"] = [this](std::vector<Value>& a) {
            NetworkManager* n = EnsureNet(); if (n && !a.empty()) n->anticheat.settings.enabled = a[0].AsBool();
            return Value{n ? n->anticheat.settings.enabled : false};
        };
        b["net_anticheat_load"] = [this](std::vector<Value>& a) {
            NetworkManager* n = EnsureNet();
            if (!n || a.empty() || !ScriptPathOk(a[0].AsString(), "net_anticheat_load")) return Value{false};
            AntiCheatSettings s; std::string err;
            if (!s.LoadFile(a[0].AsString(), &err)) { Log::Error("net_anticheat_load: ", err); return Value{false}; }
            n->anticheat.settings = s;
            return Value{true};
        };
        b["net_violations"] = [this](std::vector<Value>& a) {
            NetworkManager* n = EnsureNet();
            return Value{n && !a.empty() ? (float)n->anticheat.Violations(SafeUInt32(a[0].AsFloat())) : 0.0f};
        };
        // Claim that this peer's shot hit a networked object (its sync id), fired
        // from (ox, oy, oz) along (dx, dy, dz). The server checks it and applies
        // the damage; on the host it's applied at once.
        b["net_hit"] = [this](std::vector<Value>& a) {
            NetworkManager* n = Net();
            if (!n || a.size() < 8) return Value{};
            n->ClaimHit(a[0].AsString(), a[1].AsString(), Vec3(a[2].AsFloat(), a[3].AsFloat(), a[4].AsFloat()),
                        Vec3(a[5].AsFloat(), a[6].AsFloat(), a[7].AsFloat()));
            return Value{};
        };
        // Server: what's left of a player's budget for RPC `name` (a Net Validator
        // with a budget: ammo); a third argument adds to it first (a reload, a
        // pickup; negative takes away). -1 if no rule has a budget for it.
        b["net_budget"] = [this](std::vector<Value>& a) {
            Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            if (!s || a.size() < 2) return Value{-1.0f};
            NetValidator* nv = NetValidator::BudgetFor(*s, a[1].AsString());
            if (!nv) return Value{-1.0f};
            const auto peer = SafeUInt32(a[0].AsFloat());
            return Value{a.size() >= 3 ? nv->TopUp(peer, a[2].AsFloat()) : nv->Budget(peer)};
        };
        // Open the OkaySpace launcher's "Invite friends" window for this game.
        b["net_invite_friends"] = [](std::vector<Value>&) {
#if !defined(__EMSCRIPTEN__)
            return Value{GameIpc::RequestInvite() ? 1.0f : 0.0f};
#else
            return Value{0.0f};
#endif
        };
        // 1 once the relay has paired this peer (host wired up / slot assigned).
        // The fixed network tick (0 without one) and its rate; net_tick_rate(hz)
        // sets the host's rate before it starts (0 = off, as before).
        b["net_tick"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{n ? (float)n->NetTick() : 0.0f}; };
        b["net_tick_rate"] = [this](std::vector<Value>& a) {
            NetworkManager* n = Net();
            if (!n) return Value{0.0f};
            if (!a.empty()) n->netTickRate = std::clamp(a[0].AsFloat(), 0.0f, 240.0f);
            return Value{n->NetTickRate() > 0.0f ? n->NetTickRate() : n->netTickRate};
        };
        // Authoritative movement: the object this player's inputs drive
        // (client), and the host's switch.
        b["net_set_pawn"] = [this](std::vector<Value>& a) {
            NetworkManager* n = Net();
            Scene* s = (rt.host && rt.host->gameObject) ? rt.host->gameObject->scene() : nullptr;
            if (!n || !s || a.empty()) return Value{0.0f};
            GameObject* go = a[0].AsString().empty() ? rt.host->gameObject : ResolveObject(s, a[0]);
            n->SetPawn(go);
            return Value{go ? 1.0f : 0.0f};
        };
        b["net_authoritative"] = [this](std::vector<Value>& a) {
            NetworkManager* n = Net();
            if (!n) return Value{0.0f};
            if (!a.empty()) n->authoritativeMovement = a[0].AsFloat() != 0.0f;
            return Value{n->AuthoritativeMovement() || n->authoritativeMovement ? 1.0f : 0.0f};
        };
        b["net_relay_ready"] = [this](std::vector<Value>&) {
            NetworkManager* n = Net(); return Value{(n && n->RelayReady()) ? 1.0f : 0.0f};
        };
        b["net_disconnect"] = [this](std::vector<Value>&) {
            if (NetworkManager* n = Net()) n->Stop();
            return Value{};
        };
        b["net_connected"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{(n && n->IsConnected()) ? 1.0f : 0.0f}; };
        b["net_is_server"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{(n && n->IsServer()) ? 1.0f : 0.0f}; };
        b["net_is_client"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{(n && n->IsClient()) ? 1.0f : 0.0f}; };
        b["net_id"]    = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{n ? (float)n->LocalId() : 0.0f}; };
        b["net_peers"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{n ? (float)n->PeerCount() : 0.0f}; };
        b["net_ping"]  = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{n ? n->RttMs() : 0.0f}; };
        // Lobby room: set before net_host/net_join so peers only see their room.
        b["net_room"]  = [this](std::vector<Value>& a) {
            if (!a.empty()) { if (NetworkManager* n = EnsureNet()) n->SetRoom(a[0].AsString()); return Value{a[0].AsString()}; }
            NetworkManager* n = Net();
            return Value{n ? n->Room() : std::string{}};
        };
        // Lobby ready-up: clients mark ready; the host polls and starts the match.
        b["net_ready"] = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net()) n->SetReady(a.empty() ? true : a[0].AsFloat() != 0.0f);
            return Value{};
        };
        b["net_ready_count"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{n ? (float)n->ReadyCount() : 0.0f}; };
        b["net_all_ready"]   = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{(n && n->AllReady()) ? 1.0f : 0.0f}; };
        b["net_start_match"] = [this](std::vector<Value>&) { if (NetworkManager* n = Net()) n->StartMatch(); return Value{}; };
        b["net_match_started"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{(n && n->MatchStarted()) ? 1.0f : 0.0f}; };
        b["net_name"]  = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net()) { if (!a.empty()) n->SetLocalName(a[0].AsString()); return Value{n->LocalName()}; }
            return Value{std::string{}};
        };
        // Set the identity token presented on join (e.g. a Supabase access token);
        // returns the current token. The host verifies it if it has a verifier.
        b["net_auth_token"] = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net()) { if (!a.empty()) n->SetAuthToken(a[0].AsString()); return Value{n->AuthToken()}; }
            return Value{std::string{}};
        };
        // This peer's verified account id once joined ("" if anonymous/unverified).
        b["net_user_id"] = [this](std::vector<Value>&) {
            NetworkManager* n = Net(); return Value{n ? n->LocalUserId() : std::string{}};
        };
        // Turn packet encryption on/off (set on both ends before host/join); returns
        // the current setting (1/0). Needs a build with libsodium, else a no-op.
        b["net_encrypt"] = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net()) { if (!a.empty()) n->encryption = (a[0].AsFloat() != 0.0f); return Value{n->encryption ? 1.0f : 0.0f}; }
            return Value{0.0f};
        };
        // Client: pin the host's identity key (base64url, the key a dedicated
        // server prints for --host-key); "" removes the pin. 1 if it's well formed.
        // A pinned client joins only that host, over the sealed join.
        b["net_pin_host"] = [this](std::vector<Value>& a) {
            NetworkManager* n = Net();
            if (!n || a.empty()) return Value{0.0f};
            const std::string key = a[0].AsString();
            std::vector<std::uint8_t> raw;
            if (!key.empty() && (!net::SessionTicket::Base64UrlDecode(key, raw) || raw.size() != 32)) return Value{0.0f};
            n->pinnedHostKey = key;
            return Value{1.0f};
        };
        // 1 once an encrypted session is established, else 0.
        b["net_encrypted"] = [this](std::vector<Value>&) {
            NetworkManager* n = Net(); return Value{(n && n->Encrypted()) ? 1.0f : 0.0f};
        };
}

void OkayScriptVM::Impl::RegisterServerBrowserBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        // ---- Supabase matchmaking / server browser --------------------
        // A shared cache of the last browse, so scripts can list then join by index.
        auto mmList = std::make_shared<std::vector<GameSession>>();
        // mm_host(name, addr, port [, max, room, region]) -> session id ("" on fail).
        b["mm_host"] = [](std::vector<Value>& a) {
            if (a.size() < 3) return Value{std::string{}};
            int max = a.size() > 3 ? SafeInt(a[3].AsFloat()) : 8;
            std::string room   = a.size() > 4 ? a[4].AsString() : std::string{};
            std::string region = a.size() > 5 ? a[5].AsString() : std::string{};
            return Value{Matchmaking::Host(a[0].AsString(), a[1].AsString(),
                                           SafeInt(a[2].AsFloat()), max, room, region)};
        };
        b["mm_heartbeat"] = [](std::vector<Value>& a) {
            if (a.size() < 2) return Value{0.0f};
            return Value{Matchmaking::Heartbeat(a[0].AsString(), SafeInt(a[1].AsFloat())) ? 1.0f : 0.0f};
        };
        b["mm_unregister"] = [](std::vector<Value>& a) {
            return Value{(!a.empty() && Matchmaking::Unregister(a[0].AsString())) ? 1.0f : 0.0f};
        };
        // mm_refresh([room]) -> number of open sessions found (cached for the getters).
        b["mm_refresh"] = [mmList](std::vector<Value>& a) {
            *mmList = Matchmaking::List(a.empty() ? std::string{} : a[0].AsString());
            return Value{(float)mmList->size()};
        };
        b["mm_count"] = [mmList](std::vector<Value>&) { return Value{(float)mmList->size()}; };
        // Field getters for a cached session by index.
        auto at = [mmList](std::vector<Value>& a) -> const GameSession* {
            if (a.empty()) return nullptr;
            std::size_t i = SafeSize(a[0].AsFloat());
            return i < mmList->size() ? &(*mmList)[i] : nullptr;
        };
        b["mm_name"]    = [at](std::vector<Value>& a) { auto* g = at(a); return Value{g ? g->name : std::string{}}; };
        b["mm_addr"]    = [at](std::vector<Value>& a) { auto* g = at(a); return Value{g ? g->hostAddr : std::string{}}; };
        b["mm_port"]    = [at](std::vector<Value>& a) { auto* g = at(a); return Value{g ? (float)g->port : 0.0f}; };
        b["mm_players"] = [at](std::vector<Value>& a) { auto* g = at(a); return Value{g ? (float)g->players : 0.0f}; };
        b["mm_max"]     = [at](std::vector<Value>& a) { auto* g = at(a); return Value{g ? (float)g->maxPlayers : 0.0f}; };
        b["mm_room"]    = [at](std::vector<Value>& a) { auto* g = at(a); return Value{g ? g->room : std::string{}}; };
        // Join a cached session by index over the normal UDP transport.
        b["mm_join"] = [this, at](std::vector<Value>& a) {
            const GameSession* g = at(a);
            if (!g) return Value{0.0f};
            NetworkManager* n = EnsureNet();
            return Value{(n && n->StartClient(g->hostAddr, (std::uint16_t)g->port)) ? 1.0f : 0.0f};
        };

        // Send a message to everyone, or to one peer id.
        b["net_send"] = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net(); n && a.size() >= 2) n->Send(a[0].AsString(), a[1].AsString());
            return Value{};
        };
        b["net_send_to"] = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net(); n && a.size() >= 3) n->SendTo(SafeUInt32(a[0].AsFloat()), a[1].AsString(), a[2].AsString());
            return Value{};
        };
        // Broadcast a chat line (stamped with your name, logged everywhere).
        b["net_chat"] = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net(); n && !a.empty()) n->Chat(a[0].AsString());
            return Value{};
        };
        // Fire a named RPC on every other peer: net_rpc("name"[, data]).
        b["net_rpc"] = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net(); n && !a.empty()) n->Rpc(a[0].AsString(), a.size() > 1 ? a[1].AsString() : std::string{});
            return Value{};
        };
        // Reliable (resent until acked) variants for events you can't drop.
        b["net_send_reliable"] = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net(); n && a.size() >= 2) n->SendReliable(a[0].AsString(), a[1].AsString());
            return Value{};
        };
        b["net_send_reliable_to"] = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net(); n && a.size() >= 3) n->SendReliableTo(SafeUInt32(a[0].AsFloat()), a[1].AsString(), a[2].AsString());
            return Value{};
        };
        // Moderation: host kicks a peer; clients can check if they were kicked.
        b["net_kick"] = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net(); n && !a.empty()) n->Kick(SafeUInt32(a[0].AsFloat()), a.size() > 1 ? a[1].AsString() : std::string{});
            return Value{};
        };
        b["net_was_kicked"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{(n && n->WasKicked()) ? 1.0f : 0.0f}; };
        b["net_server_name"] = [this](std::vector<Value>&) { NetworkManager* n = Net(); return Value{n ? n->ServerName() : std::string{}}; };
        // Drain one received message into net_msg_* accessors; returns 1 if one
        // was popped (use in a while-loop), 0 when the inbox is empty.
        b["net_poll"] = [this](std::vector<Value>&) {
            NetworkManager* n = Net();
            NetworkManager::NetMessage m;
            if (n && n->PopMessage(m)) {
                netMsgChannel = m.channel; netMsgData = m.data; netMsgFrom = (float)m.from;
                return Value{1.0f};
            }
            netMsgChannel.clear(); netMsgData.clear(); netMsgFrom = 0.0f;
            return Value{0.0f};
        };
        b["net_msg_channel"] = [this](std::vector<Value>&) { return Value{netMsgChannel}; };
        b["net_msg_data"]    = [this](std::vector<Value>&) { return Value{netMsgData}; };
        b["net_msg_from"]    = [this](std::vector<Value>&) { return Value{netMsgFrom}; };
        // Server-authoritative synced variables: net_set on the server (or a
        // client request) propagates to every peer; net_get reads the local copy.
        b["net_set"] = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net(); n && a.size() >= 2) n->SetVar(a[0].AsString(), a[1].AsString());
            return Value{};
        };
        // Spawn a prefab on every peer (replicated): net_spawn("file", x, y[, z]).
        b["net_spawn"] = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net(); n && !a.empty())
                n->Spawn(a[0].AsString(), {a.size() > 1 ? a[1].AsFloat() : 0.0f,
                                           a.size() > 2 ? a[2].AsFloat() : 0.0f,
                                           a.size() > 3 ? a[3].AsFloat() : 0.0f});
            return Value{};
        };
        // Spawn a prefab THIS peer owns + auto-syncs (bullets, items); returns its
        // sync id for a later net_despawn: net_spawn_owned("file", x, y[, z]).
        b["net_spawn_owned"] = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net(); n && !a.empty()) {
                GameObject* g = n->SpawnOwned(a[0].AsString(),
                                              {a.size() > 1 ? a[1].AsFloat() : 0.0f,
                                               a.size() > 2 ? a[2].AsFloat() : 0.0f,
                                               a.size() > 3 ? a[3].AsFloat() : 0.0f});
                if (g) if (auto* ns = g->GetComponent<NetworkSync>()) return Value{ns->netId};
            }
            return Value{std::string{}};
        };
        // Despawn a net_spawn_owned object on every peer by its sync id.
        b["net_despawn"] = [this](std::vector<Value>& a) {
            if (NetworkManager* n = Net(); n && !a.empty()) n->Despawn(a[0].AsString());
            return Value{};
        };
        b["net_get"] = [this](std::vector<Value>& a) {
            NetworkManager* n = Net();
            return Value{(n && !a.empty()) ? n->GetVar(a[0].AsString()) : std::string{}};
        };
        // Object RPC / SyncVar on THIS object (needs a NetworkObject for RPCs).
        auto netObj = [this]() -> NetworkObject* {
            return (rt.host && rt.host->gameObject) ? rt.host->gameObject->GetComponent<NetworkObject>() : nullptr;
        };
        auto syncNamed = [this](const std::string& name) -> SyncVar* {
            if (!rt.host || !rt.host->gameObject) return nullptr;
            for (auto* sv : rt.host->gameObject->GetComponents<SyncVar>())
                if (sv->name == name) return sv;
            return nullptr;
        };
        b["net_obj_id"] = [netObj](std::vector<Value>&) {
            NetworkObject* o = netObj(); return Value{o ? o->Id() : std::string{}};
        };
        b["net_obj_owned"] = [netObj](std::vector<Value>&) {
            NetworkObject* o = netObj(); return Value{(o && o->IsLocallyOwned()) ? 1.0f : 0.0f};
        };
        b["net_obj_rpc"] = [netObj](std::vector<Value>& a) {
            if (NetworkObject* o = netObj(); o && !a.empty())
                o->Rpc(a[0].AsString(), a.size() > 1 ? a[1].AsString() : std::string{});
            return Value{};
        };
        b["net_obj_rpc_to"] = [netObj](std::vector<Value>& a) {
            if (NetworkObject* o = netObj(); o && a.size() >= 2)
                o->RpcTo(SafeUInt32(a[0].AsFloat()), a[1].AsString(), a.size() > 2 ? a[2].AsString() : std::string{});
            return Value{};
        };
        b["net_obj_rpc_reliable"] = [netObj](std::vector<Value>& a) {
            if (NetworkObject* o = netObj(); o && !a.empty())
                o->RpcReliable(a[0].AsString(), a.size() > 1 ? a[1].AsString() : std::string{});
            return Value{};
        };
        b["net_obj_rpc_poll"] = [this, netObj](std::vector<Value>&) {
            NetworkObject* o = netObj();
            std::uint32_t from = 0; std::string name, data;
            if (o && o->PollRpc(from, name, data)) {
                netObjRpcName = std::move(name); netObjRpcData = std::move(data); netObjRpcFrom = (float)from;
                return Value{1.0f};
            }
            netObjRpcName.clear(); netObjRpcData.clear(); netObjRpcFrom = 0.0f;
            return Value{0.0f};
        };
        b["net_obj_rpc_name"] = [this](std::vector<Value>&) { return Value{netObjRpcName}; };
        b["net_obj_rpc_data"] = [this](std::vector<Value>&) { return Value{netObjRpcData}; };
        b["net_obj_rpc_from"] = [this](std::vector<Value>&) { return Value{netObjRpcFrom}; };
        b["net_syncvar_set"] = [this, syncNamed](std::vector<Value>& a) {
            if (a.empty()) return Value{};
            const std::string name = a[0].AsString();
            const std::string val = a.size() > 1 ? a[1].AsString() : std::string{};
            if (SyncVar* sv = syncNamed(name)) { sv->Set(val); return Value{}; }
            if (!rt.host || !rt.host->gameObject) return Value{};
            NetworkObject* o = rt.host->gameObject->GetComponent<NetworkObject>();
            NetworkManager* n = Net();
            if (n && (!o || o->IsLocallyOwned())) {
                const std::string id = o ? o->Id() : rt.host->gameObject->name;
                n->SetVar(id.empty() ? name : id + "." + name, val);
            }
            return Value{};
        };
        b["net_syncvar_get"] = [this, syncNamed](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string{}};
            const std::string name = a[0].AsString();
            if (SyncVar* sv = syncNamed(name)) return Value{sv->Get()};
            NetworkManager* n = Net();
            if (!n || !rt.host || !rt.host->gameObject) return Value{std::string{}};
            NetworkObject* o = rt.host->gameObject->GetComponent<NetworkObject>();
            const std::string id = o ? o->Id() : rt.host->gameObject->name;
            return Value{n->GetVar(id.empty() ? name : id + "." + name)};
        };
}

void OkayScriptVM::Impl::RegisterSteamBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        // ---- Steam (shared process-wide service) ----------------------
        b["steam_name"]   = [](std::vector<Value>&) { return Value{Steam::Get().UserName()}; };
        b["steam_unlock"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{0.0f};
            bool ok = Steam::Get().UnlockAchievement(a[0].AsString());
            Steam::Get().StoreStats();
            return Value{ok ? 1.0f : 0.0f};
        };
        b["steam_is_unlocked"] = [](std::vector<Value>& a) {
            return Value{(!a.empty() && Steam::Get().IsAchievementUnlocked(a[0].AsString())) ? 1.0f : 0.0f};
        };
        b["steam_clear"] = [](std::vector<Value>& a) {
            if (!a.empty()) Steam::Get().ClearAchievement(a[0].AsString());
            return Value{};
        };
        b["steam_set_stat"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) Steam::Get().SetStat(a[0].AsString(), a[1].AsFloat());
            return Value{};
        };
        b["steam_get_stat"] = [](std::vector<Value>& a) {
            return Value{a.empty() ? 0.0f : Steam::Get().GetStat(a[0].AsString())};
        };
        b["steam_inc_stat"] = [](std::vector<Value>& a) {
            return Value{a.size() < 2 ? 0.0f : Steam::Get().IncrementStat(a[0].AsString(), a[1].AsFloat())};
        };
        b["steam_store"] = [](std::vector<Value>&) { return Value{Steam::Get().StoreStats() ? 1.0f : 0.0f}; };
        b["steam_leaderboard"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) Steam::Get().UploadLeaderboardScore(a[0].AsString(), (std::int32_t)a[1].AsFloat());
            return Value{};
        };
        b["steam_cloud_write"] = [](std::vector<Value>& a) {
            return Value{(a.size() >= 2 && Steam::Get().CloudWrite(a[0].AsString(), a[1].AsString())) ? 1.0f : 0.0f};
        };
        b["steam_cloud_read"] = [](std::vector<Value>& a) {
            return Value{a.empty() ? std::string{} : Steam::Get().CloudRead(a[0].AsString())};
        };
        b["steam_progress"] = [](std::vector<Value>& a) {
            if (a.size() >= 3) Steam::Get().IndicateAchievementProgress(a[0].AsString(), SafeUInt32(a[1].AsFloat()), SafeUInt32(a[2].AsFloat()));
            return Value{};
        };
        b["steam_presence"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) Steam::Get().SetRichPresence(a[0].AsString(), a[1].AsString());
            return Value{};
        };
        b["steam_friends"] = [](std::vector<Value>&) { return Value{(float)Steam::Get().FriendCount()}; };
        b["steam_owns"] = [](std::vector<Value>& a) { return Value{Steam::Get().OwnsApp(a.empty() ? 0u : SafeUInt32(a[0].AsFloat())) ? 1.0f : 0.0f}; };
        b["steam_owns_dlc"] = [](std::vector<Value>& a) { return Value{Steam::Get().IsDlcInstalled(a.empty() ? 0u : SafeUInt32(a[0].AsFloat())) ? 1.0f : 0.0f}; };
        b["steam_achievement_count"] = [](std::vector<Value>&) { return Value{(float)Steam::Get().AchievementCount()}; };
        b["steam_language"] = [](std::vector<Value>&) { return Value{Steam::Get().Language()}; };
        // Top-N leaderboard entries as an array of "rank,name,score" strings.
        b["steam_leaderboard_top"] = [](std::vector<Value>& a) {
            Value v = Value::MakeArray();
            auto arr = v.AsArray();
            if (!a.empty() && arr) {
                int n = a.size() > 1 ? SafeInt(a[1].AsFloat()) : 10;
                for (auto& e : Steam::Get().DownloadLeaderboardTop(a[0].AsString(), n))
                    arr->push_back(Value{std::to_string(e.rank) + "," + e.name + "," + std::to_string(e.score)});
            }
            return v;
        };
        b["steam_overlay"] = [](std::vector<Value>& a) {
            Steam::Get().ActivateOverlay(a.empty() ? "friends" : a[0].AsString());
            return Value{};
        };
        // Steam Cloud extras.
        b["steam_cloud_has"] = [](std::vector<Value>& a) {
            return Value{(!a.empty() && Steam::Get().CloudHasFile(a[0].AsString())) ? 1.0f : 0.0f};
        };
        b["steam_cloud_delete"] = [](std::vector<Value>& a) {
            return Value{(!a.empty() && Steam::Get().CloudDelete(a[0].AsString())) ? 1.0f : 0.0f};
        };
        b["steam_cloud_count"] = [](std::vector<Value>&) {
            return Value{(float)Steam::Get().CloudFiles().size()};
        };
        // Steam Workshop (community content).
        // Ids are 64-bit (real ones have ten digits): a float would round them (#888 F32: 2847193562 became 2847193600).
        // Publish returns a whole number (Value::Int); an id is read back from a whole number, from text of digits, or
        // from a float only when it is finite and not negative. Anything else is 0, "no item" (no out-of-range cast).
        auto workshopId = [](const Value& v) -> std::uint64_t {
            if (const std::int64_t* i = v.IntPtr()) return *i > 0 ? (std::uint64_t)*i : 0;
            if (v.IsString()) {
                const std::string t = v.AsString();
                if (t.empty() || t.size() > 20) return 0;
                std::uint64_t id = 0;
                for (char ch : t) {
                    if (ch < '0' || ch > '9') return 0;
                    const std::uint64_t d = (std::uint64_t)(ch - '0');
                    if (id > (UINT64_MAX - d) / 10) return 0;
                    id = id * 10 + d;
                }
                return id;
            }
            const float f = v.AsFloat();
            return (std::isfinite(f) && f >= 1.0f && f < 1.8e19f) ? (std::uint64_t)f : 0;
        };
        b["steam_workshop_publish"] = [](std::vector<Value>& a) {
            WorkshopItem it;
            if (a.size() >= 1) it.title = a[0].AsString();
            if (a.size() >= 2) it.contentPath = a[1].AsString();
            if (a.size() >= 3) it.description = a[2].AsString();
            const std::uint64_t id = Steam::Get().WorkshopPublish(it);
            return id <= (std::uint64_t)INT64_MAX ? Value::Int((std::int64_t)id) : Value{std::to_string(id)};
        };
        b["steam_workshop_subscribe"] = [workshopId](std::vector<Value>& a) {
            const std::uint64_t id = a.empty() ? 0 : workshopId(a[0]);
            return Value{(id && Steam::Get().WorkshopSubscribe(id)) ? 1.0f : 0.0f};
        };
        b["steam_workshop_unsubscribe"] = [workshopId](std::vector<Value>& a) {
            const std::uint64_t id = a.empty() ? 0 : workshopId(a[0]);
            return Value{(id && Steam::Get().WorkshopUnsubscribe(id)) ? 1.0f : 0.0f};
        };
        b["steam_workshop_count"] = [](std::vector<Value>&) {
            return Value{(float)Steam::Get().WorkshopSubscribedItems().size()};
        };
        b["steam_workshop_path"] = [workshopId](std::vector<Value>& a) {
            const std::uint64_t id = a.empty() ? 0 : workshopId(a[0]);
            return Value{id ? Steam::Get().WorkshopItemPath(id) : std::string{}};
        };
        // Integer stats (kills, wins, ...). Floats lose precision past 2^24, so
        // counters that climb high belong here rather than in steam_*_stat.
        b["steam_set_stat_int"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) Steam::Get().SetStatInt(a[0].AsString(), SafeInt(a[1].AsFloat()));
            return Value{};
        };
        b["steam_get_stat_int"] = [](std::vector<Value>& a) {
            return Value{a.empty() ? 0.0f : (float)Steam::Get().GetStatInt(a[0].AsString())};
        };
        b["steam_inc_stat_int"] = [](std::vector<Value>& a) {
            return Value{a.size() < 2 ? 0.0f : (float)Steam::Get().IncrementStatInt(a[0].AsString(), SafeInt(a[1].AsFloat()))};
        };
        // Achievement name by index (0..steam_achievement_count-1).
        b["steam_achievement_name"] = [](std::vector<Value>& a) {
            return Value{a.empty() ? std::string{} : Steam::Get().AchievementName(SafeInt(a[0].AsFloat()))};
        };
        // Read back a rich-presence value previously set with steam_presence.
        b["steam_get_presence"] = [](std::vector<Value>& a) {
            return Value{a.empty() ? std::string{} : Steam::Get().GetRichPresence(a[0].AsString())};
        };
}

void OkayScriptVM::Impl::RegisterFriendsBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        // ---- Friends -------------------------------------------------------
        // Big Steam ids exceed float precision, so friend/lobby ids are passed
        // as strings in and out of these builtins.
        b["steam_friend_name"] = [](std::vector<Value>& a) {
            return Value{a.empty() ? std::string{} : Steam::Get().FriendName(SafeInt(a[0].AsFloat()))};
        };
        b["steam_friend_id"] = [](std::vector<Value>& a) {
            return Value{a.empty() ? std::string{} : std::to_string(Steam::Get().FriendId(SafeInt(a[0].AsFloat())))};
        };
        b["steam_invite"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{0.0f};
            std::uint64_t fid = (std::uint64_t)std::strtoull(a[0].AsString().c_str(), nullptr, 10);
            return Value{Steam::Get().InviteFriend(fid) ? 1.0f : 0.0f};
        };
}

void OkayScriptVM::Impl::RegisterLobbyBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        // ---- Lobbies (Steam matchmaking) -----------------------------------
        b["steam_lobby_create"] = [](std::vector<Value>& a) {
            int maxm = a.empty() ? 4 : SafeInt(a[0].AsFloat());
            std::string name = a.size() > 1 ? a[1].AsString() : std::string{};
            return Value{std::to_string(Steam::Get().CreateLobby(maxm, name))};
        };
        b["steam_lobby_join"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{0.0f};
            std::uint64_t id = (std::uint64_t)std::strtoull(a[0].AsString().c_str(), nullptr, 10);
            return Value{Steam::Get().JoinLobby(id) ? 1.0f : 0.0f};
        };
        b["steam_lobby_leave"] = [](std::vector<Value>&) { Steam::Get().LeaveLobby(); return Value{}; };
        b["steam_lobby_current"] = [](std::vector<Value>&) { return Value{std::to_string(Steam::Get().CurrentLobby())}; };
        b["steam_lobby_set_data"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) Steam::Get().SetLobbyData(a[0].AsString(), a[1].AsString());
            return Value{};
        };
        b["steam_lobby_get_data"] = [](std::vector<Value>& a) {
            if (a.size() < 2) return Value{std::string{}};
            std::uint64_t id = (std::uint64_t)std::strtoull(a[0].AsString().c_str(), nullptr, 10);
            return Value{Steam::Get().GetLobbyData(id, a[1].AsString())};
        };
        // Open lobbies as an array of "id,name,members,max,joinable" strings.
        b["steam_lobby_list"] = [](std::vector<Value>&) {
            Value v = Value::MakeArray(); auto arr = v.AsArray();
            if (arr) for (auto& l : Steam::Get().LobbyList())
                arr->push_back(Value{std::to_string(l.id) + "," + l.name + "," +
                                     std::to_string(l.memberCount) + "," + std::to_string(l.maxMembers) + "," +
                                     (l.joinable ? "1" : "0")});
            return v;
        };
        // Member display names in a lobby, as a string array.
        b["steam_lobby_members"] = [](std::vector<Value>& a) {
            Value v = Value::MakeArray(); auto arr = v.AsArray();
            if (arr && !a.empty()) {
                std::uint64_t id = (std::uint64_t)std::strtoull(a[0].AsString().c_str(), nullptr, 10);
                for (auto& n : Steam::Get().LobbyMembers(id)) arr->push_back(Value{n});
            }
            return v;
        };
}

void OkayScriptVM::Impl::RegisterPlayFabBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        // ---- PlayFab (cloud login / leaderboards / saves; blocking HTTP) ----
        // Mirrors the playfab_* visual-scripting instructions. Call pf_login once
        // (e.g. in start()), not per frame — each call is a real web request.
        b["pf_login"] = [](std::vector<Value>& a) {           // pf_login(titleId, customId)
            if (a.size() < 2) return Value{0.0f};
            PlayFab::Get().Configure(a[0].AsString());
            return Value{PlayFab::Get().LoginWithCustomID(a[1].AsString()) ? 1.0f : 0.0f};
        };
        b["pf_login_password"] = [](std::vector<Value>& a) {  // pf_login_password(titleId, user, pass)
            if (a.size() < 3) return Value{0.0f};
            PlayFab::Get().Configure(a[0].AsString());
            return Value{PlayFab::Get().LoginWithPassword(a[1].AsString(), a[2].AsString()) ? 1.0f : 0.0f};
        };
        b["pf_register"] = [](std::vector<Value>& a) {        // pf_register(titleId, user, pass)
            if (a.size() < 3) return Value{0.0f};
            PlayFab::Get().Configure(a[0].AsString());
            return Value{PlayFab::Get().RegisterWithPassword(a[1].AsString(), a[2].AsString()) ? 1.0f : 0.0f};
        };
        b["pf_logged_in"] = [](std::vector<Value>&) { return Value{PlayFab::Get().IsLoggedIn() ? 1.0f : 0.0f}; };
        b["pf_id"]        = [](std::vector<Value>&) { return Value{PlayFab::Get().PlayFabId()}; };
        b["pf_error"]     = [](std::vector<Value>&) { return Value{PlayFab::Get().LastError()}; };
        b["pf_name"] = [](std::vector<Value>& a) {            // leaderboard display name
            return Value{(!a.empty() && PlayFab::Get().SetDisplayName(a[0].AsString())) ? 1.0f : 0.0f};
        };
        b["pf_set_stat"] = [](std::vector<Value>& a) {        // pf_set_stat(stat, value)
            return Value{(a.size() >= 2 &&
                          PlayFab::Get().SetStat(a[0].AsString(), SafeInt(a[1].AsFloat()))) ? 1.0f : 0.0f};
        };
        // Top-N entries as an array of "rank,name,score" strings (Steam parity).
        b["pf_leaderboard_top"] = [](std::vector<Value>& a) {
            Value v = Value::MakeArray();
            auto arr = v.AsArray();
            if (!a.empty() && arr) {
                int n = a.size() > 1 ? SafeInt(a[1].AsFloat()) : 10;
                std::vector<PlayFab::Entry> rows;
                if (PlayFab::Get().GetLeaderboard(a[0].AsString(), n, rows))
                    for (auto& e : rows)
                        arr->push_back(Value{std::to_string(e.position + 1) + "," + e.name + "," +
                                             std::to_string(e.value)});
            }
            return v;
        };
        b["pf_save"] = [](std::vector<Value>& a) {            // pf_save(key, value)
            return Value{(a.size() >= 2 &&
                          PlayFab::Get().SetUserData(a[0].AsString(), a[1].AsString())) ? 1.0f : 0.0f};
        };
        b["pf_load"] = [](std::vector<Value>& a) {            // pf_load(key) -> text
            std::string v2;
            if (!a.empty()) PlayFab::Get().GetUserData(a[0].AsString(), v2);
            return Value{v2};
        };
        b["pf_delete"] = [](std::vector<Value>& a) {
            return Value{(!a.empty() && PlayFab::Get().DeleteUserData(a[0].AsString())) ? 1.0f : 0.0f};
        };
}

void OkayScriptVM::Impl::RegisterAccountBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        // ---- Player accounts (shared process-wide service) ------------
        // account_login("user","pass") / account_register(...) return 1 on
        // success and 0 on failure; account_error() carries the reason.
        b["account_register"] = [](std::vector<Value>& a) {
            if (a.size() < 2) return Value{0.0f};
            return Value{Account::Register(a[0].AsString(), a[1].AsString()).ok ? 1.0f : 0.0f};
        };
        b["account_login"] = [](std::vector<Value>& a) {
            if (a.size() < 2) return Value{0.0f};
            return Value{Account::Login(a[0].AsString(), a[1].AsString()).ok ? 1.0f : 0.0f};
        };
        b["account_logout"] = [](std::vector<Value>&) { Account::Logout(); return Value{}; };
        b["account_is_logged_in"] = [](std::vector<Value>&) { return Value{Account::IsLoggedIn() ? 1.0f : 0.0f}; };
        b["account_is_online"] = [](std::vector<Value>&) { return Value{Account::IsOnline() ? 1.0f : 0.0f}; };
        b["account_username"] = [](std::vector<Value>&) { return Value{Account::Username()}; };
        b["account_token"]    = [](std::vector<Value>&) { return Value{Account::Token()}; };
        b["account_error"]    = [](std::vector<Value>&) { return Value{Account::LastError()}; };
        // Re-check the saved session with the server (signs out if rejected).
        b["account_verify"]   = [](std::vector<Value>&) { return Value{Account::VerifySession() ? 1.0f : 0.0f}; };
        // Authenticated requests to the account server (bearer token attached).
        // account_get(path) / account_post(path, json) return the response body,
        // or "" if the request didn't reach a 2xx (offline, not signed in, ...).
        b["account_get"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string{}};
            auto r = Account::Api(a[0].AsString(), "GET");
            return Value{r.ok ? r.body : std::string{}};
        };
        b["account_post"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string{}};
            auto r = Account::Api(a[0].AsString(), "POST", a.size() > 1 ? a[1].AsString() : std::string{});
            return Value{r.ok ? r.body : std::string{}};
        };
        // Cloud saves: per-account storage on the server so progress follows the
        // player across devices. cloud_save("slot1", save_text) etc.
        b["cloud_save"] = [](std::vector<Value>& a) {
            return Value{(a.size() >= 2 && Account::CloudSave(a[0].AsString(), a[1].AsString())) ? 1.0f : 0.0f};
        };
        b["cloud_load"] = [](std::vector<Value>& a) {
            return Value{a.empty() ? std::string{} : Account::CloudLoad(a[0].AsString())};
        };
        b["cloud_has"] = [](std::vector<Value>& a) {
            return Value{(!a.empty() && Account::CloudHas(a[0].AsString())) ? 1.0f : 0.0f};
        };
        b["cloud_delete"] = [](std::vector<Value>& a) {
            return Value{(!a.empty() && Account::CloudDelete(a[0].AsString())) ? 1.0f : 0.0f};
        };
        b["cloud_list"] = [](std::vector<Value>&) {
            Value v = Value::MakeArray();
            if (auto arr = v.AsArray())
                for (auto& k : Account::CloudList()) arr->push_back(Value{k});
            return v;
        };
        // Server leaderboards: leaderboard_submit("high", score) keeps the
        // player's best; leaderboard_top("high", n) returns "rank,name,score"
        // strings (same shape as steam_leaderboard_top).
        b["leaderboard_submit"] = [](std::vector<Value>& a) {
            return Value{(a.size() >= 2 && Account::LeaderboardSubmit(a[0].AsString(), SafeInt64(a[1].AsFloat()))) ? 1.0f : 0.0f};
        };
        b["leaderboard_top"] = [](std::vector<Value>& a) {
            Value v = Value::MakeArray();
            auto arr = v.AsArray();
            if (!a.empty() && arr) {
                int n = a.size() > 1 ? SafeInt(a[1].AsFloat()) : 10;
                for (auto& e : Account::LeaderboardTop(a[0].AsString(), n))
                    arr->push_back(Value{std::to_string(e.rank) + "," + e.name + "," + std::to_string(e.score)});
            }
            return v;
        };
        // Scriptable Objects: read reusable .okaydata assets (item/enemy/level
        // definitions, config). Loaded + cached by path.
        b["data_num"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{0.0f};
            double def = a.size() > 2 ? a[2].AsFloat() : 0.0;
            if (!ScriptPathOk(a[0].AsString(), "data_num")) return Value{(float)def};
            return Value{(float)DataAsset::Cached(a[0].AsString()).GetNumber(a[1].AsString(), def)};
        };
        b["data_str"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{std::string{}};
            std::string def = a.size() > 2 ? a[2].AsString() : std::string{};
            if (!ScriptPathOk(a[0].AsString(), "data_str")) return Value{def};
            return Value{DataAsset::Cached(a[0].AsString()).GetString(a[1].AsString(), def)};
        };
        b["data_has"] = [](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{false};
            if (!ScriptPathOk(a[0].AsString(), "data_has")) return Value{false};
            return Value{DataAsset::Cached(a[0].AsString()).Has(a[1].AsString())};
        };
        // Customize a data asset from code (then data_save to persist it).
        b["data_set"] = [](std::vector<Value>& a) {
            if (a.size() >= 3 && ScriptPathOk(a[0].AsString(), "data_set"))
                DataAsset::Cached(a[0].AsString()).Set(a[1].AsString(), a[2].AsString());
            return Value{};
        };
        b["data_save"] = [](std::vector<Value>& a) -> Value {
            if (a.empty() || !ScriptPathOk(a[0].AsString(), "data_save")) return Value{false};
            return Value{DataAsset::Cached(a[0].AsString()).Save(a[0].AsString())};
        };
        // Persistent prefs (high scores, settings) — survive across runs.
        b["prefs_set"] = [](std::vector<Value>& a) {
            if (a.size() >= 2) {
                if (a[1].IsString()) Prefs::SetString(a[0].AsString(), a[1].AsString());
                else Prefs::SetFloat(a[0].AsString(), a[1].AsFloat());
            }
            return Value{};
        };
        b["prefs_get"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{0.0f};
            return Value{Prefs::GetFloat(a[0].AsString(), a.size() > 1 ? a[1].AsFloat() : 0.0f)};
        };
        b["prefs_get_str"] = [](std::vector<Value>& a) {
            if (a.empty()) return Value{std::string{}};
            return Value{Prefs::GetString(a[0].AsString(), a.size() > 1 ? a[1].AsString() : std::string{})};
        };
        // Into the game's save folder; loading falls back to the old spot beside the game.
        b["prefs_save"] = [](std::vector<Value>& a) {
            const std::string name = a.empty() ? "game.okayprefs" : a[0].AsString();
            if (!ScriptPathOk(name, "prefs_save")) return Value{false};
            return Value{Prefs::Save(Save::Resolve(name))};
        };
        b["prefs_load"] = [](std::vector<Value>& a) {
            const std::string name = a.empty() ? "game.okayprefs" : a[0].AsString();
            if (!ScriptPathOk(name, "prefs_load")) return Value{false};
            return Value{Prefs::Load(ScriptReadPath(name))};
        };

        // Easy-Save-3-style save system: typed values, many files, write-through.
        //   save("coins", 10)                 -> default file
        //   save("spawn", new Vector3(1,2,3), "slot1.okaysave")
        //   load("coins", 0)  /  load("name", "Hero")  /  load("spawn", vec)
        // The 3rd arg (file) is optional everywhere; values keep their type.
        // The file argument, or "" when it would escape the save folder (refused + logged).
        auto saveFile = [](std::vector<Value>& a, size_t idx) -> std::string {
            std::string f = (a.size() > idx && a[idx].IsString()) ? a[idx].AsString() : Save::DefaultFile();
            return ScriptPathOk(f, "save file") ? f : std::string();
        };
        b["save"] = [saveFile](std::vector<Value>& a) -> Value {
            if (a.size() < 2) return Value{false};
            std::string key = a[0].AsString(), file = saveFile(a, 2);
            if (file.empty()) return Value{false};
            if (a[1].IsVec3())        Save::SetVec3(key, a[1].AsVec3(), file);
            else if (a[1].IsString()) Save::SetString(key, a[1].AsString(), file);
            else                      Save::SetFloat(key, a[1].AsFloat(), file);
            return Value{true};
        };
        b["load"] = [saveFile](std::vector<Value>& a) -> Value {
            if (a.empty()) return Value{0.0f};
            std::string key = a[0].AsString(), file = saveFile(a, 2);
            if (file.empty()) return a.size() > 1 ? a[1] : Value{0.0f};
            if (a.size() > 1 && a[1].IsVec3())   return Value{Save::GetVec3(key, a[1].AsVec3(), file)};
            if (a.size() > 1 && a[1].IsString()) return Value{Save::GetString(key, a[1].AsString(), file)};
            float def = a.size() > 1 ? a[1].AsFloat() : 0.0f;
            return Value{Save::GetFloat(key, def, file)};
        };
        b["save_has"] = [saveFile](std::vector<Value>& a) -> Value {
            if (a.empty()) return Value{false};
            const std::string file = saveFile(a, 1);
            return Value{!file.empty() && Save::Has(a[0].AsString(), file)};
        };
        b["save_delete"] = [saveFile](std::vector<Value>& a) -> Value {
            if (a.empty()) return Value{};
            const std::string file = saveFile(a, 1);
            if (!file.empty()) Save::Delete(a[0].AsString(), file);
            return Value{};
        };
        b["save_clear"] = [saveFile](std::vector<Value>& a) -> Value {
            const std::string file = saveFile(a, 0);
            if (!file.empty()) Save::Clear(file);
            return Value{};
        };
        // save_exists is has_save's other name (shared table); it never reached this file store (a later line
        // replaced it), so the key/value file check that stood here was dead code.
        b["save_delete_file"] = [saveFile](std::vector<Value>& a) -> Value {
            const std::string file = saveFile(a, 0);
            return Value{!file.empty() && Save::DeleteFile(file)};
        };
        // Math helpers: the language core's shared set (okayscript/include/okayscript/CoreBuiltins.hpp), the same
        // functions and rules as in the okayscript CLI.
        okayscript::InstallMathBuiltins<Value>(b);
}

} // namespace okay
