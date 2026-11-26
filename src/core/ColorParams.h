// How the computed numbers become colors, and the table every slider is built from.
#pragma once

#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace core {

enum class ColorStyle { Gradient = 0, SineWaves = 1, Moire = 2, Distance = 3, Stripes = 4, Angle = 5, OrbitTrap = 6 };
enum class TrapShape { Point = 0, Cross = 1, Circle = 2 };
enum class InteriorStyle { Solid = 0, Diagonal = 1, Depth = 2 };

struct ColorParams {
    int style = int(ColorStyle::Gradient);
    QString palette = QStringLiteral("Seahorse");
    int colorScale = 0;            // 0 linear, 1 square root, 2 logarithmic
    double density = 1.0;
    double offset = 0.0;
    double cycleSpeed = 0.0;       // palette turns per second while the view is open

    double sineFreqR = 1.0, sineFreqG = 1.0, sineFreqB = 1.0;
    double sinePhaseR = 0.0, sinePhaseG = 0.10, sinePhaseB = 0.20;
    double sineBrightness = 0.5, sineContrast = 0.5;

    double moireScale = 1.0;
    QColor moireTint{0xD9, 0xBF, 0xFF};

    double lightStrength = 0.55;
    double lightAngle = 135.0;     // degrees, 0 = from the right, counter-clockwise
    double lightHeight = 40.0;     // degrees above the surface
    double relief = 1.0;
    double specular = 0.25;

    double edgeGlow = 0.0;
    double edgeWidth = 1.5;        // pixels
    QColor edgeColor{0xFF, 0xFF, 0xFF};

    double stripeDensity = 5.0;    // needs a new render

    // Orbit trap (needs a new render): the closest each orbit comes to a point, a cross or a circle.
    int trapShape = int(TrapShape::Cross);
    double trapX = 0.0, trapY = 0.0, trapSize = 1.0;   // the shape's center; the circle's radius

    int interiorStyle = int(InteriorStyle::Solid);
    QColor interiorColor{0x00, 0x00, 0x00};
    QColor interiorColor2{0xFF, 0x20, 0xFF};

    double hue = 0.0, saturation = 1.0, brightness = 0.0, contrast = 1.0, gamma = 1.0;
    double vignette = 0.0, grain = 0.0;
    double bloom = 0.0, bloomSize = 2.5, bloomThreshold = 0.6;   // bloomSize: % of the picture's height

    // The picture wrapped onto a ball (core/Sphere.h); size 0 = flat.
    double sphereSize = 0.0, sphereShade = 0.6, sphereShine = 0.5;
    QColor sphereBackground{0x0B, 0x0D, 0x12};

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& o);
    bool operator==(const ColorParams& o) const { return toJson() == o.toJson(); }
};

// One slider: what it is called, its range, and the member it edits.
struct SliderSpec {
    const char* key;          // saved in files, never renamed
    const char* label;        // title case, shown beside the slider
    const char* card;         // the card that holds it
    const char* tip;          // a sentence for the tooltip
    double min, max, def;
    int decimals;
    const char* unit;         // shown after the number ("°", "px", "/s") or ""
    bool logScale;            // the slider moves through the range by ratio, not by difference
    bool rerender;            // changing it needs the fractal computed again (not just recolored)
    double ColorParams::*member;
    // Which styles show it (bit per ColorStyle); 0 = every style.
    unsigned styles;
};

const QList<SliderSpec>& sliderSpecs();
const SliderSpec* sliderSpec(const QString& key);
QStringList styleNames();          // index = ColorStyle
QStringList interiorStyleNames();  // index = InteriorStyle
QStringList trapShapeNames();      // index = TrapShape
QStringList colorScaleNames();

// Ready-made looks: a full set of color settings each.
struct Look {
    QString name;
    ColorParams params;
};
const QList<Look>& looks();

}  // namespace core
