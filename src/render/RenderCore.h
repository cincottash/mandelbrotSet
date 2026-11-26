// Decides what to render and when: keeps a reference orbit that fits the view (computed on a worker thread),
// renders quick low-resolution previews while the view moves and the full picture when it rests, adjusts the
// iteration limit, and recolors without recomputing when only colors change.
#pragma once

#include "GpuEngine.h"

#include "core/ColorParams.h"
#include "core/Fractal.h"
#include "core/Reference.h"
#include "core/View.h"

#include <QImage>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace render {

struct CoreStatus {
    bool referenceBusy = false;     // a reference orbit is being computed
    int referenceProgress = 0;      // iterations done
    int referenceTarget = 0;
    bool waiting = false;           // nothing can be drawn until the reference is ready
    int maxIterations = 0;          // the limit in use
    bool wideRange = false;         // the extended-range math (deep zooms)
    double lastPassMs = 0;          // graphics-card time of the last full picture
    int lastPassW = 0, lastPassH = 0;
    bool refining = false;          // a pass is in progress
    double progress = 1.0;          // of the pass in progress
    int previewDivisor = 1;         // 1 = full resolution
    quint32 glitchPixels = 0;
    int referenceLength = 0;
    bool referenceEscaped = false;
};

class RenderCore {
public:
    explicit RenderCore(GpuEngine& gpu);
    ~RenderCore();
    RenderCore(const RenderCore&) = delete;
    RenderCore& operator=(const RenderCore&) = delete;

    void setFractal(const core::FractalParams& f);
    void setColors(const core::ColorParams& c);
    void setView(const core::View& v);
    void setOutputSize(int w, int h);
    void setSupersampling(int ss) { m_ss = std::clamp(ss, 1, 4); }
    void setTime(double seconds) { m_time = seconds; }
    const core::View& view() const { return m_view; }

    // Interactive work for one screen refresh. `moving`: the view changes every frame, so previews are drawn.
    // Returns true while more work is waiting (call again on the next frame).
    bool step(double budgetMs, bool moving);
    // The newest finished colors (follow the current view with GpuEngine::display()).
    const ColorTarget& picture() const { return m_picture; }
    // The newest finished raw frame (for the autopilot's look at the picture), or null.
    const RawFrame* frontFrame() const { return (m_front >= 0 && m_frames[m_front].complete) ? &m_frames[m_front] : nullptr; }
    CoreStatus status() const;

    // Renders a whole picture now (exports, tests): blocks, computing the reference on this thread if needed.
    // `progress` gets 0..1 and returns false to cancel. A null image on cancel or error.
    QImage renderImage(const core::View& v, int w, int h, int ss, QString* error,
                       const std::function<bool(double)>& progress = {});

    // Computes a reference orbit for the deepest view of a movie now, so every frame of a zoom toward it reuses it.
    bool prepareReference(const core::View& deepest, int w, int h);
    // The iteration limit a finished render asks for (Auto iterations), or 0 when the limit is fine.
    int raisedIterations(const PassStats& s, int maxIt, quint64 pixels, const core::View& v) const;

    // The deepest the current formula can zoom (power 2: practically unlimited).
    core::FloatExp minimumScale() const;
    // The plane point of a picture pixel at the current view, at full precision.
    void planePoint(const core::View& v, int w, int h, double px, double py, core::BigFloat* x, core::BigFloat* y) const;
    // Testing: turn the iteration skipping off or on, and force a reference point.
    void setUseBla(bool on) { m_useBla = on; m_dirty = true; }
    bool useBla() const { return m_useBla; }
    int autoIterations() const { return m_autoIter; }
    // Testing: renderImage() keeps the raw numbers of its last piece here when this is on.
    bool captureRaw = false;
    std::vector<float> capturedRaw0, capturedRaw1;
    PassStats capturedStats;
    int capturedReferences = 0;   // reference orbits renderImage() computed (glitch retries included)

private:
    struct RefState {
        std::shared_ptr<core::RefOrbit> orbit;
        std::shared_ptr<core::BlaTable> bla;
        core::FractalParams fractal;
        long id = 0;
    };
    struct Job {
        core::OrbitRequest request;
        core::FloatExp dcMax;
        std::shared_ptr<core::RefOrbit> reuseOrbit;   // set: only rebuild the table for it
        core::FractalParams fractal;
    };

    int maxIterationsFor(const core::View& v) const;
    int baseIterations(const core::View& v) const;
    core::FloatExp dcMaxFor(const core::View& v, const core::BigFloat& rx, const core::BigFloat& ry, int w, int h) const;
    // Is the uploaded reference usable for this view (maybe not ideal)? Does it need replacing?
    bool referenceUsable(const core::View& v, int maxIt) const;
    bool referenceWanted(const core::View& v, int maxIt, int w, int h, bool* tableOnly) const;
    void requestReference(const core::View& v, int maxIt, int w, int h, bool tableOnly, const core::BigFloat* atX = nullptr,
                          const core::BigFloat* atY = nullptr);
    void startWorker(Job job);
    bool collectWorker();   // true if a new reference was uploaded
    void stopWorker();
    PassParams passParams(const core::View& v, int outW, int outH, int ss, int maxIt) const;
    void afterFullPass(const RawFrame& f);

    GpuEngine& m_gpu;
    core::FractalParams m_fractal;
    core::ColorParams m_colors;
    core::View m_view;
    int m_w = 800, m_h = 600, m_ss = 1;
    double m_time = 0;
    bool m_useBla = true;
    bool m_dirty = true;            // the view or formula changed since the last pass started
    bool m_colorsDirty = true;
    QString m_paletteName;

    RefState m_ref;                 // uploaded to the graphics card
    long m_nextRefId = 1;

    // Worker
    std::thread m_thread;
    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_jobDone{false};
    std::atomic<int> m_jobProgress{0};
    int m_jobTarget = 0;
    bool m_jobRunning = false;
    Job m_job;
    std::shared_ptr<core::RefOrbit> m_jobOrbit;
    std::shared_ptr<core::BlaTable> m_jobBla;
    bool m_jobOk = false;

    // Frames
    RawFrame m_frames[2];
    int m_front = -1;               // index of the last complete frame, -1 none
    int m_back = 0;                 // the frame being computed
    bool m_passActive = false;
    PassParams m_activePass;
    int m_activeDivisor = 1;
    int m_activeSS = 1;
    ColorTarget m_picture;
    double m_msPerPixelFull = -1;   // the measured cost of a full-resolution pixel, for preview sizing
    int m_autoIter = 1000;
    double m_autoIterScaleLog2 = 0; // log2 of the scale where m_autoIter was learned
    double m_lastFullMs = 0;
    int m_lastFullW = 0, m_lastFullH = 0;
    quint32 m_lastGlitches = 0;
    int m_glitchRetries = 0;
    bool m_waiting = false;
};

}  // namespace render
