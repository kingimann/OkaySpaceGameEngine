// okay-ms own header <hidusage.h>: the HID usage pages and usages the engine names, written from the USB-IF "HID Usage
// Tables" document (the page and usage numbers are that specification's) and Microsoft's public documentation of the
// header's USAGE type and HID_USAGE_* names. No text from the Windows SDK, MinGW or Wine headers. Round 36
// (OwnWin32Hid.cpp; it also needs <hidpi.h> and <hidsdi.h>, not written yet).
#pragma once
typedef unsigned short USAGE;
typedef USAGE* PUSAGE;

// Usage pages (HID Usage Tables, "Usage Page Summary").
#define HID_USAGE_PAGE_UNDEFINED ((USAGE)0x00)
#define HID_USAGE_PAGE_GENERIC ((USAGE)0x01)
#define HID_USAGE_PAGE_SIMULATION ((USAGE)0x02)
#define HID_USAGE_PAGE_VR ((USAGE)0x03)
#define HID_USAGE_PAGE_SPORT ((USAGE)0x04)
#define HID_USAGE_PAGE_GAME ((USAGE)0x05)
#define HID_USAGE_PAGE_GENERIC_DEVICE ((USAGE)0x06)
#define HID_USAGE_PAGE_KEYBOARD ((USAGE)0x07)
#define HID_USAGE_PAGE_LED ((USAGE)0x08)
#define HID_USAGE_PAGE_BUTTON ((USAGE)0x09)
#define HID_USAGE_PAGE_ORDINAL ((USAGE)0x0A)
#define HID_USAGE_PAGE_TELEPHONY ((USAGE)0x0B)
#define HID_USAGE_PAGE_CONSUMER ((USAGE)0x0C)
#define HID_USAGE_PAGE_DIGITIZER ((USAGE)0x0D)
#define HID_USAGE_PAGE_HAPTICS ((USAGE)0x0E)
#define HID_USAGE_PAGE_PID ((USAGE)0x0F)

// Generic Desktop page usages (HID Usage Tables, "Generic Desktop Page (0x01)").
#define HID_USAGE_GENERIC_POINTER ((USAGE)0x01)
#define HID_USAGE_GENERIC_MOUSE ((USAGE)0x02)
#define HID_USAGE_GENERIC_JOYSTICK ((USAGE)0x04)
#define HID_USAGE_GENERIC_GAMEPAD ((USAGE)0x05)
#define HID_USAGE_GENERIC_KEYBOARD ((USAGE)0x06)
#define HID_USAGE_GENERIC_KEYPAD ((USAGE)0x07)
#define HID_USAGE_GENERIC_MULTI_AXIS_CONTROLLER ((USAGE)0x08)
#define HID_USAGE_GENERIC_X ((USAGE)0x30)
#define HID_USAGE_GENERIC_Y ((USAGE)0x31)
#define HID_USAGE_GENERIC_Z ((USAGE)0x32)
#define HID_USAGE_GENERIC_RX ((USAGE)0x33)
#define HID_USAGE_GENERIC_RY ((USAGE)0x34)
#define HID_USAGE_GENERIC_RZ ((USAGE)0x35)
#define HID_USAGE_GENERIC_SLIDER ((USAGE)0x36)
#define HID_USAGE_GENERIC_DIAL ((USAGE)0x37)
#define HID_USAGE_GENERIC_WHEEL ((USAGE)0x38)
#define HID_USAGE_GENERIC_HATSWITCH ((USAGE)0x39)
