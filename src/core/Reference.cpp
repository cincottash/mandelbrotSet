#include "Reference.h"

#include <algorithm>
#include <cmath>

namespace core {

namespace {

struct Mp {
    mpfr_t v;
    explicit Mp(long bits) { mpfr_init2(v, bits); }
    ~Mp() { mpfr_clear(v); }
    Mp(const Mp&) = delete;
    Mp& operator=(const Mp&) = delete;
};

ComplexExp toComplexExp(mpfr_srcptr x, mpfr_srcptr y)
{
    auto fe = [](mpfr_srcptr v) {
        if (mpfr_zero_p(v) || !mpfr_number_p(v)) return FloatExp();
        long ex = 0;
        const double m = mpfr_get_d_2exp(&ex, v, MPFR_RNDN);
        return FloatExp(m, int64_t(ex));
    };
    return ComplexExp(fe(x), fe(y));
}

float clampExpFloat(double v)
{
    if (!std::isfinite(v)) return v > 0 ? 3.0e38f : -3.0e38f;
    return float(std::clamp(v, -3.0e38, 3.0e38));
}

int32_t clampExp(int64_t e) { return int32_t(std::clamp<int64_t>(e, -(1 << 29), 1 << 29)); }

}  // namespace

bool computeOrbit(const OrbitRequest& rq, RefOrbit* out, const std::atomic<bool>* cancel, std::atomic<int>* progress)
{
    const long bits = std::max<long>(rq.precision, 64);
    Mp zx(bits), zy(bits), cx(bits), cy(bits), t1(bits), t2(bits), t3(bits), wx(bits), wy(bits);
    if (rq.julia) {
        mpfr_set(zx.v, rq.x.raw(), MPFR_RNDN);
        mpfr_set(zy.v, rq.y.raw(), MPFR_RNDN);
        mpfr_set(cx.v, rq.seedX.raw(), MPFR_RNDN);
        mpfr_set(cy.v, rq.seedY.raw(), MPFR_RNDN);
    } else {
        mpfr_set_zero(zx.v, 1);
        mpfr_set_zero(zy.v, 1);
        mpfr_set(cx.v, rq.x.raw(), MPFR_RNDN);
        mpfr_set(cy.v, rq.y.raw(), MPFR_RNDN);
    }
    const int maxIt = std::max(1, rq.maxIterations);
    const double bail2 = rq.escapeRadius * rq.escapeRadius;
    RefOrbit r;
    r.request = rq;
    r.z.reserve(size_t(std::min(maxIt, 1 << 22)) + 1);
    for (int n = 0;; ++n) {
        const ComplexExp zn = toComplexExp(zx.v, zy.v);
        r.z.push_back(zn);
        const std::complex<double> zd = zn.toComplex();
        const double mag2 = zd.real() * zd.real() + zd.imag() * zd.imag();
        if (mag2 > bail2 || !std::isfinite(mag2)) {
            r.length = n;
            r.escaped = true;
            break;
        }
        if (n >= maxIt) {
            r.length = n;
            break;
        }
        if ((n & 4095) == 0) {
            if (cancel && cancel->load(std::memory_order_relaxed)) return false;
            if (progress) progress->store(n, std::memory_order_relaxed);
        }
        if (rq.formula == 1) {
            // Burning Ship: x' = x^2 - y^2 + cx, y' = 2|xy| - cy.
            mpfr_sqr(t1.v, zx.v, MPFR_RNDN);
            mpfr_sqr(t2.v, zy.v, MPFR_RNDN);
            mpfr_mul(t3.v, zx.v, zy.v, MPFR_RNDN);
            mpfr_abs(t3.v, t3.v, MPFR_RNDN);
            mpfr_sub(zx.v, t1.v, t2.v, MPFR_RNDN);
            mpfr_add(zx.v, zx.v, cx.v, MPFR_RNDN);
            mpfr_mul_2ui(zy.v, t3.v, 1, MPFR_RNDN);
            mpfr_sub(zy.v, zy.v, cy.v, MPFR_RNDN);
        } else if (rq.power == 2) {
            mpfr_sqr(t1.v, zx.v, MPFR_RNDN);
            mpfr_sqr(t2.v, zy.v, MPFR_RNDN);
            mpfr_mul(t3.v, zx.v, zy.v, MPFR_RNDN);
            mpfr_sub(zx.v, t1.v, t2.v, MPFR_RNDN);
            mpfr_add(zx.v, zx.v, cx.v, MPFR_RNDN);
            mpfr_mul_2ui(zy.v, t3.v, 1, MPFR_RNDN);
            if (rq.formula == 2) mpfr_neg(zy.v, zy.v, MPFR_RNDN);   // Tricorn: conj(z)^2 = conj(z^2)
            mpfr_add(zy.v, zy.v, cy.v, MPFR_RNDN);
        } else {
            mpfr_set(wx.v, zx.v, MPFR_RNDN);
            mpfr_set(wy.v, zy.v, MPFR_RNDN);
            for (int k = 1; k < rq.power; ++k) {
                // w = w * z
                mpfr_mul(t1.v, wx.v, zx.v, MPFR_RNDN);
                mpfr_mul(t2.v, wy.v, zy.v, MPFR_RNDN);
                mpfr_mul(t3.v, wx.v, zy.v, MPFR_RNDN);
                mpfr_mul(wy.v, wy.v, zx.v, MPFR_RNDN);
                mpfr_add(wy.v, wy.v, t3.v, MPFR_RNDN);
                mpfr_sub(wx.v, t1.v, t2.v, MPFR_RNDN);
            }
            if (rq.formula == 2) mpfr_neg(wy.v, wy.v, MPFR_RNDN);   // Tricorn: conj(z)^p = conj(z^p)
            mpfr_add(zx.v, wx.v, cx.v, MPFR_RNDN);
            mpfr_add(zy.v, wy.v, cy.v, MPFR_RNDN);
        }
    }
    r.gpuF.resize(r.z.size());
    r.gpuX.resize(r.z.size());
    auto split = [](double v, float* hi, float* lo) {
        *hi = clampExpFloat(v);
        *lo = std::isfinite(v) && std::fabs(v) < 3.0e38 ? float(v - double(*hi)) : 0.0f;
    };
    for (size_t i = 0; i < r.z.size(); ++i) {
        const ComplexExp& v = r.z[i];
        const std::complex<double> d = v.toComplex();
        GpuRefF f{};
        split(d.real(), &f.hx, &f.lx);
        split(d.imag(), &f.hy, &f.ly);
        r.gpuF[i] = f;
        GpuRefX x{};
        split(v.x, &x.hx, &x.lx);
        split(v.y, &x.hy, &x.ly);
        x.e = v.isZero() ? -(1 << 29) : clampExp(v.e);
        r.gpuX[i] = x;
    }
    if (progress) progress->store(r.length, std::memory_order_relaxed);
    *out = std::move(r);
    return true;
}

bool buildBla(const RefOrbit& orbit, const FloatExp& dcMax, double epsilon, BlaTable* out, bool keepCpu,
              const std::atomic<bool>* cancel)
{
    BlaTable t;
    t.dcMax = dcMax;
    t.epsilon = epsilon;
    if (orbit.request.power != 2 || orbit.request.formula != 0 || orbit.length < 3) {
        *out = std::move(t);
        return true;
    }
    using Entry = BlaTable::Entry;
    const ComplexExp one(1.0, 0.0);
    const bool julia = orbit.request.julia;
    const FloatExp eps(epsilon);
    // Single steps: z_{m+1} = 2 Z_m z + c, valid while the dropped z^2 is tiny next to 2 Z_m z.
    std::vector<Entry> level;
    level.resize(size_t(orbit.length - 1));
    for (int m = 1; m < orbit.length; ++m) {
        const ComplexExp& zm = orbit.z[size_t(m)];
        Entry e;
        e.a = zm * FloatExp(2.0);
        e.b = julia ? ComplexExp() : one;
        e.r = zm.abs() * eps;
        level[size_t(m - 1)] = e;
    }
    if (keepCpu) t.cpu.push_back(level);
    auto merge = [&](const Entry& x, const Entry& y) {
        Entry e;
        e.a = y.a * x.a;
        e.b = y.a * x.b + y.b;
        const FloatExp bx = x.b.abs() * dcMax;
        FloatExp ry = y.r - bx;
        if (ry.m < 0) ry = FloatExp();
        FloatExp lim;
        if (x.a.isZero()) lim = ry.isZero() ? FloatExp() : x.r;
        else lim = ry / x.a.abs();
        e.r = (lim < x.r) ? lim : x.r;
        return e;
    };
    while (level.size() >= 2) {
        if (cancel && cancel->load(std::memory_order_relaxed)) return false;
        std::vector<Entry> next(level.size() / 2);
        for (size_t j = 0; j < next.size(); ++j) next[j] = merge(level[2 * j], level[2 * j + 1]);
        level.swap(next);
        if (keepCpu) t.cpu.push_back(level);
        t.offset.push_back(int32_t(t.gpuF.size()));
        t.count.push_back(int32_t(level.size()));
        for (const Entry& e : level) {
            // Single precision: an entry whose numbers don't fit, or whose radius is 0, is never used.
            GpuBlaF f{};
            const double la = e.a.log2Abs(), lb = e.b.log2Abs(), lr = e.r.log2Abs();
            const bool fits = la < 100 && lb < 100 && lr > -125 && lr < 100;
            if (fits) {
                const auto a = e.a.toComplex(), b = e.b.toComplex();
                f = {float(a.real()), float(a.imag()), float(b.real()), float(b.imag()),
                     float(e.r.toDouble() * 0.70710678), 0, 0, 0};
            }
            t.gpuF.push_back(f);
            GpuBlaX x{};
            x.ax = float(e.a.x);
            x.ay = float(e.a.y);
            x.ae = e.a.isZero() ? -(1 << 29) : clampExp(e.a.e);
            x.bx = float(e.b.x);
            x.by = float(e.b.y);
            x.be = e.b.isZero() ? -(1 << 29) : clampExp(e.b.e);
            x.log2R = e.r.isZero() ? -1.0e30f : float(lr);
            t.gpuX.push_back(x);
        }
        t.levels = int(t.offset.size());
        if (t.levels >= 31) break;
    }
    *out = std::move(t);
    return true;
}

}  // namespace core
