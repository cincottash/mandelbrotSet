#include "FractalView.h"

#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QSurfaceFormat>
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
    const qreal dpr = devicePixelRatioF();
    const int W = std::max(1, int(std::lround(width() * dpr))), H = std::max(1, int(std::lround(height() * dpr)));
    const bool animating = std::fabs(m_zoomPending) > 1e-4 || m_panPending != QPointF() || std::fabs(m_rotatePending) > 1e-5;
    const bool moving = m_dragging || m_rotating || animating || (now - m_lastChangeNs) < 150'000'000;
    m_core->setView(m_view);
    m_core->setOutputSize(W, H);
    m_core->setTime(double(now) / 1e9);
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

QImage FractalView::renderPicture(const core::View& v, int w, int h, int ss, QString* error,
                                  const std::function<bool(double)>& progress)
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
    const QImage img = m_core->renderImage(v, w, h, ss, error, [&](double p) {
        const bool goOn = !progress || progress(p);
        makeCurrent();
        return goOn;
    });
    doneCurrent();
    m_exporting = false;
    update();
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
        stopMotion();
        break;
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
