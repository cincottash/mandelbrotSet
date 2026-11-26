#include "GifEncoder.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

struct Bin {
    float r, g, b;   // mean color of the pixels in this 5-bit cell
    float n;         // how many
};

// The colors of a picture, grouped into 32 x 32 x 32 cells (every pixel counted, or a regular sample of a large one).
std::vector<Bin> histogram(const QImage& img)
{
    std::vector<double> sum(32768 * 4, 0.0);
    const qint64 pixels = qint64(img.width()) * img.height();
    const int step = pixels > 600000 ? int(std::ceil(std::sqrt(double(pixels) / 600000.0))) : 1;
    for (int y = 0; y < img.height(); y += step) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); x += step) {
            const QRgb c = row[x];
            const int r = qRed(c), g = qGreen(c), b = qBlue(c);
            const int k = ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
            double* s = &sum[size_t(k) * 4];
            s[0] += r;
            s[1] += g;
            s[2] += b;
            s[3] += 1;
        }
    }
    std::vector<Bin> bins;
    for (int k = 0; k < 32768; ++k) {
        const double* s = &sum[size_t(k) * 4];
        if (s[3] > 0) bins.push_back({float(s[0] / s[3]), float(s[1] / s[3]), float(s[2] / s[3]), float(s[3])});
    }
    return bins;
}

float dist2(const Bin& a, float r, float g, float b)
{
    const float dr = a.r - r, dg = a.g - g, db = a.b - b;
    // Green counts most and blue least, roughly as the eye sees them.
    return 2 * dr * dr + 4 * dg * dg + 1.5f * db * db;
}

// Median cut: split the box of colors with the most spread (weighted by pixel count) at its weighted median.
std::vector<GifEncoder::Rgb> medianCut(std::vector<Bin> bins, int count)
{
    struct Box { size_t begin, end; };
    std::vector<Box> boxes{{0, bins.size()}};
    auto spread = [&](const Box& bx, int* axis) {
        float lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0};
        double n = 0;
        for (size_t i = bx.begin; i < bx.end; ++i) {
            const float v[3] = {bins[i].r, bins[i].g, bins[i].b};
            for (int a = 0; a < 3; ++a) {
                lo[a] = std::min(lo[a], v[a]);
                hi[a] = std::max(hi[a], v[a]);
            }
            n += bins[i].n;
        }
        const float w[3] = {1.4f, 2.0f, 1.2f};
        int best = 0;
        for (int a = 1; a < 3; ++a)
            if ((hi[a] - lo[a]) * w[a] > (hi[best] - lo[best]) * w[best]) best = a;
        *axis = best;
        return double(hi[best] - lo[best]) * w[best] * std::sqrt(n);
    };
    while (int(boxes.size()) < count) {
        int pick = -1, axis = 0;
        double bestScore = 0;
        for (int i = 0; i < int(boxes.size()); ++i) {
            if (boxes[size_t(i)].end - boxes[size_t(i)].begin < 2) continue;
            int a = 0;
            const double s = spread(boxes[size_t(i)], &a);
            if (s > bestScore) {
                bestScore = s;
                pick = i;
                axis = a;
            }
        }
        if (pick < 0) break;
        Box bx = boxes[size_t(pick)];
        auto key = [axis](const Bin& b) { return axis == 0 ? b.r : axis == 1 ? b.g : b.b; };
        std::sort(bins.begin() + long(bx.begin), bins.begin() + long(bx.end),
                  [&](const Bin& a, const Bin& b) { return key(a) < key(b); });
        double total = 0;
        for (size_t i = bx.begin; i < bx.end; ++i) total += bins[i].n;
        double acc = 0;
        size_t cut = bx.begin + 1;
        for (size_t i = bx.begin; i < bx.end - 1; ++i) {
            acc += bins[i].n;
            cut = i + 1;
            if (acc >= total / 2) break;
        }
        boxes[size_t(pick)] = {bx.begin, cut};
        boxes.push_back({cut, bx.end});
    }
    std::vector<GifEncoder::Rgb> pal;
    for (const Box& bx : boxes) {
        double r = 0, g = 0, b = 0, n = 0;
        for (size_t i = bx.begin; i < bx.end; ++i) {
            r += bins[i].r * bins[i].n;
            g += bins[i].g * bins[i].n;
            b += bins[i].b * bins[i].n;
            n += bins[i].n;
        }
        if (n > 0) pal.push_back({quint8(std::lround(r / n)), quint8(std::lround(g / n)), quint8(std::lround(b / n))});
    }
    return pal;
}

// The 8 x 8 Bayer matrix, as thresholds from -0.5 to 0.5.
const std::array<float, 64>& bayer()
{
    static const std::array<float, 64> m = [] {
        std::array<float, 64> t{};
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 8; ++x) {
                int v = 0;
                for (int bit = 0; bit < 3; ++bit) {
                    const int xb = (x >> bit) & 1, yb = (y >> bit) & 1;
                    v |= ((xb ^ yb) << (5 - 2 * bit)) | (yb << (4 - 2 * bit));
                }
                t[size_t(y * 8 + x)] = (v + 0.5f) / 64.0f - 0.5f;
            }
        return t;
    }();
    return m;
}

void put16(QByteArray& b, int v)
{
    b.append(char(v & 0xFF));
    b.append(char((v >> 8) & 0xFF));
}

}  // namespace

std::vector<GifEncoder::Rgb> GifEncoder::choosePalette(const QImage& img, const std::vector<Rgb>* start)
{
    const std::vector<Bin> bins = histogram(img);
    if (bins.size() <= 256) {
        std::vector<Rgb> pal;
        for (const Bin& b : bins) pal.push_back({quint8(std::lround(b.r)), quint8(std::lround(b.g)), quint8(std::lround(b.b))});
        if (pal.empty()) pal.push_back({0, 0, 0});
        return pal;
    }
    std::vector<Rgb> init = start && start->size() == 256 ? *start : medianCut(bins, 256);
    std::vector<float> cr(init.size()), cg(init.size()), cb(init.size());
    for (size_t i = 0; i < init.size(); ++i) {
        cr[i] = init[i][0];
        cg[i] = init[i][1];
        cb[i] = init[i][2];
    }
    // A few rounds of k-means over the cells, weighted by their pixel counts.
    const int rounds = start ? 3 : 4;
    std::vector<int> owner(bins.size());
    std::vector<float> err(bins.size());
    for (int round = 0; round < rounds; ++round) {
        std::vector<double> sr(cr.size(), 0), sg(cr.size(), 0), sb(cr.size(), 0), sn(cr.size(), 0);
        for (size_t i = 0; i < bins.size(); ++i) {
            float best = std::numeric_limits<float>::max();
            int bi = 0;
            for (size_t c = 0; c < cr.size(); ++c) {
                const float d = dist2(bins[i], cr[c], cg[c], cb[c]);
                if (d < best) {
                    best = d;
                    bi = int(c);
                }
            }
            owner[i] = bi;
            err[i] = best * bins[i].n;
            sr[size_t(bi)] += double(bins[i].r) * bins[i].n;
            sg[size_t(bi)] += double(bins[i].g) * bins[i].n;
            sb[size_t(bi)] += double(bins[i].b) * bins[i].n;
            sn[size_t(bi)] += bins[i].n;
        }
        for (size_t c = 0; c < cr.size(); ++c) {
            if (sn[c] > 0) {
                cr[c] = float(sr[c] / sn[c]);
                cg[c] = float(sg[c] / sn[c]);
                cb[c] = float(sb[c] / sn[c]);
            } else {
                // An unused color moves to the cell that is drawn worst.
                const size_t worst = size_t(std::max_element(err.begin(), err.end()) - err.begin());
                cr[c] = bins[worst].r;
                cg[c] = bins[worst].g;
                cb[c] = bins[worst].b;
                err[worst] = 0;
            }
        }
    }
    std::vector<Rgb> pal(cr.size());
    for (size_t c = 0; c < cr.size(); ++c)
        pal[c] = {quint8(std::clamp(std::lround(cr[c]), 0L, 255L)), quint8(std::clamp(std::lround(cg[c]), 0L, 255L)),
                  quint8(std::clamp(std::lround(cb[c]), 0L, 255L))};
    return pal;
}

std::vector<quint8> GifEncoder::mapPixels(const QImage& img, const std::vector<Rgb>& pal)
{
    // Nearest palette color for each 6-bit cell, found when first needed.
    std::vector<qint16> cache(262144, -1);
    auto nearest = [&](int r, int g, int b) {
        const int k = ((r >> 2) << 12) | ((g >> 2) << 6) | (b >> 2);
        qint16& c = cache[size_t(k)];
        if (c < 0) {
            const Bin cell{float((r & ~3) + 2), float((g & ~3) + 2), float((b & ~3) + 2), 1};
            float best = std::numeric_limits<float>::max();
            for (size_t i = 0; i < pal.size(); ++i) {
                const float d = dist2(cell, pal[i][0], pal[i][1], pal[i][2]);
                if (d < best) {
                    best = d;
                    c = qint16(i);
                }
            }
        }
        return quint8(c);
    };
    const std::array<float, 64>& t = bayer();
    const float amount = 9.0f;   // about one palette step in smooth gradients
    std::vector<quint8> out(size_t(img.width()) * size_t(img.height()));
    for (int y = 0; y < img.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        quint8* o = out.data() + size_t(y) * size_t(img.width());
        for (int x = 0; x < img.width(); ++x) {
            const float d = t[size_t((y & 7) * 8 + (x & 7))] * amount;
            const QRgb c = row[x];
            const int r = std::clamp(int(std::lround(qRed(c) + d)), 0, 255);
            const int g = std::clamp(int(std::lround(qGreen(c) + d)), 0, 255);
            const int b = std::clamp(int(std::lround(qBlue(c) + d)), 0, 255);
            o[x] = nearest(r, g, b);
        }
    }
    return out;
}

QByteArray GifEncoder::lzw(const std::vector<quint8>& idx, int minCodeSize)
{
    QByteArray data;
    data.reserve(int(idx.size() / 2));
    quint32 bitBuf = 0;
    int bitCount = 0;
    auto write = [&](int code, int size) {
        bitBuf |= quint32(code) << bitCount;
        bitCount += size;
        while (bitCount >= 8) {
            data.append(char(bitBuf & 0xFF));
            bitBuf >>= 8;
            bitCount -= 8;
        }
    };
    const int clear = 1 << minCodeSize, eoi = clear + 1;
    // The dictionary: (prefix code, next index) -> code, stamped with a generation so a clear costs nothing.
    std::vector<quint32> table(size_t(4096) * 256, 0);
    quint32 gen = 1;
    int codeSize = minCodeSize + 1;
    int maxCode = eoi;
    write(clear, codeSize);
    int cur = -1;
    for (quint8 v : idx) {
        if (cur < 0) {
            cur = v;
            continue;
        }
        const size_t key = size_t(cur) * 256 + v;
        const quint32 e = table[key];
        if ((e >> 12) == gen) {
            cur = int(e & 0xFFF);
            continue;
        }
        write(cur, codeSize);
        ++maxCode;
        table[key] = (gen << 12) | quint32(maxCode);
        if (maxCode >= (1 << codeSize)) ++codeSize;
        if (maxCode == 4095) {
            write(clear, codeSize);
            ++gen;
            codeSize = minCodeSize + 1;
            maxCode = eoi;
        }
        cur = v;
    }
    if (cur >= 0) write(cur, codeSize);
    write(clear, codeSize);
    write(eoi, minCodeSize + 1);
    if (bitCount > 0) data.append(char(bitBuf & 0xFF));
    // Sub-blocks of at most 255 bytes, then the empty block.
    QByteArray out;
    out.reserve(data.size() + data.size() / 255 + 2);
    for (qsizetype i = 0; i < data.size(); i += 255) {
        const qsizetype n = std::min<qsizetype>(255, data.size() - i);
        out.append(char(n));
        out.append(data.constData() + i, n);
    }
    out.append(char(0));
    return out;
}

bool GifEncoder::begin(QIODevice* out, int w, int h, int fps)
{
    if (!out || w < 1 || h < 1 || w > 65535 || h > 65535 || fps < 1) return false;
    m_out = out;
    m_w = w;
    m_h = h;
    m_keepEvery = 1;
    while (fps / m_keepEvery > 50) ++m_keepEvery;
    m_fps = fps / m_keepEvery;
    m_seen = 0;
    m_frames = 0;
    m_palette.clear();
    QByteArray head("GIF89a");
    put16(head, w);
    put16(head, h);
    head.append(char(0x70));   // no global colors; 8 bits per channel
    head.append(char(0));
    head.append(char(0));
    // Loop forever.
    head.append("\x21\xFF\x0B" "NETSCAPE2.0" "\x03\x01\x00\x00\x00", 19);
    return m_out->write(head) == head.size();
}

bool GifEncoder::add(const QImage& frame)
{
    if (!m_out) return false;
    if (m_seen++ % m_keepEvery != 0) return true;
    const QImage img = frame.convertToFormat(QImage::Format_RGB32);
    if (img.width() != m_w || img.height() != m_h) return false;
    m_palette = choosePalette(img, m_palette.empty() ? nullptr : &m_palette);
    const std::vector<quint8> idx = mapPixels(img, m_palette);
    // Frame times in hundredths of a second, rounded so they add up exactly.
    const int t0 = int(std::lround(m_frames * 100.0 / m_fps)), t1 = int(std::lround((m_frames + 1) * 100.0 / m_fps));
    QByteArray b;
    b.append("\x21\xF9\x04\x04", 4);   // graphic control: keep the frame (no transparency)
    put16(b, std::max(2, t1 - t0));
    b.append(char(0));
    b.append(char(0));
    b.append(char(0x2C));
    put16(b, 0);
    put16(b, 0);
    put16(b, m_w);
    put16(b, m_h);
    b.append(char(0x87));   // a local color table of 256 entries
    for (int i = 0; i < 256; ++i) {
        const Rgb c = i < int(m_palette.size()) ? m_palette[size_t(i)] : Rgb{0, 0, 0};
        b.append(char(c[0]));
        b.append(char(c[1]));
        b.append(char(c[2]));
    }
    b.append(char(8));
    b.append(lzw(idx, 8));
    ++m_frames;
    return m_out->write(b) == b.size();
}

bool GifEncoder::finish()
{
    if (!m_out) return false;
    const bool ok = m_out->write("\x3B", 1) == 1;
    m_out = nullptr;
    return ok;
}
