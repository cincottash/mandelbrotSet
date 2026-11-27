#include "RandomScene.h"

#include "Palette.h"

#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

namespace core {

namespace {

double uniform(QRandomGenerator& r, double a, double b) { return a + (b - a) * r.generateDouble(); }
double logUniform(QRandomGenerator& r, double a, double b) { return std::exp(uniform(r, std::log(a), std::log(b))); }
bool chance(QRandomGenerator& r, double p) { return r.generateDouble() < p; }

QColor randomColor(QRandomGenerator& r, int minValue = 0, int maxValue = 255)
{
    return QColor::fromHsv(r.bounded(360), r.bounded(40, 256), r.bounded(minValue, maxValue + 1));
}

// One step of the formula, in double precision.
std::complex<double> step(std::complex<double> z, std::complex<double> c, const FractalParams& f)
{
    if (f.formula == Formula::BurningShip)
        return {z.real() * z.real() - z.imag() * z.imag() + c.real(), 2 * std::abs(z.real() * z.imag()) - c.imag()};
    std::complex<double> w = z;
    for (int k = 1; k < f.power; ++k) w *= z;
    if (f.formula == Formula::Tricorn) w = std::conj(w);
    return w + c;
}

// Escape time of one point in double precision (good to about 1e-12): -1 when it stays inside.
int escapeTime(std::complex<double> z, std::complex<double> c, const FractalParams& f, int maxIt)
{
    for (int n = 0; n < maxIt; ++n) {
        if (std::norm(z) > 64.0) return n;
        z = step(z, c, f);
    }
    return -1;
}

// The smooth escape time (mu) of one point: -1 when it stays inside.
double smoothEscape(std::complex<double> z, std::complex<double> c, const FractalParams& f, int maxIt)
{
    for (int n = 0; n < maxIt; ++n) {
        const double r2 = std::norm(z);
        if (r2 > 1e6) return n + 1 - std::log(std::log(r2) / (2 * std::log(1e3))) / std::log(double(f.power));
        z = step(z, c, f);
    }
    return -1;
}

struct Sampled {
    double escapedShare;   // share of samples that escape
    double spread;         // how different the escape times are (a flat view has none)
};

Sampled sample(const FractalParams& f, std::complex<double> center, double scale, std::complex<double> seed,
            QRandomGenerator& r, int count, int maxIt, std::vector<std::pair<std::complex<double>, int>>* points)
{
    int escaped = 0;
    double sum = 0, sum2 = 0;
    for (int i = 0; i < count; ++i) {
        const std::complex<double> p = center + std::complex<double>(uniform(r, -1.6, 1.6), uniform(r, -1.0, 1.0)) * scale;
        const int n = f.julia() ? escapeTime(p, seed, f, maxIt) : escapeTime(0.0, p, f, maxIt);
        if (points) points->push_back({p, n});
        if (n >= 0) {
            ++escaped;
            const double l = std::log(1.0 + n);
            sum += l;
            sum2 += l * l;
        }
    }
    Sampled look;
    look.escapedShare = double(escaped) / count;
    const double mean = escaped ? sum / escaped : 0;
    look.spread = escaped > 1 ? std::sqrt(std::max(0.0, sum2 / escaped - mean * mean)) : 0;
    return look;
}

// Dive from the whole set toward its edge: in each step, zoom into one of the slowest escaping points.
bool dive(const FractalParams& f, std::complex<double> seed, int steps, QRandomGenerator& r, std::complex<double>* center,
          double* scale)
{
    const View home = f.home();
    std::complex<double> c(home.cx.toDouble(), home.cy.toDouble());
    double s = 1.25;
    for (int step = 0; step < steps; ++step) {
        std::vector<std::pair<std::complex<double>, int>> pts;
        const int maxIt = std::min(6000, 300 + 150 * (step + 1) * (step + 1));
        sample(f, c, s, seed, r, 500, maxIt, &pts);
        // The escaping points that took longest are next to the edge: pick one of the slowest tenth.
        std::vector<std::pair<std::complex<double>, int>> esc;
        for (const auto& p : pts)
            if (p.second >= 0) esc.push_back(p);
        if (esc.size() < 10) return false;
        std::sort(esc.begin(), esc.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        const size_t top = std::max<size_t>(1, esc.size() / 10);
        c = esc[size_t(r.bounded(int(top)))].first;
        s /= logUniform(r, 3.0, 20.0);
        if (s < 2e-11) break;   // double precision ends here
    }
    *center = c;
    *scale = s;
    return true;
}

// How much of a view has visible structure: the share of neighbors on a 32 x 20 grid whose escape times differ
// clearly (or where one is inside and the other not). A view with a speck of detail in a flat field scores low.
double busyShare(const FractalParams& f, std::complex<double> center, double scale, double angle, std::complex<double> seed,
                 double* flatShare = nullptr)
{
    const int gw = 32, gh = 20, maxIt = 4000;
    const std::complex<double> rot = std::polar(1.0, angle);
    std::vector<double> v(size_t(gw * gh));
    for (int y = 0; y < gh; ++y)
        for (int x = 0; x < gw; ++x) {
            const std::complex<double> uv((x + 0.5) / gw * 3.2 - 1.6, (y + 0.5) / gh * 2.0 - 1.0);
            const std::complex<double> p = center + uv * rot * scale;
            const int n = f.julia() ? escapeTime(p, seed, f, maxIt) : escapeTime(0.0, p, f, maxIt);
            v[size_t(y * gw + x)] = n < 0 ? -1.0 : std::log(1.0 + n);
        }
    int busy = 0, pairs = 0, flat = 0;
    auto differ = [](double a, double b) { return (a < 0) != (b < 0) || (a >= 0 && std::abs(a - b) > 0.12); };
    for (int y = 0; y < gh; ++y)
        for (int x = 0; x < gw; ++x) {
            const double a = v[size_t(y * gw + x)];
            if (x + 1 < gw) { ++pairs; busy += differ(a, v[size_t(y * gw + x + 1)]); }
            if (y + 1 < gh) { ++pairs; busy += differ(a, v[size_t((y + 1) * gw + x)]); }
            // A cell is flat when none of its neighbors differs from it.
            bool any = false;
            for (const auto& [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
                const int nx = x + dx, ny = y + dy;
                if (nx >= 0 && ny >= 0 && nx < gw && ny < gh) any = any || differ(a, v[size_t(ny * gw + nx)]);
            }
            flat += !any;
        }
    if (flatShare) *flatShare = double(flat) / (gw * gh);
    return double(busy) / pairs;
}

// How fast the escape time changes from one pixel to the next of a 1080-line picture: the medians of mu and of
// |mu change| over escaping neighbors.
void muPerPixel(const FractalParams& f, std::complex<double> center, double scale, std::complex<double> seed,
                QRandomGenerator& r, double* mu, double* step, std::vector<double>* quantiles)
{
    const double px = 2.0 * scale / 1080.0;
    std::vector<double> mus, steps;
    for (int i = 0; i < 400; ++i) {
        const std::complex<double> p = center + std::complex<double>(uniform(r, -1.6, 1.6), uniform(r, -1.0, 1.0)) * scale;
        const std::complex<double> q = p + std::polar(px, uniform(r, 0.0, 2 * std::numbers::pi));
        const double a = f.julia() ? smoothEscape(p, seed, f, 6000) : smoothEscape(0.0, p, f, 6000);
        const double b = f.julia() ? smoothEscape(q, seed, f, 6000) : smoothEscape(0.0, q, f, 6000);
        if (a < 0 || b < 0) continue;
        mus.push_back(a);
        steps.push_back(std::abs(a - b));
    }
    auto median = [](std::vector<double>& v) {
        if (v.empty()) return 0.0;
        std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
        return v[v.size() / 2];
    };
    *mu = median(mus);
    *step = median(steps);
    quantiles->clear();
    if (!mus.empty()) {
        std::sort(mus.begin(), mus.end());
        for (int i = 1; i < 10; ++i) quantiles->push_back(mus[mus.size() * i / 10]);
    }
}

// The palette position of an escape time, as colorize.frag computes it for the smooth gradient.
double gradientPosition(const ColorParams& c, double mu)
{
    double s = mu / 24.0;
    if (c.colorScale == 1) s = std::sqrt(std::max(mu, 0.0)) * 0.6;
    else if (c.colorScale == 2) s = std::log2(std::max(mu, 0.0) + 1.0) * 0.5;
    return s * c.density + c.offset;
}

// For the smooth gradient: the offset that puts the common escape times on bright, varied parts of the palette
// (with a low density they cover only a short stretch of it, which can be all dark).
void chooseOffset(ColorParams& c, const std::vector<double>& quantiles, QRandomGenerator& rnd)
{
    if (c.style != int(ColorStyle::Gradient) || quantiles.empty()) return;
    const Palette& pal = paletteByName(c.palette);
    double bestScore = -1, bestOffset = c.offset;
    const double start = rnd.generateDouble();
    for (int k = 0; k < 24; ++k) {
        ColorParams t = c;
        t.offset = std::fmod(start + k / 24.0, 1.0);
        double sum = 0, sum2 = 0;
        for (double q : quantiles) {
            double p = std::fmod(gradientPosition(t, q), 1.0);
            if (p < 0) p += 1.0;
            const QColor col = pal.at(p);
            const double l = 0.2126 * col.redF() + 0.7152 * col.greenF() + 0.0722 * col.blueF();
            sum += l;
            sum2 += l * l;
        }
        const double n = double(quantiles.size());
        const double mean = sum / n, sd = std::sqrt(std::max(0.0, sum2 / n - mean * mean));
        const double score = std::min(mean, 0.55) + 1.5 * sd;
        if (score > bestScore) {
            bestScore = score;
            bestOffset = t.offset;
        }
    }
    c.offset = bestOffset;
}

// Colors that change by at most a small part of a cycle from one pixel to the next, so the picture shows bands
// and shapes rather than static: lowers the density first, then changes to the log scale, then to a calmer style.
void tameColors(ColorParams& c, double mu, double step)
{
    const double limit = 0.12;
    auto cyclesPerPixel = [&] {
        const double d = c.density;
        switch (ColorStyle(c.style)) {
        case ColorStyle::Distance:
        case ColorStyle::OrbitTrap:
        case ColorStyle::Moire: return 0.0;                     // follow the distance, the trap or the pixel grid
        case ColorStyle::Angle: return step * d;                // the final angle turns once per unit of mu
        case ColorStyle::Stripes: return step * d * 0.5;
        default: break;
        }
        if (c.colorScale == 1) return 0.3 / std::sqrt(std::max(mu, 1.0)) * step * d;
        if (c.colorScale == 2) return 0.5 / std::log(2.0) / (mu + 1) * step * d;
        return step * d / 24.0;
    };
    for (int round = 0; round < 3 && cyclesPerPixel() > limit; ++round) {
        c.density = std::max(0.25, c.density * limit / cyclesPerPixel());
        if (cyclesPerPixel() <= limit) break;
        if (c.colorScale != 2) {
            c.colorScale = 2;
            c.density = std::max(c.density, 0.6);
        } else if (c.style == int(ColorStyle::Angle) || c.style == int(ColorStyle::Stripes)) {
            c.style = int(ColorStyle::Gradient);
        }
    }
}

}  // namespace

ColorParams randomColors(QRandomGenerator& rnd, const ColorParams& base)
{
    ColorParams c = base;
    const QList<int> styles = {0, 0, 0, 1, 1, 2, 3, 4, 4, 5, 6, 6};   // gradients and sine waves a little more often
    c.trapShape = rnd.bounded(3);
    c.trapX = chance(rnd, 0.5) ? 0.0 : uniform(rnd, -1.0, 1.0);
    c.trapY = chance(rnd, 0.5) ? 0.0 : uniform(rnd, -1.0, 1.0);
    c.trapSize = logUniform(rnd, 0.2, 2.0);
    c.style = styles[rnd.bounded(int(styles.size()))];
    c.palette = palettes()[rnd.bounded(int(palettes().size()))].name;
    c.colorScale = chance(rnd, 0.7) ? 0 : rnd.bounded(1, 3);
    c.density = logUniform(rnd, 0.25, 4.0);
    c.offset = rnd.generateDouble();
    c.cycleSpeed = 0;
    c.sineFreqR = uniform(rnd, 0.2, 3.0);
    c.sineFreqG = uniform(rnd, 0.2, 3.0);
    c.sineFreqB = uniform(rnd, 0.2, 3.0);
    c.sinePhaseR = rnd.generateDouble();
    c.sinePhaseG = rnd.generateDouble();
    c.sinePhaseB = rnd.generateDouble();
    c.sineBrightness = uniform(rnd, 0.35, 0.65);
    c.sineContrast = uniform(rnd, 0.3, 0.5);
    c.moireScale = logUniform(rnd, 0.3, 2.5);
    c.moireTint = randomColor(rnd, 180, 255);
    c.lightStrength = chance(rnd, 0.65) ? uniform(rnd, 0.25, 1.0) : 0.0;
    c.lightAngle = rnd.bounded(360);
    c.lightHeight = uniform(rnd, 15, 70);
    c.relief = logUniform(rnd, 0.4, 3.0);
    c.specular = uniform(rnd, 0.0, 0.7);
    c.edgeGlow = chance(rnd, 0.3) ? uniform(rnd, 0.3, 1.0) : 0.0;
    c.edgeWidth = logUniform(rnd, 0.6, 6.0);
    c.edgeColor = randomColor(rnd, 200, 255);
    c.stripeDensity = uniform(rnd, 2.0, 12.0);
    c.interiorStyle = rnd.bounded(3);
    c.interiorColor = randomColor(rnd, 0, 60);
    c.interiorColor2 = randomColor(rnd, 120, 255);
    c.hue = 0;
    c.saturation = uniform(rnd, 0.85, 1.25);
    c.brightness = 0;
    c.contrast = uniform(rnd, 0.95, 1.2);
    c.gamma = 1.0;
    c.vignette = chance(rnd, 0.3) ? uniform(rnd, 0.2, 0.6) : 0.0;
    c.grain = chance(rnd, 0.2) ? uniform(rnd, 0.05, 0.25) : 0.0;
    c.bloom = chance(rnd, 0.3) ? uniform(rnd, 0.3, 1.2) : 0.0;
    c.bloomSize = logUniform(rnd, 1.0, 6.0);
    c.bloomThreshold = uniform(rnd, 0.45, 0.8);
    c.sphereSize = chance(rnd, 0.1) ? uniform(rnd, 0.6, 0.9) : 0.0;
    c.sphereShade = uniform(rnd, 0.4, 0.8);
    c.sphereShine = uniform(rnd, 0.2, 0.8);
    c.sphereBackground = randomColor(rnd, 5, 30);
    // The Buddhabrot's own coloring (only used there).
    c.buddhaNebula = chance(rnd, 0.5) ? 1 : 0;
    c.buddhaExposure = logUniform(rnd, 0.7, 2.5);
    c.buddhaGamma = uniform(rnd, 0.35, 0.8);
    return c;
}

ColorParams randomColorsFor(QRandomGenerator& rnd, const ColorParams& base, const FractalParams& f, const View& v)
{
    ColorParams c = randomColors(rnd, base);
    const double scale = v.scale.toDouble();
    if (!(scale > 1e-12) || !std::isfinite(scale)) return c;
    const std::complex<double> seed(f.juliaRe.toDouble(), f.juliaIm.toDouble());
    double mu = 0, step = 0;
    std::vector<double> quantiles;
    muPerPixel(f, {v.cx.toDouble(), v.cy.toDouble()}, scale, seed, rnd, &mu, &step, &quantiles);
    tameColors(c, mu, step);
    chooseOffset(c, quantiles, rnd);
    return c;
}

Scene randomScene(QRandomGenerator& rnd)
{
    Scene best;
    double bestBusy = -1;
    for (int attempt = 0; attempt < 16; ++attempt) {
        Scene s;
        const double kind = rnd.generateDouble();
        if (kind < 0.4) {
            s.fractal.type = FractalType::Mandelbrot;
        } else if (kind < 0.68) {
            s.fractal.type = FractalType::Julia;
            if (chance(rnd, 0.25)) s.fractal.formula = chance(rnd, 0.5) ? Formula::BurningShip : Formula::Tricorn;
        } else if (kind < 0.8) {
            s.fractal.type = FractalType::Mandelbrot;
            s.fractal.power = rnd.bounded(3, 7);
        } else if (kind < 0.9) {
            s.fractal.formula = Formula::BurningShip;
        } else {
            s.fractal.formula = Formula::Tricorn;
            if (chance(rnd, 0.3)) s.fractal.power = rnd.bounded(3, 6);
        }
        s.fractal.escapeRadius = chance(rnd, 0.12) ? uniform(rnd, 2.0, 6.0) : 256.0;
        std::complex<double> seed = 0;
        if (s.fractal.julia()) {
            // Seeds near the edge of the formula's Mandelbrot-like set give the most intricate Julia sets.
            FractalParams m;
            m.formula = s.fractal.formula;
            m.power = s.fractal.power;
            std::complex<double> c;
            double sc = 1.25;
            if (!dive(m, 0.0, rnd.bounded(1, 4), rnd, &c, &sc)) continue;
            seed = c;
            s.fractal.juliaRe = QString::number(c.real(), 'g', 12);
            s.fractal.juliaIm = QString::number(c.imag(), 'g', 12);
        }
        std::complex<double> center;
        double scale = 1.25;
        const int steps = s.fractal.julia() ? rnd.bounded(0, 5) : rnd.bounded(1, 10);
        if (!dive(s.fractal, seed, steps, rnd, &center, &scale)) continue;
        const double degrees = chance(rnd, 0.6) ? uniform(rnd, 0.0, 360.0) : 0.0;
        // Skip a spot that would show one flat color, or a speck of detail in a flat field.
        const Sampled look = sample(s.fractal, center, scale, seed, rnd, 300, 4000, nullptr);
        double flat = 1.0;
        double busy = look.escapedShare > 0.1
            ? busyShare(s.fractal, center, scale, -degrees * std::numbers::pi / 180.0, seed, &flat) : 0.0;
        if (flat > 0.55) busy *= 0.5;   // detail in a corner of a mostly flat view counts for less
        QJsonObject o;
        o.insert(QStringLiteral("re"), QString::number(center.real(), 'g', 17));
        o.insert(QStringLiteral("im"), QString::number(center.imag(), 'g', 17));
        o.insert(QStringLiteral("scale"), QString::number(scale, 'g', 17));
        o.insert(QStringLiteral("angle_degrees"), degrees);
        QString err;
        if (!s.view.fromJson(o, &err)) continue;
        if (busy > bestBusy) {
            best = s;
            bestBusy = busy;
        }
        if (busy > 0.3) {
            best = s;
            break;
        }
    }
    best.colors = randomColorsFor(rnd, ColorParams(), best.fractal, best.view);
    return best;
}

}  // namespace core
