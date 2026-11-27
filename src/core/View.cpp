#include "View.h"
#include "Tr.h"

#include <QJsonValue>
#include <QRegularExpression>

#include <cmath>

namespace core {

View View::home(bool julia)
{
    View v;
    v.cx = BigFloat(julia ? 0.0 : -0.6, 128);
    v.cy = BigFloat(0.0, 128);
    v.scale = FloatExp(kHomeScale);   // the same for both, so "Zoom 1×" means fully zoomed out
    v.angle = 0.0;
    return v;
}

long View::neededPrecision() const
{
    // The pixel spacing is about scale / 1000; 64 bits beyond it keep panning exact for a long time.
    const double bits = -scale.log2Abs() + 80.0;
    return std::max<long>(96, long(std::ceil(bits)));
}

void View::fitPrecision()
{
    const long p = neededPrecision();
    // Never drop bits the center already has: a deep center read from a file stays exact after zooming out.
    if (cx.precision() < p) cx.setPrecision(p);
    if (cy.precision() < p) cy.setPrecision(p);
}

ComplexExp View::offsetOf(double u, double v) const
{
    const double c = std::cos(angle), s = std::sin(angle);
    const double ru = u * c - v * s, rv = u * s + v * c;
    return ComplexExp(ru, rv) * scale;
}

void View::pointAt(double u, double v, BigFloat* x, BigFloat* y) const
{
    const ComplexExp d = offsetOf(u, v);
    BigFloat dx(cx.precision()), dy(cy.precision());
    dx.set(d.re());
    dy.set(d.im());
    *x = cx + dx;
    *y = cy + dy;
}

void View::pan(double du, double dv)
{
    fitPrecision();
    BigFloat x(cx.precision()), y(cy.precision());
    pointAt(du, dv, &x, &y);
    cx = x;
    cy = y;
}

void View::zoomAt(double u, double v, double factor)
{
    if (!(factor > 0) || !std::isfinite(factor)) return;
    // The point under (u, v) stays put: the center moves toward it by (1 - 1/factor) of the way.
    const double k = 1.0 - 1.0 / factor;
    const FloatExp newScale = scale * FloatExp(1.0 / factor);
    // Never out past the whole set (with some room), never into numbers too small to keep.
    if (newScale > FloatExp(64.0)) return;
    if (newScale.e < -2'000'000) return;
    fitPrecision();
    pan(u * k, v * k);
    scale = newScale;
    fitPrecision();
}

void View::rotateAt(double u, double v, double radians)
{
    // Rotate around the point under (u, v): move the center around it, then turn.
    if (!std::isfinite(radians)) return;
    const double c = std::cos(radians), s = std::sin(radians);
    // The center is at -(u, v) from the pivot in view units; after turning the view by `radians` the pivot keeps
    // its plane position, so the new center is pivot + R(radians) * (center - pivot), in view units.
    const double nu = u + (-u * c + v * s);
    const double nv = v + (-u * s - v * c);
    pan(nu, nv);
    angle = std::remainder(angle + radians, 2 * M_PI);
}

QString formatFloatExp(const FloatExp& v, int digits)
{
    if (v.isZero()) return QStringLiteral("0");
    const double l10 = v.log2Abs() * 0.30102999566398120;
    if (l10 < 6 && l10 > -4 && digits <= 6) {
        const double d = v.toDouble();
        return QString::number(d, 'g', std::max(digits + 1, 3));
    }
    // Exact decimal digits through MPFR (a double can't hold 1e-5000).
    BigFloat b(80);
    b.set(v);
    mpfr_exp_t ex = 0;
    char* s = mpfr_get_str(nullptr, &ex, 10, size_t(std::max(digits, 1)), b.raw(), MPFR_RNDN);
    QString mant = QString::fromLatin1(s);
    mpfr_free_str(s);
    QString sign;
    if (mant.startsWith(QLatin1Char('-'))) {
        sign = QStringLiteral("-");
        mant.remove(0, 1);
    }
    // 0.d1d2d3... * 10^ex  =  d1.d2d3... * 10^(ex - 1)
    QString frac = mant.mid(1);
    while (frac.endsWith(QLatin1Char('0'))) frac.chop(1);
    const qint64 e10 = qint64(ex) - 1;
    QString out = sign + mant.left(1);
    if (!frac.isEmpty()) out += QLatin1Char('.') + frac;
    out += QStringLiteral("e%1%2").arg(e10 >= 0 ? QStringLiteral("+") : QString()).arg(e10);
    return out;
}

bool parseFloatExp(const QString& text, FloatExp* out)
{
    static const QRegularExpression re(QStringLiteral(R"(^\s*[+-]?(?:\d+\.?\d*|\.\d+)(?:[eE]([+-]?\d+))?\s*$)"));
    const auto m = re.match(text);
    if (!m.hasMatch()) return false;
    if (!m.captured(1).isEmpty()) {
        bool ok = false;
        const qint64 ex10 = m.captured(1).toLongLong(&ok);
        if (!ok || std::llabs(ex10) > 100'000'000) return false;
    }
    BigFloat b(80);
    if (!b.parse(text.trimmed().toStdString())) return false;
    *out = b.toFloatExp();
    return true;
}

QString View::magnificationText() const { return formatFloatExp(magnification(), 2); }

QJsonObject View::toJson() const
{
    QJsonObject o;
    // Plain numbers in text, so every computer reads them the same, with the digits this zoom needs (more would
    // only be binary rounding noise, and saving the same place twice gives the same text).
    // A digit fewer than the bits hold, so a number typed with fewer digits isn't padded with rounding noise.
    const int digits = int(std::floor(double(neededPrecision()) * 0.30103)) - 1;
    o.insert(QStringLiteral("re"), QString::fromStdString(cx.toString(digits)));
    o.insert(QStringLiteral("im"), QString::fromStdString(cy.toString(digits)));
    o.insert(QStringLiteral("scale"), formatFloatExp(scale, 17));
    o.insert(QStringLiteral("angle_degrees"), angle * 180.0 / M_PI);
    return o;
}

bool View::fromJson(const QJsonObject& o, QString* error)
{
    FloatExp sc;
    if (!parseFloatExp(o.value(QStringLiteral("scale")).toString(), &sc) || !(sc > FloatExp(0.0))
        || sc > FloatExp(64.0) || sc.e < -2'000'000) {
        if (error) *error = T("The zoom (\"scale\") is missing or out of range.");
        return false;
    }
    const std::string re = o.value(QStringLiteral("re")).toString().toStdString();
    const std::string im = o.value(QStringLiteral("im")).toString().toStdString();
    // Precision for every digit given, and at least what the zoom needs.
    const long digitsBits = long(std::max(re.size(), im.size()) * 3.33) + 16;
    View v;
    v.scale = sc;
    const long bits = std::max(v.neededPrecision(), digitsBits);
    BigFloat x(bits), y(bits);
    if (!x.parse(re) || !y.parse(im)) {
        if (error) *error = T("The center (\"re\" and \"im\") must be plain numbers.");
        return false;
    }
    if (std::fabs(x.toDouble()) > 64 || std::fabs(y.toDouble()) > 64) {
        if (error) *error = T("The center is too far outside the set.");
        return false;
    }
    v.cx = x;
    v.cy = y;
    const double deg = o.value(QStringLiteral("angle_degrees")).toDouble(0.0);
    v.angle = std::isfinite(deg) ? std::remainder(deg * M_PI / 180.0, 2 * M_PI) : 0.0;
    *this = v;
    return true;
}

ComplexExp View::centerOffsetFrom(const BigFloat& x, const BigFloat& y) const
{
    const BigFloat dx = cx - x, dy = cy - y;
    return ComplexExp(dx.toFloatExp(), dy.toFloatExp());
}

}  // namespace core
