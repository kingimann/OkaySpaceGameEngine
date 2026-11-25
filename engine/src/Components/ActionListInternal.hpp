// Private to engine/src/Components/ActionList*.cpp: the argument helpers and raycast that both ActionList.cpp
// (Update, conditions) and ActionListOps.cpp (the op families moved out of Update) use. Not installed.
#pragma once
#include "okay/Components/ActionList.hpp"
#include "okay/Math/Easing.hpp"
#include "okay/Physics/Physics3D.hpp"
#include <cstddef>
#include <string>

namespace okay {

class GameObject;
class Inventory;
class Scene;

namespace actionlist_ops {
float Num(const ActionList::Item& it, std::size_t i);
std::string Str(const ActionList::Item& it, std::size_t i);
std::string Rest(const ActionList::Item& it, std::size_t i);
int ItemCount(const ActionList::Item& it);
Inventory* ItemInv(const ActionList::Item& it, GameObject* self, Scene* s);
std::string ItemName(const ActionList::Item& it);
std::string ObjName(const ActionList::Item& it, std::size_t i);
bool CmpPass(const std::string& op, float lhs, float rhs);
Ease EaseByName(const std::string& s);
bool IsRayMod(const std::string& a);
RaycastHit3D ActionRaycast(GameObject* go, const ActionList::Item& it, std::size_t base);
} // namespace actionlist_ops

} // namespace okay
