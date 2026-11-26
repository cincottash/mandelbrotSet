// What is drawn: the formula and its numbers (not the colors).
#pragma once

#include "BigFloat.h"

#include <QJsonObject>
#include <QString>

namespace core {

enum class FractalType { Mandelbrot = 0, Julia = 1 };

struct FractalParams {
    FractalType type = FractalType::Mandelbrot;
    int power = 2;                  // z^power + c, 2..8
    // The Julia set's seed c, as plain decimal text (it can carry many digits when picked on a deep view).
    QString juliaRe = QStringLiteral("-0.8");
    QString juliaIm = QStringLiteral("0.156");
    bool autoIterations = true;
    int maxIterations = 1000;       // used when autoIterations is off; 16..kMaxIterations
    double escapeRadius = 256.0;    // 2..100000

    static constexpr int kMinIterations = 16;
    static constexpr int kMaxIterations = 5'000'000;

    bool julia() const { return type == FractalType::Julia; }
    // Deep zooms (past about 1e-24) need power 2: the faster extended-range math is written for it.
    bool deepCapable() const { return power == 2; }

    BigFloat seedX(long bits) const;
    BigFloat seedY(long bits) const;

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& o);   // missing or bad values keep their defaults
    bool sameFormula(const FractalParams& o) const
    {
        return type == o.type && power == o.power && (type == FractalType::Mandelbrot
                                                      || (juliaRe == o.juliaRe && juliaIm == o.juliaIm))
            && escapeRadius == o.escapeRadius;
    }
};

}  // namespace core
