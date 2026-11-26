// GOLDEN request texts for tests/test_okay_backend_golden_auth.cpp, recorded from AccountService.hpp at commit b9f097c46
// (before its auth calls moved onto okay::backend; built with -DOKAY_GOLDEN_OLD_HEADER against that file). Never edit by hand:
// a change here means the programs send something new.
// Recorded with OKAY_GOLDEN_PRINT=1 (tests/test_okay_backend_golden_auth.cpp).
static const char* const kGolden[][2] = {
    {"signup_with_username", R"GOLD(POST https://backend.example.test/auth/v1/signup
apikey: test-publishable-key-0000
Content-Type: application/json

{"email":"alex@example.test","password":"pw-123456","data":{"username":"alex"}}
---
=> ok=1 error=[] user=[alex] token=[eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig] refresh=[r1]
session: in=1 user=[alex] token=[eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MSIsImV4cCI6NDEwMjQ0NDgwMH0.sig] refresh=[r1]
)GOLD"},
    {"signup_needs_confirmation", R"GOLD(=> ok=0 error=[Account created — check your email to confirm, then sign in. (Turn off email confirmation in Supabase for instant sign-in.)] user=[] token=[] refresh=[]
session: in=0 user=[] token=[] refresh=[]
POST https://backend.example.test/auth/v1/signup
apikey: test-publishable-key-0000
Content-Type: application/json

{"email":"sam@example.test","password":"pw \"quoted\" \\ 1"}
---
)GOLD"},
    {"signup_refused", R"GOLD(=> ok=0 error=[User already registered] user=[] token=[] refresh=[]
POST https://backend.example.test/auth/v1/signup
apikey: test-publishable-key-0000
Content-Type: application/json

{"email":"alex@example.test","password":"pw-123456"}
---
)GOLD"},
    {"signin_email", R"GOLD(=> ok=1 error=[] user=[alex] token=[eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MyIsImV4cCI6NDEwMjQ0NDgwMH0.sig] refresh=[r3]
POST https://backend.example.test/auth/v1/token?grant_type=password
apikey: test-publishable-key-0000
Content-Type: application/json

{"email":"alex@example.test","password":"pw-123456"}
---
session: in=1 user=[alex] token=[eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MyIsImV4cCI6NDEwMjQ0NDgwMH0.sig] refresh=[r3]
)GOLD"},
    {"signin_email_no_username_in_answer", R"GOLD(=> ok=1 error=[] user=[alex.doe] token=[eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NCIsImV4cCI6NDEwMjQ0NDgwMH0.sig] refresh=[r4]
POST https://backend.example.test/auth/v1/token?grant_type=password
apikey: test-publishable-key-0000
Content-Type: application/json

{"email":"alex.doe@example.test","password":"pw-123456"}
---
)GOLD"},
    {"signin_username", R"GOLD(=> ok=0 error=[Enter a valid email address.] user=[] token=[] refresh=[]
)GOLD"},
    {"signin_username_wrong", R"GOLD(=> ok=0 error=[Enter a valid email address.] user=[] token=[] refresh=[]
)GOLD"},
    {"signin_username_unreachable", R"GOLD(=> ok=0 error=[Enter a valid email address.] user=[] token=[] refresh=[]
)GOLD"},
    {"signin_retry_once_then_ok", R"GOLD(=> ok=1 error=[] user=[alex] token=[eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NiIsImV4cCI6NDEwMjQ0NDgwMH0.sig] refresh=[r6]
POST https://backend.example.test/auth/v1/token?grant_type=password
apikey: test-publishable-key-0000
Content-Type: application/json

{"email":"alex@example.test","password":"pw-123456"}
---
POST https://backend.example.test/auth/v1/token?grant_type=password
apikey: test-publishable-key-0000
Content-Type: application/json

{"email":"alex@example.test","password":"pw-123456"}
---
)GOLD"},
    {"signin_unreachable", R"GOLD(=> ok=0 error=[Couldn't reach the account server (offline or unreachable).
Details: The server's name could not be looked up (no internet, or DNS is blocked). [connect: cannot resolve host: Name or service not known]] user=[] token=[] refresh=[]
POST https://backend.example.test/auth/v1/token?grant_type=password
apikey: test-publishable-key-0000
Content-Type: application/json

{"email":"alex@example.test","password":"pw-123456"}
---
POST https://backend.example.test/auth/v1/token?grant_type=password
apikey: test-publishable-key-0000
Content-Type: application/json

{"email":"alex@example.test","password":"pw-123456"}
---
)GOLD"},
    {"signin_refused", R"GOLD(=> ok=0 error=[Invalid login credentials] user=[] token=[] refresh=[]
POST https://backend.example.test/auth/v1/token?grant_type=password
apikey: test-publishable-key-0000
Content-Type: application/json

{"email":"alex@example.test","password":"wrong-pass"}
---
)GOLD"},
    {"signin_refused_no_text", R"GOLD(=> ok=0 error=[Account server rejected the request (HTTP 500).] user=[] token=[] refresh=[]
POST https://backend.example.test/auth/v1/token?grant_type=password
apikey: test-publishable-key-0000
Content-Type: application/json

{"email":"alex@example.test","password":"pw-123456"}
---
)GOLD"},
    {"signin_answer_without_token", R"GOLD(=> ok=0 error=[Server response was missing an access token.] user=[] token=[] refresh=[]
POST https://backend.example.test/auth/v1/token?grant_type=password
apikey: test-publishable-key-0000
Content-Type: application/json

{"email":"alex@example.test","password":"pw-123456"}
---
)GOLD"},
    {"resend_confirmation", R"GOLD(=> ok=1 error=[] user=[] token=[] refresh=[]
=> ok=0 error=[Couldn't send the email again (HTTP 429).] user=[] token=[] refresh=[]
=> ok=0 error=[Couldn't reach the account server.] user=[] token=[] refresh=[]
=> ok=0 error=[Enter a valid email address.] user=[] token=[] refresh=[]
POST https://backend.example.test/auth/v1/resend
apikey: test-publishable-key-0000
Content-Type: application/json

{"type":"signup","email":"sam@example.test"}
---
POST https://backend.example.test/auth/v1/resend
apikey: test-publishable-key-0000
Content-Type: application/json

{"type":"signup","email":"sam@example.test"}
---
POST https://backend.example.test/auth/v1/resend
apikey: test-publishable-key-0000
Content-Type: application/json

{"type":"signup","email":"sam@example.test"}
---
)GOLD"},
    {"password_reset_request", R"GOLD(=> ok=1 error=[] user=[] token=[] refresh=[]
=> ok=1 error=[] user=[] token=[] refresh=[]
=> ok=0 error=[Couldn't start password reset (HTTP 500).] user=[] token=[] refresh=[]
url: https://backend.example.test/auth/v1/recover?redirect_to=https%3A%2F%2Fr.example.test%2F%3Fa%3D1
POST https://backend.example.test/auth/v1/recover
apikey: test-publishable-key-0000
Content-Type: application/json

{"email":"sam@example.test"}
---
POST https://backend.example.test/auth/v1/recover?redirect_to=https%3A%2F%2Freset.example.test%2Fpage%3Fx%3D1%26y%3Da%20b
apikey: test-publishable-key-0000
Content-Type: application/json

{"email":"sam@example.test"}
---
POST https://backend.example.test/auth/v1/recover
apikey: test-publishable-key-0000
Content-Type: application/json

{"email":"sam@example.test"}
---
)GOLD"},
    {"password_reset_with_code", R"GOLD(=> ok=1 error=[] user=[] token=[] refresh=[]
=> ok=0 error=[That code is wrong or has expired. Ask for a new reset email.] user=[] token=[] refresh=[]
=> ok=0 error=[New password should be different] user=[] token=[] refresh=[]
body: {"type":"recovery","email":"a\"b@example.test","token":"12345678"}
POST https://backend.example.test/auth/v1/verify
apikey: test-publishable-key-0000
Content-Type: application/json

{"type":"recovery","email":"sam@example.test","token":"123456"}
---
PUT https://backend.example.test/auth/v1/user
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1NyIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"password":"new-pass-1"}
---
POST https://backend.example.test/auth/v1/verify
apikey: test-publishable-key-0000
Content-Type: application/json

{"type":"recovery","email":"sam@example.test","token":"654321"}
---
POST https://backend.example.test/auth/v1/verify
apikey: test-publishable-key-0000
Content-Type: application/json

{"type":"recovery","email":"sam@example.test","token":"1234567"}
---
PUT https://backend.example.test/auth/v1/user
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1OCIsImV4cCI6NDEwMjQ0NDgwMH0.sig
Content-Type: application/json

{"password":"new-pass-1"}
---
session: in=0 user=[] token=[] refresh=[]
)GOLD"},
    {"change_password_email_username", R"GOLD(=> ok=1 error=[] user=[alex] token=[eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MTAiLCJleHAiOjQxMDI0NDQ4MDB9.sig] refresh=[r10]
=> ok=0 error=[Email address already in use] user=[] token=[] refresh=[]
=> ok=1 error=[] user=[alex_2] token=[eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MTAiLCJleHAiOjQxMDI0NDQ4MDB9.sig] refresh=[r10]
=> ok=0 error=[Password change failed.] user=[] token=[] refresh=[]
PUT https://backend.example.test/auth/v1/user
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MTAiLCJleHAiOjQxMDI0NDQ4MDB9.sig
Content-Type: application/json

{"password":"new-pass-2"}
---
PUT https://backend.example.test/auth/v1/user
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MTAiLCJleHAiOjQxMDI0NDQ4MDB9.sig
Content-Type: application/json

{"email":"alex2@example.test"}
---
PUT https://backend.example.test/auth/v1/user
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MTAiLCJleHAiOjQxMDI0NDQ4MDB9.sig
Content-Type: application/json

{"data":{"username":"alex_2"}}
---
PUT https://backend.example.test/auth/v1/user
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MTAiLCJleHAiOjQxMDI0NDQ4MDB9.sig
Content-Type: application/json

{"password":"new-pass-3"}
---
session: in=1 user=[alex_2] token=[eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MTAiLCJleHAiOjQxMDI0NDQ4MDB9.sig] refresh=[r10]
)GOLD"},
    {"change_after_401_takes_the_newer_saved_token", R"GOLD(=> ok=1 error=[] user=[alex_3] token=[eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MTIiLCJleHAiOjQxMDI0NDQ4MDB9.sig] refresh=[r12]
PUT https://backend.example.test/auth/v1/user
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MTEiLCJleHAiOjQxMDI0NDQ4MDB9.sig
Content-Type: application/json

{"data":{"username":"alex_3"}}
---
PUT https://backend.example.test/auth/v1/user
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MTIiLCJleHAiOjQxMDI0NDQ4MDB9.sig
Content-Type: application/json

{"data":{"username":"alex_3"}}
---
session: in=1 user=[alex_3] token=[eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MTIiLCJleHAiOjQxMDI0NDQ4MDB9.sig] refresh=[r12]
)GOLD"},
    {"check_session_valid", R"GOLD(check=0
GET https://backend.example.test/auth/v1/user
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MTMiLCJleHAiOjQxMDI0NDQ4MDB9.sig


---
session: in=1 user=[alex] token=[eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MTMiLCJleHAiOjQxMDI0NDQ4MDB9.sig] refresh=[r13]
)GOLD"},
    {"check_session_refreshes", R"GOLD(check=0
GET https://backend.example.test/auth/v1/user
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MTQiLCJleHAiOjEwMDB9.sig


---
POST https://backend.example.test/auth/v1/token?grant_type=refresh_token
apikey: test-publishable-key-0000
Content-Type: application/json

{"refresh_token":"r14"}
---
session: in=1 user=[alex] token=[eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MTUiLCJleHAiOjQxMDI0NDQ4MDB9.sig] refresh=[r15]
)GOLD"},
    {"check_session_rejected", R"GOLD(check=1
GET https://backend.example.test/auth/v1/user
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MTYiLCJleHAiOjEwMDB9.sig


---
POST https://backend.example.test/auth/v1/token?grant_type=refresh_token
apikey: test-publishable-key-0000
Content-Type: application/json

{"refresh_token":"r16"}
---
session: in=0 user=[] token=[] refresh=[]
)GOLD"},
    {"check_session_unreachable_and_server_error", R"GOLD(check=2
check=3
GET https://backend.example.test/auth/v1/user
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MTciLCJleHAiOjQxMDI0NDQ4MDB9.sig


---
GET https://backend.example.test/auth/v1/user
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MTciLCJleHAiOjQxMDI0NDQ4MDB9.sig


---
session: in=1 user=[alex] token=[eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MTciLCJleHAiOjQxMDI0NDQ4MDB9.sig] refresh=[r17]
)GOLD"},
    {"refresh_if_expiring", R"GOLD(refreshed=1
again=0
POST https://backend.example.test/auth/v1/token?grant_type=refresh_token
apikey: test-publishable-key-0000
Content-Type: application/json

{"refresh_token":"r18"}
---
session: in=1 user=[alex] token=[eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MTkiLCJleHAiOjQxMDI0NDQ4MDB9.sig] refresh=[r18]
)GOLD"},
    {"verify_token", R"GOLD(verified=1 id=[u-9]
verified=0 id=[]
verified=0 id=[]
GET https://backend.example.test/auth/v1/user
apikey: test-publishable-key-0000
Authorization: Bearer other.token.x


---
GET https://backend.example.test/auth/v1/user
apikey: test-publishable-key-0000
Authorization: Bearer bad.token.y


---
)GOLD"},
    {"transport_api_calls", R"GOLD(get=200
post=201
anon=200
upload=200
upload_default_type=200
timed=200
available=1
GET https://backend.example.test/rest/v1/cloud_saves?select=key
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MjEiLCJleHAiOjQxMDI0NDQ4MDB9.sig


---
POST https://backend.example.test/rest/v1/leaderboards
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MjEiLCJleHAiOjQxMDI0NDQ4MDB9.sig
Prefer: resolution=merge-duplicates,return=minimal
Content-Type: application/json

[{"board":"b"}]
---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.x
apikey: test-publishable-key-0000
Accept: application/json
Range: 0-9


---
POST https://backend.example.test/storage/v1/object/chat/u21/a.png
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MjEiLCJleHAiOjQxMDI0NDQ4MDB9.sig
x-upsert: true
cache-control: max-age=30
Content-Type: image/png


---
POST https://backend.example.test/storage/v1/object/chat/u21/b.bin
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MjEiLCJleHAiOjQxMDI0NDQ4MDB9.sig
Content-Type: application/octet-stream


---
POST https://backend.example.test/functions/v1/session-ticket
apikey: test-publishable-key-0000
Authorization: Bearer eyJhbGciOiJub25lIn0.eyJzdWIiOiJ1MjEiLCJleHAiOjQxMDI0NDQ4MDB9.sig
Content-Type: application/json

{}
---
GET https://backend.example.test/rest/v1/profiles?select=id&username=eq.al%20ex
apikey: test-publishable-key-0000


---
)GOLD"},
    {"transport_signed_out_sends_nothing", R"GOLD(api_reached=0
cloud=0
)GOLD"},
    {"sign_out_sends_nothing", R"GOLD(session: in=0 user=[] token=[] refresh=[]
file=0
)GOLD"},
};
