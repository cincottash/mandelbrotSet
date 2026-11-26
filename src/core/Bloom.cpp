#include "Bloom.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace core {

BloomPlan bloomPlan(int w, int h, double sizePercent)
{
    BloomPlan p;
    const double sigmaFull = std::max(0.5, sizePercent / 100.0 * h);
    p.factor = std::clamp(int(std::ceil(sigmaFull / 4.0)), 1, 64);
    p.smallW = std::max(1, (w + p.factor - 1) / p.factor);
    p.smallH = std::max(1, (h + p.factor - 1) / p.factor);
    p.sigma = sigmaFull / p.factor;
    p.radius = std::max(1, int(std::ceil(3.0 * p.sigma)));
    return p;
}

double bloomWeight(double luma, double threshold)
{
    // 0 below the threshold, 1 a little above it, smooth between.
    const double a = threshold - 0.1, b = threshold + 0.15;
    const double t = std::clamp((luma - a) / (b - a), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

void applyBloom(QImage& img, double strength, double sizePercent, double threshold)
{
    if (strength <= 0 || img.isNull()) return;
    if (img.format() != QImage::Format_RGB32 && img.format() != QImage::Format_ARGB32)
        img = img.convertToFormat(QImage::Format_RGB32);
    const int w = img.width(), h = img.height();
    const BloomPlan p = bloomPlan(w, h, sizePercent);
    const int sw = p.smallW, sh = p.smallH, f = p.factor;
    // 1 and 2: bright pass, then box average into the small picture.
    std::vector<float> small(size_t(sw) * size_t(sh) * 3, 0.0f);
    for (int sy = 0; sy < sh; ++sy) {
        for (int sx = 0; sx < sw; ++sx) {
            double r = 0, g = 0, b = 0;
            int n = 0;
            for (int y = sy * f; y < std::min(h, (sy + 1) * f); ++y) {
                const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
                for (int x = sx * f; x < std::min(w, (sx + 1) * f); ++x) {
                    const double cr = qRed(row[x]) / 255.0, cg = qGreen(row[x]) / 255.0, cb = qBlue(row[x]) / 255.0;
                    const double k = bloomWeight(0.299 * cr + 0.587 * cg + 0.114 * cb, threshold);
                    r += cr * k;
                    g += cg * k;
                    b += cb * k;
                    ++n;
                }
            }
            float* o = &small[(size_t(sy) * sw + sx) * 3];
            o[0] = float(r / n);
            o[1] = float(g / n);
            o[2] = float(b / n);
        }
    }
    // 3: a separable Gaussian, clamped at the edges.
    std::vector<float> kernel(size_t(2 * p.radius + 1));
    double ksum = 0;
    for (int i = -p.radius; i <= p.radius; ++i) ksum += kernel[size_t(i + p.radius)] = float(std::exp(-0.5 * i * i / (p.sigma * p.sigma)));
    for (float& k : kernel) k = float(k / ksum);
    std::vector<float> tmp(small.size());
    for (int y = 0; y < sh; ++y)
        for (int x = 0; x < sw; ++x) {
            float acc[3] = {0, 0, 0};
            for (int i = -p.radius; i <= p.radius; ++i) {
                const int xx = std::clamp(x + i, 0, sw - 1);
                const float* s = &small[(size_t(y) * sw + xx) * 3];
                const float k = kernel[size_t(i + p.radius)];
                acc[0] += s[0] * k;
                acc[1] += s[1] * k;
                acc[2] += s[2] * k;
            }
            float* o = &tmp[(size_t(y) * sw + x) * 3];
            o[0] = acc[0];
            o[1] = acc[1];
            o[2] = acc[2];
        }
    for (int y = 0; y < sh; ++y)
        for (int x = 0; x < sw; ++x) {
            float acc[3] = {0, 0, 0};
            for (int i = -p.radius; i <= p.radius; ++i) {
                const int yy = std::clamp(y + i, 0, sh - 1);
                const float* s = &tmp[(size_t(yy) * sw + x) * 3];
                const float k = kernel[size_t(i + p.radius)];
                acc[0] += s[0] * k;
                acc[1] += s[1] * k;
                acc[2] += s[2] * k;
            }
            float* o = &small[(size_t(y) * sw + x) * 3];
            o[0] = acc[0];
            o[1] = acc[1];
            o[2] = acc[2];
        }
    // 4: enlarge (bilinear, between the small pixels' centers) and screen-blend.
    const float s = float(strength);
    for (int y = 0; y < h; ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
        const float fy = std::clamp((y + 0.5f) / f - 0.5f, 0.0f, float(sh - 1));
        const int y0 = int(fy), y1 = std::min(y0 + 1, sh - 1);
        const float ty = fy - y0;
        for (int x = 0; x < w; ++x) {
            const float fx = std::clamp((x + 0.5f) / f - 0.5f, 0.0f, float(sw - 1));
            const int x0 = int(fx), x1 = std::min(x0 + 1, sw - 1);
            const float tx = fx - x0;
            const float* a = &small[(size_t(y0) * sw + x0) * 3];
            const float* b = &small[(size_t(y0) * sw + x1) * 3];
            const float* c = &small[(size_t(y1) * sw + x0) * 3];
            const float* d = &small[(size_t(y1) * sw + x1) * 3];
            int out[3];
            const int src[3] = {qRed(row[x]), qGreen(row[x]), qBlue(row[x])};
            for (int ch = 0; ch < 3; ++ch) {
                const float v = (a[ch] * (1 - tx) + b[ch] * tx) * (1 - ty) + (c[ch] * (1 - tx) + d[ch] * tx) * ty;
                const float glow = std::min(1.0f, v * s);
                const float base = src[ch] / 255.0f;
                out[ch] = int(std::lround((1.0f - (1.0f - base) * (1.0f - glow)) * 255.0f));
            }
            row[x] = qRgb(out[0], out[1], out[2]);
        }
    }
}

}  // namespace core
