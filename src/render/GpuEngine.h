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
    quint32 maxN = 0, maxIdx = 0, glitches = 0, nearMax = 0, escaped = 0, maxEscN = 0, inside = 0, unfinished = 0;
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
    bool boxing = true;           // fill blocks enclosed by inside pixels without iterating them
    bool shallowCheck = true;     // shallow views: skip the main cardioid and the period-2 disk
};

// One render's raw numbers (two float textures) and what it was rendered for.
struct RawFrame {
    unsigned tex0 = 0, tex1 = 0;
    int allocW = 0, allocH = 0;   // texture size (may be larger than the pass)
    int rawW = 0, rawH = 0;       // the pass's raw size = out size * ss
    int outW = 0, outH = 0;
    int ss = 1;
    core::View view;
    PassParams pass;              // everything it was computed with
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
    bool passPending() const { return !m_tiles.empty() || m_groupActive; }
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
    // A small copy of the first raw texture (nearest pixels, at most maxW wide), for a quick look at the picture.
    std::vector<float> readRawSmall(const RawFrame& f, int maxW, int* w, int* h);
    // Fill the bound framebuffer with one color.
    void clear(int w, int h, const QColor& c);

private:
    unsigned compile(const QString& name, const QByteArray& source, unsigned type, QString* error);
    unsigned link(std::initializer_list<unsigned> shaders, QString* error);
    void ensureFrameTextures(RawFrame& f, int w, int h);
    void dispatchTile(const QRect& r, int slot, int budget, bool resume, int pixelSet);
    void bindPassUniforms();
    void bloom(ColorTarget& target, const core::ColorParams& c);   // the glow, on the finished colors

    unsigned m_progBloom = 0;
    unsigned m_bloomTex[2] = {0, 0}, m_bloomFbo[2] = {0, 0};
    int m_bloomW = 0, m_bloomH = 0;

    bool m_ready = false;
    QString m_renderer;
    unsigned m_progF = 0, m_progX = 0, m_progColor = 0, m_progDisplay = 0;
    unsigned m_vao = 0;
    unsigned m_refF = 0, m_refX = 0, m_blaF = 0, m_blaX = 0, m_stats = 0;
    unsigned m_paletteTex = 0;
    int m_refLen = 0;
    bool m_refEscaped = false;
    QPointF m_refC;
    std::vector<int32_t> m_blaOffset, m_blaCount;
    int m_blaLevels = 0;

    // The pass in progress.
    PassParams m_pass;
    std::deque<QRect> m_tiles;
    long long m_totalPixels = 0, m_donePixels = 0;
    double m_msPerPixel = -1;     // measured; -1 until known
    int m_tileSize = 128;
    unsigned m_passProg = 0;
    // Tiles run in groups, in rounds of dispatches that each do at most kWorkCap pixel-iterations at worst (each
    // pixel saves its state between rounds), so no dispatch runs long enough for the graphics driver to reset.
    unsigned m_state = 0, m_blocks = 0, m_progBoxes = 0;
    QList<QRect> m_group;
    long long m_groupPixels = 0;
    bool m_groupActive = false;
    int m_phase = 0;              // 0 every pixel, 1 block borders, 2 block insides (boxing)
    bool m_resume = false;

public:
    // Most pixel-iterations one dispatch round may need at worst. Measured on the RTX 4080: 1e10 to 7e10
    // iterations a second, so a round takes 0.1 to 0.4 s at worst, far under Windows' 2 s driver limit.
    static constexpr double kWorkCap = 4.0e9;
    static constexpr int kMaxTile = 128;
    static constexpr int kGroupTiles = 8;             // about 131,000 pixels: enough to fill the graphics card
    // Boxing flags per tile: 4 big blocks (64 pixels) and 64 small ones (16 pixels), as the shaders expect.
    static constexpr int kBlocksPerTile = 4 + 64;
    void setChunkForTests(int iterations) { m_chunkOverride = iterations; }   // 0 = normal

private:
    int m_chunkOverride = 0;
    int iterationsPerDispatch(long long groupPixels) const;
};

}  // namespace render
