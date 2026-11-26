// What is drawn: the formula and its numbers (not the colors).
#pragma once

#include "BigFloat.h"
#include "View.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace core {

// Buddhabrot: the Mandelbrot formula drawn by where its escaping orbits go (a separate, counting renderer).
enum class FractalType { Mandelbrot = 0, Julia = 1, Buddhabrot = 2 };
// The step from z to the next z:
//   Classic       z^p + c
//   Burning Ship  (|x| + i|y|)^2 + c, drawn with the ship upright: x' = x^2 - y^2 + cx, y' = 2|xy| - cy
//   Tricorn       conj(z)^p + c (the Mandelbar; above power 2, a multicorn)
enum class Formula { Classic = 0, BurningShip = 1, Tricorn = 2 };
QStringList formulaNames();   // index = Formula

struct FractalParams {
    FractalType type = FractalType::Mandelbrot;
    Formula formula = Formula::Classic;
    int power = 2;                  // z^power + c, 2..8 (the Burning Ship is always 2)
    // The Julia set's seed c, as plain decimal text (it can carry many digits when picked on a deep view).
    QString juliaRe = QStringLiteral("-0.8");
    QString juliaIm = QStringLiteral("0.156");
    bool autoIterations = true;
    int maxIterations = 1000;       // used when autoIterations is off; 16..kMaxIterations
    double escapeRadius = 256.0;    // 2..100000
    // Buddhabrot: the iteration limits of the red, green and blue counts, and the fewest iterations counted.
    int buddhaRed = 5000, buddhaGreen = 500, buddhaBlue = 50, buddhaMin = 0;

    static constexpr int kMinIterations = 16;
    static constexpr int kMaxIterations = 5'000'000;

    bool julia() const { return type == FractalType::Julia; }
    bool buddhabrot() const { return type == FractalType::Buddhabrot; }
    bool classic() const { return formula == Formula::Classic; }
    // Deep zooms (past about 1e-24), the skip-ahead table and the inside-the-set shortcuts need the classic
    // formula at power 2: their math is written for it.
    bool deepCapable() const { return classic() && power == 2; }
    // The whole set in view.
    View home() const;
    QString name() const;   // "the Burning Ship", "the power 3 Julia set of ...", for messages

    BigFloat seedX(long bits) const;
    BigFloat seedY(long bits) const;

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& o);   // missing or bad values keep their defaults
    bool sameFormula(const FractalParams& o) const
    {
        return type == o.type && formula == o.formula && power == o.power && (type == FractalType::Mandelbrot
                                                      || (juliaRe == o.juliaRe && juliaIm == o.juliaIm))
            && escapeRadius == o.escapeRadius
            && (type != FractalType::Buddhabrot
                || (buddhaRed == o.buddhaRed && buddhaGreen == o.buddhaGreen && buddhaBlue == o.buddhaBlue && buddhaMin == o.buddhaMin));
    }
};

}  // namespace core
