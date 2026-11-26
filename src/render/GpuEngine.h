// The graphics-card side: uploads the reference orbit, runs the iteration shader tile by tile within a time
// budget, colors the raw results and draws them. Every call needs the OpenGL 4.3 context current.
#pragma once

#include "core/ColorParams.h"
#include "core/FloatExp.h"
#include "core/Reference.h"
#include "core/View.h"

#include <QElapsedTimer>
#include <QImage>
#include <QOpenGLFunctions_4_3_Core>
#include <QString>

#include <deque>

namespace render {

struct PassStats {
    quint32 maxN = 0, maxIdx = 0, glitches = 0, nearMax = 0, escaped = 0, maxEscN = 0, inside = 0, pad = 0;
};

// One render's raw numbers (two float textures) and what it was rendered for.
struct RawFrame {
    unsigned tex0 = 0, tex1 = 0;
    int allocW = 0, allocH = 0;   // texture size (may be larger than the pass)
    int rawW = 0, rawH = 0;       // the pass's raw size = out size * ss
    int outW = 0, outH = 0;
    int ss = 1;
    core::View view;
    bool complete = false;
    PassStats stats;
    double gpuMs = 0;             // time spent computing it
    int maxIterations = 0;
    long refId = 0;               // which reference orbit it was computed with (set by the caller)
    int divisor = 1;              // 1 = full resolution, 4 = a quarter-size preview (set by the caller)
};

// A picture of colors on the graphics card.
struct ColorTarget {
    unsigned fbo = 0, tex = 0;
    int w = 0, h = 0;
    core::View view;              // the view the picture shows
    bool valid = false;
};

struct PassParams {
    core::View view;
    core::ComplexExp offset;      // view center - reference point
    int outW = 0, outH = 0;
    int ss = 1;
    int maxIterations = 1000;
    double escapeRadius = 256;
    bool julia = false;
    int power = 2;
    bool stripes = false;
    double stripeDensity = 5;
    bool wideRange = false;       // the extended-range shader (deep zooms)
    bool useBla = true;
};

class GpuEngine : protected QOpenGLFunctions_4_3_Core {
public:
    GpuEngine() = default;
    ~GpuEngine();
    GpuEngine(const GpuEngine&) = delete;
    GpuEngine& operator=(const GpuEngine&) = delete;

    // Compiles the shaders. False (with a reason) if the graphics driver lacks OpenGL 4.3.
    bool init(QString* error);
    bool ready() const { return m_ready; }
    void release();
    QString rendererName() const { return m_renderer; }

    void uploadReference(const core::RefOrbit& orbit, const core::BlaTable& bla);
    bool hasReference() const { return m_refLen > 0; }
    int referenceLength() const { return m_refLen; }

    // Starts computing `frame` for these parameters; continuePass() does the work in slices.
    void startPass(RawFrame& frame, const PassParams& p);
    // Works until the budget is spent. True when the whole frame is done.
    bool continuePass(RawFrame& frame, double budgetMs);
    bool passPending() const { return !m_tiles.empty(); }
    double passProgress() const;

    void setPalette(const QVector<quint32>& rgba);
    void ensureTarget(ColorTarget& t, int w, int h);
    void freeTarget(ColorTarget& t);
    void freeFrame(RawFrame& f);
    // Colors the raw frame into the target (sized to the frame's out size). `whole` and `origin` place it in a
    // larger picture drawn in pieces (an export), so patterns tied to pixel positions line up; empty = the frame.
    void colorize(const RawFrame& raw, const core::ColorParams& c, double timeSeconds, ColorTarget& target,
                  QSize whole = {}, QPoint origin = {});
    // Draws the picture into the currently bound framebuffer, following the current view.
    void display(const ColorTarget& picture, const core::View& current, int screenW, int screenH, const QColor& bg);
    QImage readTarget(const ColorTarget& t);
    // The raw numbers of a frame (4 floats per raw pixel, row 0 at the top): which = 0 or 1. For tests.
    std::vector<float> readRaw(const RawFrame& f, int which);
    // Fill the bound framebuffer with one color.
    void clear(int w, int h, const QColor& c);

private:
    unsigned compile(const QString& name, const QByteArray& source, unsigned type, QString* error);
    unsigned link(std::initializer_list<unsigned> shaders, QString* error);
    void ensureFrameTextures(RawFrame& f, int w, int h);
    void dispatchTile(const QRect& r);
    void bindPassUniforms();

    bool m_ready = false;
    QString m_renderer;
    unsigned m_progF = 0, m_progX = 0, m_progColor = 0, m_progDisplay = 0;
    unsigned m_vao = 0;
    unsigned m_refF = 0, m_refX = 0, m_blaF = 0, m_blaX = 0, m_stats = 0;
    unsigned m_paletteTex = 0;
    int m_refLen = 0;
    bool m_refEscaped = false;
    std::vector<int32_t> m_blaOffset, m_blaCount;
    int m_blaLevels = 0;

    // The pass in progress.
    PassParams m_pass;
    std::deque<QRect> m_tiles;
    long long m_totalPixels = 0, m_donePixels = 0;
    double m_msPerPixel = -1;     // measured; -1 until known
    int m_tileSize = 128;
    unsigned m_passProg = 0;
};

}  // namespace render
