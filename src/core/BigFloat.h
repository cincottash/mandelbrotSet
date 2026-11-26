// Arbitrary-precision real numbers (MPFR), for the view's center and the reference orbit.
#pragma once

#include "FloatExp.h"

#include <mpfr.h>

#include <string>

namespace core {

class BigFloat {
public:
    explicit BigFloat(long bits = 128);
    BigFloat(double v, long bits);
    BigFloat(const BigFloat& o);
    BigFloat(BigFloat&& o) noexcept;
    BigFloat& operator=(const BigFloat& o);
    BigFloat& operator=(BigFloat&& o) noexcept;
    ~BigFloat();

    long precision() const { return long(mpfr_get_prec(m_v)); }
    // Change the precision, keeping the value (rounded when it shrinks).
    void setPrecision(long bits);

    // Plain decimal ("-0.743643887", "1.5e-300" is accepted too). False on bad text (the value is unchanged).
    bool parse(const std::string& text);
    // Plain decimal text with enough digits for the precision, without trailing zeros: "-0.7436438870371587".
    std::string toString(int digits = 0) const;

    double toDouble() const { return mpfr_get_d(m_v, MPFR_RNDN); }
    FloatExp toFloatExp() const;
    void set(double v) { mpfr_set_d(m_v, v, MPFR_RNDN); }
    void set(const FloatExp& v);
    bool isZero() const { return mpfr_zero_p(m_v) != 0; }
    bool isFinite() const { return mpfr_number_p(m_v) != 0; }

    friend BigFloat operator+(const BigFloat& a, const BigFloat& b);
    friend BigFloat operator-(const BigFloat& a, const BigFloat& b);
    friend BigFloat operator*(const BigFloat& a, const BigFloat& b);
    BigFloat operator-() const;

    mpfr_ptr raw() { return m_v; }
    mpfr_srcptr raw() const { return m_v; }

private:
    mpfr_t m_v;
};

}  // namespace core
