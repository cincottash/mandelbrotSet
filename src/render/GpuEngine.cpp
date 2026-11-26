#include "GpuEngine.h"

#include <QFile>
#include <QOpenGLContext>
#include <QRect>
#include <QSurfaceFormat>
#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <complex>

namespace render {

namespace {

constexpr int kZeroE = -1000000000;

QByteArray loadShader(const QString& name)
{
    QFile f(QStringLiteral(":/shaders/") + name);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

int clampExp(int64_t e) { return int(std::clamp<int64_t>(e, -(1 << 29), 1 << 29)); }

}  // namespace

GpuEngine::~GpuEngine() = default;

unsigned GpuEngine::compile(const QString& name, const QByteArray& source, unsigned type, QString* error)
{
    const unsigned s = glCreateShader(type);
    const char* src = source.constData();
    const GLint len = GLint(source.size());
    glShaderSource(s, 1, &src, &len);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint logLen = 0;
        glGetShaderiv(s, GL_INFO_LOG_LENGTH, &logLen);
        QByteArray log(std::max(logLen, 1), '\0');
        glGetShaderInfoLog(s, logLen, nullptr, log.data());
        if (error) *error = QStringLiteral("The shader %1 didn't compile: %2").arg(name, QString::fromUtf8(log).trimmed());
        glDeleteShader(s);
        return 0;
    }
    return s;
}

unsigned GpuEngine::link(std::initializer_list<unsigned> shaders, QString* error)
{
    const unsigned p = glCreateProgram();
    for (unsigned s : shaders) glAttachShader(p, s);
    glLinkProgram(p);
    for (unsigned s : shaders) {
        glDetachShader(p, s);
        glDeleteShader(s);
    }
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint logLen = 0;
        glGetProgramiv(p, GL_INFO_LOG_LENGTH, &logLen);
        QByteArray log(std::max(logLen, 1), '\0');
        glGetProgramInfoLog(p, logLen, nullptr, log.data());
        if (error) *error = QStringLiteral("The shaders didn't link: %1").arg(QString::fromUtf8(log).trimmed());
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

bool GpuEngine::init(QString* error)
{
    if (m_ready) return true;
    QOpenGLContext* ctx = QOpenGLContext::currentContext();
    if (!ctx) {
        if (error) *error = QStringLiteral("No OpenGL context is available.");
        return false;
    }
    const QSurfaceFormat f = ctx->format();
    if (ctx->isOpenGLES() || f.version() < qMakePair(4, 3) || !initializeOpenGLFunctions()) {
        if (error)
            *error = QStringLiteral("The graphics driver offers OpenGL %1.%2; the renderer needs OpenGL 4.3 or newer. "
                                    "Updating the graphics driver usually fixes this.")
                         .arg(f.majorVersion()).arg(f.minorVersion());
        return false;
    }
    m_renderer = QString::fromLatin1(reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
    const QByteArray head = "#version 430 core\n";
    const QByteArray iter = loadShader(QStringLiteral("iterate.comp"));
    const QByteArray vert = loadShader(QStringLiteral("fullscreen.vert"));
    const QByteArray color = loadShader(QStringLiteral("colorize.frag"));
    const QByteArray disp = loadShader(QStringLiteral("display.frag"));
    if (iter.isEmpty() || vert.isEmpty() || color.isEmpty() || disp.isEmpty()) {
        if (error) *error = QStringLiteral("The shader files are missing from the program.");
        return false;
    }
    QString err;
    unsigned s = compile(QStringLiteral("iterate (single)"), head + iter, GL_COMPUTE_SHADER, &err);
    m_progF = s ? link({s}, &err) : 0;
    s = m_progF ? compile(QStringLiteral("iterate (wide)"), head + "#define TIER_X\n" + iter, GL_COMPUTE_SHADER, &err) : 0;
    m_progX = s ? link({s}, &err) : 0;
    if (m_progX) {
        const unsigned v = compile(QStringLiteral("fullscreen"), head + vert, GL_VERTEX_SHADER, &err);
        const unsigned c = v ? compile(QStringLiteral("colorize"), head + color, GL_FRAGMENT_SHADER, &err) : 0;
        m_progColor = c ? link({v, c}, &err) : 0;
    }
    if (m_progColor) {
        const unsigned v = compile(QStringLiteral("fullscreen"), head + vert, GL_VERTEX_SHADER, &err);
        const unsigned d = v ? compile(QStringLiteral("display"), head + disp, GL_FRAGMENT_SHADER, &err) : 0;
        m_progDisplay = d ? link({v, d}, &err) : 0;
    }
    if (!m_progDisplay) {
        if (error) *error = err;
        release();
        return false;
    }
    glGenVertexArrays(1, &m_vao);
    unsigned bufs[5];
    glGenBuffers(5, bufs);
    m_refF = bufs[0];
    m_refX = bufs[1];
    m_blaF = bufs[2];
    m_blaX = bufs[3];
    m_stats = bufs[4];
    // Empty buffers still need storage to be bound.
    const float zero[8] = {};
    for (unsigned b : bufs) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, b);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(zero), zero, GL_DYNAMIC_DRAW);
    }
    glGenBuffers(1, &m_state);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_state);
    glBufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(kMaxTile) * kMaxTile * 5 * 16, nullptr, GL_DYNAMIC_COPY);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    glGenTextures(1, &m_paletteTex);
    glBindTexture(GL_TEXTURE_2D, m_paletteTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    const quint32 gray[2] = {0xFF000000u, 0xFFFFFFFFu};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, gray);
    glBindTexture(GL_TEXTURE_2D, 0);
    m_ready = true;
    return true;
}

void GpuEngine::release()
{
    if (!QOpenGLContext::currentContext()) return;
    for (unsigned* p : {&m_progF, &m_progX, &m_progColor, &m_progDisplay})
        if (*p) { glDeleteProgram(*p); *p = 0; }
    for (unsigned* b : {&m_refF, &m_refX, &m_blaF, &m_blaX, &m_stats, &m_state})
        if (*b) { glDeleteBuffers(1, b); *b = 0; }
    if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_paletteTex) { glDeleteTextures(1, &m_paletteTex); m_paletteTex = 0; }
    m_ready = false;
    m_refLen = 0;
}

void GpuEngine::uploadReference(const core::RefOrbit& orbit, const core::BlaTable& bla)
{
    auto upload = [this](unsigned buf, const void* data, size_t bytes) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, buf);
        if (bytes == 0) {
            const float zero[8] = {};
            glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(zero), zero, GL_STATIC_DRAW);
        } else {
            glBufferData(GL_SHADER_STORAGE_BUFFER, GLsizeiptr(bytes), data, GL_STATIC_DRAW);
        }
    };
    upload(m_refF, orbit.gpuF.data(), orbit.gpuF.size() * sizeof(core::GpuRefF));
    upload(m_refX, orbit.gpuX.data(), orbit.gpuX.size() * sizeof(core::GpuRefX));
    upload(m_blaF, bla.gpuF.data(), bla.gpuF.size() * sizeof(core::GpuBlaF));
    upload(m_blaX, bla.gpuX.data(), bla.gpuX.size() * sizeof(core::GpuBlaX));
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    m_refLen = orbit.length;
    m_refEscaped = orbit.escaped;
    m_blaOffset = bla.offset;
    m_blaCount = bla.count;
    m_blaLevels = std::min(bla.levels, 32);
}

void GpuEngine::ensureFrameTextures(RawFrame& f, int w, int h)
{
    if (f.tex0 && f.allocW >= w && f.allocH >= h) return;
    freeFrame(f);
    const int aw = std::max(w, 1), ah = std::max(h, 1);
    unsigned t[2];
    glGenTextures(2, t);
    for (unsigned tex : t) {
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, aw, ah);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    f.tex0 = t[0];
    f.tex1 = t[1];
    f.allocW = aw;
    f.allocH = ah;
}

void GpuEngine::freeFrame(RawFrame& f)
{
    if (f.tex0 && QOpenGLContext::currentContext()) {
        unsigned t[2] = {f.tex0, f.tex1};
        glDeleteTextures(2, t);
    }
    f.tex0 = f.tex1 = 0;
    f.allocW = f.allocH = 0;
    f.complete = false;
}

void GpuEngine::startPass(RawFrame& frame, const PassParams& p)
{
    m_pass = p;
    frame.view = p.view;
    frame.pass = p;
    frame.ss = std::max(1, p.ss);
    frame.outW = std::max(1, p.outW);
    frame.outH = std::max(1, p.outH);
    frame.rawW = frame.outW * frame.ss;
    frame.rawH = frame.outH * frame.ss;
    frame.complete = false;
    frame.stats = PassStats();
    frame.gpuMs = 0;
    frame.maxIterations = p.maxIterations;
    ensureFrameTextures(frame, frame.rawW, frame.rawH);

    const PassStats zero;
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_stats);
    glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(zero), &zero, GL_DYNAMIC_READ);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // Big tiles for quick views, small ones when every pixel takes long (keeps each slice short, so the screen and
    // the graphics driver never wait long).
    m_chunked = p.maxIterations > chunk();
    m_tileActive = false;
    m_tileResume = false;
    m_tileSize = kMaxTile;
    m_tiles.clear();
    QList<QRect> tiles;
    for (int y = 0; y < frame.rawH; y += m_tileSize)
        for (int x = 0; x < frame.rawW; x += m_tileSize)
            tiles << QRect(x, y, std::min(m_tileSize, frame.rawW - x), std::min(m_tileSize, frame.rawH - y));
    // The middle first: the eye goes there, and it is where a zoom is aimed.
    const QPointF mid(frame.rawW / 2.0, frame.rawH / 2.0);
    std::stable_sort(tiles.begin(), tiles.end(), [&](const QRect& a, const QRect& b) {
        const QPointF da = QRectF(a).center() - mid, db = QRectF(b).center() - mid;
        return da.x() * da.x() + da.y() * da.y() < db.x() * db.x() + db.y() * db.y();
    });
    for (const QRect& r : tiles) m_tiles.push_back(r);
    m_totalPixels = qint64(frame.rawW) * frame.rawH;
    m_donePixels = 0;
    m_msPerPixel = -1;
    m_passProg = p.wideRange ? m_progX : m_progF;
}

double GpuEngine::passProgress() const
{
    return m_totalPixels > 0 ? double(m_donePixels) / double(m_totalPixels) : 1.0;
}

void GpuEngine::bindPassUniforms()
{
    const unsigned prog = m_passProg;
    auto loc = [&](const char* n) { return glGetUniformLocation(prog, n); };
    const PassParams& p = m_pass;
    const int rawW = p.outW * std::max(1, p.ss), rawH = p.outH * std::max(1, p.ss);
    glUniform2i(loc("uSize"), rawW, rawH);
    glUniform1f(loc("uInvHalfH"), float(2.0 / rawH));
    glUniform1f(loc("uHalfH"), float(rawH / 2.0));
    glUniform2f(loc("uRot"), float(std::cos(p.view.angle)), float(std::sin(p.view.angle)));
    glUniform2f(loc("uJitter"), 0.f, 0.f);
    glUniform1f(loc("uScaleM"), float(p.view.scale.m));
    glUniform1i(loc("uScaleE"), clampExp(p.view.scale.e));
    if (p.offset.isZero()) {
        glUniform2f(loc("uOffM"), 0.f, 0.f);
        glUniform1i(loc("uOffE"), kZeroE);
    } else {
        glUniform2f(loc("uOffM"), float(p.offset.x), float(p.offset.y));
        glUniform1i(loc("uOffE"), clampExp(p.offset.e));
    }
    glUniform1i(loc("uMaxIter"), p.maxIterations);
    glUniform1f(loc("uBailout2"), float(p.escapeRadius * p.escapeRadius));
    glUniform1f(loc("uLogBailout"), float(std::log(p.escapeRadius)));
    glUniform1i(loc("uRefLen"), m_refLen);
    glUniform1i(loc("uRefEscaped"), m_refEscaped ? 1 : 0);
    glUniform1i(loc("uPower"), p.power);
    glUniform1i(loc("uJulia"), p.julia ? 1 : 0);
    const int levels = (p.useBla && p.power == 2) ? m_blaLevels : 0;
    glUniform1i(loc("uBlaLevels"), levels);
    if (levels > 0) {
        glUniform1iv(loc("uBlaOffset"), levels, m_blaOffset.data());
        glUniform1iv(loc("uBlaCount"), levels, m_blaCount.data());
    }
    glUniform1i(loc("uStripe"), p.stripes ? 1 : 0);
    glUniform1f(loc("uStripeDensity"), float(p.stripeDensity));
}

void GpuEngine::dispatchTile(const QRect& r, int budget, bool resume)
{
    glUniform2i(glGetUniformLocation(m_passProg, "uOrigin"), r.x(), r.y());
    glUniform2i(glGetUniformLocation(m_passProg, "uTileSize"), r.width(), r.height());
    glUniform1i(glGetUniformLocation(m_passProg, "uIterBudget"), budget);
    glUniform1i(glGetUniformLocation(m_passProg, "uResume"), resume ? 1 : 0);
    glDispatchCompute(GLuint((r.width() + 7) / 8), GLuint((r.height() + 7) / 8), 1);
}

bool GpuEngine::continuePass(RawFrame& frame, double budgetMs)
{
    if (frame.complete) return true;
    if (!m_ready) return false;
    QElapsedTimer timer;
    timer.start();
    glUseProgram(m_passProg);
    bindPassUniforms();
    glBindImageTexture(0, frame.tex0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glBindImageTexture(1, frame.tex1, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, m_refF);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_refX);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_blaF);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_blaX);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, m_stats);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, m_state);

    // Many iterations: one tile at a time, in short dispatches that each pixel resumes from its saved state.
    while (m_chunked && (m_tileActive || !m_tiles.empty())) {
        if (double(timer.nsecsElapsed()) / 1e6 >= budgetMs) break;
        if (!m_tileActive) {
            QRect r = m_tiles.front();
            m_tiles.pop_front();
            if (r.width() > kMaxTile || r.height() > kMaxTile) {   // never bigger than the saved state holds
                for (int y = r.top(); y <= r.bottom(); y += kMaxTile)
                    for (int x = r.left(); x <= r.right(); x += kMaxTile)
                        m_tiles.push_front(QRect(x, y, std::min(kMaxTile, r.right() + 1 - x), std::min(kMaxTile, r.bottom() + 1 - y)));
                continue;
            }
            m_tile = r;
            m_tileActive = true;
            m_tileResume = false;
        }
        const quint32 zero = 0;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_stats);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, offsetof(PassStats, unfinished), sizeof(zero), &zero);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        QElapsedTimer t;
        t.start();
        dispatchTile(m_tile, chunk(), m_tileResume);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
        quint32 unfinished = 0;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_stats);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, offsetof(PassStats, unfinished), sizeof(unfinished), &unfinished);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        frame.gpuMs += double(t.nsecsElapsed()) / 1e6;
        if (unfinished == 0) {
            m_tileActive = false;
            m_donePixels += qint64(m_tile.width()) * m_tile.height();
        } else {
            m_tileResume = true;
        }
    }

    while (!m_chunked && !m_tiles.empty()) {
        const double left = budgetMs - double(timer.nsecsElapsed()) / 1e6;
        if (left <= 0) break;
        // A batch of tiles that should fit in the time left (one tile while the speed isn't known yet).
        QList<QRect> batch;
        long long pixels = 0;
        while (!m_tiles.empty()) {
            QRect r = m_tiles.front();
            if (r.width() > m_tileSize || r.height() > m_tileSize) {
                // Tiles got smaller after a slow one: split this one first.
                m_tiles.pop_front();
                for (int y = r.top(); y <= r.bottom(); y += m_tileSize)
                    for (int x = r.left(); x <= r.right(); x += m_tileSize)
                        m_tiles.push_front(QRect(x, y, std::min(m_tileSize, r.right() + 1 - x),
                                                 std::min(m_tileSize, r.bottom() + 1 - y)));
                continue;
            }
            const long long px = qint64(r.width()) * r.height();
            // The estimate comes from the tiles done so far, and the next ones may be far slower (all inside):
            // a batch's worst case (every pixel running the full limit) also stays under a fixed amount of work.
            const bool tooMuchAtWorst = double(pixels + px) * double(m_pass.maxIterations) > kWorkCap;
            if (!batch.isEmpty() && (m_msPerPixel < 0 || double(pixels + px) * m_msPerPixel > left || tooMuchAtWorst)) break;
            batch << r;
            pixels += px;
            m_tiles.pop_front();
        }
        QElapsedTimer t;
        t.start();
        for (const QRect& r : batch) dispatchTile(r, m_pass.maxIterations, false);
        glFinish();
        const double ms = double(t.nsecsElapsed()) / 1e6;
        frame.gpuMs += ms;
        m_donePixels += pixels;
        const double perPx = ms / double(std::max<long long>(pixels, 1));
        m_msPerPixel = m_msPerPixel < 0 ? perPx : 0.5 * m_msPerPixel + 0.5 * perPx;
        if (batch.size() == 1 && ms > 120 && m_tileSize > 16) m_tileSize /= 2;
    }
    glUseProgram(0);
    if (!m_tiles.empty() || m_tileActive) return false;
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_stats);
    glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(PassStats), &frame.stats);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    frame.complete = true;
    return true;
}

void GpuEngine::setPalette(const QVector<quint32>& rgba)
{
    if (!m_ready || rgba.isEmpty()) return;
    glBindTexture(GL_TEXTURE_2D, m_paletteTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GLsizei(rgba.size()), 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.constData());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void GpuEngine::ensureTarget(ColorTarget& t, int w, int h)
{
    w = std::max(w, 1);
    h = std::max(h, 1);
    if (t.fbo && t.w == w && t.h == h) return;
    freeTarget(t);
    glGenTextures(1, &t.tex);
    glBindTexture(GL_TEXTURE_2D, t.tex);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, w, h);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glGenFramebuffers(1, &t.fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    t.w = w;
    t.h = h;
    t.valid = false;
}

void GpuEngine::freeTarget(ColorTarget& t)
{
    if (QOpenGLContext::currentContext()) {
        if (t.fbo) glDeleteFramebuffers(1, &t.fbo);
        if (t.tex) glDeleteTextures(1, &t.tex);
    }
    t.fbo = t.tex = 0;
    t.w = t.h = 0;
    t.valid = false;
}

static QVector3D toVec(const QColor& c) { return QVector3D(float(c.redF()), float(c.greenF()), float(c.blueF())); }

void GpuEngine::colorize(const RawFrame& raw, const core::ColorParams& c, double timeSeconds, ColorTarget& target,
                         QSize whole, QPoint origin)
{
    if (!m_ready || !raw.tex0) return;
    ensureTarget(target, raw.outW, raw.outH);
    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, target.fbo);
    glViewport(0, 0, target.w, target.h);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(m_progColor);
    auto loc = [&](const char* n) { return glGetUniformLocation(m_progColor, n); };
    auto vec3 = [&](const char* n, const QVector3D& v) { glUniform3f(loc(n), v.x(), v.y(), v.z()); };
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, raw.tex0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, raw.tex1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_paletteTex);
    glUniform1i(loc("uRaw0"), 0);
    glUniform1i(loc("uRaw1"), 1);
    glUniform1i(loc("uPalette"), 2);
    glUniform1i(loc("uSS"), raw.ss);
    if (whole.isEmpty()) whole = QSize(raw.outW, raw.outH);
    glUniform2i(loc("uOutSize"), whole.width(), whole.height());
    glUniform2i(loc("uOrigin"), origin.x(), origin.y());
    glUniform1i(loc("uStyle"), c.style);
    glUniform1i(loc("uScale"), c.colorScale);
    glUniform1f(loc("uDensity"), float(c.density));
    const double off = c.offset + c.cycleSpeed * timeSeconds;
    glUniform1f(loc("uOffset"), float(off - std::floor(off)));
    vec3("uSineFreq", QVector3D(float(c.sineFreqR), float(c.sineFreqG), float(c.sineFreqB)));
    vec3("uSinePhase", QVector3D(float(c.sinePhaseR), float(c.sinePhaseG), float(c.sinePhaseB)));
    glUniform1f(loc("uSineBase"), float(c.sineBrightness));
    glUniform1f(loc("uSineHeight"), float(c.sineContrast));
    glUniform1f(loc("uMoireScale"), float(c.moireScale));
    vec3("uMoireTint", toVec(c.moireTint));
    glUniform1f(loc("uLightStrength"), float(c.lightStrength));
    const double a = c.lightAngle * M_PI / 180.0, h = c.lightHeight * M_PI / 180.0;
    vec3("uLightDir", QVector3D(float(std::cos(a) * std::cos(h)), float(std::sin(a) * std::cos(h)), float(std::sin(h))));
    glUniform1f(loc("uRelief"), float(c.relief));
    glUniform1f(loc("uSpecular"), float(c.specular));
    glUniform1f(loc("uEdgeGlow"), float(c.edgeGlow));
    glUniform1f(loc("uEdgeWidth"), float(c.edgeWidth));
    vec3("uEdgeColor", toVec(c.edgeColor));
    glUniform1i(loc("uInterior"), c.interiorStyle);
    vec3("uIn1", toVec(c.interiorColor));
    vec3("uIn2", toVec(c.interiorColor2));
    glUniform1f(loc("uHue"), float(c.hue * M_PI / 180.0));
    glUniform1f(loc("uSat"), float(c.saturation));
    glUniform1f(loc("uBright"), float(c.brightness));
    glUniform1f(loc("uContrast"), float(c.contrast));
    glUniform1f(loc("uGamma"), float(c.gamma));
    glUniform1f(loc("uVignette"), float(c.vignette));
    glUniform1f(loc("uGrain"), float(c.grain));
    glUniform1f(loc("uSeed"), 0.0f);
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glUseProgram(0);
    for (int unit : {2, 1, 0}) {
        glActiveTexture(GLenum(GL_TEXTURE0 + unit));
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, GLuint(prevFbo));
    target.view = raw.view;
    target.valid = true;
}

void GpuEngine::display(const ColorTarget& picture, const core::View& cur, int screenW, int screenH, const QColor& bg)
{
    if (!m_ready) return;
    glViewport(0, 0, screenW, screenH);
    if (!picture.valid) {
        clear(screenW, screenH, bg);
        return;
    }
    // q_old = M q_new + T, with M = (s1 / s0) R(a1 - a0) and T = R(-a0) (c1 - c0) / s0.
    const core::View& old = picture.view;
    const double ratio = (cur.scale / old.scale).toDouble();
    const double da = cur.angle - old.angle;
    const core::ComplexExp dcen = cur.centerOffsetFrom(old.cx, old.cy) * (core::FloatExp(1.0) / old.scale);
    std::complex<double> t = dcen.toComplex();
    if (!std::isfinite(t.real()) || !std::isfinite(t.imag()) || std::abs(t) > 1e6) t = {1e6, 1e6};
    const double ca = std::cos(-old.angle), sa = std::sin(-old.angle);
    const double tx = t.real() * ca - t.imag() * sa, ty = t.real() * sa + t.imag() * ca;
    const double r = std::isfinite(ratio) ? std::min(ratio, 1e6) : 1e6;
    const float m[4] = {float(r * std::cos(da)), float(r * std::sin(da)), float(-r * std::sin(da)), float(r * std::cos(da))};
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(m_progDisplay);
    auto loc = [&](const char* n) { return glGetUniformLocation(m_progDisplay, n); };
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, picture.tex);
    glUniform1i(loc("uImage"), 0);
    glUniform2f(loc("uImageSize"), float(picture.w), float(picture.h));
    glUniform2f(loc("uScreen"), float(screenW), float(screenH));
    glUniformMatrix2fv(loc("uM"), 1, GL_FALSE, m);
    glUniform2f(loc("uT"), float(tx), float(ty));
    glUniform3f(loc("uBackground"), float(bg.redF()), float(bg.greenF()), float(bg.blueF()));
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);
}

void GpuEngine::clear(int w, int h, const QColor& c)
{
    glViewport(0, 0, w, h);
    glClearColor(float(c.redF()), float(c.greenF()), float(c.blueF()), 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
}

std::vector<float> GpuEngine::readRaw(const RawFrame& f, int which)
{
    std::vector<float> all(size_t(f.allocW) * size_t(f.allocH) * 4);
    glBindTexture(GL_TEXTURE_2D, which == 0 ? f.tex0 : f.tex1);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, all.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    std::vector<float> out(size_t(f.rawW) * size_t(f.rawH) * 4);
    for (int y = 0; y < f.rawH; ++y)
        std::copy_n(all.begin() + ptrdiff_t(size_t(y) * size_t(f.allocW) * 4), size_t(f.rawW) * 4,
                    out.begin() + ptrdiff_t(size_t(y) * size_t(f.rawW) * 4));
    return out;
}

std::vector<float> GpuEngine::readRawSmall(const RawFrame& f, int maxW, int* outW, int* outH)
{
    const int w = std::max(1, std::min(f.rawW, maxW));
    const int h = std::max(1, int(std::lround(double(f.rawH) * w / std::max(f.rawW, 1))));
    *outW = w;
    *outH = h;
    std::vector<float> out(size_t(w) * size_t(h) * 4);
    unsigned fb[2], small = 0;
    glGenFramebuffers(2, fb);
    glGenTextures(1, &small);
    glBindTexture(GL_TEXTURE_2D, small);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, w, h);
    glBindTexture(GL_TEXTURE_2D, 0);
    GLint prevRead = 0, prevDraw = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDraw);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fb[0]);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, f.tex0, 0);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fb[1]);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, small, 0);
    glBlitFramebuffer(0, 0, f.rawW, f.rawH, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fb[1]);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_FLOAT, out.data());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(prevRead));
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(prevDraw));
    glDeleteFramebuffers(2, fb);
    glDeleteTextures(1, &small);
    return out;
}

QImage GpuEngine::readTarget(const ColorTarget& t)
{
    if (!t.fbo) return {};
    QImage img(t.w, t.h, QImage::Format_RGBA8888);
    GLint prev = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prev);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, t.fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, t.w, t.h, GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(prev));
    return img.convertToFormat(QImage::Format_RGB32);
}

}  // namespace render
