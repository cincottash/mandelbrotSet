// Numbers with a wide exponent range: a double mantissa and a 64-bit power-of-two exponent.
// Deep zooms need values like 1e-5000, far below what a double can hold (about 1e-308).
#pragma once

#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>

namespace core {

// value = m * 2^e, with 0.5 <= |m| < 1, or m == 0 (then e is kZeroExp).
struct FloatExp {
    static constexpr int64_t kZeroExp = std::numeric_limits<int32_t>::min() / 2;
    double m = 0.0;
    int64_t e = kZeroExp;

    FloatExp() = default;
    FloatExp(double v) { set(v, 0); }
    FloatExp(double mant, int64_t ex) { set(mant, ex); }

    void set(double mant, int64_t ex)
    {
        if (mant == 0.0 || !std::isfinite(mant)) {
            m = std::isfinite(mant) ? 0.0 : mant;
            e = std::isfinite(mant) ? kZeroExp : 0;
            return;
        }
        int k = 0;
        m = std::frexp(mant, &k);
        e = ex + k;
    }
    bool isZero() const { return m == 0.0; }
    // log2(|value|); -inf for zero.
    double log2Abs() const { return isZero() ? -INFINITY : std::log2(std::fabs(m)) + double(e); }
    // The nearest double (0 or inf when out of range).
    double toDouble() const
    {
        if (isZero()) return 0.0;
        if (e > 1100) return m > 0 ? INFINITY : -INFINITY;
        if (e < -1100) return 0.0;
        return std::ldexp(m, int(e));
    }
    FloatExp operator-() const { FloatExp r = *this; r.m = -r.m; return r; }
    friend FloatExp operator*(const FloatExp& a, const FloatExp& b)
    {
        if (a.isZero() || b.isZero()) return {};
        return FloatExp(a.m * b.m, a.e + b.e);
    }
    friend FloatExp operator/(const FloatExp& a, const FloatExp& b)
    {
        if (a.isZero()) return {};
        return FloatExp(a.m / b.m, a.e - b.e);
    }
    friend FloatExp operator+(const FloatExp& a, const FloatExp& b)
    {
        if (a.isZero()) return b;
        if (b.isZero()) return a;
        const int64_t d = a.e - b.e;
        if (d > 80) return a;
        if (d < -80) return b;
        if (d >= 0) return FloatExp(a.m + std::ldexp(b.m, int(-d)), a.e);
        return FloatExp(std::ldexp(a.m, int(d)) + b.m, b.e);
    }
    friend FloatExp operator-(const FloatExp& a, const FloatExp& b) { return a + (-b); }
    friend bool operator<(const FloatExp& a, const FloatExp& b)   // compares values (signs included)
    {
        if (a.m < 0 && b.m >= 0) return true;
        if (a.m >= 0 && b.m < 0) return false;
        if (a.isZero()) return b.m > 0;
        if (b.isZero()) return a.m < 0;
        const bool neg = a.m < 0;
        if (a.e != b.e) return neg ? a.e > b.e : a.e < b.e;
        return a.m < b.m;
    }
    friend bool operator>(const FloatExp& a, const FloatExp& b) { return b < a; }
    FloatExp abs() const { FloatExp r = *this; r.m = std::fabs(r.m); return r; }
    FloatExp sqrt() const
    {
        if (m <= 0) return {};
        // m * 2^e with an even exponent, so the square root halves it exactly.
        double mm = m;
        int64_t ee = e;
        if (ee & 1) { mm *= 2.0; ee -= 1; }
        return FloatExp(std::sqrt(mm), ee / 2);
    }
};

// A complex number sharing one exponent: (x + iy) * 2^e, with max(|x|,|y|) in [0.5, 1) or both zero.
struct ComplexExp {
    double x = 0.0, y = 0.0;
    int64_t e = FloatExp::kZeroExp;

    ComplexExp() = default;
    ComplexExp(double re, double im, int64_t ex = 0) { set(re, im, ex); }
    explicit ComplexExp(std::complex<double> c) { set(c.real(), c.imag(), 0); }
    ComplexExp(const FloatExp& re, const FloatExp& im)
    {
        if (re.isZero() && im.isZero()) return;
        const int64_t ex = std::max(re.isZero() ? FloatExp::kZeroExp : re.e, im.isZero() ? FloatExp::kZeroExp : im.e);
        set(shift(re.m, re.isZero() ? -2000 : re.e - ex), shift(im.m, im.isZero() ? -2000 : im.e - ex), ex);
    }

    static double shift(double v, int64_t d) { return d < -1100 ? 0.0 : std::ldexp(v, int(d)); }
    void set(double re, double im, int64_t ex)
    {
        const double mx = std::max(std::fabs(re), std::fabs(im));
        if (mx == 0.0 || !std::isfinite(mx)) {
            x = std::isfinite(mx) ? 0.0 : re;
            y = std::isfinite(mx) ? 0.0 : im;
            e = std::isfinite(mx) ? FloatExp::kZeroExp : 0;
            return;
        }
        int k = 0;
        std::frexp(mx, &k);
        x = std::ldexp(re, -k);
        y = std::ldexp(im, -k);
        e = ex + k;
    }
    bool isZero() const { return x == 0.0 && y == 0.0; }
    FloatExp re() const { return isZero() ? FloatExp() : FloatExp(x, e); }
    FloatExp im() const { return isZero() ? FloatExp() : FloatExp(y, e); }
    // |z| as a FloatExp, and log2|z|.
    FloatExp abs() const { return isZero() ? FloatExp() : FloatExp(std::hypot(x, y), e); }
    double log2Abs() const { return isZero() ? -INFINITY : std::log2(std::hypot(x, y)) + double(e); }
    std::complex<double> toComplex() const
    {
        if (isZero()) return {};
        return {shift(x, e), shift(y, e)};
    }
    friend ComplexExp operator*(const ComplexExp& a, const ComplexExp& b)
    {
        if (a.isZero() || b.isZero()) return {};
        return ComplexExp(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x, a.e + b.e);
    }
    friend ComplexExp operator*(const ComplexExp& a, const FloatExp& s)
    {
        if (a.isZero() || s.isZero()) return {};
        return ComplexExp(a.x * s.m, a.y * s.m, a.e + s.e);
    }
    friend ComplexExp operator+(const ComplexExp& a, const ComplexExp& b)
    {
        if (a.isZero()) return b;
        if (b.isZero()) return a;
        const int64_t d = a.e - b.e;
        if (d > 80) return a;
        if (d < -80) return b;
        if (d >= 0) return ComplexExp(a.x + std::ldexp(b.x, int(-d)), a.y + std::ldexp(b.y, int(-d)), a.e);
        return ComplexExp(std::ldexp(a.x, int(d)) + b.x, std::ldexp(a.y, int(d)) + b.y, b.e);
    }
    ComplexExp operator-() const { ComplexExp r = *this; r.x = -r.x; r.y = -r.y; return r; }
    friend ComplexExp operator-(const ComplexExp& a, const ComplexExp& b) { return a + (-b); }
};

}  // namespace core
