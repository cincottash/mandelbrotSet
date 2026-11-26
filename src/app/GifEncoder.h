// Animated GIF, written from scratch (no library): 256 colors per frame, chosen for that frame and carried over
// from the frame before so that colors don't flicker, with an ordered dither that stays still from frame to frame.
#pragma once

#include <QByteArray>
#include <QImage>
#include <QIODevice>

#include <array>
#include <vector>

class GifEncoder {
public:
    // Writes the header and the "loop forever" block. `fps` above 50 is halved until it fits: GIF players slow down
    // frames shorter than 2/100 of a second.
    bool begin(QIODevice* out, int w, int h, int fps);
    bool add(const QImage& frame);   // any format; converted to RGB32
    bool finish();                   // the trailer
    int keptFps() const { return m_fps; }
    int framesWritten() const { return m_frames; }

    // The parts, for tests.
    using Rgb = std::array<quint8, 3>;
    // A palette of up to 256 colors for a picture: median cut, then a few rounds of k-means (starting from `start`
    // when given, so that the next frame's palette stays close to this one).
    static std::vector<Rgb> choosePalette(const QImage& rgb32, const std::vector<Rgb>* start);
    // Palette indexes for every pixel, with an 8 x 8 ordered dither.
    static std::vector<quint8> mapPixels(const QImage& rgb32, const std::vector<Rgb>& palette);
    // GIF's LZW: the bytes of the compressed stream, split into sub-blocks, ending with the zero block.
    static QByteArray lzw(const std::vector<quint8>& indexes, int minCodeSize);

private:
    QIODevice* m_out = nullptr;
    int m_w = 0, m_h = 0, m_fps = 30, m_keepEvery = 1;
    qint64 m_seen = 0;
    int m_frames = 0;
    std::vector<Rgb> m_palette;
};
