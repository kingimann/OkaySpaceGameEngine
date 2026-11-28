// okay-ms own header <hidpi.h>: the HID parser functions the engine uses (what a raw HID report means), declarations
// only, written in our own words from Microsoft's public documentation of the header: the pages of HIDP_CAPS,
// HIDP_BUTTON_CAPS, HIDP_VALUE_CAPS, HIDP_REPORT_TYPE, HidP_GetCaps, HidP_GetButtonCaps, HidP_GetValueCaps, HidP_GetUsages,
// HidP_GetUsageValue and HidP_MaxUsageListLength ("Header hidpi.h", "DLL Hid.dll"), and its HIDP_STATUS_SUCCESS value.
// The members of the structures are in the order those pages list them, with the sizes their types have (USAGE and USHORT
// 2 bytes, UCHAR and BOOLEAN 1, ULONG and LONG 4); the reserved members fill the documented gaps. No text from the
// Windows SDK, MinGW or Wine headers. Round 37 (OwnWin32Hid.cpp).
#pragma once
#include <windows.h>
#include <hidusage.h>

// The data GetRawInputDeviceInfo(RIDI_PREPARSEDDATA) gives: opaque, only passed back to these functions.
typedef struct okay_hidp_preparsed_data_* PHIDP_PREPARSED_DATA;

typedef enum _HIDP_REPORT_TYPE { HidP_Input = 0, HidP_Output = 1, HidP_Feature = 2 } HIDP_REPORT_TYPE;

// The functions return an NTSTATUS: HIDP_STATUS_SUCCESS (facility 0x11, code 0) when they worked; errors are negative.
#define HIDP_STATUS_SUCCESS ((NTSTATUS)0x00110000L)

typedef struct _HIDP_CAPS {
    USAGE Usage;
    USAGE UsagePage;
    USHORT InputReportByteLength;
    USHORT OutputReportByteLength;
    USHORT FeatureReportByteLength;
    USHORT Reserved[17];
    USHORT NumberLinkCollectionNodes;
    USHORT NumberInputButtonCaps;
    USHORT NumberInputValueCaps;
    USHORT NumberInputDataIndices;
    USHORT NumberOutputButtonCaps;
    USHORT NumberOutputValueCaps;
    USHORT NumberOutputDataIndices;
    USHORT NumberFeatureButtonCaps;
    USHORT NumberFeatureValueCaps;
    USHORT NumberFeatureDataIndices;
} HIDP_CAPS, *PHIDP_CAPS;

// The usages a capability covers: a range (IsRange) or one usage.
typedef struct okay_hidp_range_ {
    USAGE UsageMin;
    USAGE UsageMax;
    USHORT StringMin;
    USHORT StringMax;
    USHORT DesignatorMin;
    USHORT DesignatorMax;
    USHORT DataIndexMin;
    USHORT DataIndexMax;
} okay_hidp_range_;
typedef struct okay_hidp_not_range_ {
    USAGE Usage;
    USAGE Reserved1;
    USHORT StringIndex;
    USHORT Reserved2;
    USHORT DesignatorIndex;
    USHORT Reserved3;
    USHORT DataIndex;
    USHORT Reserved4;
} okay_hidp_not_range_;

typedef struct _HIDP_BUTTON_CAPS {
    USAGE UsagePage;
    UCHAR ReportID;
    BOOLEAN IsAlias;
    USHORT BitField;
    USHORT LinkCollection;
    USAGE LinkUsage;
    USAGE LinkUsagePage;
    BOOLEAN IsRange;
    BOOLEAN IsStringRange;
    BOOLEAN IsDesignatorRange;
    BOOLEAN IsAbsolute;
    USHORT ReportCount;
    USHORT Reserved2;
    ULONG Reserved[9];
    union {
        okay_hidp_range_ Range;
        okay_hidp_not_range_ NotRange;
    };
} HIDP_BUTTON_CAPS, *PHIDP_BUTTON_CAPS;

typedef struct _HIDP_VALUE_CAPS {
    USAGE UsagePage;
    UCHAR ReportID;
    BOOLEAN IsAlias;
    USHORT BitField;
    USHORT LinkCollection;
    USAGE LinkUsage;
    USAGE LinkUsagePage;
    BOOLEAN IsRange;
    BOOLEAN IsStringRange;
    BOOLEAN IsDesignatorRange;
    BOOLEAN IsAbsolute;
    BOOLEAN HasNull;
    UCHAR Reserved;
    USHORT BitSize;
    USHORT ReportCount;
    USHORT Reserved2[5];
    ULONG UnitsExp;
    ULONG Units;
    LONG LogicalMin;
    LONG LogicalMax;
    LONG PhysicalMin;
    LONG PhysicalMax;
    union {
        okay_hidp_range_ Range;
        okay_hidp_not_range_ NotRange;
    };
} HIDP_VALUE_CAPS, *PHIDP_VALUE_CAPS;

extern "C" {
__declspec(dllimport) NTSTATUS WINAPI HidP_GetCaps(PHIDP_PREPARSED_DATA preparsed, PHIDP_CAPS caps);
__declspec(dllimport) NTSTATUS WINAPI HidP_GetButtonCaps(HIDP_REPORT_TYPE type, PHIDP_BUTTON_CAPS caps, PUSHORT length,
                                                         PHIDP_PREPARSED_DATA preparsed);
__declspec(dllimport) NTSTATUS WINAPI HidP_GetValueCaps(HIDP_REPORT_TYPE type, PHIDP_VALUE_CAPS caps, PUSHORT length,
                                                        PHIDP_PREPARSED_DATA preparsed);
__declspec(dllimport) NTSTATUS WINAPI HidP_GetUsages(HIDP_REPORT_TYPE type, USAGE page, USHORT link_collection,
                                                     PUSAGE usages, PULONG length, PHIDP_PREPARSED_DATA preparsed,
                                                     PCHAR report, ULONG report_length);
__declspec(dllimport) NTSTATUS WINAPI HidP_GetUsageValue(HIDP_REPORT_TYPE type, USAGE page, USHORT link_collection,
                                                         USAGE usage, PULONG value, PHIDP_PREPARSED_DATA preparsed,
                                                         PCHAR report, ULONG report_length);
__declspec(dllimport) ULONG WINAPI HidP_MaxUsageListLength(HIDP_REPORT_TYPE type, USAGE page,
                                                           PHIDP_PREPARSED_DATA preparsed);
}
