// Golden texts for tests/test_okay_backend_golden_community2.cpp, recorded from Messages.hpp, Servers.hpp and Privacy.hpp (the
// text form of ProfilesGet) at commit 43b419381, BEFORE they moved onto okay::backend::Database (round 6); recorded twice with the
// same result. Never edit by hand: a change here means the programs send something new. Re-record only from the old code:
// OKAY_GOLDEN_PRINT=1 ./test_okay_backend_golden_community2 > this file (then put back these header lines and the two
// duplicate-guard marker lines).
// Recorded with OKAY_GOLDEN_PRINT=1 (tests/test_okay_backend_golden_community2.cpp).
// dup-allow-begin: recorded request texts; the same header lines repeat in every request, as in the round 2 to 5 golden texts
static const char* const kGolden[][2] = {
    {"messages_send", R"GOLD(=> 1 err=[] status=201 reached=1
=> 1 err=[] status=201 reached=1
=> 0 err=[This person only accepts messages from friends.] status=403 reached=1
=> 0 err=[No connection to the server.] status=0 reached=0
=> 00
=> 1 err=[] status=201 reached=1
POST https://backend.example.test/rest/v1/messages?on_conflict=sender,client_id
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: resolution=ignore-duplicates
Content-Type: application/json

{"recipient":"u-2","body":"hi \"there\"\\ \nline\ttabcr end","client_id":"0123abcd-4567-89ab-cdef-0123456789ab"}
---
POST https://backend.example.test/rest/v1/messages?on_conflict=sender,client_id
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: resolution=ignore-duplicates
Content-Type: application/json

{"recipient":"u-2","body":"again","reply_to":5,"client_id":"11111111-2222-3333-4444-555555555555"}
---
POST https://backend.example.test/rest/v1/messages
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"recipient":"u-2","body":"again","reply_to":5}
---
POST https://backend.example.test/rest/v1/messages
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"recipient":"u-2","body":"third"}
---
POST https://backend.example.test/rest/v1/messages
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"recipient":"u 3&x","body":"no answer"}
---
POST https://backend.example.test/rest/v1/messages
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"recipient":"u-2","body":"plain"}
---
)GOLD"},
    {"messages_send_group", R"GOLD(=> 1 err=[] status=201
=> 0 err=[] status=500
=> 1 err=[] status=201
=> 1 err=[] status=201
=> 00
POST https://backend.example.test/rest/v1/group_messages?on_conflict=sender,client_id
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: resolution=ignore-duplicates
Content-Type: application/json

{"convo_id":9,"body":"hello \"all\"","sender_name":"al \"x\"","reply_to":3,"client_id":"0123abcd-4567-89ab-cdef-0123456789ab"}
---
POST https://backend.example.test/rest/v1/group_messages
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"convo_id":9,"body":"x","sender_name":"al \"x\""}
---
POST https://backend.example.test/rest/v1/group_messages?on_conflict=sender,client_id
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: resolution=ignore-duplicates
Content-Type: application/json

{"convo_id":9,"body":"y","sender_name":"al \"x\"","client_id":"11111111-2222-3333-4444-555555555555"}
---
POST https://backend.example.test/rest/v1/group_messages
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"convo_id":9,"body":"y","sender_name":"al \"x\""}
---
POST https://backend.example.test/rest/v1/group_messages
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"convo_id":9,"body":"z","sender_name":"al \"x\""}
---
)GOLD"},
    {"messages_pages", R"GOLD(  dm 1 [b1] 0 mine=0 edited=0 reply=0
  dm 2 [b "2"] 1700000000 mine=1 edited=0 reply=0
  dm 3 [b3] 1700000000 mine=0 edited=1 reply=2
  more=0
  dm 2 [b "2"] 1700000000 mine=1 edited=0 reply=0
  dm 3 [b3] 1700000000 mine=1 edited=1 reply=2
  more=1
  more=0
  more=0
  more=0
  dm 3 [b3] 1700000000 mine=1 edited=1 reply=2
  dm 2 [b "2"] 1700000000 mine=1 edited=0 reply=0
  dm 1 [b1] 0 mine=1 edited=0 reply=0
  row 9 u-2 u1
  row 8 u1 u-3
  rows=0
GET https://backend.example.test/rest/v1/messages?select=id,sender,body,created_at,edited,reply_to&or=(sender.eq.u-2,recipient.eq.u-2)&order=id.desc&limit=51
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/messages?select=id,sender,body,created_at,edited,reply_to&or=(sender.eq.a%26b%20%7Ex%2F%C3%A9,recipient.eq.a%26b%20%7Ex%2F%C3%A9)&id=lt.120&order=id.desc&limit=3
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/messages?select=id,sender,body,created_at,edited,reply_to&or=(sender.eq.u-2,recipient.eq.u-2)&order=id.desc&limit=2
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/messages?select=id,sender,body,created_at,edited,reply_to&or=(sender.eq.u-2,recipient.eq.u-2)&order=id.desc&limit=201
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/messages?select=id,sender,body,created_at,edited,reply_to&or=(sender.eq.u 2&x,recipient.eq.u 2&x)&order=created_at.asc
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/messages?select=id,sender,recipient&order=id.desc&limit=300
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/messages?select=id,sender,recipient&order=id.desc&limit=-1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
)GOLD"},
    {"messages_unread_and_conversations", R"GOLD(=> 2 newest=12
=> 2 newest=12
  in 10 u-3 [older]
  in 12 u-2 [newest]
=> 0 newest=5
  convo u-2 [newest] 1700000000 12 mine=0
  convo u-3 [older] 0 10 mine=0
  me=u1
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.al%20ex%2B%C3%A9~
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/messages?select=id,recipient&order=id.desc&limit=60
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/messages?select=id,recipient,sender,body&order=id.desc&limit=60
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/messages?select=id,recipient&order=id.desc&limit=60
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/messages?select=id,sender,recipient,body,created_at&order=id.desc&limit=300
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
)GOLD"},
    {"messages_my_id_missing", R"GOLD(=> 0 5
=> 0
=> {}
=> 0
=> 0
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.ghost
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.ghost
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.ghost
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.ghost
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.ghost
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.ghost
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
)GOLD"},
    {"messages_calls_and_invites", R"GOLD(  signal 21 from=[u-2] [[Call] ABC key] 1700000000
  signal 20 from=[u-3] [[CallEnd] ABC 30] 0
  signal 31 from=[u-2] [[Invite] Arena okay://join/1] 0
  signal 30 from=[u-4] [[Party] p-1] 0
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.alex
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/messages?select=id,sender,body,created_at&recipient=eq.u1&body=like.%5BCall*&created_at=gte.<NOW-90>&order=id.desc&limit=10
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/messages?select=id,sender,body,created_at&recipient=eq.u1&or=(body.like.%5BInvite%5D*,body.like.%5BParty%5D*)&id=gt.7&created_at=gte.<NOW-900>&order=id.desc&limit=10
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/messages?select=id,sender,body,created_at&recipient=eq.u1&body=like.%5BCall*&created_at=gte.<NOW-30>&order=id.desc&limit=10
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/messages?select=id,sender,body,created_at&recipient=eq.u1&or=(body.like.%5BInvite%5D*,body.like.%5BParty%5D*)&id=gt.-3&created_at=gte.<NOW-900>&order=id.desc&limit=10
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
)GOLD"},
    {"messages_edit_delete_reactions", R"GOLD(=> 100
=> 100
=> 110
=> {7=2|8=1} {7=1}
=> 1100
=> {7:fire=2;heart=1;} {7:fire;|9:a b;}
=> {} {}
DELETE https://backend.example.test/rest/v1/messages?id=eq.7
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
PATCH https://backend.example.test/rest/v1/messages?id=eq.7
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"body":"new \"b\"\n","edited":true}
---
POST https://backend.example.test/rest/v1/message_reactions
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: resolution=ignore-duplicates
Content-Type: application/json

{"scope":"dm","target_id":7,"emoji":"heart"}
---
DELETE https://backend.example.test/rest/v1/message_reactions?scope=eq.dm&target_id=eq.7&emoji=eq.heart
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/message_reactions?select=target_id&scope=eq.dm&emoji=eq.heart
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.alex
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/message_reactions?select=target_id&scope=eq.dm&emoji=eq.heart&reactor=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
POST https://backend.example.test/rest/v1/message_reactions
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: resolution=ignore-duplicates
Content-Type: application/json

{"scope":"dm","target_id":7,"emoji":"fire"}
---
DELETE https://backend.example.test/rest/v1/message_reactions?scope=eq.dm&target_id=eq.7&emoji=eq.a&b c
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
POST https://backend.example.test/rest/v1/message_reactions
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: resolution=ignore-duplicates
Content-Type: application/json

{"scope":"dm","target_id":7,"emoji":"x\"y"}
---
GET https://backend.example.test/rest/v1/message_reactions?select=target_id,emoji&scope=eq.dm
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/message_reactions?select=target_id,emoji&scope=eq.dm&reactor=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/message_reactions?select=target_id,emoji&scope=eq.dm
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/message_reactions?select=target_id,emoji&scope=eq.dm&reactor=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
)GOLD"},
    {"messages_typing_and_reads", R"GOLD(=> 1 yes
=> 0 missing
=> 0
=> 100 yes
=> 1 yes
=> 00
=> 0 missing
  peer read=40 viewing=1700000000 seen=1700000000 ok=1
  peer read=0 viewing=0 seen=4070908800 ok=1
  peer read=0 viewing=0 seen=0 ok=0
  peer read=0 viewing=0 seen=0 ok=0
=> 38 0 0
POST https://backend.example.test/rest/v1/typing_state?on_conflict=sender
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: resolution=merge-duplicates
Content-Type: application/json

{"peer":"u-2","at":"<NOW>"}
---
POST https://backend.example.test/rest/v1/typing_state?on_conflict=sender
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: resolution=merge-duplicates
Content-Type: application/json

{"peer":"u \"2\"","at":"<NOW>"}
---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.alex
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/typing_state?select=at&sender=eq.u 2&x&peer=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/typing_state?select=at&sender=eq.u-2&peer=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
POST https://backend.example.test/rest/v1/message_reads?on_conflict=reader,peer
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: resolution=merge-duplicates
Content-Type: application/json

{"peer":"u-2","last_read_id":42,"updated_at":"<NOW>"}
---
POST https://backend.example.test/rest/v1/message_reads?on_conflict=reader,peer
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: resolution=merge-duplicates
Content-Type: application/json

{"peer":"u-3","last_read_id":43,"updated_at":"<NOW>"}
---
GET https://backend.example.test/rest/v1/message_reads?select=last_read_id,updated_at&reader=eq.u-2&peer=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles_visible?select=last_seen&id=eq.u-2
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/message_reads?select=last_read_id,updated_at&reader=eq.u 3&x&peer=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles_visible?select=last_seen&id=eq.u 3&x
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=last_seen&id=eq.u 3&x
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/message_reads?select=last_read_id,updated_at&reader=eq.u-2&peer=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/message_reads?select=last_read_id&reader=eq.u-2&peer=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/message_reads?select=last_read_id&reader=eq.u-2&peer=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
)GOLD"},
    {"messages_groups", R"GOLD(=> 12 0
  group 12 [Team] u1 3 40 [hey] bo 1700000000 38
  member u1
  member u-2
  ids0=0
  info u1 user=[alex] name=[alex] avatar=[] role=[] read=5 seen=0
  info u-2 user=[bo] name=[Bo] avatar=[https://a.example.test/b.png] role=[] read=3 seen=1700000000
  info u-2 user=[bo] name=[bo] avatar=[] role=[admin] read=3 seen=0
  gm 40 u1 [alex] [hi] 1700000000 mine=1 edited=1 reply=39
  gm 41 u-2 [bo] [yo] 0 mine=0 edited=0 reply=0
=> 1010
=> 1010
=> 1110
=> 110
=> {40:heart=1;} {40:heart;}
POST https://backend.example.test/rest/v1/rpc/create_group
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_title":"Team \"A\"\n","p_members":["u-2","u \"3\""]}
---
POST https://backend.example.test/rest/v1/rpc/create_group
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_title":"","p_members":[]}
---
POST https://backend.example.test/rest/v1/rpc/my_groups
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{}
---
GET https://backend.example.test/rest/v1/group_members?convo_id=eq.12&select=member
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/group_members?convo_id=eq.12&select=member,last_read,role
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/group_members?convo_id=eq.12&select=member,last_read
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles_visible?select=id,username,display_name,avatar_url,last_seen&id=in.(u1,u-2)
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/group_members?convo_id=eq.13&select=member,last_read,role
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles_visible?select=id,username,display_name,avatar_url,last_seen&id=in.(u-2)
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=id,username,display_name,avatar_url,last_seen&id=in.(u-2)
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.alex
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/group_messages?convo_id=eq.12&select=id,sender,sender_name,body,created_at,edited,reply_to&order=created_at.asc
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
PATCH https://backend.example.test/rest/v1/group_messages?id=eq.40
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"body":"fixed \"t\"","edited":true}
---
DELETE https://backend.example.test/rest/v1/group_messages?id=eq.40
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
POST https://backend.example.test/rest/v1/rpc/add_group_member
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_convo":12,"p_member":"u-4"}
---
DELETE https://backend.example.test/rest/v1/group_members?convo_id=eq.12&member=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
PATCH https://backend.example.test/rest/v1/group_convos?id=eq.12
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"title":"New \"T\""}
---
PATCH https://backend.example.test/rest/v1/group_convos?id=eq.12
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"title":""}
---
PATCH https://backend.example.test/rest/v1/group_members?convo_id=eq.12&member=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"last_read":41}
---
POST https://backend.example.test/rest/v1/message_reactions
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: resolution=ignore-duplicates
Content-Type: application/json

{"scope":"group","target_id":40,"emoji":"heart"}
---
DELETE https://backend.example.test/rest/v1/message_reactions?scope=eq.group&target_id=eq.40&emoji=eq.x y&z
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/message_reactions?select=target_id,emoji&scope=eq.group
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/message_reactions?select=target_id,emoji&scope=eq.group&reactor=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
)GOLD"},
    {"messages_search_edits_pins", R"GOLD(  hit group 9 uid=[] group=12 [bo] [hello all] 1700000010 mine=0
  hit dm 5 uid=[u-2] group=0 [] [hello world] 1700000000 mine=1
  hit dm 4 uid=[u-3] group=0 [] [hello] 1699999990 mine=0
  --
  hit group 9 uid=[] group=12 [alex] [a] 1700000010 mine=1
  --
  --
=> 10
  version [v1] 1700000000
  version [v "2"] 0
  supported=1
  supported=0
  supported=1
  supported=1
=> 1110
  pin 5 1700000000 u1
  supported=1
  pins=0 supported=0
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.alex
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/messages?select=id,sender,recipient,body,created_at&body=ilike.*hello%20w%C3%B6rld~%26%3Dx*&order=id.desc&limit=40
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/group_messages?select=id,convo_id,sender,sender_name,body,created_at&body=ilike.*hello%20w%C3%B6rld~%26%3Dx*&order=id.desc&limit=40
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/messages?select=id,sender,recipient,body,created_at&body=ilike.*a%20b*&order=id.desc&limit=1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/group_messages?select=id,convo_id,sender,sender_name,body,created_at&body=ilike.*a%20b*&order=id.desc&limit=1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
POST https://backend.example.test/rest/v1/message_edits
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"scope":"dm","target_id":5,"old_body":"old \"t\"\nx"}
---
GET https://backend.example.test/rest/v1/message_edits?select=old_body,edited_at&scope=eq.dm&target_id=eq.5&order=id.asc
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/message_edits?select=old_body,edited_at&scope=eq.gr oup&x&target_id=eq.5&order=id.asc
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/message_edits?select=old_body,edited_at&scope=eq.group&target_id=eq.6&order=id.asc
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
POST https://backend.example.test/rest/v1/message_pins
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: resolution=ignore-duplicates
Content-Type: application/json

{"scope":"dm","target_id":5}
---
DELETE https://backend.example.test/rest/v1/message_pins?scope=eq.dm&target_id=eq.5
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
DELETE https://backend.example.test/rest/v1/message_pins?scope=eq.a&b&target_id=eq.6
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/message_pins?select=target_id,pinned_by,created_at&scope=eq.dm&order=created_at.desc&limit=500
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/message_pins?select=target_id,pinned_by,created_at&scope=eq.group&order=created_at.desc&limit=500
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
)GOLD"},
    {"messages_signed_out", R"GOLD(0000000000000000000
000000100000000000000000101[] []
)GOLD"},
    {"servers_list_create_join", R"GOLD(  server 4 [Den "1"] AB12 u1
  server 5 [Two]  u-2
  me=u1 again=u1 available=1
=> 1 4 [Den] AB12 err=[]
=> 0 0 []  err=[dup name]
=> 0 0 []  err=[Couldn't create server.]
=> 0 0 []  err=[Enter a server name.]
=> 1 err=[]
=> 0 err=[No server with code 'ZZ'.]
=> 0 err=[bad code]
=> 0 err=[Join failed.]
  servers=0
GET https://backend.example.test/rest/v1/servers?select=id,name,invite_code,owner&order=created_at.asc
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.al%20ex%26~
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
POST https://backend.example.test/rest/v1/rpc/create_server
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_name":"Den \"1\"\n"}
---
POST https://backend.example.test/rest/v1/rpc/create_server
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_name":"x"}
---
POST https://backend.example.test/rest/v1/rpc/create_server
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_name":"y"}
---
POST https://backend.example.test/rest/v1/rpc/join_server
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_code":"AB12"}
---
POST https://backend.example.test/rest/v1/rpc/join_server
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_code":"ZZ"}
---
POST https://backend.example.test/rest/v1/rpc/join_server
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_code":"Q&1 \""}
---
POST https://backend.example.test/rest/v1/rpc/join_server
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_code":"e"}
---
GET https://backend.example.test/rest/v1/servers?select=id,name,invite_code,owner&order=created_at.asc
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
)GOLD"},
    {"servers_channels_members", R"GOLD(  channel 9 [general]
  channel 10 [voice "x"]
  channels0=0
  member u1 [alex] [] owner online=0 seen=0
  member u-2 [Bo] [https://a.example.test/b.png] member online=1 seen=4070908800
  member u 3&x [cy] [] admin online=0 seen=1700000000
  --
  member u-2 [] [] member online=0 seen=0
  --
  --
  --
=> 100
  cm 3 [bo] [hi] 1700000000 edited=1
  cm 4 [old] [x] 0 edited=0
  msgs=00
=> 100
=> 100
GET https://backend.example.test/rest/v1/channels?server_id=eq.4&select=id,name&order=position.asc,id.asc
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/server_members?server_id=eq.4&select=member,role
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles_visible?select=id,username,display_name,avatar_url,last_seen&id=in.(u-2,u1,u 3&x)
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=id,username,display_name,avatar_url,last_seen&id=in.(u-2,u1,u 3&x)
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/server_members?server_id=eq.5&select=member,role
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=id,username,display_name,avatar_url,last_seen&id=in.(u-2)
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/server_members?server_id=eq.6&select=member,role
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
POST https://backend.example.test/rest/v1/channels
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"server_id":4,"name":"general \"x\"\n"}
---
GET https://backend.example.test/rest/v1/channel_messages?channel_id=eq.9&select=id,author_name,body,created_at,edited&order=created_at.asc&limit=60
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/channel_messages?channel_id=eq.9&select=id,author_name,body,created_at,edited&order=created_at.asc&limit=-5
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
POST https://backend.example.test/rest/v1/channel_messages
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"channel_id":9,"body":"hello\n\"x\"","author_name":"alex"}
---
PATCH https://backend.example.test/rest/v1/channel_messages?id=eq.3
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"body":"edited \"e\"","edited":true}
---
)GOLD"},
    {"servers_likes_and_owner", R"GOLD(=> 110
=> {3=2|4=1} {3=1}
=> 10
=> 1010
=> 1010
POST https://backend.example.test/rest/v1/message_reactions
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: resolution=ignore-duplicates
Content-Type: application/json

{"scope":"channel","target_id":3,"emoji":"heart"}
---
DELETE https://backend.example.test/rest/v1/message_reactions?scope=eq.channel&target_id=eq.3&emoji=eq.heart
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/message_reactions?select=target_id&scope=eq.channel&emoji=eq.heart
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.alex
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/message_reactions?select=target_id&scope=eq.channel&emoji=eq.heart&reactor=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
DELETE https://backend.example.test/rest/v1/server_members?server_id=eq.4&member=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
PATCH https://backend.example.test/rest/v1/channels?id=eq.9
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"name":"renamed \"c\""}
---
DELETE https://backend.example.test/rest/v1/channels?id=eq.9
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
PATCH https://backend.example.test/rest/v1/servers?id=eq.4
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"name":"S"}
---
DELETE https://backend.example.test/rest/v1/servers?id=eq.4
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
)GOLD"},
    {"servers_signed_out", R"GOLD(00[]0000000000000000 [Sign in first.] [Sign in first.]
)GOLD"},
};
// dup-allow-end
