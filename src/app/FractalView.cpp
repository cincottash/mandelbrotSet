#include "FractalView.h"

#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
#include <QThread>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {
const QColor kBackground(0x10, 0x11, 0x14);
constexpr double kPreviewW = 220, kPreviewH = 150;
}

FractalView::FractalView(QWidget* parent) : QOpenGLWidget(parent)
{
    QSurfaceFormat fmt = format();
    fmt.setVersion(4, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapInterval(1);
    setFormat(fmt);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(200, 150);
    setProperty("testId", QStringLiteral("fractalView"));
    m_view = core::View::home(false);
    m_clock.start();
    m_settle.setSingleShot(true);
    m_settle.setInterval(450);
    connect(&m_settle, &QTimer::timeout, this, [this] {
        if (m_dragging || m_rotating || std::fabs(m_zoomPending) > 1e-4) {
            m_settle.start();
            return;
        }
        emit viewSettled();
    });
    m_preview = new QLabel(this);
    m_preview->setObjectName(QStringLiteral("juliaPreview"));
    m_preview->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_preview->setFixedSize(int(kPreviewW) + 4, int(kPreviewH) + 4);
    m_preview->setStyleSheet(QStringLiteral("QLabel#juliaPreview { border: 2px solid rgba(255,255,255,170); background: #101114; }"));
    m_preview->hide();
    m_previewTimer.setSingleShot(true);
    m_previewTimer.setInterval(30);
    connect(&m_previewTimer, &QTimer::timeout, this, [this] { updatePreview(); });
}

FractalView::~FractalView()
{
    if (m_tried && context()) {
        makeCurrent();
        m_core.reset();
        m_previewCore.reset();
        m_gpu.release();
        m_previewGpu.release();
        doneCurrent();
    }
}

QString FractalView::gpuName() const
{
    QString n = m_gpu.rendererName();
    const int slash = n.indexOf(QLatin1Char('/'));
    if (slash > 0) n = n.left(slash);
    return n.trimmed();
}

render::CoreStatus FractalView::coreStatus() const
{
    return m_core ? m_core->status() : render::CoreStatus();
}

void FractalView::initializeGL()
{
    m_tried = true;
    QString err;
    if (!context() || !context()->isValid()) {
        m_error = QStringLiteral("No OpenGL context could be created on this computer.");
    } else if (m_gpu.init(&err) && m_previewGpu.init(&err)) {
        m_ok = true;
        m_core = std::make_unique<render::RenderCore>(m_gpu);
        m_core->setFractal(m_fractal);
        m_core->setColors(m_colors);
        m_core->setSupersampling(m_aa);
        m_previewCore = std::make_unique<render::RenderCore>(m_previewGpu);
    } else {
        m_error = err;
    }
    if (!m_ok) QTimer::singleShot(0, this, [this] { emit gpuUnavailable(m_error); });
}

void FractalView::resizeGL(int, int)
{
    placePreview();
    update();
}

void FractalView::setFractal(const core::FractalParams& f)
{
    const bool typeChanged = f.type != m_fractal.type;
    m_fractal = f;
    if (m_core) m_core->setFractal(f);
    // Deeper than the formula allows (a power above 2): back out to the deepest it can show.
    if (m_core && m_view.scale < m_core->minimumScale()) {
        m_view.scale = m_core->minimumScale();
        m_view.fitPrecision();
    }
    if (typeChanged) m_preview->hide();
    update();
}

void FractalView::setColors(const core::ColorParams& c)
{
    m_colors = c;
    if (m_core) m_core->setColors(c);
    update();
}

void FractalView::setView(const core::View& v)
{
    stopMotion();
    m_view = v;
    m_view.fitPrecision();
    markChanged();
}

void FractalView::setAntiAliasing(int s)
{
    m_aa = std::clamp(s, 1, 4);
    if (m_core) m_core->setSupersampling(m_aa);
    update();
}

void FractalView::setJuliaPreview(bool on)
{
    m_juliaPreview = on;
    if (!on) m_preview->hide();
}

void FractalView::setPickMode(bool on)
{
    m_pickMode = on;
    setCursor(on ? Qt::CrossCursor : Qt::ArrowCursor);
}

void FractalView::stopMotion()
{
    m_zoomPending = 0;
    m_panPending = QPointF();
    m_rotatePending = 0;
}

QPointF FractalView::toViewUnits(const QPointF& p) const
{
    const double hh = std::max(1, height()) / 2.0;
    return {(p.x() - width() / 2.0) / hh, -(p.y() - hh) / hh};
}

void FractalView::pointAt(const QPointF& widgetPos, QString* re, QString* im) const
{
    const QPointF q = toViewUnits(widgetPos);
    core::View v = m_view;
    v.fitPrecision();
    core::BigFloat x(v.cx.precision()), y(v.cy.precision());
    v.pointAt(q.x(), q.y(), &x, &y);
    const int digits = int(std::floor(double(v.neededPrecision()) * 0.30103)) - 1;
    *re = QString::fromStdString(x.toString(digits));
    *im = QString::fromStdString(y.toString(digits));
}

void FractalView::markChanged()
{
    m_lastChangeNs = m_clock.nsecsElapsed();
    m_settle.start();
    emit viewChanged();
    update();
}

void FractalView::applyZoom(double u, double v, double factor)
{
    if (factor > 1.0 && m_core) {
        const core::FloatExp target = m_view.scale * core::FloatExp(1.0 / factor);
        if (target < m_core->minimumScale()) {
            m_zoomPending = 0;
            emit zoomLimitReached(QStringLiteral("Power %1 can zoom to about 1e-24. Power 2 can zoom without limit.")
                                      .arg(m_fractal.power));
            return;
        }
    }
    const core::FloatExp before = m_view.scale;
    m_view.zoomAt(u, v, factor);
    if (m_view.scale.m == before.m && m_view.scale.e == before.e) m_zoomPending = 0;   // at a limit
}

void FractalView::zoomAnimated(double factor, double u, double v)
{
    if (!(factor > 0)) return;
    m_zoomAnchor = QPointF(u, v);
    m_zoomPending += std::log(factor);
    update();
}

void FractalView::panAnimated(double du, double dv)
{
    m_panPending += QPointF(du, dv);
    update();
}

void FractalView::rotateBy(double degrees)
{
    m_rotatePending += degrees * M_PI / 180.0;
    update();
}

void FractalView::advance(double dt)
{
    bool changed = false;
    const double k = 1.0 - std::exp(-dt / 0.075);   // the share of the remaining motion done this frame
    if (std::fabs(m_zoomPending) > 1e-4) {
        double s = m_zoomPending * k;
        if (std::fabs(m_zoomPending) < 0.002) s = m_zoomPending;
        m_zoomPending -= s;
        applyZoom(m_zoomAnchor.x(), m_zoomAnchor.y(), std::exp(s));
        changed = true;
    } else {
        m_zoomPending = 0;
    }
    if (std::fabs(m_panPending.x()) + std::fabs(m_panPending.y()) > 1e-5) {
        const QPointF s = m_panPending * k;
        m_panPending -= s;
        m_view.pan(s.x(), s.y());
        changed = true;
    } else {
        m_panPending = QPointF();
    }
    if (std::fabs(m_rotatePending) > 1e-5) {
        const double s = m_rotatePending * k;
        m_rotatePending -= s;
        m_view.rotateAt(0, 0, s);
        changed = true;
    } else {
        m_rotatePending = 0;
    }
    if (changed) markChanged();
}

void FractalView::paintGL()
{
    const qint64 now = m_clock.nsecsElapsed();
    const double dt = std::clamp(double(now - m_lastFrameNs) / 1e9, 0.0, 0.1);
    m_lastFrameNs = now;
    QOpenGLFunctions* f = context()->functions();
    if (m_exporting) return;   // the last frame stays on screen
    if (!m_ok) {
        f->glClearColor(float(kBackground.redF()), float(kBackground.greenF()), float(kBackground.blueF()), 1.f);
        f->glClear(GL_COLOR_BUFFER_BIT);
        return;
    }
    advance(dt);
    autopilotAdvance(dt, now);
    const qreal dpr = devicePixelRatioF();
    const int W = std::max(1, int(std::lround(width() * dpr))), H = std::max(1, int(std::lround(height() * dpr)));
    const bool animating = (m_autopilot && !m_paused) || std::fabs(m_zoomPending) > 1e-4 || m_panPending != QPointF()
        || std::fabs(m_rotatePending) > 1e-5;
    const bool moving = m_dragging || m_rotating || animating || (now - m_lastChangeNs) < 150'000'000;
    if (!m_paused) m_animTime += dt;
    m_core->setView(m_view);
    m_core->setOutputSize(W, H);
    m_core->setTime(m_animTime);
    const bool more = m_core->step(moving ? 8.0 : 22.0, moving);
    f->glBindFramebuffer(GL_FRAMEBUFFER, defaultFramebufferObject());
    m_gpu.display(m_core->picture(), m_view, W, H, kBackground);

    ++m_framesCounted;
    if (now - m_fpsWindowStart > 500'000'000) {
        m_fps = m_framesCounted / (double(now - m_fpsWindowStart) / 1e9);
        m_framesCounted = 0;
        m_fpsWindowStart = now;
    }
    m_busy = more || moving;
    emit frameDone();
    if (more || moving) update();
    else m_fps = 0;
}

bool FractalView::renderUntilIdle(int timeoutMs)
{
    if (!m_tried) grabFramebuffer();   // creates the context and initializes the engine
    if (!m_ok) return false;
    QElapsedTimer t;
    t.start();
    makeCurrent();
    const qreal dpr = devicePixelRatioF();
    m_core->setView(m_view);
    m_core->setOutputSize(std::max(1, int(width() * dpr)), std::max(1, int(height() * dpr)));
    bool more = true;
    while (more && t.elapsed() < timeoutMs) more = m_core->step(200.0, false);
    doneCurrent();
    update();
    return !more;
}

bool FractalView::beginMovie(const core::View& deepest, int w, int h)
{
    if (!m_tried) grabFramebuffer();
    if (!m_ok) return false;
    setAutopilot(false);
    m_inMovie = true;
    m_exporting = true;
    makeCurrent();
    const bool ok = m_core->prepareReference(deepest, w, h);
    doneCurrent();
    return ok;
}

std::vector<float> FractalView::probeEscapeTimes(const core::View& v, int w, int h)
{
    if (!m_ok) return {};
    makeCurrent();
    m_core->captureRaw = true;
    QString err;
    const QImage img = m_core->renderImage(v, w, h, 1, &err);
    m_core->captureRaw = false;
    std::vector<float> mu;
    if (!img.isNull()) {
        mu.resize(size_t(w) * h);
        for (size_t i = 0; i < mu.size(); ++i) mu[i] = m_core->capturedRaw0[i * 4];
    }
    m_core->capturedRaw0.clear();
    m_core->capturedRaw1.clear();
    doneCurrent();
    return mu;
}

void FractalView::endMovie()
{
    m_inMovie = false;
    m_exporting = false;
    if (m_core) m_core->setTime(m_animTime);
    update();
}

QImage FractalView::renderPicture(const core::View& v, int w, int h, int ss, QString* error,
                                  const std::function<bool(double)>& progress, double timeSeconds)
{
    if (!m_tried) grabFramebuffer();
    if (!m_ok) {
        if (error) *error = m_error;
        return {};
    }
    // While it renders, the progress dialog lets events through: the picture isn't redrawn meanwhile (it shares the
    // engine), and the context is made current again after each update, since painting other windows switches it.
    m_exporting = true;
    makeCurrent();
    if (timeSeconds >= 0) m_core->setTime(timeSeconds);
    const QImage img = m_core->renderImage(v, w, h, ss, error, [&](double p) {
        const bool goOn = !progress || progress(p);
        makeCurrent();
        return goOn;
    });
    doneCurrent();
    if (!m_inMovie) {
        m_exporting = false;
        update();
    }
    return img;
}

QImage FractalView::renderThumbnail(const core::FractalParams& f, const core::ColorParams& c, const core::View& v, int w, int h)
{
    if (!m_ok || !m_previewCore) return {};
    makeCurrent();
    m_previewCore->setFractal(f);
    m_previewCore->setColors(c);
    QString err;
    const QImage img = m_previewCore->renderImage(v, w, h, 2, &err);
    doneCurrent();
    return img;
}

QImage FractalView::currentPicture()
{
    if (!m_ok || !m_core->picture().valid) return {};
    makeCurrent();
    QImage img = m_gpu.readTarget(m_core->picture());
    doneCurrent();
    return img;
}

void FractalView::mousePressEvent(QMouseEvent* e)
{
    setFocus();
    m_lastMouse = e->position();
    m_pressPos = e->position();
    m_userSteersUntilNs = m_clock.nsecsElapsed() + 2'500'000'000LL;   // the autopilot lets the hand steer a while
    if (e->button() == Qt::LeftButton) {
        if (m_pickMode) {
            QString re, im;
            pointAt(e->position(), &re, &im);
            setPickMode(false);
            emit pointPicked(re, im);
            return;
        }
        stopMotion();
        m_dragging = true;
        setCursor(Qt::ClosedHandCursor);
    } else if (e->button() == Qt::RightButton) {
        m_rightDown = true;
        m_rotating = false;
    }
}

void FractalView::mouseMoveEvent(QMouseEvent* e)
{
    const QPointF p = e->position();
    const double hh = std::max(1, height()) / 2.0;
    if (m_dragging) {
        const QPointF d = p - m_lastMouse;
        m_view.pan(-d.x() / hh, d.y() / hh);
        markChanged();
    } else if (m_rightDown) {
        if (!m_rotating && (p - m_pressPos).manhattanLength() > 5) {
            m_rotating = true;
            setCursor(Qt::SizeAllCursor);
        }
        if (m_rotating) {
            // Turn around the middle of the view, following the pointer's angle.
            const QPointF c(width() / 2.0, height() / 2.0);
            const double a0 = std::atan2(-(m_lastMouse.y() - c.y()), m_lastMouse.x() - c.x());
            const double a1 = std::atan2(-(p.y() - c.y()), p.x() - c.x());
            double d = a1 - a0;
            if (d > M_PI) d -= 2 * M_PI;
            if (d < -M_PI) d += 2 * M_PI;
            // Turning the view one way turns the picture the other: the picture follows the pointer.
            m_view.rotateAt(0, 0, -d);
            markChanged();
        }
    }
    m_lastMouse = p;
    if (m_juliaPreview && m_fractal.type == core::FractalType::Mandelbrot && m_ok) {
        const QPointF q = toViewUnits(p);
        const core::ComplexExp off = m_view.offsetOf(q.x(), q.y());
        const std::complex<double> o = off.toComplex();
        m_previewSeed = QPointF(m_view.cx.toDouble() + o.real(), m_view.cy.toDouble() + o.imag());
        m_previewWanted = true;
        if (!m_previewTimer.isActive()) m_previewTimer.start();
    }
    const QPointF q = toViewUnits(p);
    const std::complex<double> o = m_view.offsetOf(q.x(), q.y()).toComplex();
    emit pointerMoved(m_view.cx.toDouble() + o.real(), m_view.cy.toDouble() + o.imag());
}

void FractalView::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        setCursor(m_pickMode ? Qt::CrossCursor : Qt::ArrowCursor);
        markChanged();
    } else if (e->button() == Qt::RightButton && m_rightDown) {
        m_rightDown = false;
        if (m_rotating) {
            m_rotating = false;
            setCursor(Qt::ArrowCursor);
            markChanged();
        } else {
            QString re, im;
            pointAt(e->position(), &re, &im);
            emit contextMenuRequested(e->globalPosition().toPoint(), re, im);
        }
    }
}

void FractalView::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || m_pickMode) return;
    const QPointF q = toViewUnits(e->position());
    zoomAnimated(4.0, q.x(), q.y());
}

void FractalView::wheelEvent(QWheelEvent* e)
{
    const double notches = e->angleDelta().y() / 120.0;
    if (notches == 0) return;
    const QPointF q = toViewUnits(e->position());
    zoomAnimated(std::pow(m_zoomStep, notches), q.x(), q.y());
    e->accept();
}

void FractalView::keyPressEvent(QKeyEvent* e)
{
    const double step = 0.25;
    switch (e->key()) {
    case Qt::Key_Left: panAnimated(-step, 0); break;
    case Qt::Key_Right: panAnimated(step, 0); break;
    case Qt::Key_Up: panAnimated(0, step); break;
    case Qt::Key_Down: panAnimated(0, -step); break;
    case Qt::Key_Plus:
    case Qt::Key_Equal: zoomAnimated(2.0); break;
    case Qt::Key_Minus: zoomAnimated(0.5); break;
    case Qt::Key_PageUp: rotateBy(15); break;
    case Qt::Key_PageDown: rotateBy(-15); break;
    case Qt::Key_Escape:
        if (m_pickMode) setPickMode(false);
        if (m_autopilot) setAutopilot(false);
        stopMotion();
        break;
    case Qt::Key_Space: setPaused(!m_paused); break;
    default: QOpenGLWidget::keyPressEvent(e); return;
    }
    e->accept();
}

void FractalView::leaveEvent(QEvent* e)
{
    m_previewWanted = false;
    m_preview->hide();
    QOpenGLWidget::leaveEvent(e);
}

// ---------------------------------------------------------------- autopilot

void FractalView::setPaused(bool on)
{
    if (on == m_paused) return;
    m_paused = on;
    emit pausedChanged(on);
    update();
}

void FractalView::setAutopilot(bool on)
{
    if (on == m_autopilot || (on && !m_ok)) return;
    if (!on) setPaused(false);
    m_autopilot = on;
    m_autoHasTarget = false;
    m_autoNextLookNs = 0;
    m_autoBackOut = 0;
    m_autoEmptyStreak = 0;
    stopMotion();
    emit autopilotChanged(on);
    markChanged();
}

bool FractalView::pickAutopilotTarget(QPointF* target)
{
    const render::RawFrame* f = m_core->frontFrame();
    if (!f) return false;
    int W = 0, H = 0;
    const std::vector<float> raw = m_gpu.readRawSmall(*f, 240, &W, &H);
    if (W < 8 || H < 8) return false;
    auto mu = [&](int x, int y) { return raw[(size_t(y) * W + x) * 4]; };
    // Score patches by how much edge they hold: neighbors where inside meets outside, or the escape time jumps.
    // Solid inside and smooth outside score nothing, so the autopilot never settles into an empty patch.
    const int cols = 12, rows = std::max(4, int(std::lround(12.0 * H / W)));
    const double hh = H / 2.0;
    // The previous target, in this frame's view units (for a little loyalty, so the path doesn't jitter).
    double best = 0;
    QPointF bestQ;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const int x0 = c * W / cols, x1 = (c + 1) * W / cols, y0 = r * H / rows, y1 = (r + 1) * H / rows;
            int pairs = 0, edges = 0, inside = 0, count = 0;
            for (int y = y0; y < y1 - 1; ++y)
                for (int x = x0; x < x1 - 1; ++x) {
                    const float a = mu(x, y), b = mu(x + 1, y), d = mu(x, y + 1);
                    ++count;
                    if (a < 0) ++inside;
                    for (float n : {b, d}) {
                        ++pairs;
                        if ((a < 0) != (n < 0) || (a >= 0 && std::fabs(a - n) > 0.6f)) ++edges;
                    }
                }
            if (pairs == 0) continue;
            const double edgeFrac = double(edges) / pairs, insideFrac = double(inside) / std::max(count, 1);
            if (edgeFrac < 0.03 || insideFrac > 0.97) continue;
            const QPointF q(((x0 + x1) / 2.0 - W / 2.0) / hh, -((y0 + y1) / 2.0 - hh) / hh);
            const double weight = std::exp(-(q.x() * q.x() + q.y() * q.y()) / (2 * 0.45 * 0.45));
            const double score = std::min(edgeFrac, 0.6) * weight;
            if (score > best) {
                best = score;
                bestQ = q;
            }
        }
    }
    if (best <= 0.004) return false;
    // From the frame's view to the current one (the picture may lag a little behind the view).
    const core::ComplexExp d = f->view.centerOffsetFrom(m_view.cx, m_view.cy) + f->view.offsetOf(bestQ.x(), bestQ.y());
    const std::complex<double> rel = (d * (core::FloatExp(1.0) / m_view.scale)).toComplex();
    const double ca = std::cos(-m_view.angle), sa = std::sin(-m_view.angle);
    const QPointF t(rel.real() * ca - rel.imag() * sa, rel.real() * sa + rel.imag() * ca);
    if (!std::isfinite(t.x()) || !std::isfinite(t.y())) return false;
    *target = t;
    return true;
}

void FractalView::autopilotAdvance(double dt, qint64 now)
{
    if (!m_autopilot || !m_core || m_paused) return;
    if (now >= m_autoNextLookNs) {
        m_autoNextLookNs = now + 250'000'000;
        QPointF t;
        if (pickAutopilotTarget(&t)) {
            m_autoTarget = t;
            m_autoHasTarget = true;
            m_autoBackOut = 0;
            m_autoEmptyStreak = 0;
        } else if (m_core->frontFrame() && m_autoBackOut <= 0) {
            m_autoHasTarget = false;
            m_autoBackOut = 1.2;   // nothing to see here: back out until there is
            // Each empty look in a row backs out twice as fast (up to 4x), so a dead end is left quickly without
            // overshooting far past the detail (16x went from 1e42 all the way out to 1e14).
            m_autoEmptyStreak = std::min(m_autoEmptyStreak + 1, 3);
        }
    }
    const render::CoreStatus st = m_core->status();
    double rate = m_autoRate;
    // Don't run ahead of the pictures: slow down when the newest one is far shallower than the view.
    if (const render::RawFrame* f = m_core->frontFrame()) {
        const double lag = f->view.scale.log2Abs() - m_view.scale.log2Abs();
        if (lag > 3.0) rate = 0;
        else if (lag > 1.5) rate *= 0.3;
    }
    if (st.waiting) rate = 0;   // the reference orbit for this depth is still being computed
    if (m_autoBackOut > 0) {
        rate = -1.5 * m_autoRate * std::exp2(std::max(0, m_autoEmptyStreak - 1));
        m_autoBackOut -= dt;
        if (m_view.scale > core::FloatExp(2.0)) rate = 0;
    }
    const double factor = std::exp(rate * dt);
    if (factor > 1.0 && m_view.scale * core::FloatExp(1.0 / factor) < m_core->minimumScale()) {
        setAutopilot(false);
        emit zoomLimitReached(QStringLiteral("The autopilot stopped at the deepest zoom power %1 allows (about 1e-24).")
                                  .arg(m_fractal.power));
        return;
    }
    m_view.zoomAt(0, 0, factor);
    if (m_autoHasTarget) m_autoTarget *= factor;
    if (m_autoHasTarget && now >= m_userSteersUntilNs) {
        QPointF step = m_autoTarget * (1.0 - std::exp(-dt / 1.1));
        const double len = std::hypot(step.x(), step.y()), maxLen = 0.4 * dt;
        if (len > maxLen) step *= maxLen / len;
        m_view.pan(step.x(), step.y());
        m_autoTarget -= step;
    }
    markChanged();
}

void FractalView::autopilotSteps(int frames, double secondsPerFrame)
{
    if (!m_ok || !m_autopilot) return;
    makeCurrent();
    const qreal dpr = devicePixelRatioF();
    const int W = std::max(1, int(width() * dpr)), H = std::max(1, int(height() * dpr));
    qint64 now = m_clock.nsecsElapsed();
    for (int i = 0; i < frames && m_autopilot; ++i) {
        now += qint64(secondsPerFrame * 1e9);
        m_core->setView(m_view);
        m_core->setOutputSize(W, H);
        // Each simulated frame waits for its picture, as a fast graphics card would deliver it.
        bool more = true;
        for (int k = 0; k < 50 && more && !m_core->status().waiting; ++k) more = m_core->step(50.0, true);
        // While a reference orbit is computed, a frame takes its real length (the worker needs the time).
        if (m_core->status().waiting || m_core->status().referenceBusy)
            QThread::msleep(static_cast<unsigned long>(secondsPerFrame * 1000));
        autopilotAdvance(secondsPerFrame, now);
    }
    doneCurrent();
}

void FractalView::placePreview()
{
    m_preview->move(width() - m_preview->width() - 12, 12);
}

void FractalView::updatePreview()
{
    if (!m_previewWanted || !m_juliaPreview || !m_ok || !m_previewCore || m_fractal.type != core::FractalType::Mandelbrot) {
        m_preview->hide();
        return;
    }
    core::FractalParams jf = m_fractal;
    jf.type = core::FractalType::Julia;
    jf.juliaRe = QString::number(m_previewSeed.x(), 'g', 17);
    jf.juliaIm = QString::number(m_previewSeed.y(), 'g', 17);
    jf.autoIterations = false;
    jf.maxIterations = 400;
    makeCurrent();
    m_previewCore->setFractal(jf);
    m_previewCore->setColors(m_colors);
    QString err;
    const qreal dpr = devicePixelRatioF();
    QImage img = m_previewCore->renderImage(core::View::home(true), int(kPreviewW * dpr), int(kPreviewH * dpr), 1, &err);
    doneCurrent();
    if (img.isNull()) {
        m_preview->hide();
        return;
    }
    img.setDevicePixelRatio(dpr);
    m_preview->setPixmap(QPixmap::fromImage(img));
    placePreview();
    m_preview->show();
    m_preview->raise();
}
