// okay-ms own <intrin.h>: the interlocked intrinsics, declared in our own words from Microsoft's documentation of
// _InterlockedExchange, _InterlockedExchangeAdd, _InterlockedCompareExchange, _InterlockedIncrement and
// _InterlockedDecrement. okay-ms compiles a call of one of these names inline as the x64 instruction with a LOCK prefix
// (XCHG, XADD, CMPXCHG), so it is atomic between threads and a full memory barrier; no function is called or linked.
// Each returns the value the target had before (increment and decrement: the new value). `long` is 32 bits on Windows.
#pragma once
extern "C" {
char _InterlockedExchange8(char volatile* target, char value);
short _InterlockedExchange16(short volatile* target, short value);
long _InterlockedExchange(long volatile* target, long value);
__int64 _InterlockedExchange64(__int64 volatile* target, __int64 value);
char _InterlockedExchangeAdd8(char volatile* addend, char value);
short _InterlockedExchangeAdd16(short volatile* addend, short value);
long _InterlockedExchangeAdd(long volatile* addend, long value);
__int64 _InterlockedExchangeAdd64(__int64 volatile* addend, __int64 value);
char _InterlockedCompareExchange8(char volatile* destination, char exchange, char comparand);
short _InterlockedCompareExchange16(short volatile* destination, short exchange, short comparand);
long _InterlockedCompareExchange(long volatile* destination, long exchange, long comparand);
__int64 _InterlockedCompareExchange64(__int64 volatile* destination, __int64 exchange, __int64 comparand);
short _InterlockedIncrement16(short volatile* addend);
long _InterlockedIncrement(long volatile* addend);
__int64 _InterlockedIncrement64(__int64 volatile* addend);
short _InterlockedDecrement16(short volatile* addend);
long _InterlockedDecrement(long volatile* addend);
__int64 _InterlockedDecrement64(__int64 volatile* addend);
}
