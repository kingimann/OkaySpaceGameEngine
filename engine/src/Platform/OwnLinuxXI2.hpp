#pragma once
// Private to the own Linux backend: the XInput 2.2 touch translation (OwnLinuxX11.cpp), split out so a test can feed it
// event structs it builds itself. Written from the X Input Extension protocol specification, version 2.2 ("Touch device
// support": XI_TouchBegin / XI_TouchUpdate / XI_TouchEnd, the touch class, valuator masks) and the XInput2.h interface
// (structure layout only). libXi is loaded at run time (dlopen); nothing here links it.
//
// OKAY_X11_HAVE_XI2 is 1 when the XInput2 header exists (the X11 headers are used for types only).
#if defined(OKAY_PLATFORM_X11_HEADERS) && defined(__has_include)
#  if __has_include(<X11/extensions/XInput2.h>)
#    define OKAY_X11_HAVE_XI2 1
#  endif
#endif
#ifndef OKAY_X11_HAVE_XI2
#  define OKAY_X11_HAVE_XI2 0
#endif

#if OKAY_X11_HAVE_XI2
#include "Internal.hpp"   // TouchSample
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>
#include <cstdint>

namespace okay::platform::own {

/// A touch device's pressure axis: which valuator carries it and its range. number -1 = the device has none.
struct XIPressureAxis { int number = -1; double min = 0.0, max = 0.0; };

/// The pressure axis among a device's classes: the valuator whose label is `mtPressure` ("Abs MT Pressure", what touch
/// screens report per contact), else `absPressure` ("Abs Pressure"). Either atom may be 0 (not interned).
XIPressureAxis PressureAxisOf(const XIDeviceInfo& dev, Atom mtPressure, Atom absPressure);

/// The value of valuator `number` in an event's valuator state (values are packed in the order of the mask's set bits).
/// False when the event does not carry that valuator.
bool XIValuatorValue(const XIValuatorState& v, int number, double& out);

/// An XI_TouchBegin / XI_TouchUpdate / XI_TouchEnd device event as a TouchSample: id = detail (the touch id), x / y =
/// event_x / event_y (window-relative), pressure from `axis` scaled to 0..1 (1 when the device or the event has none).
/// Every XI_TouchEnd ends the contact (the touch id may be reused afterwards). False for any other event type.
bool TouchSampleFromXI(const XIDeviceEvent& ev, const XIPressureAxis& axis, int winW, int winH, uint32_t windowId,
                       uint32_t nowMs, TouchSample& out);

/// How many devices deliver touch: slave or floating devices with a touch class (direct: a touch screen; dependent: a
/// touch pad), each counted once. Master devices are left out (their touch class mirrors a slave's).
int CountTouchDevices(const XIDeviceInfo* devs, int n);

} // namespace okay::platform::own
#endif
