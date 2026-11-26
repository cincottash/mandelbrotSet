#include "RenderCore.h"

#include "core/Bloom.h"
#include "core/Palette.h"
#include "core/Sphere.h"

#include <QElapsedTimer>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace render {

namespace {

constexpr double kWideBelowLog2 = -80.0;   // scales below 2^-80 need the extended-range shader

bool sameView(const core::View& a, const core::View& b)
{
    return a.scale.m == b.scale.m && a.scale.e == b.scale.e && a.angle == b.angle
        && mpfr_equal_p(a.cx.raw(), b.cx.raw()) && mpfr_equal_p(a.cy.raw(), b.cy.raw());
}

bool samePassContent(const PassParams& a, const PassParams& b)
{
    return sameView(a.view, b.view) && a.maxIterations == b.maxIterations && a.escapeRadius == b.escapeRadius
        && a.julia == b.julia && a.power == b.power && a.formula == b.formula && a.stripes == b.stripes
        && a.stripeDensity == b.stripeDensity
        && a.wideRange == b.wideRange && a.useBla == b.useBla && a.boxing == b.boxing && a.shallowCheck == b.shallowCheck
        && a.offset.x == b.offset.x && a.offset.y == b.offset.y
        && a.offset.e == b.offset.e;
}

}  // namespace

RenderCore::RenderCore(GpuEngine& gpu) : m_gpu(gpu) { m_view = core::View::home(false); }

RenderCore::~RenderCore()
{
    stopWorker();
    m_gpu.freeFrame(m_frames[0]);
    m_gpu.freeFrame(m_frames[1]);
    m_gpu.freeTarget(m_picture);
}

void RenderCore::setFractal(const core::FractalParams& f)
{
    const bool formula = !f.sameFormula(m_fractal);
    const bool iters = f.autoIterations != m_fractal.autoIterations || f.maxIterations != m_fractal.maxIterations;
    m_fractal = f;
    if (formula) {
        // A different formula: the old reference and the learned iteration limit no longer apply.
        stopWorker();
        m_ref = RefState();
        m_autoIter = 0;
        m_glitchRetries = 0;
    }
    if (formula || iters) m_dirty = true;
}

void RenderCore::setColors(const core::ColorParams& c)
{
    const bool stripes = (c.style == int(core::ColorStyle::Stripes)) != (m_colors.style == int(core::ColorStyle::Stripes))
        || c.stripeDensity != m_colors.stripeDensity;
    m_colors = c;
    m_colorsDirty = true;
    if (stripes) m_dirty = true;
}

void RenderCore::setView(const core::View& v)
{
    if (!sameView(v, m_view)) {
        m_view = v;
        m_dirty = true;
    }
}

void RenderCore::setOutputSize(int w, int h)
{
    w = std::max(w, 1);
    h = std::max(h, 1);
    if (w != m_w || h != m_h) {
        m_w = w;
        m_h = h;
        m_dirty = true;
    }
}

core::FloatExp RenderCore::minimumScale() const
{
    if (!m_fractal.deepCapable()) return core::FloatExp(1.0, int64_t(kWideBelowLog2) + 1);
    return core::FloatExp(1.0, -1'999'000);
}

int RenderCore::baseIterations(const core::View& v) const
{
    const double depth = std::max(0.0, -v.scale.log2Abs());   // halvings of the view below the plane's unit
    return int(std::clamp(600.0 + 110.0 * depth, 600.0, double(core::FractalParams::kMaxIterations)));
}

int RenderCore::maxIterationsFor(const core::View& v) const
{
    if (!m_fractal.autoIterations) return m_fractal.maxIterations;
    int it = baseIterations(v);
    // What the last pictures needed, while the view stays about as deep (zooming far out forgets it).
    if (m_autoIter > 0 && v.scale.log2Abs() < m_autoIterScaleLog2 + 3.0) it = std::max(it, m_autoIter);
    return std::clamp(it, core::FractalParams::kMinIterations, core::FractalParams::kMaxIterations);
}

core::FloatExp RenderCore::dcMaxFor(const core::View& v, const core::BigFloat& rx, const core::BigFloat& ry, int w,
                                    int h) const
{
    const double aspect = double(std::max(w, 1)) / double(std::max(h, 1));
    const core::FloatExp radius = v.scale * core::FloatExp(std::sqrt(aspect * aspect + 1.0));
    const core::FloatExp off = v.centerOffsetFrom(rx, ry).abs();
    return off + radius;
}

bool RenderCore::referenceUsable(const core::View& v, int) const
{
    if (!m_ref.orbit || !m_gpu.hasReference()) return false;
    if (!m_ref.fractal.sameFormula(m_fractal)) return false;
    const double needBits = -v.scale.log2Abs() + 40.0;
    return double(m_ref.orbit->request.precision) >= needBits;
}

bool RenderCore::referenceWanted(const core::View& v, int maxIt, int w, int h, bool* tableOnly) const
{
    *tableOnly = false;
    if (!referenceUsable(v, maxIt)) return true;
    const core::RefOrbit& o = *m_ref.orbit;
    if (double(o.request.precision) < -v.scale.log2Abs() + 64.0) return true;
    if (!o.escaped && o.length < maxIt) return true;
    // Far from the reference: the pixel offsets lose precision, so start again from the view's center.
    const core::FloatExp off = v.centerOffsetFrom(o.request.x, o.request.y).abs();
    if (off > v.scale * core::FloatExp(64.0)) return true;
    if (m_fractal.deepCapable() && m_useBla) {
        const core::FloatExp need = dcMaxFor(v, o.request.x, o.request.y, w, h);
        if (!m_ref.bla || need > m_ref.bla->dcMax || m_ref.bla->dcMax > need * core::FloatExp(1024.0)) {
            *tableOnly = true;
            return true;
        }
    }
    return false;
}

void RenderCore::requestReference(const core::View& v, int maxIt, int w, int h, bool tableOnly, const core::BigFloat* atX,
                                  const core::BigFloat* atY)
{
    Job job;
    job.fractal = m_fractal;
    if (tableOnly && m_ref.orbit) {
        job.reuseOrbit = m_ref.orbit;
        job.request = m_ref.orbit->request;
    } else {
        core::OrbitRequest& rq = job.request;
        rq.precision = v.neededPrecision();
        rq.x = core::BigFloat(rq.precision);
        rq.y = core::BigFloat(rq.precision);
        if (atX && atY) {
            mpfr_set(rq.x.raw(), atX->raw(), MPFR_RNDN);
            mpfr_set(rq.y.raw(), atY->raw(), MPFR_RNDN);
        } else {
            mpfr_set(rq.x.raw(), v.cx.raw(), MPFR_RNDN);
            mpfr_set(rq.y.raw(), v.cy.raw(), MPFR_RNDN);
        }
        rq.julia = m_fractal.julia();
        rq.seedX = m_fractal.seedX(rq.precision);
        rq.seedY = m_fractal.seedY(rq.precision);
        rq.power = m_fractal.power;
        rq.formula = int(m_fractal.formula);
        rq.maxIterations = maxIt;
        rq.escapeRadius = m_fractal.escapeRadius;
    }
    // Room to zoom out and pan a little before the table must be built again.
    job.dcMax = dcMaxFor(v, job.request.x, job.request.y, w, h) * core::FloatExp(4.0);
    startWorker(std::move(job));
}

void RenderCore::startWorker(Job job)
{
    stopWorker();
    m_cancel = false;
    m_jobDone = false;
    m_jobProgress = 0;
    m_jobTarget = job.reuseOrbit ? 0 : job.request.maxIterations;
    m_jobRunning = true;
    m_job = job;
    m_jobOrbit.reset();
    m_jobBla.reset();
    m_jobOk = false;
    m_thread = std::thread([this, job = std::move(job)]() mutable {
        std::shared_ptr<core::RefOrbit> orbit = job.reuseOrbit;
        bool ok = true;
        if (!orbit) {
            auto o = std::make_shared<core::RefOrbit>();
            ok = core::computeOrbit(job.request, o.get(), &m_cancel, &m_jobProgress);
            orbit = o;
        }
        auto bla = std::make_shared<core::BlaTable>();
        if (ok && job.request.power == 2 && job.request.formula == 0) ok = core::buildBla(*orbit, job.dcMax, std::ldexp(1.0, -24), bla.get(), false, &m_cancel);
        if (ok) {
            m_jobOrbit = orbit;
            m_jobBla = bla;
        }
        m_jobOk = ok;
        m_jobDone = true;
    });
}

void RenderCore::stopWorker()
{
    if (m_thread.joinable()) {
        m_cancel = true;
        m_thread.join();
    }
    m_jobRunning = false;
    m_jobDone = false;
}

bool RenderCore::collectWorker()
{
    if (!m_jobRunning || !m_jobDone) return false;
    if (m_thread.joinable()) m_thread.join();
    m_jobRunning = false;
    if (!m_jobOk || !m_jobOrbit) return false;
    if (!m_job.fractal.sameFormula(m_fractal)) return false;   // the formula changed while it ran
    m_gpu.uploadReference(*m_jobOrbit, *m_jobBla);
    m_ref.orbit = m_jobOrbit;
    m_ref.bla = m_jobBla;
    m_ref.fractal = m_job.fractal;
    m_ref.id = m_nextRefId++;
    m_jobOrbit.reset();
    m_jobBla.reset();
    m_dirty = true;
    return true;
}

PassParams RenderCore::passParams(const core::View& v, int outW, int outH, int ss, int maxIt) const
{
    PassParams p;
    p.view = v;
    p.outW = outW;
    p.outH = outH;
    p.ss = ss;
    p.maxIterations = maxIt;
    p.escapeRadius = m_fractal.escapeRadius;
    p.julia = m_fractal.julia();
    p.power = m_fractal.power;
    p.formula = int(m_fractal.formula);
    p.stripes = m_colors.style == int(core::ColorStyle::Stripes);
    p.stripeDensity = m_colors.stripeDensity;
    if (m_ref.orbit) p.offset = v.centerOffsetFrom(m_ref.orbit->request.x, m_ref.orbit->request.y);
    p.wideRange = m_fractal.deepCapable() && v.scale.log2Abs() < kWideBelowLog2;
    p.useBla = m_useBla && m_ref.bla && m_ref.bla->valid() && m_fractal.deepCapable();
    // Boxing fills enclosed inside pixels without their "Depth" value, so it is off for the colorings that show it.
    // Cheap views (few iterations) don't gain from boxing's extra passes. The Burning Ship and the Tricorn aren't
    // polynomials of c, so their inside can have holes: no boxing there.
    p.boxing = m_boxing && m_colors.interiorStyle != int(core::InteriorStyle::Depth) && maxIt >= 4096 && m_fractal.classic();
    p.shallowCheck = m_boxing && m_colors.interiorStyle != int(core::InteriorStyle::Depth) && m_fractal.classic();
    if (p.useBla && m_ref.orbit) {
        const core::FloatExp need = dcMaxFor(v, m_ref.orbit->request.x, m_ref.orbit->request.y, outW, outH);
        if (need > m_ref.bla->dcMax) p.useBla = false;   // the table would give wrong results this far out
    }
    return p;
}

int RenderCore::raisedIterations(const PassStats& s, int maxIt, quint64 pixels, const core::View& v) const
{
    // Both rules stop at a cap that grows with the depth: 16 times the depth's base. (The seahorse spiral at 3e-30
    // looked black at 11,388 and showed all its detail at 46,000.) Without it, the edge of a minibrot, where some
    // pixels always escape late, pushed the limit to 5,000,000 and frames to seconds.
    const int cap = int(std::min<double>(core::FractalParams::kMaxIterations, std::max(baseIterations(v) * 16.0, 20000.0)));
    if (maxIt >= cap || pixels == 0) return 0;
    // Many pixels escaping late (over 1 % of the escaping ones in the limit's top half): the limit cuts off detail.
    // A zoomed-out view has a thin boundary of slow pixels that never needs more, hence the share of all pixels.
    if (s.escaped > 0 && double(s.nearMax) > 0.01 * double(s.escaped) && double(s.nearMax) > 0.0005 * double(pixels))
        return std::min(maxIt * 2, cap);
    // Nearly everything "inside": deep down that usually means nothing has escaped *yet* (a black picture where the
    // detail needs more iterations). Raise faster; the inside of a real cardioid or bulb stops at the cap.
    if (double(s.inside) > 0.995 * double(pixels)) return std::min(maxIt * 4, cap);
    return 0;
}

bool RenderCore::prepareReference(const core::View& deepest, int w, int h)
{
    if (!m_gpu.ready()) return false;
    core::View v = deepest;
    v.fitPrecision();
    stopWorker();
    requestReference(v, maxIterationsFor(v), w, h, false);
    if (m_thread.joinable()) m_thread.join();
    m_jobDone = true;
    return collectWorker();
}

void RenderCore::afterFullPass(const RawFrame& f)
{
    const PassStats& s = f.stats;
    const quint64 pixels = quint64(f.rawW) * quint64(f.rawH);
    m_lastFullMs = f.gpuMs;
    m_lastFullW = f.outW;
    m_lastFullH = f.outH;
    m_lastGlitches = s.glitches;
    if (pixels > 0 && f.gpuMs > 0) m_msPerPixelFull = f.gpuMs / double(pixels);
    const int maxIt = f.maxIterations;
    if (m_fractal.autoIterations) {
        // Many pixels escaping late: the limit cuts off detail, so raise it and render again.
        if (const int raised = raisedIterations(s, maxIt, pixels, f.view)) {
            m_autoIter = raised;
            m_autoIterScaleLog2 = f.view.scale.log2Abs();
            m_dirty = true;
        } else if (s.maxEscN > 0 && int(s.maxEscN) * 3 < maxIt && maxIt > baseIterations(f.view)
                   && double(s.inside) < 0.5 * double(pixels)) {
            // Far more than the slowest escaping pixel needs (and not a mostly-inside view): come back down.
            m_autoIter = std::max(baseIterations(f.view), int(s.maxEscN) * 2);
            m_autoIterScaleLog2 = f.view.scale.log2Abs();
        }
    }
    // Pixels that outlived an escaping reference lose precision: start a reference at the deepest pixel instead.
    if (s.glitches > std::max<quint64>(16, pixels / 2000) && m_glitchRetries < 3 && !m_jobRunning) {
        ++m_glitchRetries;
        const int x = int(s.maxIdx % quint32(f.rawW)), y = int(s.maxIdx / quint32(f.rawW));
        core::BigFloat px(f.view.cx.precision()), py(f.view.cy.precision());
        planePoint(f.view, f.rawW, f.rawH, x + 0.5, y + 0.5, &px, &py);
        requestReference(f.view, maxIterationsFor(f.view), f.outW, f.outH, false, &px, &py);
    } else if (s.glitches == 0) {
        m_glitchRetries = 0;
    }
}

void RenderCore::planePoint(const core::View& v, int w, int h, double px, double py, core::BigFloat* x,
                            core::BigFloat* y) const
{
    const double hh = std::max(h, 1) / 2.0;
    v.pointAt((px - w / 2.0) / hh, -(py - hh) / hh, x, y);
}

bool RenderCore::step(double budgetMs, bool moving)
{
    if (!m_gpu.ready()) return false;
    QElapsedTimer timer;
    timer.start();
    collectWorker();
    const int maxIt = maxIterationsFor(m_view);
    bool tableOnly = false;
    if (!m_jobRunning && referenceWanted(m_view, maxIt, m_w, m_h, &tableOnly))
        requestReference(m_view, maxIt, m_w, m_h, tableOnly);
    if (!referenceUsable(m_view, maxIt)) {
        m_waiting = true;
        return true;
    }
    m_waiting = false;
    if (m_colors.palette != m_paletteName) {
        m_gpu.setPalette(core::paletteByName(m_colors.palette).sample(1024));
        m_paletteName = m_colors.palette;
        m_colorsDirty = true;
    }

    // Which resolution to compute next.
    auto previewDivisor = [&]() {
        const double perPx = m_msPerPixelFull > 0 ? m_msPerPixelFull : 2e-5;
        const double allowed = std::max(1.0, budgetMs * 0.7 / perPx);
        for (int d : {1, 2, 3, 4, 6, 8, 12, 16, 24})
            if (double(m_w / d) * double(m_h / d) <= allowed) return d;
        return 32;
    };
    const RawFrame* front = m_front >= 0 ? &m_frames[m_front] : nullptr;
    const bool frontCurrent = front && front->complete && front->refId == m_ref.id && front->maxIterations == maxIt
        && front->outW == std::max(1, m_w / front->divisor) && front->outH == std::max(1, m_h / front->divisor)
        && samePassContent(passParams(m_view, front->outW, front->outH, 1, maxIt), front->pass);
    const int frontDiv = frontCurrent ? front->divisor : 0;
    int wantDiv = 0, wantSS = 0;
    if (moving) {
        wantDiv = previewDivisor();
        wantSS = 1;
        if (frontCurrent && frontDiv <= wantDiv) wantDiv = 0;   // already as sharp as a preview gets
    } else if (!frontCurrent) {
        // Nothing for this view yet: a quick preview first when the full picture takes a while.
        const int d = previewDivisor();
        wantDiv = (d >= 3) ? d : 1;
        wantSS = 1;
    } else if (frontDiv > 1) {
        wantDiv = 1;
        wantSS = 1;
    } else if (front->ss < m_ss) {
        wantDiv = 1;
        wantSS = m_ss;
    }

    bool more = m_jobRunning || m_colors.cycleSpeed != 0.0;
    if (wantDiv > 0) {
        const int ow = std::max(1, m_w / wantDiv), oh = std::max(1, m_h / wantDiv);
        PassParams p = passParams(m_view, ow, oh, wantSS, maxIt);
        const bool same = m_passActive && samePassContent(p, m_activePass) && m_activeDivisor == wantDiv
            && m_activeSS == wantSS && m_activePass.outW == ow && m_activePass.outH == oh
            && m_frames[m_back].refId == m_ref.id;
        if (!same) {
            m_back = (m_front == 0) ? 1 : 0;
            m_gpu.startPass(m_frames[m_back], p);
            m_frames[m_back].refId = m_ref.id;
            m_frames[m_back].divisor = wantDiv;
            m_passActive = true;
            m_activePass = p;
            m_activeDivisor = wantDiv;
            m_activeSS = wantSS;
        }
        const double left = std::max(2.0, budgetMs - double(timer.nsecsElapsed()) / 1e6);
        if (m_gpu.continuePass(m_frames[m_back], left)) {
            m_passActive = false;
            m_front = m_back;
            RawFrame& f = m_frames[m_front];
            if (wantDiv > 1 && f.gpuMs > 0) {
                // Previews also teach the cost per pixel (scaled: a preview pixel covers several full ones).
                const double perPx = f.gpuMs / (double(f.rawW) * double(f.rawH));
                if (m_msPerPixelFull < 0) m_msPerPixelFull = perPx;
                else m_msPerPixelFull = 0.7 * m_msPerPixelFull + 0.3 * perPx;
            }
            if (wantDiv == 1 && wantSS == 1) afterFullPass(f);
            m_gpu.colorize(f, m_colors, m_time, m_picture);
            m_colorsDirty = false;
        }
        more = true;
    } else if ((m_colorsDirty || m_colors.cycleSpeed != 0.0) && front && front->complete) {
        m_gpu.colorize(*front, m_colors, m_time, m_picture);
        m_colorsDirty = false;
    }
    m_dirty = false;
    return more || m_passActive;
}

CoreStatus RenderCore::status() const
{
    CoreStatus s;
    s.referenceBusy = m_jobRunning;
    s.referenceProgress = m_jobProgress.load();
    s.referenceTarget = m_jobTarget;
    s.waiting = m_waiting;
    s.maxIterations = maxIterationsFor(m_view);
    s.wideRange = m_fractal.deepCapable() && m_view.scale.log2Abs() < kWideBelowLog2;
    s.lastPassMs = m_lastFullMs;
    s.lastPassW = m_lastFullW;
    s.lastPassH = m_lastFullH;
    s.refining = m_passActive;
    s.progress = m_passActive ? m_gpu.passProgress() : 1.0;
    s.previewDivisor = m_passActive ? m_activeDivisor : 1;
    s.glitchPixels = m_lastGlitches;
    if (m_ref.orbit) {
        s.referenceLength = m_ref.orbit->length;
        s.referenceEscaped = m_ref.orbit->escaped;
    }
    return s;
}

QImage RenderCore::renderImage(const core::View& v0, int w, int h, int ss, QString* error,
                               const std::function<bool(double)>& progress)
{
    if (!m_gpu.ready()) {
        if (error) *error = QStringLiteral("The graphics card renderer isn't available.");
        return {};
    }
    if (w < 1 || h < 1 || w > 32768 || h > 32768) {
        if (error) *error = QStringLiteral("The picture size must be between 1 and 32768 pixels on each side.");
        return {};
    }
    ss = std::clamp(ss, 1, 4);
    core::View v = v0;
    v.fitPrecision();
    m_gpu.setPalette(core::paletteByName(m_colors.palette).sample(1024));
    m_paletteName = m_colors.palette;
    int maxIt = maxIterationsFor(v);

    // A reference that fits this view, computed here and now.
    capturedReferences = 0;
    auto syncReference = [&](const core::BigFloat* atX, const core::BigFloat* atY, bool tableOnly = false) {
        if (!tableOnly) ++capturedReferences;
        stopWorker();
        requestReference(v, maxIt, w, h, tableOnly && m_ref.orbit, atX, atY);
        if (m_thread.joinable()) m_thread.join();
        m_jobDone = true;
        return collectWorker();
    };
    auto ensureReference = [&]() {
        bool tableOnly = false;
        const bool usable = referenceUsable(v, maxIt);
        if (!usable || referenceWanted(v, maxIt, w, h, &tableOnly)) return syncReference(nullptr, nullptr, tableOnly && usable);
        return true;
    };
    if (!ensureReference()) {
        if (error) *error = QStringLiteral("The reference orbit couldn't be computed.");
        return {};
    }
    RawFrame frame;

    // With Auto iterations, a small whole-view probe first settles the limit, so every piece uses the same one.
    if (m_fractal.autoIterations) {
        const int pw = std::max(16, std::min(w, 320)), ph = std::max(16, int(std::lround(double(h) * pw / w)));
        for (int raise = 0; raise < 6; ++raise) {
            m_gpu.startPass(frame, passParams(v, pw, ph, 1, maxIt));
            while (!m_gpu.continuePass(frame, 150.0)) {
                if (progress && !progress(0.0)) {
                    m_gpu.freeFrame(frame);
                    if (error) *error = QStringLiteral("Cancelled.");
                    return {};
                }
            }
            const int raised = raisedIterations(frame.stats, maxIt, quint64(pw) * quint64(ph), v);
            if (!raised) break;
            maxIt = raised;
            m_autoIter = maxIt;
            m_autoIterScaleLog2 = v.scale.log2Abs();
            if (!ensureReference()) {
                if (error) *error = QStringLiteral("The reference orbit couldn't be computed.");
                return {};
            }
        }
    }

    // Large pictures are drawn in pieces, so the graphics card's memory is never the limit.
    const int maxRawPixels = 16 * 1024 * 1024;
    const int piece = std::max(64, int(std::sqrt(double(maxRawPixels)) / ss));
    QImage out(w, h, QImage::Format_RGB32);
    out.fill(Qt::black);
    QPainter painter(&out);
    ColorTarget target;
    const long long totalPixels = qint64(w) * h;
    long long donePixels = 0;
    bool cancelled = false;
    const double fullHalfH = h / 2.0;
    for (int y0 = 0; y0 < h && !cancelled; y0 += piece) {
        for (int x0 = 0; x0 < w && !cancelled; x0 += piece) {
            const int pw = std::min(piece, w - x0), ph = std::min(piece, h - y0);
            // This piece's own view: the same pixel size, centered on the piece.
            core::View pv = v;
            const double cxPx = x0 + pw / 2.0, cyPx = y0 + ph / 2.0;
            v.pointAt((cxPx - w / 2.0) / fullHalfH, -(cyPx - fullHalfH) / fullHalfH, &pv.cx, &pv.cy);
            pv.scale = v.scale * core::FloatExp(double(ph) / double(h));
            for (int attempt = 0; attempt < 3; ++attempt) {
                const PassParams p = passParams(pv, pw, ph, ss, maxIt);
                m_gpu.startPass(frame, p);
                while (!m_gpu.continuePass(frame, 150.0)) {
                    const double done = (double(donePixels) + m_gpu.passProgress() * pw * ph) / double(totalPixels);
                    if (progress && !progress(done)) {
                        cancelled = true;
                        break;
                    }
                }
                if (cancelled) break;
                const quint64 px = quint64(frame.rawW) * quint64(frame.rawH);
                if (frame.stats.glitches > std::max<quint64>(16, px / 2000) && attempt < 2) {
                    const int gx = int(frame.stats.maxIdx % quint32(frame.rawW)), gy = int(frame.stats.maxIdx / quint32(frame.rawW));
                    core::BigFloat rx(pv.cx.precision()), ry(pv.cy.precision());
                    planePoint(pv, frame.rawW, frame.rawH, gx + 0.5, gy + 0.5, &rx, &ry);
                    if (syncReference(&rx, &ry)) continue;
                }
                break;
            }
            if (cancelled) break;
            if (captureRaw) {
                capturedRaw0 = m_gpu.readRaw(frame, 0);
                capturedRaw1 = m_gpu.readRaw(frame, 1);
                capturedStats = frame.stats;
            }
            m_gpu.colorize(frame, m_colors, m_time, target, QSize(w, h), QPoint(x0, y0));
            painter.drawImage(x0, y0, m_gpu.readTarget(target));
            donePixels += qint64(pw) * ph;
            if (progress && !progress(double(donePixels) / double(totalPixels))) cancelled = true;
        }
    }
    painter.end();
    m_gpu.freeFrame(frame);
    m_gpu.freeTarget(target);
    m_dirty = true;
    m_colorsDirty = true;
    if (cancelled) {
        if (error) *error = QStringLiteral("Cancelled.");
        return {};
    }
    // The ball and the glow span the pieces, so they are added once the picture is whole.
    if (m_colors.sphereSize > 0) {
        core::SphereLook s;
        s.size = m_colors.sphereSize;
        s.shade = m_colors.sphereShade;
        s.shine = m_colors.sphereShine;
        s.background = m_colors.sphereBackground;
        const double a = m_colors.lightAngle * M_PI / 180.0, hh = m_colors.lightHeight * M_PI / 180.0;
        s.lightX = std::cos(a) * std::cos(hh);
        s.lightY = std::sin(a) * std::cos(hh);
        s.lightZ = std::sin(hh);
        out = core::applySphere(out, s);
    }
    core::applyBloom(out, m_colors.bloom, m_colors.bloomSize, m_colors.bloomThreshold);
    return out;
}

}  // namespace render
