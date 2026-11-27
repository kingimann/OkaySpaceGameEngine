// OkayBackend Functions: server functions called with a JSON body (docs/platform/okay_backend.md, sections 4, 7.5 and 17).
//
// The functions the programs call: "delete-account" (supabase/functions/delete-account: deletes the caller's data and
// account after checking the password) and "session-ticket" (supabase/functions/session-ticket: a short signed ticket a game
// server checks on join). Who is calling is the signed-in session's token; the body never carries a user id.
//
// Written from the behaviour the programs need and Supabase Edge Functions' publicly documented route (POST
// /functions/v1/<name>), which the Supabase provider sends (okay/Backend/Supabase.hpp). Header-only and free of <mutex> /
// <thread> (CLAUDE.md rule 21a).
#pragma once

#include "okay/Backend/Transport.hpp"   // Response

#include <string>

namespace okay::backend {

/// A call to make: the function's name and its JSON body. The request-building code (AccountData, AntiCheatBackend) returns
/// one, so the call is unit tested without a server.
struct FunctionCall {
    std::string name;
    std::string body;
};

/// The Functions calls every provider answers. A function the server does not have answers 404.
class Functions {
public:
    virtual ~Functions() = default;
    /// Call `name` with the JSON `json`. `timeoutSec` > 0 limits the whole call (a call that runs out of time comes back
    /// not reached, with maybeSent set when it may have arrived; it is never sent again by itself).
    virtual Response Invoke(const std::string& name, const std::string& json, int timeoutSec = 0) = 0;
    Response Invoke(const FunctionCall& call, int timeoutSec = 0) { return Invoke(call.name, call.body, timeoutSec); }
};

} // namespace okay::backend
