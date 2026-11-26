// Where the camera is: the center (arbitrary precision), the zoom and the rotation.
#pragma once

#include "BigFloat.h"
#include "FloatExp.h"

#include <QJsonObject>
#include <QString>

namespace core {

struct View {
    static constexpr double kHomeScale = 1.25;   // half the view's height, in the plane, when fully zoomed out

    BigFloat cx{-0.5, 128};
    BigFloat cy{0.0, 128};
    FloatExp scale{kHomeScale};   // half the view's height, in plane units
    double angle = 0.0;           // radians, counter-clockwise

    static View home(bool julia);
    // Bits the center needs at this zoom (the pixel spacing plus a margin).
    long neededPrecision() const;
    void fitPrecision();

    // Plane offset from the center of a point in view units: u to the right, v up, both -1..1 across the height.
    ComplexExp offsetOf(double u, double v) const;
    // The plane point at (u, v), at the center's precision.
    void pointAt(double u, double v, BigFloat* x, BigFloat* y) const;
    void pan(double du, double dv);                 // in view units
    void zoomAt(double u, double v, double factor); // > 1 zooms in, keeping the point under (u, v) in place
    void zoomBy(double factor) { zoomAt(0, 0, factor); }
    void rotateAt(double u, double v, double radians);

    // How far in: 1 when fully zoomed out, 1e100 a hundred decimal places in.
    FloatExp magnification() const { return FloatExp(kHomeScale) / scale; }
    double log10Magnification() const { return (FloatExp(kHomeScale).log2Abs() - scale.log2Abs()) * 0.30102999566; }
    // "3.2e+141" style text for the magnification (plain "12.5" when small).
    QString magnificationText() const;

    QJsonObject toJson() const;
    // False if the object holds no usable view; *this is unchanged then.
    bool fromJson(const QJsonObject& o, QString* error);

    // The offset of this view's center from a point, in the plane (wide exponent).
    ComplexExp centerOffsetFrom(const BigFloat& x, const BigFloat& y) const;
};

// "3.2e+141" for any FloatExp (positive), with the given significant digits.
QString formatFloatExp(const FloatExp& v, int digits = 2);
// Parses "3.2e+141" or "1250" into a FloatExp (beyond the double range too). False on bad text.
bool parseFloatExp(const QString& text, FloatExp* out);

}  // namespace core
