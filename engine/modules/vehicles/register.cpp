// Vehicles module: the vehicle, vehicletune, vehiclesusp and vehicle2d scene records.
// The scene chain still calls these at the old point in the object, so a save with
// every module on stays byte-identical. vehicletune and vehiclesusp are written by
// the vehicle writer (they follow that component); their own writers are empty.
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Scene/ComponentSerializer.hpp"
#include "okay/Components/VehicleController.hpp"
#include "okay/Components/VehicleController2D.hpp"
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace okay {
namespace modules {

namespace {

VehicleController* LastVehicle(GameObject& go) {
    VehicleController* last = nullptr;
    for (auto* v : go.GetComponents<VehicleController>()) last = v;
    return last;
}

void WriteVehicle(const GameObject& go, std::ostream& out) {
    for (auto* v : go.GetComponents<VehicleController>()) {
        out << "  vehicle " << v->acceleration << " " << v->maxSpeed << " " << v->reverseSpeed
            << " " << v->brakeForce << " " << v->drag << " " << v->turnSpeed << " " << v->grip
            << " " << v->handbrakeGrip << " " << v->groundCheckDistance
            << " " << (v->followCamera ? 1 : 0) << " " << v->camDistance << " " << v->camHeight
            << " " << v->camLerp << " " << (int)(unsigned char)v->handbrakeKey << "\n";
        {   // the open-world settings that differ from the defaults, plus the wheel spacing
            std::ostringstream tl; tl << v->ToLine();
            if (std::fabs(v->wheelBase - 2.4f) > 1e-4f) tl << "wheelBase=" << v->wheelBase << " ";
            if (std::fabs(v->trackWidth - 1.6f) > 1e-4f) tl << "trackWidth=" << v->trackWidth << " ";
            if (!tl.str().empty()) out << "  vehicletune " << tl.str() << "\n";
        }
        if (v->suspension)
            out << "  vehiclesusp 1 " << v->rideHeight << " " << v->springStrength << " "
                << v->springDamping << " " << v->suspensionTravel << " " << v->wheelBase << " "
                << v->trackWidth << " " << v->bodyLean << " " << v->maxTilt << " " << v->tiltSmooth << "\n";
    }
}

void ReadVehicle(GameObject& go, std::istream& in) {
    auto* v = go.AddComponent<VehicleController>();
    in >> v->acceleration >> v->maxSpeed >> v->reverseSpeed >> v->brakeForce
       >> v->drag >> v->turnSpeed >> v->grip >> v->handbrakeGrip
       >> v->groundCheckDistance;
    int fc = 0; in >> fc; v->followCamera = (fc != 0);
    in >> v->camDistance >> v->camHeight >> v->camLerp;
    int hk = 32; in >> hk; v->handbrakeKey = (char)hk;
}

void ReadTune(GameObject& go, std::istream& in) {
    auto* v = LastVehicle(go);
    std::string rest; std::getline(in, rest);
    if (v) v->FromLine(rest);
    if (v) {
        std::istringstream ts(rest); std::string tok;
        while (ts >> tok) {
            if (tok.rfind("wheelBase=", 0) == 0) v->wheelBase = (float)std::atof(tok.c_str() + 10);
            else if (tok.rfind("trackWidth=", 0) == 0) v->trackWidth = (float)std::atof(tok.c_str() + 11);
        }
    }
}

void ReadSusp(GameObject& go, std::istream& in) {
    auto* v = LastVehicle(go);
    int on = 0; in >> on;
    float rh, ss, sd, st, wb, tw, bl, mt, ts;
    in >> rh >> ss >> sd >> st >> wb >> tw >> bl >> mt >> ts;
    if (v) {
        v->suspension = (on != 0);
        v->rideHeight = rh; v->springStrength = ss; v->springDamping = sd;
        v->suspensionTravel = st; v->wheelBase = wb; v->trackWidth = tw;
        v->bodyLean = bl; v->maxTilt = mt; v->tiltSmooth = ts;
    }
}

void WriteVehicle2D(const GameObject& go, std::ostream& out) {
    for (auto* v2 : go.GetComponents<VehicleController2D>()) {
        out << "  vehicle2d " << v2->acceleration << " " << v2->maxSpeed << " " << v2->reverseSpeed
            << " " << v2->brakeForce << " " << v2->drag << " " << v2->turnSpeed << " " << v2->grip
            << " " << v2->handbrakeGrip << " " << (v2->sideView ? 1 : 0)
            << " " << (int)(unsigned char)v2->handbrakeKey << "\n";
    }
}

void ReadVehicle2D(GameObject& go, std::istream& in) {
    auto* v2 = go.AddComponent<VehicleController2D>();
    in >> v2->acceleration >> v2->maxSpeed >> v2->reverseSpeed >> v2->brakeForce
       >> v2->drag >> v2->turnSpeed >> v2->grip >> v2->handbrakeGrip;
    int sv = 0; in >> sv; v2->sideView = (sv != 0);
    int hk = 32; in >> hk; v2->handbrakeKey = (char)hk;
}

void AddRec(const char* name, void (*write)(const GameObject&, std::ostream&),
            void (*read)(GameObject&, std::istream&)) {
    ComponentSerializerRegistry::Entry rec;
    rec.name = name;
    rec.write = write;
    rec.read = read;
    rec.inChain = true;
    ComponentSerializerRegistry::Register(std::move(rec));
}

} // namespace

void Register_vehicles(ModuleContext&) {
    AddRec("vehicle", &WriteVehicle, &ReadVehicle);
    AddRec("vehicletune", [](const GameObject&, std::ostream&) {}, &ReadTune);
    AddRec("vehiclesusp", [](const GameObject&, std::ostream&) {}, &ReadSusp);
    AddRec("vehicle2d", &WriteVehicle2D, &ReadVehicle2D);
}

} // namespace modules
} // namespace okay
