// OkayBackend Storage: files in buckets (docs/platform/okay_backend.md, sections 4, 7.4 and 17).
//
// The buckets the programs use: "chat" (attachments, custom emoji and stickers, public read), "workshop" (workshop listings'
// files, public read), "releases" (the updater's files, and official/ for the official game; public read) and "game-content"
// (written only by scripts/publish-content.sh). What may be written where is decided by the server's rules
// (docs/sql/chat_media.sql, docs/sql/social.sql, docs/sql/official_game_admin.sql), never here.
//
// Object paths are sent as given: the caller encodes them (as the programs always did), so a path with "/" keeps its folders.
// A provider never sends a request twice by itself.
//
// Written from the behaviour the programs need (upload with replace, public read, listing with paging, removal) and Supabase
// Storage's publicly documented object routes, which the Supabase provider sends (okay/Backend/Supabase.hpp). Header-only and
// free of <mutex> / <thread> (CLAUDE.md rule 21a).
#pragma once

#include "okay/Backend/Transport.hpp"   // Response

#include <string>
#include <vector>

namespace okay::backend {

/// How an upload is stored.
struct UploadOptions {
    bool upsert = false;      ///< replace an object that is already there (otherwise the server refuses it)
    int  cacheSeconds = -1;   ///< how long a public read may be cached ("cache-control: max-age=N"); below 0: the server's default
};

/// One page of a listing: the objects and folders directly under `prefix` (a folder path ending in "/", or ""), sorted.
struct ListOptions {
    std::string prefix;
    int  limit = 100;
    int  offset = 0;
    std::string sortColumn = "name";
    bool descending = false;
};

/// The Storage calls every provider answers.
class Storage {
public:
    virtual ~Storage() = default;
    /// Upload the file `localFile` as the object `path` in `bucket`. `contentType` "" sends "application/octet-stream".
    virtual Response Upload(const std::string& bucket, const std::string& path, const std::string& localFile,
                            const std::string& contentType, const UploadOptions& options = {}) = 0;
    /// Read a public object (the same bytes anyone gets from PublicUrl). `query` ("" = none) is added after a "?", for
    /// example a cache-buster.
    virtual Response ReadPublic(const std::string& bucket, const std::string& path, const std::string& query = {}) = 0;
    /// One page of the listing under options.prefix. The answer is a JSON array; a folder has no "id".
    virtual Response List(const std::string& bucket, const ListOptions& options) = 0;
    /// Remove several objects in one request. The answer lists the objects that were removed.
    virtual Response Remove(const std::string& bucket, const std::vector<std::string>& paths) = 0;
    /// Remove one object.
    virtual Response RemoveObject(const std::string& bucket, const std::string& path) = 0;
    /// The address anyone can read a public object at. Pure: nothing is sent.
    virtual std::string PublicUrl(const std::string& bucket, const std::string& path) const = 0;
};

} // namespace okay::backend
