// okay-ms own header <hidsdi.h>: the HID device functions the engine uses, declarations only, written in our own words
// from Microsoft's public documentation of HidD_GetProductString, HidD_GetManufacturerString, HidD_GetSerialNumberString,
// HidD_GetAttributes and HIDD_ATTRIBUTES ("Header hidsdi.h", "DLL Hid.dll"). No text from the Windows SDK, MinGW or
// Wine headers. Round 37 (OwnWin32Hid.cpp).
#pragma once
#include <windows.h>
#include <hidpi.h>

typedef struct _HIDD_ATTRIBUTES {
    ULONG Size;  // the caller sets it to sizeof(HIDD_ATTRIBUTES)
    USHORT VendorID;
    USHORT ProductID;
    USHORT VersionNumber;
} HIDD_ATTRIBUTES, *PHIDD_ATTRIBUTES;

extern "C" {
// The strings are UTF-16 and end with a zero if the buffer has room; lengths are in bytes.
__declspec(dllimport) BOOLEAN WINAPI HidD_GetProductString(HANDLE device, PVOID buffer, ULONG length);
__declspec(dllimport) BOOLEAN WINAPI HidD_GetManufacturerString(HANDLE device, PVOID buffer, ULONG length);
__declspec(dllimport) BOOLEAN WINAPI HidD_GetSerialNumberString(HANDLE device, PVOID buffer, ULONG length);
__declspec(dllimport) BOOLEAN WINAPI HidD_GetAttributes(HANDLE device, PHIDD_ATTRIBUTES attributes);
}
