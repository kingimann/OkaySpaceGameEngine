#pragma once
// Live Updates storage provider over OkayBackend Storage (okay/Backend/Storage.hpp): the content of one game lives in a bucket under a
// folder prefix, and the sync and the publisher use it through the same Transport interface as a local folder (ContentTransport.hpp).
// Which service answers is the Storage provider's business (OkayBackend picks it); nothing here names one. Design note:
// docs/design/live_updates.md.
//
//   Get    ReadPublic: the whole object (status 200). A byte range is not asked for: the sync treats a 200 to a resumed file as "start
//          that file again" (ContentSync.hpp), so resume is lost with this provider, not correctness. maxBytes is enforced here.
//   List   Storage::List page by page (100 a page), going into folders (a folder entry has no "id"); size from "metadata.size" when the
//          answer has it, else 0 (unknown).
//   Put    Upload with replace (upsert). The manifest and its signature are sent with a short cache time so clients see a new version
//          soon; content files keep the server's default. Known gap: a CHANGED file under the same name may be served stale by a cache
//          for a while; the sync refuses those bytes by their SHA-256 and the update waits (correct, but slow). Next round: store files
//          under their hash (see the design note).
//
// Written for this project from the Storage interface's own contract. Header only, no threads (rule 21a).
#include <algorithm>
#include <string>
#include <vector>

#include "ContentTransport.hpp"
#include "ContentManifest.hpp"
#include "okay/Backend/Storage.hpp"
#include "okay/Core/Json.hpp"

namespace okaycontent {

class StorageTransport : public Transport {
public:
    /// `prefix` is the folder of this game's content in the bucket ("" or "mygame/"; a missing trailing '/' is added).
    StorageTransport(okay::backend::Storage& storage, std::string bucket, std::string prefix, int listCacheSeconds = 30)
        : m_s(storage), m_bucket(std::move(bucket)), m_prefix(std::move(prefix)), m_manifestCache(listCacheSeconds) {
        if (!m_prefix.empty() && m_prefix.back() != '/') m_prefix += '/';
    }

    GetResult Get(const std::string& name, std::uint64_t rangeFrom, std::uint64_t maxBytes, const GetHandlers& h) override {
        (void)rangeFrom;   // not supported: answered with the whole object (200)
        GetResult r;
        const okay::backend::Response resp = m_s.ReadPublic(m_bucket, m_prefix + JoinUrl("", name).substr(1));
        if (!resp.reached) { r.error = resp.error.empty() ? "no answer from the storage" : resp.error; return r; }
        GetHead head;
        head.status = static_cast<int>(resp.status);
        if (resp.ok) { head.status = 200; head.totalSize = resp.body.size(); }
        if (h.onHead && !h.onHead(head)) { r.aborted = true; return r; }
        if (!resp.ok) { r.reached = true; return r; }
        if (maxBytes && resp.body.size() > maxBytes) { r.error = "the body is larger than allowed"; return r; }
        const std::size_t piece = 64 * 1024;
        for (std::size_t i = 0; i < resp.body.size(); i += piece) {
            const std::size_t n = std::min(piece, resp.body.size() - i);
            if (h.onData && !h.onData(reinterpret_cast<const std::uint8_t*>(resp.body.data() + i), n)) { r.aborted = true; return r; }
        }
        r.reached = true;
        return r;
    }
    const char* Describe() const override { return "backend-storage"; }

    StoreResult List(const std::string& prefix, std::vector<Listed>& out) override {
        out.clear();
        StoreResult r;
        std::vector<std::string> folders = {std::string()};   // relative to m_prefix
        for (std::size_t f = 0; f < folders.size(); ++f) {
            if (folders.size() > 10000) { r.error = "too many folders"; return r; }
            for (int offset = 0;; offset += kPage) {
                okay::backend::ListOptions lo;
                lo.prefix = m_prefix + folders[f];
                lo.limit = kPage;
                lo.offset = offset;
                const okay::backend::Response resp = m_s.List(m_bucket, lo);
                if (!resp.ok) { r.error = resp.reached ? "the storage answered " + std::to_string(resp.status) : (resp.error.empty() ? "no answer" : resp.error); return r; }
                okay::json::Value v;
                std::string why;
                if (!okay::json::Parse(resp.body, v, &why) || !v.IsArray()) { r.error = "the storage's listing is not a JSON array" + (why.empty() ? std::string() : ": " + why); return r; }
                for (const okay::json::Value& item : v.array) {
                    const okay::json::Value* nm = item.Get("name");
                    if (!nm || !nm->IsString() || nm->string.empty()) continue;
                    const std::string rel = folders[f] + nm->string;
                    const okay::json::Value* id = item.Get("id");
                    if (!id || id->IsNull()) { folders.push_back(rel + "/"); continue; }   // a folder
                    if (rel.compare(0, prefix.size(), prefix) != 0) continue;
                    Listed l;
                    l.name = rel;
                    if (const okay::json::Value* md = item.Get("metadata")) if (const okay::json::Value* sz = md->Get("size")) sz->AsIndex(l.size);
                    out.push_back(l);
                }
                if (v.array.size() < static_cast<std::size_t>(kPage)) break;
            }
        }
        std::sort(out.begin(), out.end(), [](const Listed& a, const Listed& b) { return a.name < b.name; });
        r.ok = true;
        return r;
    }

    StoreResult Put(const std::string& name, const std::string& localFile) override {
        StoreResult r;
        okay::backend::UploadOptions o;
        o.upsert = true;
        if (name == kManifestName || name == kSigName) o.cacheSeconds = m_manifestCache;
        const okay::backend::Response resp = m_s.Upload(m_bucket, m_prefix + JoinUrl("", name).substr(1), localFile, std::string(), o);
        if (!resp.ok) { r.error = resp.reached ? "the storage answered " + std::to_string(resp.status) : (resp.error.empty() ? "no answer" : resp.error); return r; }
        r.ok = true;
        return r;
    }

private:
    static constexpr int kPage = 100;
    okay::backend::Storage& m_s;
    std::string m_bucket, m_prefix;
    int m_manifestCache;
};

}  // namespace okaycontent
