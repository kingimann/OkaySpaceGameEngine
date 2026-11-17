#pragma once
#include "okay/Math/Vec3.hpp"
#include <functional>
#include <memory>
#include <string>

namespace okay {

class GameObject;

/// A place someone can sit or drive from: a chair, a bench, a vehicle seat. The components keep their own
/// settings; a module registers an adapter for each (engine/modules/<name>/register.cpp).
class ISeat {
public:
    virtual ~ISeat() = default;
    virtual GameObject* Occupant() const = 0;
    bool IsOccupied() const { return Occupant() != nullptr; }
    virtual bool IsDriver() const { return false; }
    /// Seat `who`. False (nothing changes) when the seat or the person cannot.
    virtual bool Enter(GameObject* who) = 0;
    /// Stand the occupant up at the exit point.
    virtual void Leave() = 0;
    virtual Vec3 SitPoint() const = 0;
    virtual Vec3 ExitPoint() const = 0;
    virtual const char* Model() const = 0;
};

namespace seat {
using Finder = std::function<std::unique_ptr<ISeat>(GameObject*)>;
void AddFinder(const std::string& name, Finder f);
void ClearFinders();   // tests
/// The seat on `go` (not its parents), or null.
std::unique_ptr<ISeat> Find(GameObject* go);
/// The seat `who` is sitting in, scanning the scene's objects, or null.
std::unique_ptr<ISeat> SeatOf(GameObject* who);
} // namespace seat

} // namespace okay
