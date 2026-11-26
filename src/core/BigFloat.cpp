#include "BigFloat.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace core {

BigFloat::BigFloat(long bits)
{
    mpfr_init2(m_v, std::max<long>(bits, MPFR_PREC_MIN));
    mpfr_set_zero(m_v, 1);
}

BigFloat::BigFloat(double v, long bits) : BigFloat(bits) { mpfr_set_d(m_v, v, MPFR_RNDN); }

BigFloat::BigFloat(const BigFloat& o)
{
    mpfr_init2(m_v, mpfr_get_prec(o.m_v));
    mpfr_set(m_v, o.m_v, MPFR_RNDN);
}

BigFloat::BigFloat(BigFloat&& o) noexcept
{
    mpfr_init2(m_v, MPFR_PREC_MIN);
    mpfr_swap(m_v, o.m_v);
}

BigFloat& BigFloat::operator=(const BigFloat& o)
{
    if (this != &o) {
        mpfr_set_prec(m_v, mpfr_get_prec(o.m_v));
        mpfr_set(m_v, o.m_v, MPFR_RNDN);
    }
    return *this;
}

BigFloat& BigFloat::operator=(BigFloat&& o) noexcept
{
    if (this != &o) mpfr_swap(m_v, o.m_v);
    return *this;
}

BigFloat::~BigFloat() { mpfr_clear(m_v); }

void BigFloat::setPrecision(long bits)
{
    bits = std::max<long>(bits, MPFR_PREC_MIN);
    if (bits != precision()) mpfr_prec_round(m_v, bits, MPFR_RNDN);
}

bool BigFloat::parse(const std::string& text)
{
    std::string t;
    for (char ch : text)
        if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r' && ch != '_') t += ch;
    if (t.empty()) return false;
    // Only plain numbers: digits, one point, an optional sign and an optional exponent. MPFR alone would also
    // accept "nan", "inf" and hexadecimal.
    size_t i = 0;
    if (t[i] == '+' || t[i] == '-') ++i;
    bool digits = false, point = false;
    for (; i < t.size() && t[i] != 'e' && t[i] != 'E'; ++i) {
        if (t[i] >= '0' && t[i] <= '9') digits = true;
        else if (t[i] == '.' && !point) point = true;
        else return false;
    }
    if (!digits) return false;
    if (i < t.size()) {
        ++i;
        if (i < t.size() && (t[i] == '+' || t[i] == '-')) ++i;
        if (i >= t.size()) return false;
        for (; i < t.size(); ++i)
            if (t[i] < '0' || t[i] > '9') return false;
    }
    mpfr_t tmp;
    mpfr_init2(tmp, mpfr_get_prec(m_v));
    char* end = nullptr;
    const int r = mpfr_strtofr(tmp, t.c_str(), &end, 10, MPFR_RNDN);
    (void)r;
    const bool ok = end && *end == '\0' && mpfr_number_p(tmp);
    if (ok) mpfr_set(m_v, tmp, MPFR_RNDN);
    mpfr_clear(tmp);
    return ok;
}

std::string BigFloat::toString(int digits) const
{
    if (mpfr_zero_p(m_v)) return "0";
    if (!mpfr_number_p(m_v)) return "0";
    if (digits <= 0) digits = int(std::ceil(double(precision()) * 0.30103)) + 2;
    mpfr_exp_t exp10 = 0;
    char* s = mpfr_get_str(nullptr, &exp10, 10, size_t(digits), m_v, MPFR_RNDN);
    std::string mant(s);
    mpfr_free_str(s);
    bool neg = false;
    if (!mant.empty() && mant[0] == '-') {
        neg = true;
        mant.erase(0, 1);
    }
    // The value is 0.<mant> * 10^exp10. Write it as plain decimal.
    std::string out;
    if (exp10 <= 0) {
        out = "0." + std::string(size_t(-exp10), '0') + mant;
    } else if (size_t(exp10) >= mant.size()) {
        out = mant + std::string(size_t(exp10) - mant.size(), '0');
    } else {
        out = mant.substr(0, size_t(exp10)) + "." + mant.substr(size_t(exp10));
    }
    if (out.find('.') != std::string::npos) {
        while (!out.empty() && out.back() == '0') out.pop_back();
        if (!out.empty() && out.back() == '.') out.pop_back();
    }
    if (out.empty()) out = "0";
    return (neg && out != "0") ? "-" + out : out;
}

FloatExp BigFloat::toFloatExp() const
{
    if (mpfr_zero_p(m_v) || !mpfr_number_p(m_v)) return {};
    long ex = 0;
    const double m = mpfr_get_d_2exp(&ex, m_v, MPFR_RNDN);
    return FloatExp(m, int64_t(ex));
}

void BigFloat::set(const FloatExp& v)
{
    if (v.isZero()) {
        mpfr_set_zero(m_v, 1);
        return;
    }
    mpfr_set_d(m_v, v.m, MPFR_RNDN);
    if (v.e >= 0) mpfr_mul_2ui(m_v, m_v, (unsigned long)v.e, MPFR_RNDN);
    else mpfr_div_2ui(m_v, m_v, (unsigned long)(-v.e), MPFR_RNDN);
}

BigFloat operator+(const BigFloat& a, const BigFloat& b)
{
    BigFloat r(std::max(a.precision(), b.precision()));
    mpfr_add(r.m_v, a.m_v, b.m_v, MPFR_RNDN);
    return r;
}

BigFloat operator-(const BigFloat& a, const BigFloat& b)
{
    BigFloat r(std::max(a.precision(), b.precision()));
    mpfr_sub(r.m_v, a.m_v, b.m_v, MPFR_RNDN);
    return r;
}

BigFloat operator*(const BigFloat& a, const BigFloat& b)
{
    BigFloat r(std::max(a.precision(), b.precision()));
    mpfr_mul(r.m_v, a.m_v, b.m_v, MPFR_RNDN);
    return r;
}

BigFloat BigFloat::operator-() const
{
    BigFloat r(precision());
    mpfr_neg(r.m_v, m_v, MPFR_RNDN);
    return r;
}

}  // namespace core
