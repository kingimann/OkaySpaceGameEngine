// okay-ms own Win32 header, part of <windows.h>: Raw Input (keyboards, mice and HID devices read through WM_INPUT).
// Written in our own words from Microsoft's public Win32 API reference: the pages of RAWINPUTDEVICE, RAWINPUTDEVICELIST,
// RAWINPUTHEADER, RAWMOUSE, RAWKEYBOARD, RAWHID, RAWINPUT, RID_DEVICE_INFO (and its _MOUSE, _KEYBOARD and _HID parts),
// RegisterRawInputDevices, GetRawInputData, GetRawInputDeviceList and GetRawInputDeviceInfoW ("Header winuser.h",
// "DLL User32.dll"; members in the order the pages list them, documented flag values). No text from the Windows SDK,
// MinGW or Wine headers. Round 37 (OwnWin32.cpp, OwnWin32Hid.cpp).
#pragma once
#include "window.h"

typedef struct okay_win32_hrawinput_* HRAWINPUT;

typedef struct tagRAWINPUTDEVICE {
    USHORT usUsagePage;
    USHORT usUsage;
    DWORD dwFlags;
    HWND hwndTarget;
} RAWINPUTDEVICE, *PRAWINPUTDEVICE, *LPRAWINPUTDEVICE;
typedef const RAWINPUTDEVICE* PCRAWINPUTDEVICE;

typedef struct tagRAWINPUTDEVICELIST {
    HANDLE hDevice;
    DWORD dwType;
} RAWINPUTDEVICELIST, *PRAWINPUTDEVICELIST;

typedef struct tagRAWINPUTHEADER {
    DWORD dwType;
    DWORD dwSize;
    HANDLE hDevice;
    WPARAM wParam;
} RAWINPUTHEADER, *PRAWINPUTHEADER;

typedef struct tagRAWMOUSE {
    USHORT usFlags;
    union {
        ULONG ulButtons;
        struct {
            USHORT usButtonFlags;
            USHORT usButtonData;
        };
    };
    ULONG ulRawButtons;
    LONG lLastX;
    LONG lLastY;
    ULONG ulExtraInformation;
} RAWMOUSE, *PRAWMOUSE;

typedef struct tagRAWKEYBOARD {
    USHORT MakeCode;
    USHORT Flags;
    USHORT Reserved;
    USHORT VKey;
    UINT Message;
    ULONG ExtraInformation;
} RAWKEYBOARD, *PRAWKEYBOARD;

typedef struct tagRAWHID {
    DWORD dwSizeHid;  // the size of one report
    DWORD dwCount;    // the number of reports in bRawData
    BYTE bRawData[1];
} RAWHID, *PRAWHID;

typedef struct tagRAWINPUT {
    RAWINPUTHEADER header;
    union {
        RAWMOUSE mouse;
        RAWKEYBOARD keyboard;
        RAWHID hid;
    } data;
} RAWINPUT, *PRAWINPUT, *LPRAWINPUT;

typedef struct tagRID_DEVICE_INFO_MOUSE {
    DWORD dwId;
    DWORD dwNumberOfButtons;
    DWORD dwSampleRate;
    BOOL fHasHorizontalWheel;
} RID_DEVICE_INFO_MOUSE;
typedef struct tagRID_DEVICE_INFO_KEYBOARD {
    DWORD dwType;
    DWORD dwSubType;
    DWORD dwKeyboardMode;
    DWORD dwNumberOfFunctionKeys;
    DWORD dwNumberOfIndicators;
    DWORD dwNumberOfKeysTotal;
} RID_DEVICE_INFO_KEYBOARD;
typedef struct tagRID_DEVICE_INFO_HID {
    DWORD dwVendorId;
    DWORD dwProductId;
    DWORD dwVersionNumber;
    USHORT usUsagePage;
    USHORT usUsage;
} RID_DEVICE_INFO_HID;
typedef struct tagRID_DEVICE_INFO {
    DWORD cbSize;
    DWORD dwType;
    union {
        RID_DEVICE_INFO_MOUSE mouse;
        RID_DEVICE_INFO_KEYBOARD keyboard;
        RID_DEVICE_INFO_HID hid;
    };
} RID_DEVICE_INFO, *PRID_DEVICE_INFO, *LPRID_DEVICE_INFO;

#define RIM_TYPEMOUSE 0
#define RIM_TYPEKEYBOARD 1
#define RIM_TYPEHID 2
#define RID_INPUT 0x10000003
#define RID_HEADER 0x10000005
#define RIDI_PREPARSEDDATA 0x20000005
#define RIDI_DEVICENAME 0x20000007
#define RIDI_DEVICEINFO 0x2000000b
#define RIDEV_REMOVE 0x00000001
#define RIDEV_EXCLUDE 0x00000010
#define RIDEV_PAGEONLY 0x00000020
#define RIDEV_NOLEGACY 0x00000030
#define RIDEV_INPUTSINK 0x00000100
#define RIDEV_CAPTUREMOUSE 0x00000200
#define RIDEV_NOHOTKEYS 0x00000200
#define RIDEV_APPKEYS 0x00000400
#define RIDEV_EXINPUTSINK 0x00001000
#define RIDEV_DEVNOTIFY 0x00002000
#define GIDC_ARRIVAL 1
#define GIDC_REMOVAL 2
#define RIM_INPUT 0
#define RIM_INPUTSINK 1
#define GET_RAWINPUT_CODE_WPARAM(w) ((w) & 0xff)
#define MOUSE_MOVE_RELATIVE 0x00
#define MOUSE_MOVE_ABSOLUTE 0x01
#define MOUSE_VIRTUAL_DESKTOP 0x02
#define RI_MOUSE_LEFT_BUTTON_DOWN 0x0001
#define RI_MOUSE_LEFT_BUTTON_UP 0x0002
#define RI_MOUSE_RIGHT_BUTTON_DOWN 0x0004
#define RI_MOUSE_RIGHT_BUTTON_UP 0x0008
#define RI_MOUSE_MIDDLE_BUTTON_DOWN 0x0010
#define RI_MOUSE_MIDDLE_BUTTON_UP 0x0020
#define RI_MOUSE_BUTTON_4_DOWN 0x0040
#define RI_MOUSE_BUTTON_4_UP 0x0080
#define RI_MOUSE_BUTTON_5_DOWN 0x0100
#define RI_MOUSE_BUTTON_5_UP 0x0200
#define RI_MOUSE_WHEEL 0x0400
#define RI_MOUSE_HWHEEL 0x0800
#define RI_KEY_MAKE 0
#define RI_KEY_BREAK 1
#define RI_KEY_E0 2
#define RI_KEY_E1 4

OKAY_WIN32_IMPORT BOOL RegisterRawInputDevices(PCRAWINPUTDEVICE devices, UINT count, UINT size);
OKAY_WIN32_IMPORT UINT GetRawInputData(HRAWINPUT input, UINT command, LPVOID data, PUINT size, UINT header_size);
OKAY_WIN32_IMPORT UINT GetRawInputDeviceList(PRAWINPUTDEVICELIST list, PUINT count, UINT size);
OKAY_WIN32_IMPORT UINT GetRawInputDeviceInfoW(HANDLE device, UINT command, LPVOID data, PUINT size);
