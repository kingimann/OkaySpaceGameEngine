// okay-ms own <process.h>: the Microsoft C runtime's thread start and end functions, declared in our own words from
// Microsoft's public C runtime documentation of _beginthreadex and _endthreadex. They live in the C runtime DLL
// (msvcrt.dll or the universal CRT), not in okay-ms's own runtime. _beginthreadex returns the thread handle as an
// integer (0 when it failed); the caller closes it with CloseHandle.
#pragma once
extern "C" {
__declspec(dllimport) unsigned long long _beginthreadex(void* security, unsigned stack_size,
                                                        unsigned (*start)(void*), void* arg, unsigned flags,
                                                        unsigned* thread_id);
__declspec(dllimport) void _endthreadex(unsigned exit_code);
__declspec(dllimport) int _getpid(void);
}
