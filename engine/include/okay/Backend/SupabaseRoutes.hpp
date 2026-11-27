// OkayBackend: the Supabase provider's Storage and Functions route texts, and nothing else (docs/platform/okay_backend.md,
// section 17). Pure string functions with no dependencies beyond <string>, so the updater (launcher/VersionsManager.cpp,
// which is also built into the editor and several small tests) can use the releases address without the account service.
//
// Written from Supabase's publicly documented HTTP routes for Storage (an object at /storage/v1/object/<bucket>/<path>, its
// public read at /storage/v1/object/public/<bucket>/<path>, a listing at POST /storage/v1/object/list/<bucket>, a removal of
// several objects at DELETE /storage/v1/object/<bucket>) and Edge Functions (POST /functions/v1/<name>). These routes are an
// interface we call; no Supabase client library or source code is used.
//
// Paths and names are written as given: the caller encodes them (the programs always did; see okay/Backend/Storage.hpp).
#pragma once

#include <string>

namespace okay::backend::supabase::routes {

/// The start every Storage object route shares ("/storage/v1/object/"). Also how the launcher recognises a link to a stored
/// file in a chat message.
inline const char* StorageObjectRoot() { return "/storage/v1/object/"; }
/// One object, for an upload or a single removal: "/storage/v1/object/<bucket>/<path>".
inline std::string StorageObject(const std::string& bucket, const std::string& path) {
    return StorageObjectRoot() + bucket + "/" + path;
}
/// The bucket itself, for removing several objects in one request: "/storage/v1/object/<bucket>".
inline std::string StorageBucket(const std::string& bucket) { return StorageObjectRoot() + bucket; }
/// A public read: "/storage/v1/object/public/<bucket>/<path>" (with `path` "", the bucket's public folder, ending in "/").
inline std::string StoragePublic(const std::string& bucket, const std::string& path) {
    return StorageObjectRoot() + std::string("public/") + bucket + "/" + path;
}
/// A listing: "/storage/v1/object/list/<bucket>".
inline std::string StorageList(const std::string& bucket) { return StorageObjectRoot() + std::string("list/") + bucket; }
/// An Edge Function: "/functions/v1/<name>".
inline std::string Function(const std::string& name) { return "/functions/v1/" + name; }

} // namespace okay::backend::supabase::routes
