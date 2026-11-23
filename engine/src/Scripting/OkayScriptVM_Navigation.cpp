#include "OkayScriptVMInternal.hpp"

namespace okay {

void OkayScriptVM::Impl::RegisterNavigationBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto sceneOf = c.sceneOf;
        // ---- Navigation: the Nav Mesh Agent on this object (or a named one) --------
        // nav_move_to(x, y, z) / nav_move_to("Name", x, y, z): walk there over the
        // navmesh; nav_stop(), nav_arrived() -> 1 once the point was reached,
        // nav_remaining() -> path length left (0 when idle).
        auto navOf = [this, sceneOf](std::vector<Value>& a, std::size_t& first) -> NavMeshAgent* {
            first = 0;
            GameObject* g = rt.host ? rt.host->gameObject : nullptr;
            if (!a.empty() && a[0].IsString()) {
                first = 1;
                Scene* s = sceneOf();
                g = s ? ResolveObject(s, a[0]) : nullptr;
            }
            return g ? g->GetComponent<NavMeshAgent>() : nullptr;
        };
        b["nav_move_to"] = [navOf](std::vector<Value>& a) {
            std::size_t i = 0; NavMeshAgent* n = navOf(a, i);
            if (!n || a.size() < i + 3) return Value{0.0f};
            n->SetDestination({a[i].AsFloat(), a[i + 1].AsFloat(), a[i + 2].AsFloat()});
            return Value{1.0f};
        };
        b["nav_stop"] = [navOf](std::vector<Value>& a) {
            std::size_t i = 0; NavMeshAgent* n = navOf(a, i);
            if (!n) return Value{0.0f};
            n->Stop(); return Value{1.0f};
        };
        b["nav_arrived"] = [navOf](std::vector<Value>& a) {
            std::size_t i = 0; NavMeshAgent* n = navOf(a, i);
            return Value{n && n->Arrived() ? 1.0f : 0.0f};
        };
        b["nav_remaining"] = [navOf](std::vector<Value>& a) {
            std::size_t i = 0; NavMeshAgent* n = navOf(a, i);
            return Value{n ? n->RemainingDistance() : 0.0f};
        };
}

void OkayScriptVM::Impl::RegisterGridPathBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto sceneOf = c.sceneOf;
        // ---- 2D grid paths: the Grid Path Agent on this object (or a named one) ----
        // grid_move_to(x, y) walks to the cell that contains that world point.
        // grid_move_cell(x, y) walks to that cell. A leading name picks another object.
        auto gridOf = [this, sceneOf](std::vector<Value>& a, std::size_t& first) -> GridPathAgent2D* {
            first = 0;
            GameObject* g = rt.host ? rt.host->gameObject : nullptr;
            if (!a.empty() && a[0].IsString()) {
                first = 1;
                Scene* s = sceneOf();
                g = s ? ResolveObject(s, a[0]) : nullptr;
            }
            return g ? g->GetComponent<GridPathAgent2D>() : nullptr;
        };
        auto namedObject = [this, sceneOf](std::vector<Value>& a, std::size_t& first) -> GameObject* {
            first = 0;
            GameObject* g = rt.host ? rt.host->gameObject : nullptr;
            if (!a.empty() && a[0].IsString()) {
                first = 1;
                Scene* s = sceneOf();
                g = s ? ResolveObject(s, a[0]) : nullptr;
            }
            return g;
        };
        b["grid_move_to"] = [gridOf](std::vector<Value>& a) {
            std::size_t i = 0; GridPathAgent2D* n = gridOf(a, i);
            if (!n || a.size() < i + 2) return Value{0.0f};
            n->SetDestination(a[i].AsFloat(), a[i + 1].AsFloat());
            return Value{1.0f};
        };
        b["grid_move_cell"] = [gridOf](std::vector<Value>& a) {
            std::size_t i = 0; GridPathAgent2D* n = gridOf(a, i);
            if (!n || a.size() < i + 2) return Value{0.0f};
            n->SetCell((int)a[i].AsFloat(), (int)a[i + 1].AsFloat());
            return Value{1.0f};
        };
        b["grid_stop"] = [gridOf](std::vector<Value>& a) {
            std::size_t i = 0; GridPathAgent2D* n = gridOf(a, i);
            if (!n) return Value{0.0f};
            n->Stop(); return Value{1.0f};
        };
        b["grid_arrived"] = [gridOf](std::vector<Value>& a) {
            std::size_t i = 0; GridPathAgent2D* n = gridOf(a, i);
            return Value{n && n->Arrived() ? 1.0f : 0.0f};
        };
        b["grid_set_tile_cost"] = [namedObject](std::vector<Value>& a) {
            std::size_t i = 0; GameObject* g = namedObject(a, i);
            if (!g || a.size() < i + 2) return Value{0.0f};
            auto* c = g->GetComponent<GridCostLayer>();
            if (!c) c = g->AddComponent<GridCostLayer>();
            c->SetTileCost((int)a[i].AsFloat(), a[i + 1].AsFloat());
            return Value{1.0f};
        };
        b["grid_set_cell_cost"] = [namedObject](std::vector<Value>& a) {
            std::size_t i = 0; GameObject* g = namedObject(a, i);
            if (!g || a.size() < i + 3) return Value{0.0f};
            auto* c = g->GetComponent<GridCostLayer>();
            if (!c) c = g->AddComponent<GridCostLayer>();
            c->SetCellCost((int)a[i].AsFloat(), (int)a[i + 1].AsFloat(), a[i + 2].AsFloat());
            return Value{1.0f};
        };
        b["grid_block"] = [namedObject](std::vector<Value>& a) {
            std::size_t i = 0; GameObject* g = namedObject(a, i);
            if (!g || a.size() < i + 2) return Value{0.0f};
            auto* o = g->GetComponent<GridObstacle2D>();
            if (!o) o = g->AddComponent<GridObstacle2D>();
            o->Place((int)a[i].AsFloat(), (int)a[i + 1].AsFloat());
            return Value{1.0f};
        };
        b["grid_clear"] = [namedObject](std::vector<Value>& a) {
            std::size_t i = 0; GameObject* g = namedObject(a, i);
            if (!g) return Value{0.0f};
            auto* o = g->GetComponent<GridObstacle2D>();
            if (!o) return Value{0.0f};
            o->Clear();
            return Value{1.0f};
        };
}

void OkayScriptVM::Impl::RegisterNavmeshQueryBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto sceneOf = c.sceneOf;
        // ---- Navmesh queries (no agent needed) ----------------------------------
        // nav_path_exists(x, y, z) -> 1 when a walkable route leads from this object to
        // the point (nav_path_exists(fx, fy, fz, tx, ty, tz) between two points);
        // nav_path_length(...) -> that route's length (-1 when there is none);
        // nav_point_walkable(x, y, z[, maxDist]) -> 1 when the baked navmesh has ground
        // within maxDist (default 1); nav_nearest_point(x, y, z[, maxDist]) -> the closest
        // walkable point as a vector (the point itself when there is none within range).
        // The scene's baked navmesh is used, else the sampled grid (as agents do).
        auto navRoute = [this, sceneOf](std::vector<Value>& a, std::vector<Vec3>& path) -> bool {
            Scene* s = sceneOf();
            if (!s) return false;
            Vec3 from{}, to{};
            if (a.size() >= 6) {
                from = {a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()};
                to   = {a[3].AsFloat(), a[4].AsFloat(), a[5].AsFloat()};
            } else if (a.size() >= 3 && rt.host && rt.host->transform) {
                from = rt.host->transform->Position();
                to   = {a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()};
            } else return false;
            return Navigation::FindPath(*s, from, to, path, 0.4f, rt.host ? rt.host->gameObject : nullptr) && !path.empty();
        };
        b["nav_path_exists"] = [navRoute](std::vector<Value>& a) {
            std::vector<Vec3> path;
            return Value{navRoute(a, path) ? 1.0f : 0.0f};
        };
        b["nav_path_length"] = [navRoute](std::vector<Value>& a) {
            std::vector<Vec3> path;
            if (!navRoute(a, path)) return Value{-1.0f};
            float len = 0.0f;
            for (std::size_t i = 1; i < path.size(); ++i) len += (path[i] - path[i - 1]).Magnitude();
            return Value{len};
        };
        auto navSample = [sceneOf](std::vector<Value>& a, Vec3& p, Vec3& out) -> bool {
            Scene* s = sceneOf();
            if (!s || a.size() < 3) return false;
            p = {a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()};
            return Navigation::SamplePosition(*s, p, a.size() > 3 ? a[3].AsFloat() : 1.0f, out);
        };
        b["nav_point_walkable"] = [navSample](std::vector<Value>& a) {
            Vec3 p, out;
            return Value{navSample(a, p, out) ? 1.0f : 0.0f};
        };
        b["nav_nearest_point"] = [navSample](std::vector<Value>& a) {
            Vec3 p{}, out;
            if (a.size() < 3) return Value{Vec3::Zero};
            Vec3 q{a[0].AsFloat(), a[1].AsFloat(), a[2].AsFloat()};
            std::vector<Value> b2 = a;
            if (b2.size() < 4) b2.push_back(Value{50.0f});   // "nearest" looks further than "walkable"
            return Value{navSample(b2, p, out) ? out : q};
        };
}

void OkayScriptVM::Impl::RegisterBehaviorTreeBuiltins(BuiltinCtx& c) {
        auto& b = c.b;
        auto go = c.go;
        auto sceneOf = c.sceneOf;
        // ---- Behavior trees: the Behavior Tree Runner on this object (or a named one) ----
        // bt_set(key, value) / bt_set("Name", key, value) writes the blackboard,
        // bt_get(key) / bt_get("Name", key) reads it, bt_run("Assets/Guard.okaybt")
        // loads and starts a tree (bt_run() starts the current one over), bt_stop().
        auto btOf = [sceneOf, go](std::vector<Value>& a, std::size_t need, std::size_t& first) -> BehaviorTreeRunner* {
            first = 0;
            GameObject* g = go();
            if (a.size() > need && a[0].IsString()) {
                first = 1;
                Scene* s = sceneOf();
                g = s ? ResolveObject(s, a[0]) : nullptr;
            }
            return g ? g->GetComponent<BehaviorTreeRunner>() : nullptr;
        };
        b["bt_set"] = [btOf](std::vector<Value>& a) {
            std::size_t i = 0; BehaviorTreeRunner* r = btOf(a, 2, i);
            if (!r || a.size() < i + 2) return Value{0.0f};
            r->blackboard.Set(a[i].AsString(), BtValue::FromVs(a[i + 1]));
            return Value{1.0f};
        };
        b["bt_get"] = [btOf](std::vector<Value>& a) -> Value {
            std::size_t i = 0; BehaviorTreeRunner* r = btOf(a, 1, i);
            if (!r || a.size() < i + 1) return Value{};
            const BtValue* v = r->blackboard.Find(a[i].AsString());
            return v ? v->ToVs() : Value{};
        };
        b["bt_run"] = [go](std::vector<Value>& a) {
            GameObject* g = go();
            if (!g) return Value{0.0f};
            auto* r = g->GetComponent<BehaviorTreeRunner>();
            if (!r) r = g->AddComponent<BehaviorTreeRunner>();
            if (!a.empty() && !a[0].AsString().empty()) {
                std::string err;
                if (!r->LoadTree(a[0].AsString(), &err)) { Log::Warning("[script] bt_run: ", err); return Value{0.0f}; }
            }
            r->Run();
            return Value{r->IsRunning() ? 1.0f : 0.0f};
        };
        b["bt_stop"] = [go](std::vector<Value>&) {
            GameObject* g = go();
            auto* r = g ? g->GetComponent<BehaviorTreeRunner>() : nullptr;
            if (r) r->Stop();
            return Value{r ? 1.0f : 0.0f};
        };
}

} // namespace okay
