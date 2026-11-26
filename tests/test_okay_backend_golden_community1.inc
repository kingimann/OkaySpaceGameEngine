// Golden texts for tests/test_okay_backend_golden_community1.cpp, recorded from Profiles.hpp, Friends.hpp, BlockList.hpp, Privacy.hpp
// (ProfilesGet), Invites.hpp and Parties.hpp at commit b2f943ed7, BEFORE they moved onto okay::backend::Database (round 5); recorded
// twice with the same result. Never edit by hand: a change here means the programs send something new. Re-record only from the old
// code: OKAY_GOLDEN_PRINT=1 ./test_okay_backend_golden_community1 > this file (then put back these header lines and the two
// duplicate-guard marker lines).
// Recorded with OKAY_GOLDEN_PRINT=1 (tests/test_okay_backend_golden_community1.cpp).
// dup-allow-begin: recorded request texts; the same header lines repeat in every request, as in the round 2 to 4 golden texts
static const char* const kGolden[][2] = {
    {"profiles_load_by_name_full", R"GOLD(  found=1 reached=1 failed=0 showcase=1 ach=1 extras=1 follow=1
  id=[u-2] user=[bo] display=[Bo "B"] avatar=[https://a.example.test/b.png] bio=[hi
there] accent=4 status=[busy] fav=[Arena] loc=[Mars] playing=[Arena] code=[R-1] seen=4070908800 since=1700000000 online=1
  showAch=[a1|a2] showGames=[7|9] banner=2 verified=1 socials={x=@bo|web=https://bo.example.test} badges=[staff|beta]
  play arena [Arena] 3600 1700000000
  ach a1 [First] 1700000000 pts=100
  ach a2 [a2] 0 pts=10
  game 7 [Game7] by [bo] game https://w.example.test/7 dl=12
  friends=3 followers=5 following=2 iFollow=1 followsMe=1 isMe=0 row=41 status=[pending] incoming=1
  act [bo] online [] 1700000000
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.b%20o%26x%3D1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles_visible?select=id,username,display_name,avatar_url,bio,accent,status,favorite_game,location,playing,session_code,last_seen,created_at,showcase_achievements,showcase_games,banner_accent,social_links,verified&id=eq.u-2
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/user_achievements?select=ach_id,label,unlocked_at,points&owner=eq.u-2&order=unlocked_at.desc
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/workshop_items?select=id,author_name,title,description,kind,storage_path,downloads&author=eq.u-2&order=downloads.desc
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profile_badges?select=badge&owner=eq.u-2&order=awarded_at.asc
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/user_playtime?select=game_key,title,seconds,last_played&owner=eq.u-2&order=seconds.desc&limit=30
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
POST https://backend.example.test/rest/v1/rpc/profile_friend_count
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_uid":"u-2"}
---
POST https://backend.example.test/rest/v1/rpc/follow_counts
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_uid":"u-2"}
---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.alex
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/follows?select=follower,followee&or=(and(follower.eq.u1,followee.eq.u-2),and(follower.eq.u-2,followee.eq.u1))
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/friendships?select=id,status,requester,addressee&or=(requester.eq.u-2,addressee.eq.u-2)
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/activity?select=actor_name,kind,detail,created_at&actor=eq.u-2&order=created_at.desc&limit=8
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
)GOLD"},
    {"profiles_load_fallbacks", R"GOLD(  found=1 reached=1 failed=0 showcase=0 ach=0 extras=0 follow=0
  id=[u-3] user=[cy] display=[cy] avatar=[] bio=[] accent=0 status=[] fav=[] loc=[] playing=[] code=[] seen=946684800 since=0 online=0
  showAch=[] showGames=[] banner=-1 verified=0 socials={} badges=[]
  friends=-1 followers=-1 following=-1 iFollow=0 followsMe=0 isMe=0 row=9 status=[accepted] incoming=0
GET https://backend.example.test/rest/v1/profiles_visible?select=id,username,display_name,avatar_url,bio,accent,status,favorite_game,location,playing,session_code,last_seen,created_at,showcase_achievements,showcase_games,banner_accent,social_links,verified&id=eq.u-3%2F..%2Fx
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=id,username,display_name,avatar_url,bio,accent,status,favorite_game,location,playing,session_code,last_seen,created_at,showcase_achievements,showcase_games,banner_accent,social_links,verified&id=eq.u-3%2F..%2Fx
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=id,username,display_name,avatar_url,bio,accent,status,favorite_game,location,playing,session_code,last_seen,created_at,showcase_achievements,showcase_games,banner_accent&id=eq.u-3%2F..%2Fx
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=id,username,display_name,avatar_url,bio,accent,status,favorite_game,location,playing,session_code,last_seen,created_at&id=eq.u-3%2F..%2Fx
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/user_achievements?select=ach_id,label,unlocked_at,points&owner=eq.u-3%2F..%2Fx&order=unlocked_at.desc
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/user_achievements?select=ach_id,label,unlocked_at&owner=eq.u-3%2F..%2Fx&order=unlocked_at.desc
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/workshop_items?select=id,author_name,title,description,kind,storage_path,downloads&author=eq.u-3%2F..%2Fx&order=downloads.desc
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
POST https://backend.example.test/rest/v1/rpc/profile_friend_count
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_uid":"u-3/../x"}
---
POST https://backend.example.test/rest/v1/rpc/follow_counts
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_uid":"u-3/../x"}
---
GET https://backend.example.test/rest/v1/friendships?select=id,status,requester,addressee&or=(requester.eq.u-3%2F..%2Fx,addressee.eq.u-3%2F..%2Fx)
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/activity?select=actor_name,kind,detail,created_at&actor=eq.u-3%2F..%2Fx&order=created_at.desc&limit=8
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
)GOLD"},
    {"profiles_load_me_and_misses", R"GOLD(  found=1 reached=1 failed=0 showcase=1 ach=1 extras=1 follow=1
  id=[u1] user=[alex] display=[alex] avatar=[] bio=[] accent=0 status=[] fav=[] loc=[] playing=[] code=[] seen=0 since=0 online=0
  showAch=[] showGames=[] banner=-1 verified=0 socials={} badges=[]
  friends=2 followers=-1 following=-1 iFollow=0 followsMe=0 isMe=1 row=0 status=[] incoming=0
  found=0 reached=1 failed=0 showcase=0 ach=0 extras=0 follow=0
  id=[] user=[] display=[] avatar=[] bio=[] accent=0 status=[] fav=[] loc=[] playing=[] code=[] seen=0 since=0 online=0
  showAch=[] showGames=[] banner=-1 verified=0 socials={} badges=[]
  friends=-1 followers=-1 following=-1 iFollow=0 followsMe=0 isMe=0 row=0 status=[] incoming=0
  found=0 reached=1 failed=1 showcase=0 ach=0 extras=0 follow=0
  id=[] user=[] display=[] avatar=[] bio=[] accent=0 status=[] fav=[] loc=[] playing=[] code=[] seen=0 since=0 online=0
  showAch=[] showGames=[] banner=-1 verified=0 socials={} badges=[]
  friends=-1 followers=-1 following=-1 iFollow=0 followsMe=0 isMe=0 row=0 status=[] incoming=0
  found=0 reached=0 failed=0 showcase=0 ach=0 extras=0 follow=0
  id=[] user=[] display=[] avatar=[] bio=[] accent=0 status=[] fav=[] loc=[] playing=[] code=[] seen=0 since=0 online=0
  showAch=[] showGames=[] banner=-1 verified=0 socials={} badges=[]
  friends=-1 followers=-1 following=-1 iFollow=0 followsMe=0 isMe=0 row=0 status=[] incoming=0
  found=0 reached=1 failed=0 showcase=1 ach=0 extras=1 follow=0
  id=[] user=[] display=[] avatar=[] bio=[] accent=0 status=[] fav=[] loc=[] playing=[] code=[] seen=0 since=0 online=0
  showAch=[] showGames=[] banner=-1 verified=0 socials={} badges=[]
  friends=-1 followers=-1 following=-1 iFollow=0 followsMe=0 isMe=0 row=0 status=[] incoming=0
  found=0 reached=0 failed=0 showcase=0 ach=0 extras=0 follow=0
  id=[] user=[] display=[] avatar=[] bio=[] accent=0 status=[] fav=[] loc=[] playing=[] code=[] seen=0 since=0 online=0
  showAch=[] showGames=[] banner=-1 verified=0 socials={} badges=[]
  friends=-1 followers=-1 following=-1 iFollow=0 followsMe=0 isMe=0 row=0 status=[] incoming=0
GET https://backend.example.test/rest/v1/profiles_visible?select=id,username,display_name,avatar_url,bio,accent,status,favorite_game,location,playing,session_code,last_seen,created_at,showcase_achievements,showcase_games,banner_accent,social_links,verified&id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/user_achievements?select=ach_id,label,unlocked_at,points&owner=eq.u1&order=unlocked_at.desc
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/workshop_items?select=id,author_name,title,description,kind,storage_path,downloads&author=eq.u1&order=downloads.desc
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profile_badges?select=badge&owner=eq.u1&order=awarded_at.asc
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/user_playtime?select=game_key,title,seconds,last_played&owner=eq.u1&order=seconds.desc&limit=30
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
POST https://backend.example.test/rest/v1/rpc/profile_friend_count
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_uid":"u1"}
---
POST https://backend.example.test/rest/v1/rpc/follow_counts
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_uid":"u1"}
---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.alex
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/friendships?select=id,status,requester,addressee&or=(requester.eq.u1,addressee.eq.u1)
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/activity?select=actor_name,kind,detail,created_at&actor=eq.u1&order=created_at.desc&limit=8
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.nobody
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.err
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.gone
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles_visible?select=id,username,display_name,avatar_url,bio,accent,status,favorite_game,location,playing,session_code,last_seen,created_at,showcase_achievements,showcase_games,banner_accent,social_links,verified&id=eq.u-404
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
)GOLD"},
    {"profiles_writes", R"GOLD(=> 1
=> 1
=> 1
=> 0
=> {x=@me|gh=me} supported=1
=> {} supported=0
=> 1
=> 1
=> 1
=> 0
=> 1
=> 1
=> 1
=> 1
=> 0
=> 1
=> 0
=> 1
=> 0
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.alex
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
PATCH https://backend.example.test/rest/v1/profiles?id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"showcase_achievements":["a1","a\"2","a3","a4","a5","a6"],"showcase_games":[7,8,9,10],"banner_accent":3}
---
POST https://backend.example.test/rest/v1/user_achievements?on_conflict=owner,ach_id
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: resolution=merge-duplicates
Content-Type: application/json

[{"ach_id":"first","label":"First \"one\"","unlocked_at":"2023-11-14T22:13:20Z","points":25},{"ach_id":"b\\2","label":"Two\nlines","points":10}]
---
POST https://backend.example.test/rest/v1/user_achievements?on_conflict=owner,ach_id
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: resolution=merge-duplicates
Content-Type: application/json

[{"ach_id":"first","label":"First \"one\"","unlocked_at":"2023-11-14T22:13:20Z"},{"ach_id":"b\\2","label":"Two\nlines"}]
---
POST https://backend.example.test/rest/v1/user_achievements?on_conflict=owner,ach_id
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: resolution=merge-duplicates
Content-Type: application/json

[{"ach_id":"b\\2","label":"Two\nlines","points":10}]
---
GET https://backend.example.test/rest/v1/profiles?select=social_links&id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=social_links&id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
PATCH https://backend.example.test/rest/v1/profiles?id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"social_links":{"x":"@me","web":"wwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwwww"}}
---
POST https://backend.example.test/rest/v1/user_playtime?on_conflict=owner,game_key
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: resolution=merge-duplicates
Content-Type: application/json

[{"game_key":"arena","title":"Arena \"1\"","seconds":3600,"last_played":"2023-11-14T22:13:20Z"},{"game_key":"editor","title":"Editor","seconds":0}]
---
POST https://backend.example.test/rest/v1/follows
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"followee":"u-2"}
---
POST https://backend.example.test/rest/v1/follows
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"followee":"u \"3\""}
---
DELETE https://backend.example.test/rest/v1/follows?follower=eq.u1&followee=eq.u-2%26x
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
DELETE https://backend.example.test/rest/v1/follows?follower=eq.u-4&followee=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
PATCH https://backend.example.test/rest/v1/profiles?id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"followers_see_activity":true}
---
POST https://backend.example.test/rest/v1/friendships
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"addressee":"u-5"}
---
PATCH https://backend.example.test/rest/v1/friendships?id=eq.12
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"status":"accepted"}
---
DELETE https://backend.example.test/rest/v1/friendships?id=eq.13
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
)GOLD"},
    {"profiles_follow_lists", R"GOLD(  follow id=[u-2] user=[bo] display=[Bo] avatar=[] seen=0
  follow id=[u-3] user=[cy] display=[cy] avatar=[a] seen=1700000000
supported=1
=> 0 supported=1
=> 0 supported=0
=> 1 err=[]
=> 0 err=[That's you!]
=> 0 err=[No player with that username.]
=> 0 err=[Couldn't follow them (already following?).]
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.al%20ex%2B%C3%A9
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/follows?select=follower,followee&followee=eq.u1&order=created_at.desc&limit=200
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles_visible?select=id,username,display_name,avatar_url,last_seen&id=in.(u-2,u-3)
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/follows?select=follower,followee&follower=eq.u-9&order=created_at.desc&limit=200
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/follows?select=follower,followee&followee=eq.u-9&order=created_at.desc&limit=200
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.dee
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
POST https://backend.example.test/rest/v1/follows
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"followee":"u-7"}
---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.al%20ex%2B%C3%A9
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.nobody
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.eve
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
POST https://backend.example.test/rest/v1/follows
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"followee":"u-8"}
---
)GOLD"},
    {"profiles_signed_out", R"GOLD(=> 00000000000
)GOLD"},
    {"friends_profile", R"GOLD(=> code=[AB12CD]
=> [Alex A]
=> 1
=> [u]
=> 1
=> [b"q]
=> 1
  friend id=[u1] user=[alex] status=[] in=0 online=1 seen=4070908800 playing=[Arena] code=[R] display=[Alex] avatar=[https://a.example.test/a.png] bio=[b] accent=3 status2=[s] fav=[g] loc=[l] row=0
=> 7
=> 1
=> [st]
=> 1
=> [fg]
=> 1
=> [lo]
=> 1
POST https://backend.example.test/rest/v1/profiles?on_conflict=id
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: resolution=merge-duplicates
Content-Type: application/json

{"username":"alex"}
---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.alex
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles_visible?select=username,friend_code,playing,session_code,display_name,avatar_url,bio,accent,status,favorite_game,location,last_seen&id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
PATCH https://backend.example.test/rest/v1/profiles?id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"friend_code":"<CODE>"}
---
GET https://backend.example.test/rest/v1/profiles_visible?select=username,friend_code,playing,session_code,display_name,avatar_url,bio,accent,status,favorite_game,location,last_seen&id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=username,friend_code,playing,session_code,display_name,avatar_url,bio,accent,status,favorite_game,location,last_seen&id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=username,friend_code,playing,session_code,display_name,avatar_url,bio,accent,status,favorite_game,location,last_seen&id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
PATCH https://backend.example.test/rest/v1/profiles?id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"display_name":"Alex \"A\""}
---
GET https://backend.example.test/rest/v1/profiles?select=username,friend_code,playing,session_code,display_name,avatar_url,bio,accent,status,favorite_game,location,last_seen&id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
PATCH https://backend.example.test/rest/v1/profiles?id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"avatar_url":"https://a.example.test/a b.png"}
---
GET https://backend.example.test/rest/v1/profiles?select=username,friend_code,playing,session_code,display_name,avatar_url,bio,accent,status,favorite_game,location,last_seen&id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
PATCH https://backend.example.test/rest/v1/profiles?id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"bio":"line1\nline2\t\u0001"}
---
GET https://backend.example.test/rest/v1/profiles?select=username,friend_code,playing,session_code,display_name,avatar_url,bio,accent,status,favorite_game,location,last_seen&id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=username,friend_code,playing,session_code,display_name,avatar_url,bio,accent,status,favorite_game,location,last_seen&id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
PATCH https://backend.example.test/rest/v1/profiles?id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"accent":-2}
---
GET https://backend.example.test/rest/v1/profiles?select=username,friend_code,playing,session_code,display_name,avatar_url,bio,accent,status,favorite_game,location,last_seen&id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
PATCH https://backend.example.test/rest/v1/profiles?id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"status":"away"}
---
GET https://backend.example.test/rest/v1/profiles?select=username,friend_code,playing,session_code,display_name,avatar_url,bio,accent,status,favorite_game,location,last_seen&id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
PATCH https://backend.example.test/rest/v1/profiles?id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"favorite_game":"Arena"}
---
GET https://backend.example.test/rest/v1/profiles?select=username,friend_code,playing,session_code,display_name,avatar_url,bio,accent,status,favorite_game,location,last_seen&id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
PATCH https://backend.example.test/rest/v1/profiles?id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"location":"Moon"}
---
)GOLD"},
    {"friends_requests_list", R"GOLD(=> 1 err=[]
=> 0 err=[No player named 'nobody'.]
=> 0 err=[{"message":"blocked_by_user"}]
=> 0 err=[Request failed.]
=> 1 err=[]
=> 1
=> 1
  friend id=[u-2] user=[bo] status=[accepted] in=0 online=0 seen=1700000000 playing=[] code=[] display=[bo] avatar=[] bio=[] accent=0 status2=[] fav=[] loc=[] row=5
  friend id=[u-3] user=[cy] status=[pending] in=1 online=0 seen=0 playing=[] code=[] display=[Cy] avatar=[] bio=[] accent=0 status2=[] fav=[] loc=[] row=6
=> pending 1
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.bo
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
POST https://backend.example.test/rest/v1/friendships
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"addressee":"u-2"}
---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.nobody
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.cy
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
POST https://backend.example.test/rest/v1/friendships
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"addressee":"u-3"}
---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.dee
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
POST https://backend.example.test/rest/v1/friendships
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"addressee":"u-4"}
---
GET https://backend.example.test/rest/v1/profiles?select=id&friend_code=eq.AB1%202CD
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
POST https://backend.example.test/rest/v1/friendships
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"addressee":"u-5"}
---
PATCH https://backend.example.test/rest/v1/friendships?id=eq.5
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"status":"accepted"}
---
DELETE https://backend.example.test/rest/v1/friendships?id=eq.-3
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/friendships?select=id,requester,addressee,status
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles_visible?select=username,friend_code,playing,session_code,display_name,avatar_url,bio,accent,status,favorite_game,location,last_seen&id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles_visible?select=username,friend_code,playing,session_code,display_name,avatar_url,bio,accent,status,favorite_game,location,last_seen&id=eq.u-2
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles_visible?select=username,friend_code,playing,session_code,display_name,avatar_url,bio,accent,status,favorite_game,location,last_seen&id=eq.u-3
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles_visible?select=username,friend_code,playing,session_code,display_name,avatar_url,bio,accent,status,favorite_game,location,last_seen&id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.alex
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/friendships?select=addressee,status&status=eq.pending
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
)GOLD"},
    {"friends_presence_feed", R"GOLD(  ev [bo] id=[u-2] playing [Arena] 1700000000
  ev [cy] id=[u-3] online [] 0
  ev [bo] id=[u-2] online [] 0
  ev [x] id=[]  [] 0
=> 0
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.alex
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
PATCH https://backend.example.test/rest/v1/profiles?id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"last_seen":"<NOW>","playing":"Playing \"Arena\"","session_code":"R-9"}
---
PATCH https://backend.example.test/rest/v1/profiles?id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"last_seen":"2000-01-01T00:00:00Z","playing":null,"session_code":null}
---
PATCH https://backend.example.test/rest/v1/profiles?id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"last_seen":"<NOW>","playing":"In the launcher","session_code":""}
---
PATCH https://backend.example.test/rest/v1/profiles?id=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"last_seen":"<NOW>","playing":"In the launcher","session_code":""}
---
POST https://backend.example.test/rest/v1/activity
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"kind":"achievement","detail":"First \"win\"","actor_name":"alex"}
---
GET https://backend.example.test/rest/v1/activity?select=actor,actor_name,kind,detail,created_at&actor=neq.u1&order=created_at.desc&limit=5
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=id,username&id=in.(u-2,u-3)
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/activity?select=actor,actor_name,kind,detail,created_at&actor=neq.u1&order=created_at.desc&limit=20
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
)GOLD"},
    {"friends_signed_out", R"GOLD(=> 000000[Sign in first.]
)GOLD"},
    {"privacy_profiles_get", R"GOLD(=> ok=1 reached=1 status=200 body=[[{"id":"a"}]] state=1
=> ok=0 reached=1 status=500 body=[{}] state=1
=> ok=1 reached=1 status=200 body=[[]] state=2
=> ok=1 reached=1 status=200 body=[[{"id":"c"}]] state=2
=> ok=0 reached=0 status=0 body=[] state=2
GET https://backend.example.test/rest/v1/profiles_visible?select=id&id=eq.a
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles_visible?select=id&id=eq.b
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles_visible?select=id&id=eq.c
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=id&id=eq.c
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=id&id=eq.d
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles?select=id&id=eq.e
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
)GOLD"},
    {"blocklist", R"GOLD(=> ok=1 missing=0 offline=0 msg=[]
  blocked [u-2] name=[bo]
  blocked [u-3] name=[-]
=> ok=1 missing=0 offline=0 msg=[]
=> ok=0 missing=0 offline=0 msg=[The server returned an error (HTTP 400).]
=> ok=1 missing=0 offline=0 msg=[]
=> ok=0 missing=0 offline=0 msg=[Your sign-in expired. Sign in again.]
=> ok=1 missing=0 offline=0 msg=[]
  found=1 {"dm_from":"friends","profile_visibility":"nobody","online_visibility":"everyone","friend_requests":"nobody","calls_from":"friends"}
=> ok=1 missing=0 offline=0 msg=[]
  found=0
=> ok=1 missing=0 offline=0 msg=[]
=> ok=0 missing=1 offline=0 msg=[Not set up on the server yet: the server owner must run docs/sql/blocks.sql once.]
=> ok=0 missing=1 offline=0 msg=[Not set up on the server yet: the server owner must run docs/sql/blocks.sql once.]
=> ok=0 missing=0 offline=1 msg=[No connection to the server.]
=> ok=0 missing=0 offline=0 msg=[The server refused this (permission denied).]
  support blocks=2 settings=2
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.al%20ex%2B%C3%A9
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/blocks?select=blocked,blocked_name&blocker=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
POST https://backend.example.test/rest/v1/blocks?on_conflict=blocker,blocked
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: resolution=ignore-duplicates
Content-Type: application/json

{"blocked":"u-4","blocked_name":"Dee \"D\""}
---
POST https://backend.example.test/rest/v1/blocks?on_conflict=blocker,blocked
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: resolution=ignore-duplicates
Content-Type: application/json

{"blocked":"u-5","blocked_name":""}
---
DELETE https://backend.example.test/rest/v1/blocks?blocked=eq.u-4
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
DELETE https://backend.example.test/rest/v1/blocks?blocked=eq.u 5&x
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/privacy_settings?select=dm_from,profile_visibility,online_visibility,friend_requests,calls_from
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/privacy_settings?select=dm_from,profile_visibility,online_visibility,friend_requests,calls_from
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
POST https://backend.example.test/rest/v1/privacy_settings?on_conflict=user_id
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: resolution=merge-duplicates
Content-Type: application/json

{"dm_from":"nobody","profile_visibility":"nobody","online_visibility":"everyone","friend_requests":"nobody","calls_from":"everyone"}
---
POST https://backend.example.test/rest/v1/privacy_settings?on_conflict=user_id
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: resolution=merge-duplicates
Content-Type: application/json

{"dm_from":"nobody","profile_visibility":"nobody","online_visibility":"everyone","friend_requests":"nobody","calls_from":"everyone"}
---
POST https://backend.example.test/rest/v1/blocks?on_conflict=blocker,blocked
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: resolution=ignore-duplicates
Content-Type: application/json

{"blocked":"u-6","blocked_name":"x"}
---
DELETE https://backend.example.test/rest/v1/blocks?blocked=eq.u-6
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/blocks?select=blocked,blocked_name&blocker=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
)GOLD"},
    {"blocklist_no_id_signed_out", R"GOLD(=> ok=0 missing=0 offline=1 msg=[Could not look up your account.]
=> ok=0 missing=0 offline=0 msg=[Sign in to keep your block list on your account.]
=> ok=0 missing=0 offline=0 msg=[Sign in to keep your block list on your account.]
=> ok=0 missing=0 offline=0 msg=[Sign in to keep your block list on your account.]
=> ok=0 missing=0 offline=0 msg=[Sign in to keep privacy settings on your account.]
=> ok=0 missing=0 offline=0 msg=[Sign in to keep privacy settings on your account.]
  found=0
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.alex
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
)GOLD"},
    {"invites", R"GOLD(=> 31 err=[] support=1
=> 0 err=[] support=2
=> 0 err=[] support=2
=> 0 err=[That invite is not complete.] support=2
  invite 31 from=[u1] to=[u-2] game [Arena] link=[okay://join/R-1] ver=[1.2] target=[exported] pending 1700000000 1700003600
=> 0
  invite 31 from=[u1] to=[u-2] game [Arena] link=[okay://join/R-1] ver=[1.2] target=[exported] pending 1700000000 1700003600
  invite 0 from=[] to=[] game [] link=[] ver=[] target=[] pending 0 0
=> [accepted]
=> []
=> [] support=2
=> [cancelled]
POST https://backend.example.test/rest/v1/game_invites
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: return=representation
Content-Type: application/json

{"recipient":"u-2","kind":"game","title":"Arena \"1\"","link":"okay://join/R-1","game_version":"1.2","target":"exported","expires_at":"<NOW+3600>"}
---
POST https://backend.example.test/rest/v1/game_invites
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: return=representation
Content-Type: application/json

{"recipient":"u-2","kind":"game","title":"Arena \"1\"","link":"okay://join/R-1","game_version":"1.2","target":"exported"}
---
POST https://backend.example.test/rest/v1/game_invites
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Prefer: return=representation
Content-Type: application/json

{"recipient":"u-3","kind":"game","title":"Arena \"1\"","link":"okay://join/R-1","game_version":"1.2","target":"exported"}
---
GET https://backend.example.test/rest/v1/game_invites?select=*&recipient=eq.u1&id=gt.5&order=id.desc&limit=10
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/game_invites?select=*&recipient=eq.u1&id=gt.0&order=id.desc&limit=30
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/game_invites?select=*&sender=eq.u1&order=id.desc&limit=-2
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/game_invites?select=*&id=eq.31&order=id.desc&limit=1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
POST https://backend.example.test/rest/v1/rpc/respond_invite
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_id":31,"p_accept":true}
---
POST https://backend.example.test/rest/v1/rpc/respond_invite
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_id":32,"p_accept":false}
---
POST https://backend.example.test/rest/v1/rpc/cancel_invite
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_id":33}
---
POST https://backend.example.test/rest/v1/rpc/cancel_invite
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_id":-1}
---
)GOLD"},
    {"parties_mine", R"GOLD(=> 1
  party id=[p-1] leader=[u1] game=[Arena] link=[okay://join/R] max=1
  member [u1] user=[alex] display=[] avatar=[] ready=1 online=0 playing=[] joined=1700000000
  member [u-2] user=[bo] display=[Bo] avatar=[a] ready=0 online=1 playing=[Arena] joined=0
=> 1 valid=0
=> 0
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.al%20ex%2B%C3%A9
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/party_members?select=party_id&member=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/parties?select=id,leader,game,join_link,max_size&id=eq.p-1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/party_members?select=member,ready,joined_at&party_id=eq.p-1&order=joined_at.asc
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/profiles_visible?select=id,username,display_name,avatar_url,last_seen,playing&id=in.(u1,u-2)
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/party_members?select=party_id&member=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/rest/v1/party_members?select=party_id&member=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
)GOLD"},
    {"parties_actions", R"GOLD(=> 1 err=[]
=> 0 err=[Parties aren't set up on the server yet.]
=> 0 err=[Party is full]
=> 0 err=[That invite isn't valid.]
=> 0 err=[Couldn't reach the server.]
=> 0 err=[Something went wrong.]
=> 1 err=[]
=> 1 err=[]
=> 1
=> 1
=> 0
=> 1
=> 0
POST https://backend.example.test/rest/v1/rpc/create_party
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{}
---
POST https://backend.example.test/rest/v1/rpc/join_party
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_party":"p-2"}
---
POST https://backend.example.test/rest/v1/rpc/join_party
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_party":"p \"3\""}
---
POST https://backend.example.test/rest/v1/rpc/leave_party
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{}
---
POST https://backend.example.test/rest/v1/rpc/kick_from_party
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_member":"u-2"}
---
POST https://backend.example.test/rest/v1/rpc/promote_party_leader
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_member":"u-3\n"}
---
POST https://backend.example.test/rest/v1/rpc/promote_party_leader
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_member":"u-4"}
---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.alex
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
PATCH https://backend.example.test/rest/v1/party_members?party_id=eq.p-1&member=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"ready":true}
---
PATCH https://backend.example.test/rest/v1/party_members?party_id=eq.p 1&x&member=eq.u1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"ready":false}
---
PATCH https://backend.example.test/rest/v1/parties?id=eq.p-1
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"game":"Arena \"1\"","join_link":"okay://join/R\n"}
---
)GOLD"},
};
// dup-allow-end
