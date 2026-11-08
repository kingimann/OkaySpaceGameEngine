// AssetDatabase: the search / filter / sort / page query an asset browser sits on.
#include "okay/Core/AssetDatabase.hpp"
#include "AssetDatabaseInternal.hpp"
#include <algorithm>
#include <chrono>
#include <set>
#include <unordered_set>

namespace okay {

using adb::Lower;
using adb::StartsWith;

namespace {
std::int64_t NowSeconds() {
    using namespace std::chrono;
    return static_cast<std::int64_t>(duration_cast<seconds>(system_clock::now().time_since_epoch()).count());
}
std::string TrimFolder(std::string f) {
    for (char& c : f) if (c == '\\') c = '/';
    while (!f.empty() && f.back() == '/') f.pop_back();
    while (StartsWith(f, "./")) f.erase(0, 2);
    return f;
}
} // namespace

AssetPage AssetDatabase::Search(const AssetQuery& q) const {
    const std::string text = Lower(q.text);
    const std::string folder = Lower(TrimFolder(q.folder));
    std::set<AssetType> types(q.types.begin(), q.types.end());
    std::set<std::string> exts;
    for (const auto& e : q.extensions) {
        std::string x = Lower(e);
        if (!x.empty() && x[0] == '.') x.erase(0, 1);
        if (!x.empty()) exts.insert(x);
    }
    std::vector<std::string> labels;
    for (const auto& l : q.labels) if (!l.empty()) labels.push_back(Lower(l));
    const std::int64_t now = q.now != 0 ? q.now : NowSeconds();
    std::int64_t since = q.modifiedAfter;
    if (q.modifiedWithinSeconds > 0) since = std::max(since, now - q.modifiedWithinSeconds);

    // Graph filters are computed once.
    std::unordered_set<Guid, GuidHash> usersOf, depsOf;
    const bool filterUsers = !q.referencing.IsNull(), filterDeps = !q.referencedBy.IsNull();
    if (filterUsers) for (const Guid& g : UsedBy(q.referencing, q.transitive)) usersOf.insert(g);
    if (filterDeps) for (const Guid& g : DependenciesOf(q.referencedBy, q.transitive)) depsOf.insert(g);
    if (q.unusedOnly) BuildReverse();

    std::vector<const Slot*> hits;
    for (const auto& kv : m_slots) {
        const Slot& s = kv.second;
        const AssetRecord& r = s.rec;
        if (!types.empty() && !types.count(r.kind)) continue;
        if (!exts.empty() && !exts.count(r.Extension())) continue;
        if (!folder.empty()) {
            const std::string lp = Lower(r.path);
            if (!StartsWith(lp, folder + "/")) continue;
            if (!q.recursive && lp.find('/', folder.size() + 1) != std::string::npos) continue;
        }
        if (!text.empty()) {
            const std::string hay = Lower(q.matchFolders ? r.path : r.Name());
            if (hay.find(text) == std::string::npos) continue;
        }
        if (since > 0 && r.modified < since) continue;
        if (!labels.empty()) {
            bool any = false, all = true;
            for (const auto& l : labels) { const bool has = r.HasLabel(l); any = any || has; all = all && has; }
            if (q.anyLabel ? !any : !all) continue;
        }
        if (filterUsers && !usersOf.count(r.guid)) continue;
        if (filterDeps && !depsOf.count(r.guid)) continue;
        if (q.unusedOnly && m_reverse.count(r.guid)) continue;
        if (q.withMissingRefsOnly) {
            bool missing = false;
            for (const Edge& e : s.edges) if (e.missing) { missing = true; break; }
            if (!missing) continue;
        }
        hits.push_back(&s);
    }

    auto nameKey = [](const AssetRecord& r) { return Lower(r.Name()); };
    auto cmp = [&](const Slot* a, const Slot* b) {
        const AssetRecord& x = a->rec;
        const AssetRecord& y = b->rec;
        int c = 0;
        switch (q.sort) {
            case AssetQuery::Sort::Name: { const std::string nx = nameKey(x), ny = nameKey(y); c = nx < ny ? -1 : nx > ny ? 1 : 0; break; }
            case AssetQuery::Sort::Path: { const std::string px = Lower(x.path), py = Lower(y.path); c = px < py ? -1 : px > py ? 1 : 0; break; }
            case AssetQuery::Sort::Type: c = static_cast<int>(x.kind) - static_cast<int>(y.kind); break;
            case AssetQuery::Sort::Modified: c = x.modified < y.modified ? -1 : x.modified > y.modified ? 1 : 0; break;
            case AssetQuery::Sort::Size: c = x.size < y.size ? -1 : x.size > y.size ? 1 : 0; break;
        }
        if (q.descending) c = -c;
        if (c != 0) return c < 0;
        if (q.sort != AssetQuery::Sort::Name) {   // second key: name, then path
            const std::string nx = nameKey(x), ny = nameKey(y);
            if (nx != ny) return nx < ny;
        }
        return x.path < y.path;
    };
    std::sort(hits.begin(), hits.end(), cmp);

    AssetPage page;
    page.total = hits.size();
    page.pageSize = q.pageSize;
    page.pageCount = q.pageSize == 0 ? 1 : std::max<std::size_t>(1, (hits.size() + q.pageSize - 1) / q.pageSize);
    page.page = std::min(q.page, page.pageCount - 1);
    std::size_t from = 0, to = hits.size();
    if (q.pageSize > 0) {
        from = std::min(hits.size(), page.page * q.pageSize);
        to = std::min(hits.size(), from + q.pageSize);
    }
    for (std::size_t i = from; i < to; ++i) page.items.push_back(&hits[i]->rec);
    return page;
}

std::map<AssetType, std::size_t> AssetDatabase::CountByType() const {
    std::map<AssetType, std::size_t> out;
    for (const auto& kv : m_slots) ++out[kv.second.rec.kind];
    return out;
}

std::map<AssetType, std::size_t> AssetDatabase::CountByType(const AssetQuery& query) const {
    AssetQuery q = query;
    q.pageSize = 0;
    q.page = 0;
    q.types.clear();   // counts per kind are only useful across types
    std::map<AssetType, std::size_t> out;
    for (const AssetRecord* r : Search(q).items) ++out[r->kind];
    return out;
}

} // namespace okay
