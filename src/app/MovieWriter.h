// Writes the frames of a zoom movie: MP4 video (Windows' own H.264 encoder), animated WebP, or numbered PNG
// pictures in a folder.
#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

#include <memory>

class MovieWriter {
public:
    enum class Format { Mp4 = 0, WebP = 1, PngFrames = 2 };

    virtual ~MovieWriter() = default;
    // `path`: the file (MP4, WebP) or the folder (PNG frames).
    static std::unique_ptr<MovieWriter> create(Format f, const QString& path, int w, int h, int fps, QString* error);
    static QStringList formatNames();             // index = Format
    static bool available(Format f, QString* why);
    static QString extension(Format f);           // ".mp4", ".webp", "" (a folder)

    virtual bool add(const QImage& frame, QString* error) = 0;   // RGB32, the size given to create()
    virtual bool finish(QString* error) = 0;                     // writes what's left; the file is complete after
};
