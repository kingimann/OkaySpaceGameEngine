// OkayBackend Database: reading and writing table rows, and calling database functions (RPC), the same way on every
// provider (docs/platform/okay_backend.md, section 4; round 3).
//
//   db.From("leaderboards").Select("name,score").Eq("board", board).Order("score", true).Limit(10).Get();
//   db.From("cloud_saves").Returning(Return::Minimal).Upsert("[{\"key\":\"k\",\"data\":\"d\"}]");
//   db.Rpc("report_match", "{\"p\":{...}}");
//
// A Query writes the request as PostgREST's URL syntax ("table?select=...&col=op.value&order=...&limit=..."), the one text
// both providers read: the Supabase provider (okay/Backend/Supabase.hpp) puts "/rest/v1/" in front and sends it; the Local
// backend (round 10) will parse the same text. The parts are written in the order they are called, because the programs'
// requests have always had their parts in a fixed order and the golden tests (tests/test_okay_backend_golden_db.cpp) hold
// every moved request to the exact bytes it had before.
//
// Written from PostgREST's publicly documented URL interface (horizontal filtering with "column=operator.value", the
// operators eq, neq, gt, gte, lt, lte, like, ilike, in, is; the logical operators "or=(...)" and "and(...)"; "select",
// "order" with ".asc"/".desc", "limit", "offset",
// "on_conflict"; the "Prefer" header's return= and resolution= options; "Accept: application/vnd.pgrst.object+json" for one
// row; functions under "rpc/") and RFC 3986 (percent-encoding). That interface is something we call; no PostgREST or
// Supabase code is used. Header-only and free of <mutex> / <thread> (CLAUDE.md rule 21a).
#pragma once

#include "okay/Backend/Transport.hpp"                // Response
#include "okay/Platform/Account/AccountDetail.hpp"   // detail::PercentEncode (RFC 3986 unreserved set)

#include <string>
#include <utility>
#include <vector>

namespace okay::backend {

/// One database request as a provider receives it: the method, the resource ("table?query", no leading "/"), the body
/// (JSON) and the extra header lines ("Prefer: ...", "Accept: ..."; one per line, "" for none).
struct DbRequest {
    std::string method;
    std::string path;
    std::string body;
    std::string headers;
};

/// How a filter value is written into the address. Encoded (the default) percent-encodes every byte outside RFC 3986's
/// unreserved set (A-Z a-z 0-9 - _ . ~), so a value can never add a part to the query; an item of a list (In, Cond inside
/// Or / And) that holds one of PostgREST's reserved characters is first put in double quotes. AsIs writes it unchanged: only
/// for values the program made itself (a timestamp) or for the moved sites that always sent their value unchanged (sections
/// 16 and 18 of docs/platform/okay_backend.md list them). EncodedUnquoted percent-encodes but never quotes a list item: only
/// for the moved sites that always sent list items that way (section 18); outside a list it is the same as Encoded.
enum class ValueText { Encoded, AsIs, EncodedUnquoted };

/// What an insert, upsert or update answers with ("Prefer: return=..."). Unset sends no return= option.
enum class Return { Unset, Minimal, Representation };

/// The values PostgREST's "is" operator accepts.
enum class IsValue { Null, True, False, Unknown };

class Database;

/// A request on one table, built step by step. Each step adds its part to the address in call order (two Order steps share
/// one "order=" part). Then Get / Insert / Upsert / Update / Delete sends it through the Database it came from, which
/// must still exist then.
class Query {
public:
    Query(Database& db, std::string table) : db_(&db), table_(std::move(table)) {}

    /// The columns to read ("id,name", "*", or with an embedded table). Replaces an earlier Select.
    Query& Select(const std::string& columns) {
        for (auto& p : parts_) if (p.first == "select") { p.second = Name(columns); return *this; }
        parts_.emplace_back("select", Name(columns));
        return *this;
    }
    Query& Eq(const std::string& column, const std::string& value, ValueText t = ValueText::Encoded) { return Filter(column, "eq", value, t); }
    Query& Neq(const std::string& column, const std::string& value, ValueText t = ValueText::Encoded) { return Filter(column, "neq", value, t); }
    Query& Gt(const std::string& column, const std::string& value, ValueText t = ValueText::Encoded) { return Filter(column, "gt", value, t); }
    Query& Gte(const std::string& column, const std::string& value, ValueText t = ValueText::Encoded) { return Filter(column, "gte", value, t); }
    Query& Lt(const std::string& column, const std::string& value, ValueText t = ValueText::Encoded) { return Filter(column, "lt", value, t); }
    Query& Lte(const std::string& column, const std::string& value, ValueText t = ValueText::Encoded) { return Filter(column, "lte", value, t); }
    /// Pattern match ("*" is the wildcard in an address, as PostgREST documents; the value is encoded like any other).
    Query& Like(const std::string& column, const std::string& pattern) { return Filter(column, "like", pattern, ValueText::Encoded); }
    Query& Ilike(const std::string& column, const std::string& pattern) { return Filter(column, "ilike", pattern, ValueText::Encoded); }
    /// The column is one of `values`: "in.(a,b,c)". With ValueText::Encoded a value holding a comma, a bracket, a quote, a
    /// backslash or an outer space is written in double quotes with \" and \\ escaped (PostgREST's reserved-character rule),
    /// then encoded; see ValueText for the other two.
    Query& In(const std::string& column, const std::vector<std::string>& values, ValueText t = ValueText::Encoded) {
        std::string list = "in.(";
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (i) list += ',';
            list += ListItem(values[i], t);
        }
        parts_.emplace_back(Name(column), list + ")");
        return *this;
    }
    /// Rows that match any of `conditions` (each made by Cond or And): "or=(a,b)".
    Query& Or(const std::vector<std::string>& conditions) {
        parts_.emplace_back("or", "(" + Joined(conditions) + ")");
        return *this;
    }
    /// One condition inside Or / And: "column.op.value" (`op` one of PostgREST's operators: eq, neq, gt, gte, lt, lte, like,
    /// ilike, is). The value is written like an item of In.
    static std::string Cond(const std::string& column, const std::string& op, const std::string& value, ValueText t = ValueText::Encoded) {
        return Name(column) + "." + Name(op) + "." + ListItem(value, t);
    }
    /// Conditions that must all hold, inside Or: "and(a,b)".
    static std::string And(const std::vector<std::string>& conditions) { return "and(" + Joined(conditions) + ")"; }
    Query& Is(const std::string& column, IsValue v) {
        static const char* const words[] = {"null", "true", "false", "unknown"};
        parts_.emplace_back(Name(column), std::string("is.") + words[(int)v]);
        return *this;
    }
    /// Sort by `column` (descending when asked). A second Order adds a tie-breaker to the same "order=" part.
    Query& Order(const std::string& column, bool descending = false) {
        const std::string item = Name(column) + (descending ? ".desc" : ".asc");
        for (auto& p : parts_) if (p.first == "order") { p.second += "," + item; return *this; }
        parts_.emplace_back("order", item);
        return *this;
    }
    /// At most `n` rows. Written as given (a negative number is the server's to refuse, as it always was).
    Query& Limit(long long n) { parts_.emplace_back("limit", std::to_string(n)); return *this; }
    /// Skip the first `n` rows.
    Query& Offset(long long n) { parts_.emplace_back("offset", std::to_string(n)); return *this; }
    /// Rows `first` to `last`, both counted from 0 and both included: "offset=first&limit=last-first+1" (no rows when
    /// last < first; negative numbers count as 0).
    Query& Range(long long first, long long last) {
        if (first < 0) first = 0;
        if (last < first) last = first - 1;
        Offset(first);
        const long long span = last - first;   // >= -1, never overflows (both are >= -1 here)
        return Limit(span < 0x7fffffffffffffffLL ? span + 1 : span);
    }
    /// The answer is one JSON object instead of an array (the server refuses when the filters match more or fewer rows).
    Query& Single() { single_ = true; return *this; }
    /// For Upsert: the columns of the unique key that decides "same row" ("on_conflict=board,user_id").
    Query& OnConflict(const std::string& columns) { parts_.emplace_back("on_conflict", Name(columns)); return *this; }
    /// What an Insert / Upsert / Update answers with.
    Query& Returning(Return r) { return_ = r; return *this; }
    /// Any other "Prefer" option ("count=exact"), sent after the resolution and return options.
    Query& Prefer(const std::string& option) { prefer_.push_back(option); return *this; }

    /// The request each call sends. Public so tests can read the exact text without sending anything.
    DbRequest Build(const std::string& method, const std::string& body = {}, bool upsert = false) const {
        DbRequest r;
        r.method = method;
        r.path = Name(table_);
        for (std::size_t i = 0; i < parts_.size(); ++i)
            r.path += (i ? "&" : "?") + parts_[i].first + "=" + parts_[i].second;
        r.body = body;
        std::string prefer;
        auto add = [&prefer](const std::string& o) { prefer += (prefer.empty() ? "" : ",") + o; };
        if (upsert) add("resolution=merge-duplicates");
        if (return_ == Return::Minimal) add("return=minimal");
        else if (return_ == Return::Representation) add("return=representation");
        for (const std::string& o : prefer_) add(o);
        if (!prefer.empty()) r.headers = "Prefer: " + prefer;
        if (single_) r.headers += (r.headers.empty() ? "" : "\n") + std::string("Accept: application/vnd.pgrst.object+json");
        return r;
    }

    inline Response Get();
    /// Add rows: `json` is one object or an array of objects.
    inline Response Insert(const std::string& json);
    /// Add rows, or change the rows that have the same key ("Prefer: resolution=merge-duplicates").
    inline Response Upsert(const std::string& json);
    /// Change the rows the filters match: `json` is the columns to set.
    inline Response Update(const std::string& json);
    /// Remove the rows the filters match.
    inline Response Delete();

    /// A table or column name as written into the address: RFC 3986's unreserved characters (letters, digits, "- _ . ~")
    /// and the ones PostgREST uses in select and order lists (", * ( ) : !" and ">") are kept; every other byte is
    /// percent-encoded, so a name can never end the part it is in ("&", "=", "#", "?", space).
    static std::string Name(const std::string& s) {
        std::string o;
        o.reserve(s.size());
        for (char c : s) {
            const bool keep = c == ',' || c == '*' || c == '(' || c == ')' || c == ':' || c == '!' || c == '>';
            o += keep ? std::string(1, c) : okay::account::detail::PercentEncode(std::string(1, c));   // (keeps A-Z a-z 0-9 - _ . ~)
        }
        return o;
    }

private:
    Database* db_;
    std::string table_;
    std::vector<std::pair<std::string, std::string>> parts_;
    std::vector<std::string> prefer_;
    Return return_ = Return::Unset;
    bool single_ = false;

    Query& Filter(const std::string& column, const char* op, const std::string& value, ValueText t) {
        parts_.emplace_back(Name(column), std::string(op) + "." +
                                              (t == ValueText::AsIs ? value : okay::account::detail::PercentEncode(value)));
        return *this;
    }
    static std::string ListItem(const std::string& v, ValueText t) {
        if (t == ValueText::AsIs) return v;
        return okay::account::detail::PercentEncode(t == ValueText::EncodedUnquoted ? v : InItem(v));
    }
    static std::string Joined(const std::vector<std::string>& items) {
        std::string s;
        for (std::size_t i = 0; i < items.size(); ++i) s += (i ? "," : "") + items[i];
        return s;
    }
    static std::string InItem(const std::string& v) {
        bool quote = v.empty() || v.front() == ' ' || v.back() == ' ';
        for (char c : v) if (c == ',' || c == '(' || c == ')' || c == '"' || c == '\\') { quote = true; break; }
        if (!quote) return v;
        std::string o = "\"";
        for (char c : v) { if (c == '"' || c == '\\') o += '\\'; o += c; }
        return o + "\"";
    }
};

/// The Database calls. A provider sends a DbRequest (the Supabase provider under "/rest/v1/") and runs functions.
class Database {
public:
    virtual ~Database() = default;
    /// Start a request on `table`.
    Query From(const std::string& table) { return Query(*this, table); }
    /// Send a built request.
    virtual Response Send(const DbRequest& request) = 0;
    /// Call the database function `function` with `json` as its named arguments ({"p_game":"..."}).
    virtual Response Rpc(const std::string& function, const std::string& json) = 0;
};

inline Response Query::Get() { return db_->Send(Build("GET")); }
inline Response Query::Insert(const std::string& json) { return db_->Send(Build("POST", json)); }
inline Response Query::Upsert(const std::string& json) { return db_->Send(Build("POST", json, true)); }
inline Response Query::Update(const std::string& json) { return db_->Send(Build("PATCH", json)); }
inline Response Query::Delete() { return db_->Send(Build("DELETE")); }

} // namespace okay::backend
