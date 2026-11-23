// Typed script API, PostProcess, Wind and navigation: the Bind<Topic> functions of the binding table (see EngineApiInternal.hpp).
#include "EngineApiInternal.hpp"

namespace okay::script::engine_api {

void Ctx::BindRendering() {
    // ---- PostProcess / Fog / Weather: the scene's look (docs/rendering/rendering.md) ----
    {
        auto look = [](CallContext& c) -> Scene::RenderSettings& {
            Scene* s = c.self ? c.self->scene() : nullptr;
            if (!s) throw RuntimeError("This needs a script running in a scene", c.loc);
            return s->renderSettings;
        };
        ApiType pp; pp.name = "PostProcess"; pp.isStatic = true;
        pp.doc = "The scene's post-processing: exposure, tonemap, bloom, colour grading, vignette, FXAA, SSAO and depth of field (saved with the scene; the Lighting panel edits the same values).";
        pp.staticMethods.push_back(method("Set", {tString, tFloat}, {"name", "value"}, tBool,
            [look](const Value&, std::vector<Value>& a, CallContext& c) {
                const bool ok = PostSet(look(c), S(a[0]), F(a[1]));
                if (!ok) Log::Warning("[script] PostProcess.Set: unknown setting \"", S(a[0]), "\"");
                return Value{ok};
            }, "Set one value by name: \"exposure\", \"tonemap\" (0 none, 1 ACES, 2 filmic), \"bloom\" (0/1), \"bloom_threshold\", "
               "\"bloom_intensity\", \"bloom_radius\", \"bloom_softness\", \"grading\", \"temperature\", \"tint\", \"saturation\", \"contrast\", "
               "\"vignette\", \"vignette_smoothness\", \"fxaa\", \"ssao\", \"ssao_radius\", \"ssao_intensity\", \"ssao_power\", \"dof\", "
               "\"dof_focus\", \"dof_range\". False for a name it doesn't know."));
        pp.staticProps.push_back(prop("exposure", tFloat,
            [look](const Value&, CallContext& c) { return Value{look(c).post.exposure}; },
            [look](const Value&, const Value& v, CallContext& c) { PostSet(look(c), "exposure", F(v)); },
            "Scene brightness multiplier (1 = as lit)."));
        api.types.push_back(std::move(pp));

        ApiType fg; fg.name = "Fog"; fg.isStatic = true;
        fg.doc = "The scene's fog: linear (start / end), exponential or height fog, in the colour you set or the sky's.";
        fg.staticMethods.push_back(method("Set", {tBool, tFloat, tFloat}, {"enabled", "start", "end"}, tVoid,
            [look](const Value&, std::vector<Value>& a, CallContext& c) {
                auto& rs = look(c); rs.fog = std::get<bool>(a[0]); rs.fogStart = F(a[1]); rs.fogEnd = F(a[2]); return Value{}; },
            "Turn the fog on or off, fading from start to end (world units from the camera) in Linear mode."));
        fg.staticMethods.push_back(method("SetColor", {tFloat, tFloat, tFloat}, {"r", "g", "b"}, tVoid,
            [look](const Value&, std::vector<Value>& a, CallContext& c) {
                auto& rs = look(c); rs.fogColor = {Mathf::Clamp01(F(a[0])), Mathf::Clamp01(F(a[1])), Mathf::Clamp01(F(a[2])), 1.0f};
                rs.fogMatchSky = false; return Value{}; },
            "The fog colour (0 to 1 each). Stops matching the sky's horizon."));
        fg.staticMethods.push_back(method("SetMode", {tString, tFloat}, {"mode", "density"}, tVoid,
            [look](const Value&, std::vector<Value>& a, CallContext& c) {
                auto& rs = look(c); rs.fogMode = FogModeFromName(S(a[0])); rs.fogDensity = Mathf::Clamp(F(a[1]), 0.0f, 10.0f); return Value{}; },
            "\"linear\", \"exponential\" or \"height\", with the density the last two thicken by per unit of distance."));
        fg.staticProps.push_back(prop("enabled", tBool,
            [look](const Value&, CallContext& c) { return Value{look(c).fog}; },
            [look](const Value&, const Value& v, CallContext& c) { look(c).fog = std::get<bool>(v); },
            "Whether the fog is on."));
        api.types.push_back(std::move(fg));

        ApiType we; we.name = "Weather"; we.isStatic = true;
        we.doc = "Rain, snow and storms falling around the camera, and the wind particles and water follow.";
        we.staticMethods.push_back(method("Set", {tString, tFloat}, {"preset", "intensity"}, tVoid,
            [look](const Value&, std::vector<Value>& a, CallContext& c) {
                auto& w = look(c).weather; w.preset = WeatherPresetFromName(S(a[0])); w.intensity = Mathf::Clamp01(F(a[1])); return Value{}; },
            "\"none\", \"rain\", \"snow\" or \"storm\" at an intensity from 0 to 1; it cross-fades over the scene's weather fade time."));
        we.staticMethods.push_back(method("SetWind", {tFloat, tFloat, tFloat}, {"x", "y", "z"}, tVoid,
            [look](const Value&, std::vector<Value>& a, CallContext& c) { look(c).weather.wind = {F(a[0]), F(a[1]), F(a[2])}; return Value{}; },
            "The wind (world units per second) that particles drift with and water waves follow."));
        we.staticProps.push_back(ro("preset", tString,
            [look](const Value&, CallContext& c) { return Value{std::string(WeatherPresetName(look(c).weather.preset))}; },
            "The current preset's name."));
        api.types.push_back(std::move(we));
    }
    // ---- Wind (docs/gameplay/wind.md) ----
    {
        auto sceneOf = [](CallContext& c) -> Scene* {
            Scene* s = c.self ? c.self->scene() : nullptr;
            if (!s) throw RuntimeError("This needs a script running in a scene", c.loc);
            return s;
        };
        auto here = [](CallContext& c) -> Vec3 { return (c.self && c.self->transform) ? c.self->transform->Position() : Vec3{0, 0, 0}; };
        ApiType wi; wi.name = "Wind"; wi.isStatic = true;
        wi.doc = "The wind field: how hard and which way the air moves at a point (weather, wind zones and the scene's own wind together).";
        wi.staticProps.push_back(ro("Strength", tFloat,
            [here](const Value&, CallContext& c) { return Value{WindField::At(here(c)).Magnitude()}; },
            "The wind speed (metres per second) where this object is."));
        wi.staticProps.push_back(ro("Direction", tVec,
            [here](const Value&, CallContext& c) {
                const Vec3 w = WindField::At(here(c));
                return Value{w.SqrMagnitude() > 1e-10f ? w.Normalized() : Vec3{0, 0, 0}}; },
            "The way the wind blows where this object is, as a unit vector (0, 0, 0 when it is calm)."));
        ApiMethod at = method("At", {tVec}, {"position"}, tVec,
            [](const Value&, std::vector<Value>& a, CallContext&) { return Value{WindField::At(V(a[0]))}; },
            "The wind (speed and direction, metres per second) at a world position, right now.");
        at.pure = true; wi.staticMethods.push_back(std::move(at));
        wi.staticMethods.push_back(method("SetStrength", {tFloat}, {"metresPerSecond"}, tVoid,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                const float f = F(a[0]); if (std::isfinite(f)) WindZone::EnsureGlobal(*sceneOf(c))->strength = Mathf::Clamp(f, -200.0f, 200.0f); return Value{}; },
            "Set the scene's own wind speed (about 3 is a breeze, 10 a strong wind). Makes the wind object if the scene has none."));
        wi.staticMethods.push_back(method("SetDirection", {tVec}, {"direction"}, tVoid,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) { WindZone::EnsureGlobal(*sceneOf(c))->Aim(V(a[0])); return Value{}; },
            "Aim the scene's own wind along a direction, for example Wind.SetDirection(Vec3(1, 0, 0)) blows toward +X."));
        wi.staticMethods.push_back(method("SetGust", {tFloat, tFloat}, {"amount", "perSecond"}, tVoid,
            [sceneOf](const Value&, std::vector<Value>& a, CallContext& c) {
                WindZone* z = WindZone::EnsureGlobal(*sceneOf(c));
                if (std::isfinite(F(a[0]))) z->gustAmount = Mathf::Clamp01(F(a[0]));
                if (std::isfinite(F(a[1]))) z->gustFrequency = Mathf::Clamp(F(a[1]), 0.01f, 10.0f);
                return Value{}; },
            "How much the scene's wind swells and drops (0 steady, 1 calm to double) and how many gusts come each second (0.2 = one every 5 seconds)."));
        api.types.push_back(std::move(wi));
    }
}

void Ctx::BindNavigation() {
    // ---- NavMeshAgent ----
    {
        ApiType na; na.name = "NavMeshAgent"; na.isComponent = true;
        na.doc = "Walks the object along the scene's navigation mesh (the grid when none is baked). The object needs a Nav Mesh Agent.";
        na.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<NavMeshAgent>() : nullptr; };
        auto agentOf = [](const Value& self, CallContext& c) -> NavMeshAgent* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* a = g->GetComponent<NavMeshAgent>();
            if (!a) throw RuntimeError("'" + g->name + "' has no Nav Mesh Agent (add one in the Inspector)", c.loc);
            return a;
        };
        na.methods.push_back(method("SetDestination", {tVec}, {"point"}, tVoid,
            [agentOf](const Value& s, std::vector<Value>& a, CallContext& c) { agentOf(s, c)->SetDestination(V(a[0])); return Value{}; },
            "Plan a path to a world point and start walking there."));
        na.methods.push_back(method("Stop", {}, {}, tVoid,
            [agentOf](const Value& s, std::vector<Value>&, CallContext& c) { agentOf(s, c)->Stop(); return Value{}; },
            "Stop where it is (Resume continues to the same point)."));
        na.methods.push_back(method("Resume", {}, {}, tVoid,
            [agentOf](const Value& s, std::vector<Value>&, CallContext& c) { agentOf(s, c)->Resume(); return Value{}; },
            "Carry on to the destination after Stop."));
        ApiMethod hasPath = method("HasPath", {}, {}, tBool,
            [agentOf](const Value& s, std::vector<Value>&, CallContext& c) { return Value{agentOf(s, c)->HasPath()}; },
            "True while it is walking to a destination.");
        hasPath.pure = true; na.methods.push_back(std::move(hasPath));
        ApiMethod remaining = method("RemainingDistance", {}, {}, tFloat,
            [agentOf](const Value& s, std::vector<Value>&, CallContext& c) { return Value{agentOf(s, c)->RemainingDistance()}; },
            "Path length left to the destination (0 when there is none).");
        remaining.pure = true; na.methods.push_back(std::move(remaining));
        ApiMethod arrived = method("Arrived", {}, {}, tBool,
            [agentOf](const Value& s, std::vector<Value>&, CallContext& c) { return Value{agentOf(s, c)->Arrived()}; },
            "True once the last destination was reached (until the next SetDestination).");
        arrived.pure = true; na.methods.push_back(std::move(arrived));
        na.props.push_back(prop("speed", tFloat,
            [agentOf](const Value& s, CallContext& c) { return Value{agentOf(s, c)->speed}; },
            [agentOf](const Value& s, const Value& v, CallContext& c) { agentOf(s, c)->speed = std::max(0.0f, F(v)); },
            "Top speed in units per second."));
        na.props.push_back(prop("stoppingDistance", tFloat,
            [agentOf](const Value& s, CallContext& c) { return Value{agentOf(s, c)->stoppingDistance}; },
            [agentOf](const Value& s, const Value& v, CallContext& c) { agentOf(s, c)->stoppingDistance = std::max(0.0f, F(v)); },
            "How close to the destination counts as there."));
        na.props.push_back(ro("isStopped", tBool,
            [agentOf](const Value& s, CallContext& c) { return Value{agentOf(s, c)->IsStopped()}; },
            "True after Stop, until Resume or a new destination."));
        na.props.push_back(ro("velocity", tVec,
            [agentOf](const Value& s, CallContext& c) { return Value{agentOf(s, c)->Velocity()}; },
            "How it is moving right now, in units per second."));
        na.props.push_back(ro("destination", tVec,
            [agentOf](const Value& s, CallContext& c) { return Value{agentOf(s, c)->Destination()}; },
            "The point it was last sent to."));
        api.types.push_back(std::move(na));
    }
    // ---- 2D grid path agent, costs and obstacles ----
    {
        ApiType ag; ag.name = "GridPathAgent2D"; ag.isComponent = true;
        ag.doc = "Walks the object over a 2D tilemap. Empty tiles are open until Pay for mud and roads is on. The object needs a Grid Path Agent.";
        ag.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<GridPathAgent2D>() : nullptr; };
        auto agentOf = [](const Value& self, CallContext& c) -> GridPathAgent2D* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* a = g->GetComponent<GridPathAgent2D>();
            if (!a) throw RuntimeError("'" + g->name + "' has no Grid Path Agent (add one in the Inspector)", c.loc);
            return a;
        };
        ag.methods.push_back(method("SetDestination", {tFloat, tFloat}, {"x", "y"}, tVoid,
            [agentOf](const Value& s, std::vector<Value>& a, CallContext& c) { agentOf(s, c)->SetDestination(F(a[0]), F(a[1])); return Value{}; },
            "Plan a path to the cell that contains this world point, and start walking."));
        ag.methods.push_back(method("SetCell", {tInt, tInt}, {"x", "y"}, tVoid,
            [agentOf](const Value& s, std::vector<Value>& a, CallContext& c) { agentOf(s, c)->SetCell(I(a[0]), I(a[1])); return Value{}; },
            "Plan a path to this cell, and start walking."));
        ag.methods.push_back(method("Stop", {}, {}, tVoid,
            [agentOf](const Value& s, std::vector<Value>&, CallContext& c) { agentOf(s, c)->Stop(); return Value{}; },
            "Stop where it is and drop the path."));
        ApiMethod arrived = method("Arrived", {}, {}, tBool,
            [agentOf](const Value& s, std::vector<Value>&, CallContext& c) { return Value{agentOf(s, c)->Arrived()}; },
            "True once the last cell was reached.");
        arrived.pure = true; ag.methods.push_back(std::move(arrived));
        ApiMethod hasPath = method("HasPath", {}, {}, tBool,
            [agentOf](const Value& s, std::vector<Value>&, CallContext& c) { return Value{agentOf(s, c)->HasPath()}; },
            "True while it is walking a path.");
        hasPath.pure = true; ag.methods.push_back(std::move(hasPath));
        ag.props.push_back(prop("speed", tFloat,
            [agentOf](const Value& s, CallContext& c) { return Value{agentOf(s, c)->speed}; },
            [agentOf](const Value& s, const Value& v, CallContext& c) { agentOf(s, c)->speed = std::max(0.0f, F(v)); },
            "How fast it walks, in world units per second."));
        ag.props.push_back(prop("allowDiagonal", tBool,
            [agentOf](const Value& s, CallContext& c) { return Value{agentOf(s, c)->allowDiagonal}; },
            [agentOf](const Value& s, const Value& v, CallContext& c) { agentOf(s, c)->allowDiagonal = std::get<bool>(v); },
            "Also step diagonally. A diagonal step never squeezes between two walls."));
        ag.props.push_back(prop("smoothPath", tBool,
            [agentOf](const Value& s, CallContext& c) { return Value{agentOf(s, c)->smoothPath}; },
            [agentOf](const Value& s, const Value& v, CallContext& c) { agentOf(s, c)->smoothPath = std::get<bool>(v); },
            "Straighten the route wherever the line of sight is clear."));
        ag.props.push_back(prop("useCosts", tBool,
            [agentOf](const Value& s, CallContext& c) { return Value{agentOf(s, c)->useCosts}; },
            [agentOf](const Value& s, const Value& v, CallContext& c) { agentOf(s, c)->useCosts = std::get<bool>(v); },
            "Pay the tilemap's movement costs. Off keeps every open cell the same price."));
        api.types.push_back(std::move(ag));

        ApiType cost; cost.name = "GridCostLayer"; cost.isComponent = true;
        cost.doc = "Prices for a 2D tilemap: a road is cheap, mud is slow, and 0 blocks the tile. A Grid Path Agent pays them when Pay for mud and roads is on.";
        cost.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<GridCostLayer>() : nullptr; };
        auto costOf = [](const Value& self, CallContext& c) -> GridCostLayer* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* layer = g->GetComponent<GridCostLayer>();
            if (!layer) throw RuntimeError("'" + g->name + "' has no Grid Movement Costs (add one in the Inspector)", c.loc);
            return layer;
        };
        cost.methods.push_back(method("SetTileCost", {tInt, tFloat}, {"tile", "cost"}, tVoid,
            [costOf](const Value& s, std::vector<Value>& a, CallContext& c) { costOf(s, c)->SetTileCost(I(a[0]), F(a[1])); return Value{}; },
            "Set the price of one tile number. 0 or less makes that tile a wall. Agents on that tile plan again."));
        cost.methods.push_back(method("SetCellCost", {tInt, tInt, tFloat}, {"x", "y", "cost"}, tVoid,
            [costOf](const Value& s, std::vector<Value>& a, CallContext& c) { costOf(s, c)->SetCellCost(I(a[0]), I(a[1]), F(a[2])); return Value{}; },
            "Set the price of one cell, on top of its tile. 0 or less blocks it."));
        api.types.push_back(std::move(cost));

        ApiType block; block.name = "GridObstacle2D"; block.isComponent = true;
        block.doc = "Blocks cells on a 2D tilemap. An agent whose path runs through them plans a new route at once.";
        block.find = [](GameObject* go) -> Component* { return go ? go->GetComponent<GridObstacle2D>() : nullptr; };
        auto blockOf = [](const Value& self, CallContext& c) -> GridObstacle2D* {
            GameObject* g = Resolve(std::get<ObjRef>(self), c.loc);
            auto* o = g->GetComponent<GridObstacle2D>();
            if (!o) throw RuntimeError("'" + g->name + "' has no Grid Obstacle (add one in the Inspector)", c.loc);
            return o;
        };
        block.methods.push_back(method("Place", {tInt, tInt}, {"x", "y"}, tVoid,
            [blockOf](const Value& s, std::vector<Value>& a, CallContext& c) { blockOf(s, c)->Place(I(a[0]), I(a[1])); return Value{}; },
            "Block cells starting at this cell. Agents whose path used one of them plan again."));
        block.methods.push_back(method("Clear", {}, {}, tVoid,
            [blockOf](const Value& s, std::vector<Value>&, CallContext& c) { blockOf(s, c)->Clear(); return Value{}; },
            "Stop blocking cells."));
        api.types.push_back(std::move(block));
    }
}

} // namespace okay::script::engine_api
