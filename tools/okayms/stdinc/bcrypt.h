// okay-ms own <bcrypt.h>: only the call that asks Windows for random bytes, declared in our own words from Microsoft's
// public BCryptGenRandom documentation. It lives in bcrypt.dll. No cryptography is written here: the operating system
// makes the bytes. BCRYPT_USE_SYSTEM_PREFERRED_RNG lets the algorithm handle be null. The result is an NTSTATUS (0 is
// success).
#pragma once
#include "windows.h"

typedef PVOID BCRYPT_HANDLE;
typedef PVOID BCRYPT_ALG_HANDLE;
#define BCRYPT_USE_SYSTEM_PREFERRED_RNG 0x00000002
#define BCRYPT_SUCCESS(status) (((NTSTATUS)(status)) >= 0)
#ifndef NT_SUCCESS
#define NT_SUCCESS(status) (((NTSTATUS)(status)) >= 0)
#endif
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)

extern "C" __declspec(dllimport) long BCryptGenRandom(void* algorithm, unsigned char* buffer, unsigned long size,
                                                      unsigned long flags);
