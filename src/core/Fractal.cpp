#include "Fractal.h"

#include <QJsonValue>

#include <algorithm>
#include <cmath>

namespace core {

static BigFloat parsed(const QString& text, long bits, double fallback)
{
    BigFloat v(std::max<long>(bits, long(text.size() * 3.33) + 16));
    if (!v.parse(text.toStdString())) v.set(fallback);
    return v;
}

BigFloat FractalParams::seedX(long bits) const { return parsed(juliaRe, bits, -0.8); }
BigFloat FractalParams::seedY(long bits) const { return parsed(juliaIm, bits, 0.156); }

QJsonObject FractalParams::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("type"), type == FractalType::Julia ? QStringLiteral("julia") : QStringLiteral("mandelbrot"));
    o.insert(QStringLiteral("power"), power);
    o.insert(QStringLiteral("julia_re"), juliaRe);
    o.insert(QStringLiteral("julia_im"), juliaIm);
    o.insert(QStringLiteral("auto_iterations"), autoIterations);
    o.insert(QStringLiteral("max_iterations"), maxIterations);
    o.insert(QStringLiteral("escape_radius"), escapeRadius);
    return o;
}

void FractalParams::fromJson(const QJsonObject& o)
{
    const FractalParams d;
    const QString t = o.value(QStringLiteral("type")).toString();
    type = t == QStringLiteral("julia") ? FractalType::Julia : FractalType::Mandelbrot;
    power = std::clamp(o.value(QStringLiteral("power")).toInt(d.power), 2, 8);
    BigFloat probe(64);
    const QString re = o.value(QStringLiteral("julia_re")).toString(d.juliaRe);
    const QString im = o.value(QStringLiteral("julia_im")).toString(d.juliaIm);
    juliaRe = probe.parse(re.toStdString()) && std::fabs(probe.toDouble()) < 16 ? re : d.juliaRe;
    juliaIm = probe.parse(im.toStdString()) && std::fabs(probe.toDouble()) < 16 ? im : d.juliaIm;
    autoIterations = o.value(QStringLiteral("auto_iterations")).toBool(d.autoIterations);
    maxIterations = std::clamp(o.value(QStringLiteral("max_iterations")).toInt(d.maxIterations), kMinIterations, kMaxIterations);
    const double r = o.value(QStringLiteral("escape_radius")).toDouble(d.escapeRadius);
    escapeRadius = std::isfinite(r) ? std::clamp(r, 2.0, 100000.0) : d.escapeRadius;
}

}  // namespace core
