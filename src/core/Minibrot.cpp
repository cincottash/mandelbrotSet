#include "Minibrot.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace core {

namespace {

// A complex number of MPFR reals.
struct MpC {
    mpfr_t re, im;
    explicit MpC(long bits)
    {
        mpfr_init2(re, bits);
        mpfr_init2(im, bits);
        mpfr_set_zero(re, 1);
        mpfr_set_zero(im, 1);
    }
    ~MpC()
    {
        mpfr_clear(re);
        mpfr_clear(im);
    }
    MpC(const MpC&) = delete;
    MpC& operator=(const MpC&) = delete;
};

// r = a * b (r may be a or b).
void mul(MpC& r, const MpC& a, const MpC& b, MpC& t)
{
    mpfr_mul(t.re, a.re, b.re, MPFR_RNDN);
    mpfr_mul(t.im, a.im, b.im, MPFR_RNDN);
    mpfr_sub(t.re, t.re, t.im, MPFR_RNDN);            // re part
    mpfr_mul(t.im, a.re, b.im, MPFR_RNDN);
    mpfr_fma(t.im, a.im, b.re, t.im, MPFR_RNDN);      // im part
    mpfr_set(r.re, t.re, MPFR_RNDN);
    mpfr_set(r.im, t.im, MPFR_RNDN);
}

ComplexExp toCE(const MpC& z)
{
    auto fe = [](mpfr_srcptr v) {
        if (mpfr_zero_p(v) || !mpfr_number_p(v)) return FloatExp();
        long ex = 0;
        const double m = mpfr_get_d_2exp(&ex, v, MPFR_RNDN);
        return FloatExp(m, ex);
    };
    return ComplexExp(fe(z.re), fe(z.im));
}

void setCE(MpC& z, const ComplexExp& v)
{
    BigFloat a(long(mpfr_get_prec(z.re))), b(long(mpfr_get_prec(z.im)));
    a.set(v.re());
    b.set(v.im());
    mpfr_set(z.re, a.raw(), MPFR_RNDN);
    mpfr_set(z.im, b.raw(), MPFR_RNDN);
}

}  // namespace

int findPeriod(const BigFloat& x, const BigFloat& y, const FloatExp& radius, int maxPeriod, const std::atomic<bool>* cancel)
{
    const long bits = std::max<long>(64, long(-radius.log2Abs()) + 64);
    MpC c(bits), z(bits), t(bits);
    mpfr_set(c.re, x.raw(), MPFR_RNDN);
    mpfr_set(c.im, y.raw(), MPFR_RNDN);
    ComplexExp dz;   // dz/dc, which grows far past what a double holds
    const ComplexExp one(1.0, 0.0);
    for (int n = 1; n <= maxPeriod; ++n) {
        // dz' = 2 z dz + 1, then z' = z^2 + c
        dz = toCE(z) * dz * FloatExp(2.0) + one;
        mul(z, z, z, t);
        mpfr_add(z.re, z.re, c.re, MPFR_RNDN);
        mpfr_add(z.im, z.im, c.im, MPFR_RNDN);
        const ComplexExp zc = toCE(z);
        if (zc.log2Abs() > 2.0) return 0;   // |z| > 4: escaped before any nucleus showed up
        // The disk of points around c maps to about a disk of radius |dz| r around z: when it holds 0, a nucleus of
        // period n is inside.
        if (zc.abs() < dz.abs() * radius) return n;
        if (cancel && (n & 1023) == 0 && cancel->load()) return 0;
    }
    return 0;
}

bool findNucleus(BigFloat& x, BigFloat& y, int period, long precision, const std::atomic<bool>* cancel)
{
    const long bits = std::max<long>(precision, 64);
    MpC c(bits), z(bits), dz(bits), t(bits), step(bits);
    mpfr_set(c.re, x.raw(), MPFR_RNDN);
    mpfr_set(c.im, y.raw(), MPFR_RNDN);
    double lastStep = INFINITY;
    for (int it = 0; it < 80; ++it) {
        mpfr_set_zero(z.re, 1);
        mpfr_set_zero(z.im, 1);
        mpfr_set_zero(dz.re, 1);
        mpfr_set_zero(dz.im, 1);
        for (int n = 0; n < period; ++n) {
            // dz = 2 z dz + 1
            mul(dz, z, dz, t);
            mpfr_mul_2ui(dz.re, dz.re, 1, MPFR_RNDN);
            mpfr_mul_2ui(dz.im, dz.im, 1, MPFR_RNDN);
            mpfr_add_ui(dz.re, dz.re, 1, MPFR_RNDN);
            mul(z, z, z, t);
            mpfr_add(z.re, z.re, c.re, MPFR_RNDN);
            mpfr_add(z.im, z.im, c.im, MPFR_RNDN);
            if (cancel && (n & 4095) == 0 && cancel->load()) return false;
        }
        // step = z / dz
        const ComplexExp zc = toCE(z), dc = toCE(dz);
        if (dc.isZero()) return false;
        // Division in wide numbers: z / dz = z * conj(dz) / |dz|^2.
        const ComplexExp conjD(dc.x, -dc.y, dc.e);
        const FloatExp mag2 = dc.abs() * dc.abs();
        const ComplexExp s = (zc * conjD) * (FloatExp(1.0) / mag2);
        setCE(step, s);
        mpfr_sub(c.re, c.re, step.re, MPFR_RNDN);
        mpfr_sub(c.im, c.im, step.im, MPFR_RNDN);
        const double ls = s.log2Abs();
        if (!std::isfinite(ls) || ls < -double(bits) + 24) break;   // settled to the precision in use
        if (it > 20 && ls > lastStep + 4) return false;              // wandering off
        lastStep = ls;
    }
    x = BigFloat(bits);
    y = BigFloat(bits);
    mpfr_set(x.raw(), c.re, MPFR_RNDN);
    mpfr_set(y.raw(), c.im, MPFR_RNDN);
    return true;
}

ComplexExp minibrotSize(const BigFloat& x, const BigFloat& y, int period)
{
    const long bits = std::max(x.precision(), y.precision());
    MpC c(bits), z(bits), t(bits);
    mpfr_set(c.re, x.raw(), MPFR_RNDN);
    mpfr_set(c.im, y.raw(), MPFR_RNDN);
    ComplexExp l(1.0, 0.0), b(1.0, 0.0);
    for (int j = 1; j < period; ++j) {
        mul(z, z, z, t);
        mpfr_add(z.re, z.re, c.re, MPFR_RNDN);
        mpfr_add(z.im, z.im, c.im, MPFR_RNDN);
        l = toCE(z) * l * FloatExp(2.0);
        // b += 1 / l
        const ComplexExp conjL(l.x, -l.y, l.e);
        const FloatExp m2 = l.abs() * l.abs();
        b = b + conjL * (FloatExp(1.0) / m2);
    }
    // size = 1 / (b l^2)
    const ComplexExp d = b * l * l;
    const ComplexExp conjD(d.x, -d.y, d.e);
    const FloatExp m2 = d.abs() * d.abs();
    return conjD * (FloatExp(1.0) / m2);
}

Minibrot findMinibrot(const View& v, int maxPeriod, const std::atomic<bool>* cancel)
{
    Minibrot m;
    const FloatExp radius = v.scale * FloatExp(1.2);
    const int p = findPeriod(v.cx, v.cy, radius, maxPeriod, cancel);
    if (p <= 0) return m;
    BigFloat x = v.cx, y = v.cy;
    // Enough bits for the minibrot, which is far smaller than the view.
    const long bits = std::max<long>(v.neededPrecision() * 2 + 64, 128);
    x.setPrecision(bits);
    y.setPrecision(bits);
    if (!findNucleus(x, y, p, bits, cancel)) return m;
    m.period = p;
    m.x = x;
    m.y = y;
    m.size = minibrotSize(x, y, p);
    m.found = !m.size.isZero() && std::isfinite(m.size.log2Abs());
    return m;
}

View minibrotHome(const Minibrot& m)
{
    View home = View::home(false);
    View v;
    const long bits = std::max(m.x.precision(), long(-m.size.log2Abs()) + 96);
    // center = nucleus + size * (home center)
    const ComplexExp off = m.size * home.cx.toFloatExp();
    BigFloat ox(bits), oy(bits);
    ox.set(off.re());
    oy.set(off.im());
    v.cx = m.x + ox;
    v.cy = m.y + oy;
    v.cx.setPrecision(bits);
    v.cy.setPrecision(bits);
    v.scale = FloatExp(View::kHomeScale) * m.size.abs();
    v.angle = std::atan2(m.size.y, m.size.x);
    return v;
}

// ---------------------------------------------------------------- the self-similar path

LoopPath::LoopPath(const Minibrot& m) : m_m(m) {}

double LoopPath::doublings() const { return -m_m.size.log2Abs(); }

View LoopPath::at(double t) const
{
    const View to = minibrotHome(m_m);
    return zoomPathAt(View::home(false), to, t);
}

View zoomPathAt(const View& from, const View& to, double t)
{
    // t may go past 1: the path keeps zooming the same way (a loop's last frames blend into its first).
    t = std::clamp(t, 0.0, 4.0);
    if (t <= 0.0) return from;
    if (t == 1.0) return to;
    double da = std::remainder(to.angle - from.angle, 2 * M_PI);
    // The shrink-and-turn map T(w) = N + r w that takes the start view to the end view, with r = (s1/s0) e^(i da).
    const FloatExp ratio = to.scale / from.scale;
    const ComplexExp r = ComplexExp(std::cos(da), std::sin(da)) * ratio;
    const long extra = t > 1.0 ? long(std::ceil((t - 1.0) * -ratio.log2Abs())) : 0;
    const long bits = std::max({from.cx.precision(), to.cx.precision(), to.neededPrecision() + 32 + extra});
    View v;
    v.scale = from.scale * FloatExp(std::exp2(ratio.log2Abs() * t - std::floor(ratio.log2Abs() * t)),
                                    int64_t(std::floor(ratio.log2Abs() * t)));
    v.angle = std::remainder(from.angle + da * t, 2 * M_PI);
    const std::complex<double> oneMinusR = std::complex<double>(1.0, 0.0) - r.toComplex();
    if (std::abs(oneMinusR) < 1e-9) {
        // No zoom at all: slide straight across.
        BigFloat tx(bits), ty(bits);
        tx.set(t);
        v.cx = from.cx + (to.cx - from.cx) * tx;
        v.cy = from.cy + (to.cy - from.cy) * tx;
        return v;
    }
    // N = to.c - r * from.c, F = N / (1 - r): the point the zoom closes in on.
    MpC fc(bits), rr(bits), tmp(bits), n(bits), k(bits), f(bits);
    mpfr_set(fc.re, from.cx.raw(), MPFR_RNDN);
    mpfr_set(fc.im, from.cy.raw(), MPFR_RNDN);
    setCE(rr, r);
    mul(n, rr, fc, tmp);
    mpfr_sub(n.re, to.cx.raw(), n.re, MPFR_RNDN);
    mpfr_sub(n.im, to.cy.raw(), n.im, MPFR_RNDN);
    // k = 1 / (1 - r), computed with MPFR so the end of the path lands exactly on the end view.
    MpC om(bits);
    mpfr_ui_sub(om.re, 1, rr.re, MPFR_RNDN);
    mpfr_neg(om.im, rr.im, MPFR_RNDN);
    mpfr_t den;
    mpfr_init2(den, bits);
    mpfr_sqr(den, om.re, MPFR_RNDN);
    mpfr_fma(den, om.im, om.im, den, MPFR_RNDN);
    mpfr_div(k.re, om.re, den, MPFR_RNDN);
    mpfr_div(k.im, om.im, den, MPFR_RNDN);
    mpfr_neg(k.im, k.im, MPFR_RNDN);
    mpfr_clear(den);
    mul(f, n, k, tmp);
    // center(t) = F + r^t (from.c - F)
    MpC d(bits);
    mpfr_sub(d.re, fc.re, f.re, MPFR_RNDN);
    mpfr_sub(d.im, fc.im, f.im, MPFR_RNDN);
    const ComplexExp dce = toCE(d);
    const double lr = ratio.log2Abs() * t;
    const ComplexExp rt = ComplexExp(std::cos(da * t), std::sin(da * t)) *
                          FloatExp(std::exp2(lr - std::floor(lr)), int64_t(std::floor(lr)));
    const ComplexExp off = rt * dce;
    BigFloat ox(bits), oy(bits);
    ox.set(off.re());
    oy.set(off.im());
    v.cx = BigFloat(bits);
    v.cy = BigFloat(bits);
    mpfr_add(v.cx.raw(), f.re, ox.raw(), MPFR_RNDN);
    mpfr_add(v.cy.raw(), f.im, oy.raw(), MPFR_RNDN);
    return v;
}

}  // namespace core
