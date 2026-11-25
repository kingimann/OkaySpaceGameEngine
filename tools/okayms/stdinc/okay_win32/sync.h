// okay-ms own Win32 header, part of <windows.h>: waiting on handles, events, critical sections, slim reader/writer
// locks and condition variables. Written in our own words from Microsoft's public Win32 documentation of the
// "Synchronization" functions. The structure sizes are the documented x64 ones: a CRITICAL_SECTION is 40 bytes
// (a debug-information pointer, two LONG counts, the owner thread handle, a semaphore handle and a spin count);
// an SRWLOCK and a CONDITION_VARIABLE are one pointer that starts as zero.
#pragma once
#include "base.h"

#define INFINITE 0xFFFFFFFF
#define WAIT_OBJECT_0 0x00000000L
#define WAIT_ABANDONED 0x00000080L
#define WAIT_ABANDONED_0 0x00000080L
#define WAIT_TIMEOUT 0x00000102L
#define WAIT_FAILED ((DWORD)0xFFFFFFFF)
#define MAXIMUM_WAIT_OBJECTS 64

OKAY_WIN32_IMPORT DWORD WaitForSingleObject(HANDLE handle, DWORD milliseconds);
OKAY_WIN32_IMPORT DWORD WaitForMultipleObjects(DWORD count, const HANDLE* handles, BOOL wait_all, DWORD milliseconds);

// Events: manual_reset TRUE stays signalled until ResetEvent; FALSE wakes one waiter and clears itself.
OKAY_WIN32_IMPORT HANDLE CreateEventW(LPSECURITY_ATTRIBUTES attributes, BOOL manual_reset, BOOL initial_state, LPCWSTR name);
OKAY_WIN32_IMPORT HANDLE CreateEventA(LPSECURITY_ATTRIBUTES attributes, BOOL manual_reset, BOOL initial_state, LPCSTR name);
OKAY_WIN32_IMPORT BOOL SetEvent(HANDLE event);
OKAY_WIN32_IMPORT BOOL ResetEvent(HANDLE event);

// Named or unnamed mutex objects (kernel objects, not std::mutex).
OKAY_WIN32_IMPORT HANDLE CreateMutexW(LPSECURITY_ATTRIBUTES attributes, BOOL initial_owner, LPCWSTR name);
OKAY_WIN32_IMPORT BOOL ReleaseMutex(HANDLE mutex);

typedef struct _RTL_CRITICAL_SECTION {
    void* DebugInfo;
    LONG LockCount;
    LONG RecursionCount;
    HANDLE OwningThread;
    HANDLE LockSemaphore;
    ULONG_PTR SpinCount;
} CRITICAL_SECTION, *PCRITICAL_SECTION, *LPCRITICAL_SECTION, RTL_CRITICAL_SECTION;

OKAY_WIN32_IMPORT void InitializeCriticalSection(LPCRITICAL_SECTION cs);
OKAY_WIN32_IMPORT BOOL InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION cs, DWORD spin_count);
OKAY_WIN32_IMPORT void DeleteCriticalSection(LPCRITICAL_SECTION cs);
OKAY_WIN32_IMPORT void EnterCriticalSection(LPCRITICAL_SECTION cs);
OKAY_WIN32_IMPORT BOOL TryEnterCriticalSection(LPCRITICAL_SECTION cs);
OKAY_WIN32_IMPORT void LeaveCriticalSection(LPCRITICAL_SECTION cs);

typedef struct _RTL_SRWLOCK {
    PVOID Ptr;
} SRWLOCK, *PSRWLOCK, RTL_SRWLOCK;
#define SRWLOCK_INIT {0}
OKAY_WIN32_IMPORT void InitializeSRWLock(PSRWLOCK lock);
OKAY_WIN32_IMPORT void AcquireSRWLockExclusive(PSRWLOCK lock);
OKAY_WIN32_IMPORT void ReleaseSRWLockExclusive(PSRWLOCK lock);
OKAY_WIN32_IMPORT BOOLEAN TryAcquireSRWLockExclusive(PSRWLOCK lock);
OKAY_WIN32_IMPORT void AcquireSRWLockShared(PSRWLOCK lock);
OKAY_WIN32_IMPORT void ReleaseSRWLockShared(PSRWLOCK lock);

typedef struct _RTL_CONDITION_VARIABLE {
    PVOID Ptr;
} CONDITION_VARIABLE, *PCONDITION_VARIABLE, RTL_CONDITION_VARIABLE;
#define CONDITION_VARIABLE_INIT {0}
#define CONDITION_VARIABLE_LOCKMODE_SHARED 0x1
OKAY_WIN32_IMPORT void InitializeConditionVariable(PCONDITION_VARIABLE cv);
OKAY_WIN32_IMPORT BOOL SleepConditionVariableCS(PCONDITION_VARIABLE cv, PCRITICAL_SECTION cs, DWORD milliseconds);
OKAY_WIN32_IMPORT BOOL SleepConditionVariableSRW(PCONDITION_VARIABLE cv, PSRWLOCK lock, DWORD milliseconds, ULONG flags);
OKAY_WIN32_IMPORT void WakeConditionVariable(PCONDITION_VARIABLE cv);
OKAY_WIN32_IMPORT void WakeAllConditionVariable(PCONDITION_VARIABLE cv);
