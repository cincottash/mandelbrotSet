#include "MovieWriter.h"

#include "GifEncoder.h"
#include "core/Tr.h"

#include <QDir>
#include <QFile>
#include <QSaveFile>

#include <algorithm>

#include <webp/encode.h>
#include <webp/mux.h>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#endif

namespace {

// ---------------------------------------------------------------- PNG frames

class PngFrames : public MovieWriter {
public:
    PngFrames(const QString& dir) : m_dir(dir) {}
    bool add(const QImage& frame, QString* error) override
    {
        const QString f = QDir(m_dir).filePath(QStringLiteral("frame-%1.png").arg(++m_n, 5, 10, QLatin1Char('0')));
        if (!frame.save(f)) {
            if (error) *error = T("Couldn't write %1.").arg(QDir::toNativeSeparators(f));
            return false;
        }
        return true;
    }
    bool finish(QString*) override { return true; }

private:
    QString m_dir;
    int m_n = 0;
};

// ---------------------------------------------------------------- animated WebP

class WebPWriter : public MovieWriter {
public:
    WebPWriter(const QString& path, int w, int h, int fps) : m_path(path), m_w(w), m_h(h), m_fps(fps)
    {
        WebPAnimEncoderOptions opt;
        WebPAnimEncoderOptionsInit(&opt);
        opt.anim_params.loop_count = 0;   // forever
        m_enc = WebPAnimEncoderNew(w, h, &opt);
        WebPConfigInit(&m_cfg);
        m_cfg.quality = 88;
        m_cfg.method = 4;
    }
    ~WebPWriter() override
    {
        if (m_enc) WebPAnimEncoderDelete(m_enc);
    }
    bool ok() const { return m_enc != nullptr; }
    bool add(const QImage& frame, QString* error) override
    {
        const QImage img = frame.convertToFormat(QImage::Format_RGB32);
        WebPPicture pic;
        if (!WebPPictureInit(&pic)) return fail(error);
        pic.width = m_w;
        pic.height = m_h;
        pic.use_argb = 1;
        if (!WebPPictureImportBGRA(&pic, img.constBits(), int(img.bytesPerLine()))) {
            WebPPictureFree(&pic);
            return fail(error);
        }
        const int ts = int(qint64(m_frames) * 1000 / m_fps);
        const bool ok = WebPAnimEncoderAdd(m_enc, &pic, ts, &m_cfg);
        WebPPictureFree(&pic);
        if (!ok) return fail(error);
        ++m_frames;
        return true;
    }
    bool finish(QString* error) override
    {
        const int ts = int(qint64(m_frames) * 1000 / m_fps);
        if (!WebPAnimEncoderAdd(m_enc, nullptr, ts, nullptr)) return fail(error);
        WebPData data;
        WebPDataInit(&data);
        if (!WebPAnimEncoderAssemble(m_enc, &data)) return fail(error);
        QSaveFile f(m_path);
        bool ok = f.open(QIODevice::WriteOnly)
            && f.write(reinterpret_cast<const char*>(data.bytes), qint64(data.size)) == qint64(data.size) && f.commit();
        WebPDataClear(&data);
        if (!ok && error) *error = T("Couldn't write %1.").arg(QDir::toNativeSeparators(m_path));
        return ok;
    }

private:
    bool fail(QString* error)
    {
        if (error) *error = T("The WebP encoder stopped: %1").arg(QString::fromUtf8(m_enc ? WebPAnimEncoderGetError(m_enc) : "no encoder"));
        return false;
    }
    QString m_path;
    int m_w, m_h, m_fps;
    int m_frames = 0;
    WebPAnimEncoder* m_enc = nullptr;
    WebPConfig m_cfg;
};

// ---------------------------------------------------------------- animated GIF

class GifWriter : public MovieWriter {
public:
    GifWriter(const QString& path) : m_file(path) {}
    bool open(int w, int h, int fps, QString* error)
    {
        if (!m_file.open(QIODevice::WriteOnly) || !m_enc.begin(&m_file, w, h, fps)) {
            if (error) *error = T("Couldn't create %1.").arg(QDir::toNativeSeparators(m_file.fileName()));
            return false;
        }
        return true;
    }
    bool add(const QImage& frame, QString* error) override
    {
        if (m_enc.add(frame)) return true;
        if (error) *error = T("Couldn't write %1 (is the disk full?).").arg(QDir::toNativeSeparators(m_file.fileName()));
        return false;
    }
    bool finish(QString* error) override
    {
        if (m_enc.finish() && m_file.commit()) return true;
        if (error) *error = T("Couldn't write %1.").arg(QDir::toNativeSeparators(m_file.fileName()));
        return false;
    }

private:
    QSaveFile m_file;
    GifEncoder m_enc;
};

// ---------------------------------------------------------------- MP4 (Windows Media Foundation)

#ifdef Q_OS_WIN
template <typename Com>
void release(Com*& p)
{
    if (p) {
        p->Release();
        p = nullptr;
    }
}

class Mp4Writer : public MovieWriter {
public:
    Mp4Writer(int w, int h, int fps) : m_w(w), m_h(h), m_fps(fps) {}
    ~Mp4Writer() override
    {
        release(m_writer);
        if (m_mfStarted) MFShutdown();
        if (m_comStarted) CoUninitialize();
    }
    bool open(const QString& path, QString* error)
    {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        m_comStarted = SUCCEEDED(co);
        if (FAILED(MFStartup(MF_VERSION))) return fail(error, T("Windows' video system couldn't start."));
        m_mfStarted = true;
        IMFAttributes* attr = nullptr;
        MFCreateAttributes(&attr, 2);
        attr->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
        attr->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
        HRESULT hr = MFCreateSinkWriterFromURL(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()), nullptr, attr, &m_writer);
        release(attr);
        if (FAILED(hr)) return fail(error, T("Couldn't create %1.").arg(QDir::toNativeSeparators(path)));
        IMFMediaType* out = nullptr;
        MFCreateMediaType(&out);
        out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        // Fractals are full of fine detail: a generous bit rate (about 0.2 bits per pixel per frame).
        const double bits = double(m_w) * m_h * m_fps * 0.2;
        out->SetUINT32(MF_MT_AVG_BITRATE, UINT32(std::min(bits, 200e6)));
        out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        MFSetAttributeSize(out, MF_MT_FRAME_SIZE, UINT32(m_w), UINT32(m_h));
        MFSetAttributeRatio(out, MF_MT_FRAME_RATE, UINT32(m_fps), 1);
        MFSetAttributeRatio(out, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        hr = m_writer->AddStream(out, &m_stream);
        release(out);
        if (FAILED(hr)) return fail(error, T("The H.264 encoder doesn't accept %1 × %2 at %3 frames per second.")
                                               .arg(m_w).arg(m_h).arg(m_fps));
        IMFMediaType* in = nullptr;
        MFCreateMediaType(&in);
        in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
        in->SetUINT32(MF_MT_DEFAULT_STRIDE, UINT32(m_w * 4));   // positive: rows from the top
        MFSetAttributeSize(in, MF_MT_FRAME_SIZE, UINT32(m_w), UINT32(m_h));
        MFSetAttributeRatio(in, MF_MT_FRAME_RATE, UINT32(m_fps), 1);
        MFSetAttributeRatio(in, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
        hr = m_writer->SetInputMediaType(m_stream, in, nullptr);
        release(in);
        if (FAILED(hr)) return fail(error, T("The H.264 encoder doesn't accept this picture size (%1 × %2). "
                                             "MP4 works up to 4096 × 2304; choose PNG Frames for larger.")
                                               .arg(m_w).arg(m_h));
        if (FAILED(m_writer->BeginWriting())) return fail(error, T("The video couldn't be started."));
        return true;
    }
    bool add(const QImage& frame, QString* error) override
    {
        const QImage img = frame.convertToFormat(QImage::Format_RGB32);
        const DWORD bytes = DWORD(m_w) * DWORD(m_h) * 4;
        IMFMediaBuffer* buf = nullptr;
        if (FAILED(MFCreateMemoryBuffer(bytes, &buf))) return fail(error, T("Out of memory."));
        BYTE* data = nullptr;
        buf->Lock(&data, nullptr, nullptr);
        MFCopyImage(data, LONG(m_w * 4), img.constBits(), LONG(img.bytesPerLine()), DWORD(m_w * 4), DWORD(m_h));
        buf->Unlock();
        buf->SetCurrentLength(bytes);
        IMFSample* s = nullptr;
        MFCreateSample(&s);
        s->AddBuffer(buf);
        const LONGLONG dur = 10'000'000LL / m_fps;
        s->SetSampleTime(m_t);
        s->SetSampleDuration(dur);
        const HRESULT hr = m_writer->WriteSample(m_stream, s);
        release(s);
        release(buf);
        m_t += dur;
        if (FAILED(hr)) return fail(error, T("The video encoder stopped (error 0x%1).").arg(quint32(hr), 8, 16, QLatin1Char('0')));
        return true;
    }
    bool finish(QString* error) override
    {
        if (!m_writer) return false;
        const HRESULT hr = m_writer->Finalize();
        release(m_writer);
        if (FAILED(hr)) return fail(error, T("The video couldn't be finished."));
        return true;
    }

private:
    bool fail(QString* error, const QString& text)
    {
        if (error) *error = text;
        return false;
    }
    int m_w, m_h, m_fps;
    IMFSinkWriter* m_writer = nullptr;
    DWORD m_stream = 0;
    LONGLONG m_t = 0;
    bool m_mfStarted = false, m_comStarted = false;
};
#endif

}  // namespace

QStringList MovieWriter::formatNames()
{
    return {T("MP4 Video"), T("Animated WebP"), T("PNG Frames"), T("Animated GIF")};
}

QString MovieWriter::extension(Format f)
{
    switch (f) {
    case Format::Mp4: return QStringLiteral(".mp4");
    case Format::WebP: return QStringLiteral(".webp");
    case Format::Gif: return QStringLiteral(".gif");
    default: return QString();
    }
}

MovieWriter::Format MovieWriter::fromPath(const QString& path)
{
    for (Format f : {Format::Mp4, Format::WebP, Format::Gif})
        if (path.endsWith(extension(f), Qt::CaseInsensitive)) return f;
    return Format::PngFrames;
}

bool MovieWriter::available(Format f, QString* why)
{
#ifdef Q_OS_WIN
    Q_UNUSED(f);
    Q_UNUSED(why);
    return true;
#else
    if (f == Format::Mp4) {
        if (why) *why = T("MP4 uses Windows' own video encoder. On Linux, choose Animated WebP or PNG Frames.");
        return false;
    }
    return true;
#endif
}

std::unique_ptr<MovieWriter> MovieWriter::create(Format f, const QString& path, int w, int h, int fps, QString* error)
{
    if (w < 16 || h < 16 || fps < 1 || fps > 240) {
        if (error) *error = T("The movie size or frame rate is out of range.");
        return nullptr;
    }
    QString why;
    if (!available(f, &why)) {
        if (error) *error = why;
        return nullptr;
    }
    switch (f) {
    case Format::PngFrames: {
        if (!QDir().mkpath(path)) {
            if (error) *error = T("Couldn't create the folder %1.").arg(QDir::toNativeSeparators(path));
            return nullptr;
        }
        return std::make_unique<PngFrames>(path);
    }
    case Format::WebP: {
        if (w > 16383 || h > 16383) {
            if (error) *error = T("WebP pictures can be at most 16383 pixels on a side.");
            return nullptr;
        }
        auto wr = std::make_unique<WebPWriter>(path, w, h, fps);
        if (!wr->ok()) {
            if (error) *error = T("The WebP encoder couldn't start.");
            return nullptr;
        }
        return wr;
    }
    case Format::Gif: {
        if (w > 65535 || h > 65535) {
            if (error) *error = T("GIF pictures can be at most 65535 pixels on a side.");
            return nullptr;
        }
        auto wr = std::make_unique<GifWriter>(path);
        if (!wr->open(w, h, fps, error)) return nullptr;
        return wr;
    }
    case Format::Mp4: {
#ifdef Q_OS_WIN
        if ((w % 2) || (h % 2)) {
            if (error) *error = T("MP4 video needs an even width and height.");
            return nullptr;
        }
        auto wr = std::make_unique<Mp4Writer>(w, h, fps);
        if (!wr->open(path, error)) return nullptr;
        return wr;
#else
        return nullptr;
#endif
    }
    }
    return nullptr;
}
