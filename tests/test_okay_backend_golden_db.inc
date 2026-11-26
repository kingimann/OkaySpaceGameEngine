// GOLDEN request texts for tests/test_okay_backend_golden_db.cpp, recorded from the call sites at commit 3ec33606e (AccountService.hpp,
// Matchmaking.hpp, MatchResults.hpp and Matchmaker.cpp before they moved onto okay::backend::Database; the engine's log lines removed).
// Never edit by hand: a change here means the programs send something new.
// Recorded with OKAY_GOLDEN_PRINT=1 (tests/test_okay_backend_golden_db.cpp).
static const char* const kGolden[][2] = {
    {"username_available", R"GOLD(=> 1
=> 0
=> 1
=> 1
=> 1
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.alex
apikey: test-publishable-key-0000


---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.taken_name
apikey: test-publishable-key-0000


---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.al%20ex%2B%C3%A9%2F1%26x%3Dy
apikey: test-publishable-key-0000


---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.a.b-c~d
apikey: test-publishable-key-0000


---
)GOLD"},
    {"cloud_save", R"GOLD(=> 1
=> 0
=> 0
=> 0
=> 0
POST https://backend.example.test/rest/v1/cloud_saves
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: resolution=merge-duplicates,return=minimal
Content-Type: application/json

[{"key":"save1","data":"{\"lvl\":3,\"name\":\"a\\\\b\"}\nline2\t\r"}]
---
POST https://backend.example.test/rest/v1/cloud_saves
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: resolution=merge-duplicates,return=minimal
Content-Type: application/json

[{"key":"slot-2.bak","data":"x"}]
---
POST https://backend.example.test/rest/v1/cloud_saves
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: resolution=merge-duplicates,return=minimal
Content-Type: application/json

[{"key":"Save_3","data":""}]
---
)GOLD"},
    {"cloud_load", R"GOLD(=> [hello "you"
]
=> []
=> []
=> []
=> []
GET https://backend.example.test/rest/v1/cloud_saves?select=data&key=eq.save1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/cloud_saves?select=data&key=eq.save2
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/cloud_saves?select=data&key=eq.a.b
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/cloud_saves?select=data&key=eq.c-d
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
)GOLD"},
    {"cloud_has_delete_list", R"GOLD(=> 1
=> 0
=> 0
=> 1
=> 0
=> 0
=> [a|b.c]
=> []
GET https://backend.example.test/rest/v1/cloud_saves?select=key&key=eq.save1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/cloud_saves?select=key&key=eq.save2
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/cloud_saves?select=key&key=eq.save3
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
DELETE https://backend.example.test/rest/v1/cloud_saves?key=eq.save1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
DELETE https://backend.example.test/rest/v1/cloud_saves?key=eq.save9
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/cloud_saves?select=key
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/cloud_saves?select=key
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
)GOLD"},
    {"leaderboard_submit_top", R"GOLD(=> 1
=> 0
=> 0
  #1 alice 500
  #2 bob 300
=> 0
=> 0
POST https://backend.example.test/rest/v1/leaderboards
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: resolution=merge-duplicates,return=minimal
Content-Type: application/json

[{"board":"arcade","name":"al\"ex","score":1250}]
---
POST https://backend.example.test/rest/v1/leaderboards
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: resolution=merge-duplicates,return=minimal
Content-Type: application/json

[{"board":"arcade.hard","name":"al\"ex","score":-5}]
---
GET https://backend.example.test/rest/v1/leaderboards?select=name,score&board=eq.arcade&order=score.desc&limit=10
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/leaderboards?select=name,score&board=eq.arcade&order=score.desc&limit=3
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/leaderboards?select=name,score&board=eq.arcade&order=score.desc&limit=-1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
)GOLD"},
    {"signed_out_sends_nothing", R"GOLD(=> 0[]00[]000[Sign in first.]000
)GOLD"},
    {"newer_token_after_401", R"GOLD(=> [d]
GET https://backend.example.test/rest/v1/cloud_saves?select=data&key=eq.save1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/cloud_saves?select=data&key=eq.save1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MiIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
)GOLD"},
    {"mm_host_advertise", R"GOLD(=> [s-1]
=> []
=> [s-2]
=> [s-3]
=> []
POST https://backend.example.test/rest/v1/game_sessions
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MyIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: return=representation
Content-Type: application/json

[{"name":"Alex's \"Arena\"","host_addr":"203.0.113.5","port":7777,"max_players":8,"players":0,"room":"room-1","region":"eu"}]
---
POST https://backend.example.test/rest/v1/game_sessions
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MyIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: return=representation
Content-Type: application/json

[{"name":"Plain","host_addr":"203.0.113.5","port":7778,"max_players":8,"players":0,"room":"","region":""}]
---
POST https://backend.example.test/rest/v1/game_sessions
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MyIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: return=representation
Content-Type: application/json

[{"name":"Duel \\ night","host_addr":"198.51.100.7","port":45000,"max_players":4,"players":1,"room":"r","region":"us","game":"Arena Game","map":"Dust","has_password":true,"dedicated":false}]
---
POST https://backend.example.test/rest/v1/game_sessions
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MyIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: return=representation
Content-Type: application/json

[{"name":"Duel \\ night","host_addr":"198.51.100.7","port":45000,"max_players":4,"players":1,"room":"r","region":"us","game":"Arena Game","map":"Dust","has_password":true,"dedicated":false}]
---
POST https://backend.example.test/rest/v1/game_sessions
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MyIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: return=representation
Content-Type: application/json

[{"name":"Duel \\ night","host_addr":"198.51.100.7","port":45000,"max_players":4,"players":1,"room":"r","region":"us"}]
---
POST https://backend.example.test/rest/v1/game_sessions
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MyIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: return=representation
Content-Type: application/json

[{"name":"Duel \\ night","host_addr":"198.51.100.7","port":45000,"max_players":4,"players":1,"room":"r","region":"us","game":"Arena Game","map":"Dust","has_password":true,"dedicated":false}]
---
POST https://backend.example.test/rest/v1/game_sessions
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MyIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: return=representation
Content-Type: application/json

[{"name":"Duel \\ night","host_addr":"198.51.100.7","port":45000,"max_players":4,"players":1,"room":"r","region":"us"}]
---
)GOLD"},
    {"mm_browse", R"GOLD(  session id=[s-1] name=[One "q"] room=[] addr=[203.0.113.5:7777] region=[eu] players=2/8 game=[Arena Game] map=[Dust] pw=1 dedicated=0
  session id=[s-9] name=[Old] room=[r] addr=[h:1] region=[] players=1/2 game=[] map=[] pw=0 dedicated=0
=> 0
=> 0
GET https://backend.example.test/rest/v1/game_sessions?select=id,name,room,host_addr,port,players,max_players,region,game,map,has_password,dedicated&updated_at=gt.<FRESH>&order=players.desc,updated_at.desc&limit=200&game=eq.Arena%20Game
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MyIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/game_sessions?select=id,name,room,host_addr,port,players,max_players,region,game,map,has_password,dedicated&updated_at=gt.<FRESH>&order=players.desc,updated_at.desc&limit=200
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MyIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/game_sessions?select=id,name,room,host_addr,port,players,max_players,region&updated_at=gt.<FRESH>&order=players.desc,updated_at.desc&limit=200
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MyIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/game_sessions?select=id,name,room,host_addr,port,players,max_players,region,game,map,has_password,dedicated&updated_at=gt.<FRESH>&order=players.desc,updated_at.desc&limit=200&game=eq.A%26B%3Dc%20d
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MyIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/game_sessions?select=id,name,room,host_addr,port,players,max_players,region,game,map,has_password,dedicated&updated_at=gt.<FRESH>&order=players.desc,updated_at.desc&limit=200
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MyIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
)GOLD"},
    {"mm_heartbeat_unregister_list", R"GOLD(=> 1
=> 0
=> 0
=> 1
=> 0
  session id=[s-1] name=[One] room=[r1] addr=[203.0.113.5:7777] region=[eu] players=2/8 game=[] map=[] pw=0 dedicated=0
  session id=[s-2] name=[Two] room=[r1] addr=[h2:1] region=[] players=0/4 game=[] map=[] pw=0 dedicated=0
=> 0
=> 0
PATCH https://backend.example.test/rest/v1/game_sessions?id=eq.7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MyIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"players":3}
---
PATCH https://backend.example.test/rest/v1/game_sessions?id=eq.s-2
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MyIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"players":-1}
---
DELETE https://backend.example.test/rest/v1/game_sessions?id=eq.7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MyIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/game_sessions?select=id,name,room,host_addr,port,players,max_players,region&order=updated_at.desc
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MyIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/game_sessions?select=id,name,room,host_addr,port,players,max_players,region&order=updated_at.desc&room=eq.room-1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MyIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/game_sessions?select=id,name,room,host_addr,port,players,max_players,region&order=updated_at.desc&room=eq.Lobby A
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MyIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
)GOLD"},
    {"mr_report", R"GOLD(=> 1 id=[m-1] err=[]
=> 0 id=[] err=[Match results aren't set up on the server yet (docs/sql/matches.sql).]
=> 0 id=[] err=[Not a member of this match]
=> 0 id=[] err=[Couldn't reach the server.]
POST https://backend.example.test/rest/v1/rpc/report_match
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NCIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p":{"game":"Arena \"Game\"","map":"Dust\n2","server":"EU 1","mode":"duel","dedicated":true,"duration":312,"players":[{"user_id":"u-1","name":"alex","score":30,"place":1,"won":true,"self":true,"seconds":300},{"user_id":null,"name":"guest\\1","score":10,"place":2,"won":false,"self":false,"seconds":120}]}}
---
POST https://backend.example.test/rest/v1/rpc/report_match
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NCIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p":{"game":"Arena \"Game\"","map":"Dust\n2","server":"EU 1","mode":"duel","dedicated":true,"duration":312,"players":[{"user_id":"u-1","name":"alex","score":30,"place":1,"won":true,"self":true,"seconds":300},{"user_id":null,"name":"guest\\1","score":10,"place":2,"won":false,"self":false,"seconds":120}]}}
---
POST https://backend.example.test/rest/v1/rpc/report_match
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NCIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p":{"game":"Arena \"Game\"","map":"Dust\n2","server":"EU 1","mode":"duel","dedicated":true,"duration":312,"players":[{"user_id":"u-1","name":"alex","score":30,"place":1,"won":true,"self":true,"seconds":300},{"user_id":null,"name":"guest\\1","score":10,"place":2,"won":false,"self":false,"seconds":120}]}}
---
POST https://backend.example.test/rest/v1/rpc/report_match
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NCIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p":{"game":"Arena \"Game\"","map":"Dust\n2","server":"EU 1","mode":"duel","dedicated":true,"duration":312,"players":[{"user_id":"u-1","name":"alex","score":30,"place":1,"won":true,"self":true,"seconds":300},{"user_id":null,"name":"guest\\1","score":10,"place":2,"won":false,"self":false,"seconds":120}]}}
---
)GOLD"},
    {"mr_reads", R"GOLD(  stats user=[u-1] name=[Alex] avatar=[https://a.example.test/1.png] matches=12 wins=7 total=340 best=60 seconds=3600 last=1795168800 rank=1
  stats user=[u-2] name=[bo] avatar=[] matches=3 wins=0 total=20 best=9 seconds=400 last=0 rank=2
supported=1
=> 0 supported=0
=> 1
  stats user=[u-1] name=[alex] avatar=[] matches=12 wins=7 total=340 best=60 seconds=3600 last=1795168800 rank=4
=> 0
  recent user=[u-2] name=[Bo] game=[Arena] when=1795249800 matches=2
supported=1
=> 0 supported=0
POST https://backend.example.test/rest/v1/rpc/game_leaderboard
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NCIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_game":"Arena \"Game\"","p_stat":"wins","p_limit":5}
---
POST https://backend.example.test/rest/v1/rpc/game_leaderboard
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NCIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_game":"Arena","p_stat":"time","p_limit":50}
---
POST https://backend.example.test/rest/v1/rpc/my_game_stats
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NCIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_game":"Arena"}
---
POST https://backend.example.test/rest/v1/rpc/my_game_stats
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NCIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_game":"Arena\n2"}
---
POST https://backend.example.test/rest/v1/rpc/recent_players
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NCIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_limit":10}
---
POST https://backend.example.test/rest/v1/rpc/recent_players
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NCIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_limit":-3}
---
)GOLD"},
    {"mq_join_refused", R"GOLD(start=1
=> state=failed error=[Matchmaking isn't set up on the server yet (docs/sql/matchmaking.sql).] match=[] server=[] name=[] room=[] host=0 players=0
start=1
=> state=failed error=[Only your party leader can start matchmaking.] match=[] server=[] name=[] room=[] host=0 players=0
start=1
=> state=failed error=[Couldn't reach the matchmaking server.] match=[] server=[] name=[] room=[] host=0 players=0
start=1
=> state=failed error=[The matchmaking server sent an odd answer.] match=[] server=[] name=[] room=[] host=0 players=0
POST https://backend.example.test/rest/v1/rpc/queue_join
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_game":"Arena \"Game\"","p_mode":"duel","p_region":"eu","p_players":4,"p_min_players":2,"p_party":false}
---
POST https://backend.example.test/rest/v1/rpc/queue_join
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_game":"Arena \"Game\"","p_mode":"duel","p_region":"eu","p_players":4,"p_min_players":2,"p_party":false}
---
POST https://backend.example.test/rest/v1/rpc/queue_join
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_game":"Arena \"Game\"","p_mode":"duel","p_region":"eu","p_players":4,"p_min_players":2,"p_party":false}
---
POST https://backend.example.test/rest/v1/rpc/queue_join
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_game":"Arena \"Game\"","p_mode":"duel","p_region":"eu","p_players":4,"p_min_players":2,"p_party":false}
---
)GOLD"},
    {"mq_search_then_gone", R"GOLD(start=1
=> state=failed error=[The search ended (were you signed in somewhere else?).] match=[] server=[] name=[] room=[] host=0 players=0
POST https://backend.example.test/rest/v1/rpc/queue_join
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_game":"Arena \"Game\"","p_mode":"duel","p_region":"eu","p_players":4,"p_min_players":2,"p_party":false}
---
POST https://backend.example.test/rest/v1/rpc/queue_poll
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_ticket":"7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7"}
---
POST https://backend.example.test/rest/v1/rpc/queue_poll
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_ticket":"7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7"}
---
POST https://backend.example.test/rest/v1/rpc/queue_poll
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_ticket":"7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7"}
---
POST https://backend.example.test/rest/v1/rpc/queue_leave
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_ticket":"7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7"}
---
)GOLD"},
    {"mq_matched_server", R"GOLD(start=1
=> state=found error=[] match=[as-1] server=[203.0.113.9:7777] name=[EU 1] room=[match-1a2b] host=0 players=4
POST https://backend.example.test/rest/v1/rpc/queue_join
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_game":"Arena \"Game\"","p_mode":"duel","p_region":"eu","p_players":4,"p_min_players":2,"p_party":false}
---
POST https://backend.example.test/rest/v1/rpc/queue_poll
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_ticket":"7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7"}
---
POST https://backend.example.test/rest/v1/rpc/queue_leave
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_ticket":"7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7"}
---
)GOLD"},
    {"mq_host_posts_link", R"GOLD(start=1
connect=1 port=45293
=> state=found error=[] match=[as-2] server=[] name=[] room=[] host=1 players=2
POST https://backend.example.test/rest/v1/rpc/queue_join
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_game":"Arena \"Game\"","p_mode":"duel","p_region":"eu","p_players":4,"p_min_players":2,"p_party":false}
---
POST https://backend.example.test/rest/v1/rpc/queue_poll
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_ticket":"7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7"}
---
POST https://backend.example.test/rest/v1/rpc/queue_host_ready
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_assignment":"as-2","p_link":"okay://join?game=Arena%20%22Game%22&addr=127.0.0.1:45293&name=Match"}
---
POST https://backend.example.test/rest/v1/rpc/queue_leave
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_ticket":"7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7"}
---
)GOLD"},
};
