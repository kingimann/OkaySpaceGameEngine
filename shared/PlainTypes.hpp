#pragma once
// The one place the launcher, the editor and their shared headers get "SDL.h" from. A normal build includes SDL's header, so nothing changes. With
// OKAY_NO_SDL (CMake option OKAY_NO_SDL: the program is built and linked with no SDL at all) it declares only what those programs still name in
// plain data and handle types: the integer typedefs, the three opaque handle types (SDL_Window, SDL_Renderer, SDL_Texture: pointers that are never
// dereferenced and, on the own backend, are not even SDL objects), and the SDL_* rectangle / colour / vertex names as aliases of the engine's own
// identical-layout types (okay::ui2d, which already static_asserts they match). No function and no constant of SDL is declared here.
#ifndef OKAY_NO_SDL
#  ifndef SDL_MAIN_HANDLED
#    define SDL_MAIN_HANDLED
#  endif
#  include <SDL.h>
#else
#  include <cstdint>
#  include "okay/UI/UIPainter.hpp"
typedef std::uint8_t Uint8; typedef std::uint16_t Uint16; typedef std::uint32_t Uint32; typedef std::uint64_t Uint64;
typedef std::int8_t Sint8;  typedef std::int16_t Sint16;  typedef std::int32_t Sint32;  typedef std::int64_t Sint64;
struct SDL_Window; struct SDL_Renderer; struct SDL_Texture;
using SDL_Rect = okay::ui2d::Rect;
using SDL_FRect = okay::ui2d::FRect;
using SDL_Point = okay::ui2d::Point;
using SDL_FPoint = okay::ui2d::FPoint;
using SDL_Color = okay::ui2d::RGBA;
using SDL_Vertex = okay::ui2d::Vertex;
#endif
