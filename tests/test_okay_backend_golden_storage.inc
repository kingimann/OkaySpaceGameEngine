// Golden texts for tests/test_okay_backend_golden_storage.cpp, recorded from the Storage and Functions call sites BEFORE they moved
// onto OkayBackend (round 4). Re-record only from the old code: OKAY_GOLDEN_PRINT=1 ./test_okay_backend_golden_storage > this file.
// Recorded with OKAY_GOLDEN_PRINT=1 (tests/test_okay_backend_golden_storage.cpp).
static const char* const kGolden[][2] = {
    {"chat_attachment", R"GOLD(GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.alex
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
POST https://backend.example.test/storage/v1/object/chat/7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7/<RND>-level_plan__v2_.png
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
x-upsert: true
Content-Type: image/png

(file bytes) PNG\x89\x0d\x0a\x1a\x0a\x00\x01\xff body
---
=> url=[https://backend.example.test/storage/v1/object/public/chat/7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7/<RND>-level_plan__v2_.png] err=[]
)GOLD"},
    {"chat_attachment_refused", R"GOLD(GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.alex
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
POST https://backend.example.test/storage/v1/object/chat/7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7/<RND>-a.bin
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
x-upsert: true
Content-Type: application/octet-stream

(file bytes) x
---
POST https://backend.example.test/storage/v1/object/chat/7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7/<RND>-a.bin
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
x-upsert: true
Content-Type: application/octet-stream

(file bytes) x
---
POST https://backend.example.test/storage/v1/object/chat/7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7/<RND>-a.bin
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
x-upsert: true
Content-Type: application/octet-stream

(file bytes) x
---
=> url=[] err=[Attachments aren't set up on the server yet (run docs/sql/chat_media.sql).]
=> url=[] err=[{"error":"Payload too large"}]
=> url=[] err=[Upload failed.]
)GOLD"},
    {"chat_attachment_signed_out", R"GOLD(=> url=[] err=[Sign in first.]
)GOLD"},
    {"library", R"GOLD(GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.alex
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
POST https://backend.example.test/storage/v1/object/chat/7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7/emoji/partyparrot.gif
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
x-upsert: true
Content-Type: image/gif

(file bytes) PNG\x89\x0d\x0a\x1a\x0a\x00\x01\xff body
---
POST https://backend.example.test/storage/v1/object/chat/7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7/stickers/wave.gif
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
x-upsert: true
Content-Type: image/gif

(file bytes) PNG\x89\x0d\x0a\x1a\x0a\x00\x01\xff body
---
POST https://backend.example.test/storage/v1/object/list/chat
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"prefix":"7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7/emoji/","limit":500,"offset":0,"sortBy":{"column":"name","order":"asc"}}
---
POST https://backend.example.test/storage/v1/object/list/chat
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"prefix":"7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7/stickers/","limit":500,"offset":0,"sortBy":{"column":"name","order":"asc"}}
---
DELETE https://backend.example.test/storage/v1/object/chat/7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7/emoji/party.gif
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
DELETE https://backend.example.test/storage/v1/object/chat/7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7/emoji/gone.png
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
=> url=[https://backend.example.test/storage/v1/object/public/chat/7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7/emoji/partyparrot.gif] err=[]
=> url=[] err=[boom]
=> url=[] err=[Pick a name (letters, numbers, _ or -).]
=> list
  item name=[party] file=[party.gif] url=[https://backend.example.test/storage/v1/object/public/chat/7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7/emoji/party.gif]
  item name=[wave] file=[wave.png] url=[https://backend.example.test/storage/v1/object/public/chat/7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7/emoji/wave.png]
=> list
=> delete 1
=> delete 0
=> delete 0
=> public [https://backend.example.test/storage/v1/object/public/chat/u-1/emoji/x.png]
)GOLD"},
    {"workshop_upload", R"GOLD(GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.alex
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
POST https://backend.example.test/storage/v1/object/workshop/7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7/<RND>-My_Map__1.zip
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
x-upsert: true
Content-Type: application/octet-stream

(file bytes) PNG\x89\x0d\x0a\x1a\x0a\x00\x01\xff body
---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.alex
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
POST https://backend.example.test/storage/v1/object/workshop/7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7/<RND>-My_Map__1.zip
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
x-upsert: true
Content-Type: application/octet-stream

(file bytes) PNG\x89\x0d\x0a\x1a\x0a\x00\x01\xff body
---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.alex
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
POST https://backend.example.test/storage/v1/object/workshop/7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7/<RND>-My_Map__1.zip
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
x-upsert: true
Content-Type: application/octet-stream

(file bytes) PNG\x89\x0d\x0a\x1a\x0a\x00\x01\xff body
---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.alex
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
=> url=[https://backend.example.test/storage/v1/object/public/workshop/7f1c2d3e-4b5a-6978-8091-a2b3c4d5e6f7/<RND>-My_Map__1.zip] err=[]
=> url=[] err=[That file is too big for the server's upload limit.]
=> url=[] err=[File uploads aren't set up on the server (create the 'workshop' bucket from docs/sql/social.sql).]
=> url=[] err=[Couldn't resolve your account.]
)GOLD"},
    {"official_reads", R"GOLD(GET https://backend.example.test/storage/v1/object/public/releases/official/VERSION.txt?cb=<CB>
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/storage/v1/object/public/releases/official/VERSION.txt?cb=<CB>
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/storage/v1/object/public/releases/official/VERSION.txt?cb=<CB>
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/storage/v1/object/public/releases/official/v1.2.0/manifest.json?cb=<CB>
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/storage/v1/object/public/releases/official/a%20b/%C3%BC%26%3F.txt?cb=<CB>
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
=> live [1.2.0] reached=1
=> live [] reached=1
=> live [] reached=0
=> manifest 0
=> get ok=1 body=[notes]
)GOLD"},
    {"official_uploads", R"GOLD(POST https://backend.example.test/storage/v1/object/releases/official/v1.2.0/data%201.pak
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
x-upsert: true
cache-control: max-age=31536000
Content-Type: application/octet-stream

(file bytes) PNG\x89\x0d\x0a\x1a\x0a\x00\x01\xff body
---
POST https://backend.example.test/storage/v1/object/releases/official/v1.2.0/data%201.pak
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
x-upsert: true
cache-control: max-age=31536000
Content-Type: application/octet-stream

(file bytes) PNG\x89\x0d\x0a\x1a\x0a\x00\x01\xff body
---
POST https://backend.example.test/storage/v1/object/releases/official/NOTES.txt
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
x-upsert: true
cache-control: max-age=30
Content-Type: text/plain; charset=utf-8

(file bytes) line one\x0aline "two"\x0a
---
POST https://backend.example.test/storage/v1/object/releases/official/VERSION.txt
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
x-upsert: true
cache-control: max-age=30
Content-Type: text/plain

(file bytes) 1.2.0\x0a
---
=> upload ok=1
=> upload ok=0 error=[Run docs/sql/official_game_admin.sql on Supabase to let admins publish.] needSql=1
=> text 1
=> text 0 reached=0
)GOLD"},
    {"official_lists", R"GOLD(POST https://backend.example.test/storage/v1/object/list/releases
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"prefix":"official/v1.2.0/","limit":1000,"offset":0,"sortBy":{"column":"name","order":"asc"}}
---
POST https://backend.example.test/storage/v1/object/list/releases
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"prefix":"official/v1.2.0/","limit":1000,"offset":1000,"sortBy":{"column":"name","order":"asc"}}
---
POST https://backend.example.test/storage/v1/object/list/releases
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"prefix":"official/v1.2.0/sub dir/","limit":1000,"offset":0,"sortBy":{"column":"name","order":"asc"}}
---
POST https://backend.example.test/storage/v1/object/list/releases
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"prefix":"official/","limit":1000,"offset":0,"sortBy":{"column":"name","order":"asc"}}
---
POST https://backend.example.test/storage/v1/object/list/releases
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"prefix":"official/v9/","limit":1000,"offset":0,"sortBy":{"column":"name","order":"asc"}}
---
=> all 1 count=1002 first=[official/v1.2.0/f0.bin] last=[official/v1.2.0/last.bin]
=> top ok=1 body=[[{"name":"v1.2.0","id":null},{"name":"VERSION.txt","id":"1"}]]
=> all 0 count=0
)GOLD"},
    {"official_delete", R"GOLD(GET https://backend.example.test/storage/v1/object/public/releases/official/VERSION.txt?cb=<CB>
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
POST https://backend.example.test/storage/v1/object/list/releases
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"prefix":"official/v1.2.0/","limit":1000,"offset":0,"sortBy":{"column":"name","order":"asc"}}
---
DELETE https://backend.example.test/storage/v1/object/releases
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"prefixes":["official/v1.2.0/f0","official/v1.2.0/f1","official/v1.2.0/f2","official/v1.2.0/f3","official/v1.2.0/f4","official/v1.2.0/f5","official/v1.2.0/f6","official/v1.2.0/f7","official/v1.2.0/f8","official/v1.2.0/f9","official/v1.2.0/f10","official/v1.2.0/f11","official/v1.2.0/f12","official/v1.2.0/f13","official/v1.2.0/f14","official/v1.2.0/f15","official/v1.2.0/f16","official/v1.2.0/f17","official/v1.2.0/f18","official/v1.2.0/f19","official/v1.2.0/f20","official/v1.2.0/f21","official/v1.2.0/f22","official/v1.2.0/f23","official/v1.2.0/f24","official/v1.2.0/f25","official/v1.2.0/f26","official/v1.2.0/f27","official/v1.2.0/f28","official/v1.2.0/f29","official/v1.2.0/f30","official/v1.2.0/f31","official/v1.2.0/f32","official/v1.2.0/f33","official/v1.2.0/f34","official/v1.2.0/f35","official/v1.2.0/f36","official/v1.2.0/f37","official/v1.2.0/f38","official/v1.2.0/f39","official/v1.2.0/f40","official/v1.2.0/f41","official/v1.2.0/f42","official/v1.2.0/f43","official/v1.2.0/f44","official/v1.2.0/f45","official/v1.2.0/f46","official/v1.2.0/f47","official/v1.2.0/f48","official/v1.2.0/f49","official/v1.2.0/f50","official/v1.2.0/f51","official/v1.2.0/f52","official/v1.2.0/f53","official/v1.2.0/f54","official/v1.2.0/f55","official/v1.2.0/f56","official/v1.2.0/f57","official/v1.2.0/f58","official/v1.2.0/f59","official/v1.2.0/f60","official/v1.2.0/f61","official/v1.2.0/f62","official/v1.2.0/f63","official/v1.2.0/f64","official/v1.2.0/f65","official/v1.2.0/f66","official/v1.2.0/f67","official/v1.2.0/f68","official/v1.2.0/f69","official/v1.2.0/f70","official/v1.2.0/f71","official/v1.2.0/f72","official/v1.2.0/f73","official/v1.2.0/f74","official/v1.2.0/f75","official/v1.2.0/f76","official/v1.2.0/f77","official/v1.2.0/f78","official/v1.2.0/f79","official/v1.2.0/f80","official/v1.2.0/f81","official/v1.2.0/f82","official/v1.2.0/f83","official/v1.2.0/f84","official/v1.2.0/f85","official/v1.2.0/f86","official/v1.2.0/f87","official/v1.2.0/f88","official/v1.2.0/f89","official/v1.2.0/f90","official/v1.2.0/f91","official/v1.2.0/f92","official/v1.2.0/f93","official/v1.2.0/f94","official/v1.2.0/f95","official/v1.2.0/f96","official/v1.2.0/f97","official/v1.2.0/f98","official/v1.2.0/f99","official/v1.2.0/f100","official/v1.2.0/f101","official/v1.2.0/f102","official/v1.2.0/f103","official/v1.2.0/f104","official/v1.2.0/f105","official/v1.2.0/f106","official/v1.2.0/f107","official/v1.2.0/f108","official/v1.2.0/f109","official/v1.2.0/f110","official/v1.2.0/f111","official/v1.2.0/f112","official/v1.2.0/f113","official/v1.2.0/f114","official/v1.2.0/f115","official/v1.2.0/f116","official/v1.2.0/f117","official/v1.2.0/f118","official/v1.2.0/f119","official/v1.2.0/f120","official/v1.2.0/f121","official/v1.2.0/f122","official/v1.2.0/f123","official/v1.2.0/f124","official/v1.2.0/f125","official/v1.2.0/f126","official/v1.2.0/f127","official/v1.2.0/f128","official/v1.2.0/f129","official/v1.2.0/f130","official/v1.2.0/f131","official/v1.2.0/f132","official/v1.2.0/f133","official/v1.2.0/f134","official/v1.2.0/f135","official/v1.2.0/f136","official/v1.2.0/f137","official/v1.2.0/f138","official/v1.2.0/f139","official/v1.2.0/f140","official/v1.2.0/f141","official/v1.2.0/f142","official/v1.2.0/f143","official/v1.2.0/f144","official/v1.2.0/f145","official/v1.2.0/f146","official/v1.2.0/f147","official/v1.2.0/f148","official/v1.2.0/f149","official/v1.2.0/f150","official/v1.2.0/f151","official/v1.2.0/f152","official/v1.2.0/f153","official/v1.2.0/f154","official/v1.2.0/f155","official/v1.2.0/f156","official/v1.2.0/f157","official/v1.2.0/f158","official/v1.2.0/f159","official/v1.2.0/f160","official/v1.2.0/f161","official/v1.2.0/f162","official/v1.2.0/f163","official/v1.2.0/f164","official/v1.2.0/f165","official/v1.2.0/f166","official/v1.2.0/f167","official/v1.2.0/f168","official/v1.2.0/f169","official/v1.2.0/f170","official/v1.2.0/f171","official/v1.2.0/f172","official/v1.2.0/f173","official/v1.2.0/f174","official/v1.2.0/f175","official/v1.2.0/f176","official/v1.2.0/f177","official/v1.2.0/f178","official/v1.2.0/f179","official/v1.2.0/f180","official/v1.2.0/f181","official/v1.2.0/f182","official/v1.2.0/f183","official/v1.2.0/f184","official/v1.2.0/f185","official/v1.2.0/f186","official/v1.2.0/f187","official/v1.2.0/f188","official/v1.2.0/f189","official/v1.2.0/f190","official/v1.2.0/f191","official/v1.2.0/f192","official/v1.2.0/f193","official/v1.2.0/f194","official/v1.2.0/f195","official/v1.2.0/f196","official/v1.2.0/f197","official/v1.2.0/f198","official/v1.2.0/f199"]}
---
DELETE https://backend.example.test/storage/v1/object/releases
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"prefixes":["official/v1.2.0/f200"]}
---
POST https://backend.example.test/rest/v1/rpc/log_official_publish
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"p_version":"1.2.0","p_action":"delete"}
---
GET https://backend.example.test/storage/v1/object/public/releases/official/VERSION.txt?cb=<CB>
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
GET https://backend.example.test/storage/v1/object/public/releases/official/VERSION.txt?cb=<CB>
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig


---
POST https://backend.example.test/storage/v1/object/list/releases
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"prefix":"official/v1.1.0/","limit":1000,"offset":0,"sortBy":{"column":"name","order":"asc"}}
---
DELETE https://backend.example.test/storage/v1/object/releases
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"prefixes":["official/v1.1.0/x\"y.bin"]}
---
=> delete 1 why=[] needSql=0
=> delete 0 why=[v1.3.0 is live. Make another version live first.] needSql=0
=> delete 0 why=[Run docs/sql/official_game_admin.sql on Supabase to let admins publish.] needSql=1
)GOLD"},
    {"delete_account", R"GOLD(POST https://backend.example.test/functions/v1/delete-account
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"confirm_username":"al\"ex","password":"pa\\ss\u0001word"}
---
POST https://backend.example.test/functions/v1/delete-account
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"confirm_username":"alex","password":"pw"}
---
POST https://backend.example.test/functions/v1/delete-account
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"confirm_username":"alex","password":"pw"}
---
POST https://backend.example.test/functions/v1/delete-account
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"confirm_username":"alex","password":"pw"}
---
=> ok=0 signOut=0 notDeployed=1 step=[] message=[Account deletion is not set up on this server yet: the delete-account function is not deployed. Nothing was deleted. Ask the server owner to deploy it (docs/online/account_data.md).]
=> ok=0 signOut=0 notDeployed=0 step=[forum_posts] message=[Deletion stopped at step "forum_posts". Some of your data may already be gone, but your account still exists. Retry to finish, or contact the server owner.]
=> ok=0 signOut=0 notDeployed=0 step=[] message=[No reply from the server. If you were offline, nothing was deleted. If the server was busy the deletion may have finished: try again, and if your account is already gone, signing in will fail.]
=> ok=1 signOut=1 notDeployed=0 step=[] message=[Your account and data were deleted. You have been signed out.]
)GOLD"},
    {"delete_account_signed_out", R"GOLD(=> ok=0 signOut=0 notDeployed=0 step=[] message=[No reply from the server. If you were offline, nothing was deleted. If the server was busy the deletion may have finished: try again, and if your account is already gone, signing in will fail.]
)GOLD"},
    {"session_ticket_shared", R"GOLD(POST https://backend.example.test/functions/v1/session-ticket
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MyIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json
(time limit: 8 s)

{"aud":"arena-1","cnf":"kkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkk","hw_hash":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}
---
POST https://backend.example.test/functions/v1/session-ticket
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MyIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json
(time limit: 8 s)

{"aud":"203.0.113.7:45000"}
---
POST https://backend.example.test/functions/v1/session-ticket
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MyIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json
(time limit: 8 s)

{"aud":"203.0.113.7:45000"}
---
POST https://backend.example.test/functions/v1/session-ticket
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MyIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json
(time limit: 8 s)

{"aud":"arena-2","cnf":"kkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkk"}
---
POST https://backend.example.test/functions/v1/session-ticket
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MyIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json
(time limit: 8 s)

{"aud":"arena-3"}
---
=> outcome=0 ticket=[okt1.payload.sig] attempts=1 message=[]
=> outcome=0 ticket=[okt1.payload.sig] attempts=2 message=[]
=> outcome=4 ticket=[] attempts=1 message=[The ticket service isn't set up on this server.]
=> outcome=1 ticket=[] attempts=1 message=[You can't join: this account or machine has an anti-cheat ban.]
=> outcome=3 ticket=[] attempts=0 message=[That isn't a valid game server id for a session ticket.]
)GOLD"},
    {"session_ticket_launcher", R"GOLD(POST https://backend.example.test/functions/v1/session-ticket
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NCIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json
(time limit: 8 s)

{"aud":"arena-1","cnf":"kkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkk"}
---
POST https://backend.example.test/functions/v1/session-ticket
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NCIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json
(time limit: 8 s)

{"aud":"arena-1","cnf":"kkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkk","hw_hash":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}
---
=> outcome=0 ticket=[okt1.payload.sig] attempts=1 message=[]
=> outcome=5 ticket=[] attempts=1 message=[Couldn't reach the ticket service.]
)GOLD"},
    {"releases_bucket_base", R"GOLD([] [https://abc123.supabase.co] => [https://abc123.supabase.co/storage/v1/object/public/releases/]
[] [https://abc123.supabase.co/] => [https://abc123.supabase.co/storage/v1/object/public/releases/]
[] [https://abc123.supabase.co///] => [https://abc123.supabase.co/storage/v1/object/public/releases/]
[https://other.supabase.co] [https://abc123.supabase.co] => [https://other.supabase.co/storage/v1/object/public/releases/]
[http://127.0.0.1:9000] [] => [http://127.0.0.1:9000/storage/v1/object/public/releases/]
[http://127.0.0.1:9000/] [x] => [http://127.0.0.1:9000/storage/v1/object/public/releases/]
[] [] => []
[] [http://abc.supabase.co] => []
[] [junk] => []
[] [https://github.com/o/r] => []
[https://api.github.com] [] => []
[] [https://raw.githubusercontent.com/o/r] => []
[] [https://abc.supabase.co/sub/path] => [https://abc.supabase.co/sub/path/storage/v1/object/public/releases/]
[] [https://abc.supabase.co?x=1] => [https://abc.supabase.co?x=1/storage/v1/object/public/releases/]
[] [https://abc.supabase.co/ space] => []
[] [https://] => []
[http://localhost:9000] [] => []
[] [HTTPS://ABC.SUPABASE.CO] => []
)GOLD"},
};
