// The picture: draws the fractal with the graphics card and turns mouse and keys into moves of the view.
//   Wheel: zoom at the pointer (smoothly)   Drag: move   Right-drag: turn   Double-click: zoom in 4x
//   Right-click: menu   Arrows: move   + and -: zoom   Page Up and Page Down: turn
#pragma once

#include "core/ColorParams.h"
#include "core/Fractal.h"
#include "core/View.h"
#include "render/GpuEngine.h"
#include "render/RenderCore.h"

#include <QElapsedTimer>
#include <QImage>
#include <QOpenGLWidget>
#include <QPointer>
#include <QTimer>

#include <functional>
#include <memory>

class QLabel;

class FractalView : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit FractalView(QWidget* parent = nullptr);
    ~FractalView() override;

    void setFractal(const core::FractalParams& f);
    void setColors(const core::ColorParams& c);
    void setView(const core::View& v);     // jumps there, stopping any motion
    const core::View& view() const { return m_view; }
    void setAntiAliasing(int samplesPerSide);
    void setZoomStep(double factorPerNotch) { m_zoomStep = std::clamp(factorPerNotch, 1.05, 16.0); }
    void setJuliaPreview(bool on);
    bool juliaPreview() const { return m_juliaPreview; }
    void setPickMode(bool on);
    bool pickMode() const { return m_pickMode; }

    bool gpuReady() const { return m_ok; }
    bool gpuTried() const { return m_tried; }
    QString gpuError() const { return m_error; }
    QString gpuName() const;
    render::CoreStatus coreStatus() const;
    double framesPerSecond() const { return m_fps; }
    bool busy() const { return m_busy; }

    // Autopilot: zooms in on its own, forever, steering toward the richest detail near the middle.
    void setAutopilot(bool on);
    bool autopilot() const { return m_autopilot; }
    void setAutopilotSpeed(double factorPerSecond) { m_autoRate = std::log(std::clamp(factorPerSecond, 1.01, 16.0)); }
    double autopilotSpeed() const { return std::exp(m_autoRate); }
    // Testing: run the autopilot for a stretch of simulated time, a frame at a time.
    void autopilotSteps(int frames, double secondsPerFrame);

    // Smooth moves (as the keys and buttons do).
    void zoomAnimated(double factor, double u = 0, double v = 0);
    void panAnimated(double du, double dv);
    void rotateBy(double degrees);
    void stopMotion();

    // A full picture of any view and size (blocks; drawn in pieces when large). Null on cancel or error.
    QImage renderPicture(const core::View& v, int w, int h, int samplesPerSide, QString* error,
                         const std::function<bool(double)>& progress = {}, double timeSeconds = -1);
    // A movie holds the engine from its first frame to its last (the picture on screen waits meanwhile), with
    // the reference orbit computed once for its deepest frame.
    bool beginMovie(const core::View& deepest, int w, int h);
    void endMovie();
    // The escape times (mu, -1 inside) of a small render of a view, for matching a loop's colors.
    std::vector<float> probeEscapeTimes(const core::View& v, int w, int h);
    // The newest colors as a picture (for place thumbnails).
    QImage currentPicture();
    // A small picture of any place, on the preview's renderer (the main view's reference orbit stays).
    QImage renderThumbnail(const core::FractalParams& f, const core::ColorParams& c, const core::View& v, int w, int h);
    // Renders until the picture is finished or the time runs out (tests and screenshots). True when finished.
    bool renderUntilIdle(int timeoutMs);
    // The plane point under a widget position, as text with all the digits the zoom needs.
    void pointAt(const QPointF& widgetPos, QString* re, QString* im) const;
    render::RenderCore* core() { return m_core.get(); }

signals:
    void viewChanged();
    void viewSettled();           // no change for a moment: a good time to remember the view
    void frameDone();
    void pointerMoved(double re, double im);
    void pointPicked(const QString& re, const QString& im);
    void contextMenuRequested(const QPoint& global, const QString& re, const QString& im);
    void gpuUnavailable(const QString& reason);
    void zoomLimitReached(const QString& why);
    void autopilotChanged(bool on);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    QPointF toViewUnits(const QPointF& widgetPos) const;   // u right, v up, -1..1 across the height
    void markChanged();
    void advance(double dt);
    void applyZoom(double u, double v, double factor);
    void updatePreview();
    void placePreview();
    void autopilotAdvance(double dt, qint64 now);
    bool pickAutopilotTarget(QPointF* target);   // in view units of the current view; needs the context current

    render::GpuEngine m_gpu;
    std::unique_ptr<render::RenderCore> m_core;
    render::GpuEngine m_previewGpu;
    std::unique_ptr<render::RenderCore> m_previewCore;
    bool m_ok = false, m_tried = false;
    bool m_exporting = false;
    bool m_inMovie = false;
    QString m_error;

    core::View m_view;
    core::FractalParams m_fractal;
    core::ColorParams m_colors;
    int m_aa = 2;
    double m_zoomStep = 1.5;

    // Motion
    QElapsedTimer m_clock;
    qint64 m_lastFrameNs = 0;
    qint64 m_lastChangeNs = -1'000'000'000;
    double m_zoomPending = 0;     // log of the zoom still to apply
    QPointF m_zoomAnchor;         // in view units
    QPointF m_panPending;         // view units still to move
    double m_rotatePending = 0;   // radians
    bool m_dragging = false, m_rotating = false, m_rightDown = false;
    QPointF m_lastMouse, m_pressPos;
    double m_rotateStartAngle = 0;
    bool m_pickMode = false;

    // Status
    double m_fps = 0;
    int m_framesCounted = 0;
    qint64 m_fpsWindowStart = 0;
    bool m_busy = false;
    QTimer m_settle;

    // Autopilot
    bool m_autopilot = false;
    double m_autoRate = 0.6931;   // log of the zoom per second (2x)
    QPointF m_autoTarget;         // where it steers, in current view units
    bool m_autoHasTarget = false;
    qint64 m_autoNextLookNs = 0;
    qint64 m_userSteersUntilNs = 0;
    double m_autoBackOut = 0;     // seconds left of zooming out after the view went empty

    // Julia preview
    bool m_juliaPreview = false;
    QLabel* m_preview = nullptr;
    QTimer m_previewTimer;
    QPointF m_previewSeed;
    bool m_previewWanted = false;
};
