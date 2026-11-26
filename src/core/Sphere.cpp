#include "Sphere.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace core {

namespace {

struct Rgbf { float r, g, b; };

// Bilinear sample at a position in pixels (pixel centers at +0.5), clamped at the edges, like the graphics card's
// linear filtering with clamp-to-edge.
Rgbf sample(const QImage& img, float x, float y)
{
    const int w = img.width(), h = img.height();
    x = std::clamp(x - 0.5f, 0.0f, float(w - 1));
    y = std::clamp(y - 0.5f, 0.0f, float(h - 1));
    const int x0 = int(x), y0 = int(y), x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    const float tx = x - x0, ty = y - y0;
    const QRgb* r0 = reinterpret_cast<const QRgb*>(img.constScanLine(y0));
    const QRgb* r1 = reinterpret_cast<const QRgb*>(img.constScanLine(y1));
    auto lerp = [&](int (*ch)(QRgb)) {
        const float a = ch(r0[x0]) * (1 - tx) + ch(r0[x1]) * tx;
        const float b = ch(r1[x0]) * (1 - tx) + ch(r1[x1]) * tx;
        return (a * (1 - ty) + b * ty) / 255.0f;
    };
    return {lerp(qRed), lerp(qGreen), lerp(qBlue)};
}

}  // namespace

QImage applySphere(const QImage& flatIn, const SphereLook& look)
{
    if (look.size <= 0 || flatIn.isNull()) return flatIn;
    const QImage flat = flatIn.convertToFormat(QImage::Format_RGB32);
    const int w = flat.width(), h = flat.height();
    QImage out(w, h, QImage::Format_RGB32);
    const float cx = w / 2.0f, cy = h / 2.0f;
    const float R = float(look.size) * h / 2.0f;
    const float halfPi = float(std::numbers::pi / 2);
    float lx = float(look.lightX), ly = float(look.lightY), lz = float(look.lightZ);
    const float ll = std::sqrt(lx * lx + ly * ly + lz * lz);
    lx /= ll;
    ly /= ll;
    lz /= ll;
    // The half vector between the light and the viewer (0, 0, 1), for the highlight.
    float hx = lx, hy = ly, hz = lz + 1.0f;
    const float hl = std::sqrt(hx * hx + hy * hy + hz * hz);
    hx /= hl;
    hy /= hl;
    hz /= hl;
    const float bgR = look.background.redF(), bgG = look.background.greenF(), bgB = look.background.blueF();
    const float shade = float(look.shade), shine = float(look.shine);
    for (int py = 0; py < h; ++py) {
        QRgb* row = reinterpret_cast<QRgb*>(out.scanLine(py));
        for (int px = 0; px < w; ++px) {
            const float dx = (px + 0.5f - cx) / R, dyDown = (py + 0.5f - cy) / R;
            const float qx = dx, qy = -dyDown;
            const float r2 = qx * qx + qy * qy;
            // The background, darker in a soft shadow below the ball.
            const float sx = (px + 0.5f - cx) / (R * 0.9f), sy = (py + 0.5f - (cy + R * 1.02f)) / (R * 0.18f);
            const float shadow = 0.55f * std::exp(-(sx * sx + sy * sy));
            float r = bgR * (1 - shadow), g = bgG * (1 - shadow), b = bgB * (1 - shadow);
            const float edge = std::clamp((1.0f - std::sqrt(r2)) * R + 0.5f, 0.0f, 1.0f);
            if (edge > 0) {
                const float qq = std::min(r2, 1.0f);
                const float z = std::sqrt(1.0f - qq);
                const float lon = std::atan2(qx, z), lat = std::asin(std::clamp(qy, -1.0f, 1.0f));
                const Rgbf c = sample(flat, cx + lon / halfPi * (h / 2.0f), cy - lat / halfPi * (h / 2.0f));
                const float diffuse = std::max(qx * lx + qy * ly + z * lz, 0.0f);
                const float k = 1.0f + shade * (0.25f + 0.95f * diffuse - 1.0f);
                const float spec = std::pow(std::max(qx * hx + qy * hy + z * hz, 0.0f), 48.0f) * shine;
                const float br = std::min(c.r * k + spec, 1.0f), bg = std::min(c.g * k + spec, 1.0f), bb = std::min(c.b * k + spec, 1.0f);
                r = r + (br - r) * edge;
                g = g + (bg - g) * edge;
                b = b + (bb - b) * edge;
            }
            row[px] = qRgb(int(std::lround(std::clamp(r, 0.0f, 1.0f) * 255)), int(std::lround(std::clamp(g, 0.0f, 1.0f) * 255)),
                           int(std::lround(std::clamp(b, 0.0f, 1.0f) * 255)));
        }
    }
    return out;
}

}  // namespace core
