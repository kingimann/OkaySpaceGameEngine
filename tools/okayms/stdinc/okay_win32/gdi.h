// okay-ms own Win32 header, part of <windows.h>: the GDI pieces of the platform layer (device-independent bitmaps drawn
// into a window, icon bitmaps) and the OpenGL context functions that come with it (pixel formats from gdi32.dll, the
// wgl functions from opengl32.dll). Written in our own words from Microsoft's public Win32 API reference: the pages of
// BITMAPINFOHEADER, BITMAPINFO, BITMAPV5HEADER, RGBQUAD, CIEXYZ, CIEXYZTRIPLE, PIXELFORMATDESCRIPTOR, CreateDIBSection,
// StretchDIBits, CreateBitmap, DeleteObject, ChoosePixelFormat, SetPixelFormat, DescribePixelFormat, SwapBuffers and the
// wgl functions named below ("Header wingdi.h"; their members in the order the pages list them, and their documented
// constant values). No text from the Windows SDK, MinGW or Wine headers. Round 37.
#pragma once
#include "base.h"

typedef struct tagRGBQUAD {
    BYTE rgbBlue;
    BYTE rgbGreen;
    BYTE rgbRed;
    BYTE rgbReserved;
} RGBQUAD;

typedef struct tagBITMAPINFOHEADER {
    DWORD biSize;
    LONG biWidth;
    LONG biHeight;  // negative: the rows go from the top down
    WORD biPlanes;
    WORD biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    LONG biXPelsPerMeter;
    LONG biYPelsPerMeter;
    DWORD biClrUsed;
    DWORD biClrImportant;
} BITMAPINFOHEADER, *PBITMAPINFOHEADER, *LPBITMAPINFOHEADER;

typedef struct tagBITMAPINFO {
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD bmiColors[1];
} BITMAPINFO, *PBITMAPINFO, *LPBITMAPINFO;

// Colour-space end points: fixed-point numbers with 2 integer and 30 fraction bits.
typedef long FXPT2DOT30;
typedef struct tagCIEXYZ {
    FXPT2DOT30 ciexyzX;
    FXPT2DOT30 ciexyzY;
    FXPT2DOT30 ciexyzZ;
} CIEXYZ;
typedef struct tagCIEXYZTRIPLE {
    CIEXYZ ciexyzRed;
    CIEXYZ ciexyzGreen;
    CIEXYZ ciexyzBlue;
} CIEXYZTRIPLE;

// 124 bytes.
typedef struct {
    DWORD bV5Size;
    LONG bV5Width;
    LONG bV5Height;
    WORD bV5Planes;
    WORD bV5BitCount;
    DWORD bV5Compression;
    DWORD bV5SizeImage;
    LONG bV5XPelsPerMeter;
    LONG bV5YPelsPerMeter;
    DWORD bV5ClrUsed;
    DWORD bV5ClrImportant;
    DWORD bV5RedMask;
    DWORD bV5GreenMask;
    DWORD bV5BlueMask;
    DWORD bV5AlphaMask;
    DWORD bV5CSType;
    CIEXYZTRIPLE bV5Endpoints;
    DWORD bV5GammaRed;
    DWORD bV5GammaGreen;
    DWORD bV5GammaBlue;
    DWORD bV5Intent;
    DWORD bV5ProfileData;
    DWORD bV5ProfileSize;
    DWORD bV5Reserved;
} BITMAPV5HEADER, *LPBITMAPV5HEADER, *PBITMAPV5HEADER;

#define BI_RGB 0L
#define BI_BITFIELDS 3L
#define DIB_RGB_COLORS 0
#define DIB_PAL_COLORS 1
#define SRCCOPY ((DWORD)0x00CC0020)

typedef struct tagPIXELFORMATDESCRIPTOR {
    WORD nSize;
    WORD nVersion;
    DWORD dwFlags;
    BYTE iPixelType;
    BYTE cColorBits;
    BYTE cRedBits;
    BYTE cRedShift;
    BYTE cGreenBits;
    BYTE cGreenShift;
    BYTE cBlueBits;
    BYTE cBlueShift;
    BYTE cAlphaBits;
    BYTE cAlphaShift;
    BYTE cAccumBits;
    BYTE cAccumRedBits;
    BYTE cAccumGreenBits;
    BYTE cAccumBlueBits;
    BYTE cAccumAlphaBits;
    BYTE cDepthBits;
    BYTE cStencilBits;
    BYTE cAuxBuffers;
    BYTE iLayerType;
    BYTE bReserved;
    DWORD dwLayerMask;
    DWORD dwVisibleMask;
    DWORD dwDamageMask;
} PIXELFORMATDESCRIPTOR, *PPIXELFORMATDESCRIPTOR, *LPPIXELFORMATDESCRIPTOR;
#define PFD_DOUBLEBUFFER 0x00000001
#define PFD_STEREO 0x00000002
#define PFD_DRAW_TO_WINDOW 0x00000004
#define PFD_DRAW_TO_BITMAP 0x00000008
#define PFD_SUPPORT_GDI 0x00000010
#define PFD_SUPPORT_OPENGL 0x00000020
#define PFD_TYPE_RGBA 0
#define PFD_TYPE_COLORINDEX 1
#define PFD_MAIN_PLANE 0

extern "C" {
__declspec(dllimport) HBITMAP CreateDIBSection(HDC dc, const BITMAPINFO* info, UINT usage, void** bits, HANDLE section,
                                               DWORD offset);
__declspec(dllimport) int StretchDIBits(HDC dc, int x_dest, int y_dest, int dest_width, int dest_height, int x_src,
                                        int y_src, int src_width, int src_height, const void* bits,
                                        const BITMAPINFO* info, UINT usage, DWORD rop);
__declspec(dllimport) HBITMAP CreateBitmap(int width, int height, UINT planes, UINT bits_per_pixel, const void* bits);
__declspec(dllimport) BOOL DeleteObject(HGDIOBJ object);
__declspec(dllimport) int ChoosePixelFormat(HDC dc, const PIXELFORMATDESCRIPTOR* pfd);
__declspec(dllimport) BOOL SetPixelFormat(HDC dc, int format, const PIXELFORMATDESCRIPTOR* pfd);
__declspec(dllimport) int DescribePixelFormat(HDC dc, int format, UINT bytes, LPPIXELFORMATDESCRIPTOR pfd);
__declspec(dllimport) BOOL SwapBuffers(HDC dc);
// opengl32.dll
__declspec(dllimport) HGLRC wglCreateContext(HDC dc);
__declspec(dllimport) BOOL wglDeleteContext(HGLRC context);
__declspec(dllimport) BOOL wglMakeCurrent(HDC dc, HGLRC context);
__declspec(dllimport) HGLRC wglGetCurrentContext(void);
__declspec(dllimport) HDC wglGetCurrentDC(void);
__declspec(dllimport) PROC wglGetProcAddress(LPCSTR name);
__declspec(dllimport) BOOL wglShareLists(HGLRC a, HGLRC b);
}
