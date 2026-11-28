// okay-ms own header <imm.h>: the Input Method Manager functions the engine uses (text composed with an input method,
// for example Japanese or Chinese), declarations only, written in our own words from Microsoft's public documentation of
// ImmGetContext, ImmReleaseContext, ImmGetCompositionStringW, ImmSetCompositionWindow, ImmSetCandidateWindow,
// ImmAssociateContext, COMPOSITIONFORM and CANDIDATEFORM ("Header imm.h", "DLL Imm32.dll"), with the documented GCS_ and
// CFS_ values. No text from the Windows SDK, MinGW or Wine headers. Round 37 (OwnWin32.cpp).
#pragma once
#include <windows.h>

typedef struct okay_win32_himc_* HIMC;

typedef struct tagCOMPOSITIONFORM {
    DWORD dwStyle;
    POINT ptCurrentPos;
    RECT rcArea;
} COMPOSITIONFORM, *PCOMPOSITIONFORM, *LPCOMPOSITIONFORM;
typedef struct tagCANDIDATEFORM {
    DWORD dwIndex;
    DWORD dwStyle;
    POINT ptCurrentPos;
    RECT rcArea;
} CANDIDATEFORM, *PCANDIDATEFORM, *LPCANDIDATEFORM;

#define CFS_DEFAULT 0x0000
#define CFS_RECT 0x0001
#define CFS_POINT 0x0002
#define CFS_FORCE_POSITION 0x0020
#define CFS_CANDIDATEPOS 0x0040
#define CFS_EXCLUDE 0x0080

#define GCS_COMPREADSTR 0x0001
#define GCS_COMPREADATTR 0x0002
#define GCS_COMPREADCLAUSE 0x0004
#define GCS_COMPSTR 0x0008
#define GCS_COMPATTR 0x0010
#define GCS_COMPCLAUSE 0x0020
#define GCS_CURSORPOS 0x0080
#define GCS_DELTASTART 0x0100
#define GCS_RESULTREADSTR 0x0200
#define GCS_RESULTREADCLAUSE 0x0400
#define GCS_RESULTSTR 0x0800
#define GCS_RESULTCLAUSE 0x1000
#define ISC_SHOWUICOMPOSITIONWINDOW 0x80000000

extern "C" {
__declspec(dllimport) HIMC ImmGetContext(HWND hwnd);
__declspec(dllimport) BOOL ImmReleaseContext(HWND hwnd, HIMC context);
__declspec(dllimport) HIMC ImmAssociateContext(HWND hwnd, HIMC context);
// The length in bytes of what was asked for (index: one GCS_ value), or a negative error.
__declspec(dllimport) LONG ImmGetCompositionStringW(HIMC context, DWORD index, LPVOID buffer, DWORD length);
__declspec(dllimport) BOOL ImmSetCompositionWindow(HIMC context, LPCOMPOSITIONFORM form);
__declspec(dllimport) BOOL ImmSetCandidateWindow(HIMC context, LPCANDIDATEFORM form);
}
