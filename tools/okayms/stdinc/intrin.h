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

// Bit scans and the 64 x 64 -> 128-bit product, as Microsoft documents _BitScanForward, _BitScanReverse (and the 64-bit
// forms) and _umul128 for x64: a scan stores the index of the lowest (forward) or highest (reverse) set bit and returns
// 1, or returns 0 when the mask is 0 (the index is then left as it was); _umul128 returns the low half and stores the
// high half. Here they are plain inline functions (written from those descriptions), not single instructions yet.
inline unsigned char _BitScanForward64(unsigned long* index, unsigned __int64 mask) {
    if (mask == 0) return 0;
    unsigned long i = 0;
    while ((mask & 1) == 0) {
        mask >>= 1;
        ++i;
    }
    *index = i;
    return 1;
}
inline unsigned char _BitScanReverse64(unsigned long* index, unsigned __int64 mask) {
    if (mask == 0) return 0;
    unsigned long i = 63;
    while ((mask >> i) == 0) --i;
    *index = i;
    return 1;
}
inline unsigned char _BitScanForward(unsigned long* index, unsigned long mask) {
    return _BitScanForward64(index, mask);
}
inline unsigned char _BitScanReverse(unsigned long* index, unsigned long mask) {
    return _BitScanReverse64(index, mask);
}
inline unsigned __int64 _umul128(unsigned __int64 a, unsigned __int64 b, unsigned __int64* high) {
    // Base 2^32 digits: each partial sum fits in 64 bits because (2^32-1)^2 + 2 (2^32-1) = 2^64 - 1.
    const unsigned __int64 a0 = a & 0xFFFFFFFFull, a1 = a >> 32, b0 = b & 0xFFFFFFFFull, b1 = b >> 32;
    const unsigned __int64 low = a0 * b0;
    const unsigned __int64 t = a1 * b0 + (low >> 32);
    const unsigned __int64 u = a0 * b1 + (t & 0xFFFFFFFFull);
    *high = a1 * b1 + (t >> 32) + (u >> 32);
    return (u << 32) | (low & 0xFFFFFFFFull);
}
