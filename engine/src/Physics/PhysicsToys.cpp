// Inspector rows (property reflection) and scene-file records for the Physics Surface and the physics toys.
// The scene records are written from the reflected properties, so a new row is saved and loaded with no extra code:
//   <record> name="text" name="text" ...      (every value is its exact text form, quoted; unknown names are skipped)
// Written from this engine's PropertyRegistry / ComponentSerializerRegistry only.
#include "okay/Components/PhysicsToys.hpp"
#include "okay/Scene/ComponentSerializer.hpp"
#include "okay/Scene/PropertyBuilder.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include "okay/Scene/ReflectedRecord.hpp"
#include <sstream>

namespace okay {
namespace {

using reflected::RegisterReflectedRecord;

void Rows() {
    {
        PropertyBuilder<PhysicsSurface> b("PhysicsSurface", "physicssurface", "Physics 3D");
        b.AddString("material", &PhysicsSurface::material).Tooltip("How this surface feels. A preset (Ice, Rubber, Bouncy Ball, Metal, Wood, Stone, Glass, Mud, Sand, Sticky, Slippery, Trampoline, Cloth) or the name of a .okayphysmat file in your project")
         .AddBool("includeChildren", &PhysicsSurface::includeChildren).Tooltip("Also use it for every child object's colliders")
         .AddBool("massFromDensity", &PhysicsSurface::massFromDensity).Tooltip("Work out the body's mass from the material's density and the size of its colliders");
        b.Commit();
    }
    {
        PropertyBuilder<ConveyorBelt> b("ConveyorBelt", "conveyorbelt", "Physics 3D");
        b.AddVec3("direction", &ConveyorBelt::direction).Tooltip("Which way the belt runs, in the belt's own axes (0,0,1 = along its length). Needs a Box Collider 3D on the same object")
         .AddFloat("speed", &ConveyorBelt::speed).Range(-100, 100, 0.1).Tooltip("Belt speed in metres per second. A negative number runs it backwards")
         .AddFloat("grip", &ConveyorBelt::grip).Range(0, 1000, 1).Tooltip("How fast a body catches up with the belt")
         .AddFloat("reach", &ConveyorBelt::reach).Range(0.05, 5, 0.05).Tooltip("How far above the belt a body still counts as standing on it")
         .AddInt("layerMask", &ConveyorBelt::layerMask).Tooltip("Which collision layers are carried (-1 = all)");
        b.Commit();
    }
    {
        PropertyBuilder<ForceField> b("ForceField", "forcefield", "Physics 3D");
        b.AddEnum("mode", &ForceField::mode, {"Attract", "Repel", "Gravity Well", "Swirl"}).Tooltip("Pull in, push out, a little planet, or spin things around")
         .AddEnum("falloff", &ForceField::falloff, {"Constant", "Linear", "Inverse Square"}).Tooltip("How the strength fades with distance (a Gravity Well always fades by the inverse square)")
         .AddFloat("radius", &ForceField::radius).Range(0.1, 500, 0.1).Tooltip("How far the field reaches, in metres")
         .AddFloat("strength", &ForceField::strength).Range(0, 1000, 0.5).Tooltip("Acceleration at the centre, metres per second each second. Gravity is 9.8")
         .AddFloat("coreRadius", &ForceField::coreRadius).Range(0.05, 50, 0.05).Tooltip("Inside this the pull stops growing")
         .AddBool("active", &ForceField::active).Tooltip("Switch the field on or off (a script can too)")
         .AddInt("layerMask", &ForceField::layerMask).Tooltip("Which collision layers feel it (-1 = all)");
        b.Commit();
    }
    {
        PropertyBuilder<GravityZone> b("GravityZone", "gravityzone", "Physics 3D");
        b.AddVec3("size", &GravityZone::size).Tooltip("The box of space, in metres (times the object's scale)")
         .AddVec3("gravity", &GravityZone::gravity).Tooltip("Gravity inside the box. 0,0,0 floats; 0,-2,0 is like a small moon; 0,9.8,0 falls upward")
         .AddInt("layerMask", &GravityZone::layerMask).Tooltip("Which collision layers feel it (-1 = all)");
        b.Commit();
    }
    {
        PropertyBuilder<Turntable> b("Turntable", "turntable", "Physics 3D");
        b.AddVec3("axis", &Turntable::axis).Tooltip("What it turns about, in the object's own axes (0,1,0 = like a roundabout)")
         .AddFloat("degreesPerSecond", &Turntable::degreesPerSecond).Range(-3600, 3600, 1).Tooltip("Turning speed in degrees per second")
         .AddFloat("rampTime", &Turntable::rampTime).Range(0, 60, 0.1).Tooltip("Seconds to reach full speed (0 = at once)");
        b.Commit();
    }
    {
        PropertyBuilder<MovingPlatform> b("MovingPlatform", "movingplatform", "Physics 3D");
        b.AddEnum("mode", &MovingPlatform::mode, {"Ping Pong", "Loop"}).Tooltip("Ping Pong goes there and back; Loop goes round the points and starts again")
         .AddInt("pointCount", &MovingPlatform::pointCount).Range(2, 4, 1).Tooltip("How many points it visits. Point 1 is where it starts")
         .AddVec3("offset2", &MovingPlatform::offset2).Tooltip("Point 2, as an offset from the start")
         .AddVec3("offset3", &MovingPlatform::offset3).Tooltip("Point 3, as an offset from the start")
         .AddVec3("offset4", &MovingPlatform::offset4).Tooltip("Point 4, as an offset from the start")
         .AddFloat("speed", &MovingPlatform::speed).Range(0.01, 100, 0.1).Tooltip("Metres per second between points")
         .AddFloat("pause", &MovingPlatform::pause).Range(0, 60, 0.1).Tooltip("Seconds it waits at each point")
         .AddBool("easing", &MovingPlatform::easing).Tooltip("Slow down and speed up at the ends of each leg");
        b.Commit();
    }
    {
        PropertyBuilder<Explosion> b("Explosion", "explosion", "Physics 3D");
        b.AddFloat("force", &Explosion::force).Range(0, 10000, 1).Tooltip("Impulse at the centre: a 1 kg body flies at this speed. Heavier things fly less far")
         .AddFloat("radius", &Explosion::radius).Range(0.1, 500, 0.1).Tooltip("How far the blast reaches, in metres")
         .AddFloat("upwardBias", &Explosion::upwardBias).Range(0, 5, 0.05).Tooltip("Extra upward kick so things fly up as well as out")
         .AddFloat("damage", &Explosion::damage).Range(0, 100000, 1).Tooltip("Damage at the centre, handed to the damage hook (fades with distance)")
         .AddBool("onStart", &Explosion::onStart).Tooltip("Go off by itself after the delay when the game starts (off = a script calls Trigger)")
         .AddFloat("delay", &Explosion::delay).Range(0, 3600, 0.1).Tooltip("Seconds after the game starts (needs On Start)")
         .AddBool("destroyAfter", &Explosion::destroyAfter).Tooltip("Remove this object after it goes off")
         .AddInt("layerMask", &Explosion::layerMask).Tooltip("Which collision layers are shoved (-1 = all)");
        b.Commit();
    }
    {
        PropertyBuilder<Breakable> b("Breakable", "breakable", "Physics 3D");
        b.AddFloat("breakSpeed", &Breakable::breakSpeed).Range(0.1, 200, 0.1).Tooltip("How hard it must be hit to break (closing speed, metres per second)")
         .AddInt("pieces", &Breakable::pieces).Range(1, 4, 1).Tooltip("Pieces per side: 2 makes 8 pieces, 3 makes 27")
         .AddFloat("pieceLife", &Breakable::pieceLife).Range(0.5, 600, 0.5).Tooltip("Seconds before the pieces disappear")
         .AddFloat("burstSpeed", &Breakable::burstSpeed).Range(0, 50, 0.1).Tooltip("How fast the pieces fly apart");
        b.Commit();
    }
}

} // namespace

void RegisterPhysicsToyProperties() { Rows(); }   // (registering again just replaces the same entries)

namespace detail {
void RegisterPhysicsToySerializers() {
    RegisterPhysicsToyProperties();   // the writers read the Inspector rows
    RegisterReflectedRecord<PhysicsSurface>("PhysicsSurface", "physicssurface");
    RegisterReflectedRecord<ConveyorBelt>("ConveyorBelt", "conveyorbelt");
        RegisterReflectedRecord<ForceField>("ForceField", "forcefield");
    RegisterReflectedRecord<GravityZone>("GravityZone", "gravityzone");
    RegisterReflectedRecord<Turntable>("Turntable", "turntable");
    RegisterReflectedRecord<MovingPlatform>("MovingPlatform", "movingplatform");
    RegisterReflectedRecord<Explosion>("Explosion", "explosion");
    RegisterReflectedRecord<Breakable>("Breakable", "breakable");
}
} // namespace detail

} // namespace okay
