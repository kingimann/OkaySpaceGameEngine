// Textual include, inside a namespace that already has Fe, Mul and Square (Fe25519_16.hpp and
// Fe25519_64.hpp): the two fixed addition chains for inversion and for the square-root exponent.
// The exponents are public constants, so nothing here depends on a secret value. Every step is
// a Square or a Mul; the chains are the standard ones for 2^255 - 19 and are checked against the
// bit-by-bit PowPublic in tests/test_crypto_field.cpp.

// r = a^(2^n) (n >= 1)
inline void SquareN(Fe& r, const Fe& a, int n) {
    Square(r, a);
    for (int i = 1; i < n; ++i) Square(r, r);
}

// a^(p-2) = a^(2^255 - 21): the inverse (0 maps to 0).
inline void Invert(Fe& r, const Fe& z) {
    Fe t0, t1, t2, t3;
    Square(t0, z);          // z^2
    SquareN(t1, t0, 2);     // z^8
    Mul(t1, z, t1);         // z^9
    Mul(t0, t0, t1);        // z^11
    Square(t2, t0);         // z^22
    Mul(t1, t1, t2);        // z^(2^5 - 1)
    SquareN(t2, t1, 5);
    Mul(t1, t2, t1);        // 2^10 - 1
    SquareN(t2, t1, 10);
    Mul(t2, t2, t1);        // 2^20 - 1
    SquareN(t3, t2, 20);
    Mul(t2, t3, t2);        // 2^40 - 1
    SquareN(t2, t2, 10);
    Mul(t1, t2, t1);        // 2^50 - 1
    SquareN(t2, t1, 50);
    Mul(t2, t2, t1);        // 2^100 - 1
    SquareN(t3, t2, 100);
    Mul(t2, t3, t2);        // 2^200 - 1
    SquareN(t2, t2, 50);
    Mul(t1, t2, t1);        // 2^250 - 1
    SquareN(t1, t1, 5);     // 2^255 - 32
    Mul(r, t1, t0);         // 2^255 - 21
}

// a^((p-5)/8) = a^(2^252 - 3), for square roots.
inline void Pow22523(Fe& r, const Fe& z) {
    Fe t0, t1, t2;
    Square(t0, z);          // z^2
    SquareN(t1, t0, 2);     // z^8
    Mul(t1, z, t1);         // z^9
    Mul(t0, t0, t1);        // z^11
    Square(t0, t0);         // z^22
    Mul(t0, t1, t0);        // 2^5 - 1
    SquareN(t1, t0, 5);
    Mul(t0, t1, t0);        // 2^10 - 1
    SquareN(t1, t0, 10);
    Mul(t1, t1, t0);        // 2^20 - 1
    SquareN(t2, t1, 20);
    Mul(t1, t2, t1);        // 2^40 - 1
    SquareN(t1, t1, 10);
    Mul(t0, t1, t0);        // 2^50 - 1
    SquareN(t1, t0, 50);
    Mul(t1, t1, t0);        // 2^100 - 1
    SquareN(t2, t1, 100);
    Mul(t1, t2, t1);        // 2^200 - 1
    SquareN(t1, t1, 50);
    Mul(t0, t1, t0);        // 2^250 - 1
    SquareN(t0, t0, 2);     // 2^252 - 4
    Mul(r, t0, z);          // 2^252 - 3
}
