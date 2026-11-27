#include "Fractal.h"
#include "Tr.h"

#include <QJsonArray>
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

QStringList formulaNames()
{
    return {T("Classic"), T("Burning Ship"), T("Tricorn")};   // only shown (files keep a key)
}

static const char* const kFormulaKeys[] = {"classic", "burning_ship", "tricorn"};

View FractalParams::home() const
{
    View v = View::home(julia());
    if (buddhabrot() && formula == Formula::Classic) {
        // The classic Buddhabrot sits upright: the set turned a quarter turn, its antenna up as the head.
        v.cx = BigFloat(-0.4, 128);
        v.cy = BigFloat(0.0, 128);
        v.scale = FloatExp(1.45);
        v.angle = std::acos(0.0);
    } else if (!julia() && formula == Formula::BurningShip) {
        v.cx = BigFloat(-0.45, 128);
        v.cy = BigFloat(0.5, 128);
    } else if (!julia() && formula == Formula::Tricorn) {
        v.cx = BigFloat(-0.2, 128);
        v.cy = BigFloat(0.0, 128);
    }
    return v;
}

QString FractalParams::name() const
{
    // Whole phrases, so the article and the word order can follow the language
    const bool p2 = power == 2;
    QString kind;   // without an article, for the Julia set's "(...)"
    QString with;   // with one, as in "Now the Burning Ship."
    switch (formula) {
    case Formula::BurningShip:
        kind = p2 ? T("Burning Ship") : T("power %1 Burning Ship").arg(power);
        with = p2 ? T("the Burning Ship") : T("the power %1 Burning Ship").arg(power);
        break;
    case Formula::Tricorn:
        kind = p2 ? T("Tricorn") : T("power %1 Tricorn").arg(power);
        with = p2 ? T("the Tricorn") : T("the power %1 Tricorn").arg(power);
        break;
    default:
        kind = p2 ? T("Mandelbrot set") : T("power %1 Mandelbrot set").arg(power);
        with = p2 ? T("the Mandelbrot set") : T("the power %1 Mandelbrot set").arg(power);
        break;
    }
    if (!julia()) return with;
    const bool neg = juliaIm.startsWith(QLatin1Char('-'));
    return T("the Julia set of %1 %2 %3i (%4)")
        .arg(juliaRe.left(10), neg ? QStringLiteral("−") : QStringLiteral("+"), neg ? juliaIm.mid(1, 9) : juliaIm.left(9), kind);
}

QJsonObject FractalParams::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("type"), type == FractalType::Julia        ? QStringLiteral("julia")
                                     : type == FractalType::Buddhabrot ? QStringLiteral("buddhabrot")
                                                                       : QStringLiteral("mandelbrot"));
    o.insert(QStringLiteral("buddha_limits"), QJsonArray{buddhaRed, buddhaGreen, buddhaBlue});
    o.insert(QStringLiteral("buddha_min"), buddhaMin);
    o.insert(QStringLiteral("formula"), QLatin1String(kFormulaKeys[int(formula)]));
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
    type = t == QStringLiteral("julia")        ? FractalType::Julia
           : t == QStringLiteral("buddhabrot") ? FractalType::Buddhabrot
                                               : FractalType::Mandelbrot;
    const QJsonArray lim = o.value(QStringLiteral("buddha_limits")).toArray();
    auto limit = [&](int i, int def) { return std::clamp(lim.at(i).toInt(def), 2, 200000); };
    buddhaRed = limit(0, d.buddhaRed);
    buddhaGreen = limit(1, d.buddhaGreen);
    buddhaBlue = limit(2, d.buddhaBlue);
    buddhaMin = std::clamp(o.value(QStringLiteral("buddha_min")).toInt(d.buddhaMin), 0, 10000);
    const QString fk = o.value(QStringLiteral("formula")).toString();
    formula = d.formula;
    for (int i = 0; i < 3; ++i)
        if (fk == QLatin1String(kFormulaKeys[i])) formula = Formula(i);
    power = std::clamp(o.value(QStringLiteral("power")).toInt(d.power), 2, 8);
    if (formula == Formula::BurningShip) power = 2;
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
