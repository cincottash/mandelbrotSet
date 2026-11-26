#include "ColorParams.h"

#include "Palette.h"

#include <QJsonValue>

#include <algorithm>
#include <cmath>

namespace core {

namespace {
constexpr unsigned kGradient = 1u << int(ColorStyle::Gradient);
constexpr unsigned kSine = 1u << int(ColorStyle::SineWaves);
constexpr unsigned kMoire = 1u << int(ColorStyle::Moire);
constexpr unsigned kDistance = 1u << int(ColorStyle::Distance);
constexpr unsigned kStripes = 1u << int(ColorStyle::Stripes);
constexpr unsigned kAngle = 1u << int(ColorStyle::Angle);
constexpr unsigned kBanded = kGradient | kSine | kDistance | kStripes | kAngle;
}  // namespace

const QList<SliderSpec>& sliderSpecs()
{
    using C = ColorParams;
    static const QList<SliderSpec> list = {
        {"density", "Density", "COLOR", "How quickly the colors repeat. Higher packs more color bands into the view.",
         0.02, 50.0, 1.0, 2, "", true, false, &C::density, kBanded},
        {"offset", "Offset", "COLOR", "Slides the colors along the bands.", 0.0, 1.0, 0.0, 2, "", false, false, &C::offset, kBanded},
        {"cycle_speed", "Cycle Speed", "COLOR", "Animates the colors flowing along the bands, in palette turns per second.",
         -1.0, 1.0, 0.0, 2, "/s", false, false, &C::cycleSpeed, kBanded},
        {"stripe_density", "Stripe Count", "COLOR", "How many stripes wrap around each band. Changing it renders the view again.",
         1.0, 20.0, 5.0, 1, "", false, true, &C::stripeDensity, kStripes},
        {"moire_scale", "Pattern Scale", "COLOR",
         "The fabric pattern's frequency. 1 matches the original Python renders pixel for pixel.",
         0.05, 4.0, 1.0, 3, "", true, false, &C::moireScale, kMoire},

        {"sine_freq_r", "Red Frequency", "SINE WAVES", "How many red waves pass per palette turn.", 0.0, 5.0, 1.0, 2, "", false, false, &C::sineFreqR, kSine},
        {"sine_freq_g", "Green Frequency", "SINE WAVES", "How many green waves pass per palette turn.", 0.0, 5.0, 1.0, 2, "", false, false, &C::sineFreqG, kSine},
        {"sine_freq_b", "Blue Frequency", "SINE WAVES", "How many blue waves pass per palette turn.", 0.0, 5.0, 1.0, 2, "", false, false, &C::sineFreqB, kSine},
        {"sine_phase_r", "Red Phase", "SINE WAVES", "Shifts the red wave.", 0.0, 1.0, 0.0, 2, "", false, false, &C::sinePhaseR, kSine},
        {"sine_phase_g", "Green Phase", "SINE WAVES", "Shifts the green wave.", 0.0, 1.0, 0.10, 2, "", false, false, &C::sinePhaseG, kSine},
        {"sine_phase_b", "Blue Phase", "SINE WAVES", "Shifts the blue wave.", 0.0, 1.0, 0.20, 2, "", false, false, &C::sinePhaseB, kSine},
        {"sine_base", "Base Level", "SINE WAVES", "The middle brightness the waves swing around.", 0.0, 1.0, 0.5, 2, "", false, false, &C::sineBrightness, kSine},
        {"sine_height", "Wave Height", "SINE WAVES", "How far the waves swing above and below the base level.", 0.0, 1.0, 0.5, 2, "", false, false, &C::sineContrast, kSine},

        {"light_strength", "Strength", "LIGHTING", "How much the light shapes the surface. 0 is flat.", 0.0, 1.0, 0.55, 2, "", false, false, &C::lightStrength, 0},
        {"light_angle", "Light Angle", "LIGHTING", "The direction the light comes from, counter-clockwise from the right.", 0.0, 360.0, 135.0, 0, "°", false, false, &C::lightAngle, 0},
        {"light_height", "Light Height", "LIGHTING", "How high the light is above the surface. Low light gives long shading.", 5.0, 90.0, 40.0, 0, "°", false, false, &C::lightHeight, 0},
        {"relief", "Relief", "LIGHTING", "How steep the surface looks.", 0.1, 10.0, 1.0, 2, "", true, false, &C::relief, 0},
        {"specular", "Shine", "LIGHTING", "Glossy highlights where the light reflects toward the viewer.", 0.0, 1.0, 0.25, 2, "", false, false, &C::specular, 0},

        {"edge_glow", "Edge Glow", "EDGES", "Lights up the thin filaments close to the set.", 0.0, 1.0, 0.0, 2, "", false, false, &C::edgeGlow, 0},
        {"edge_width", "Edge Width", "EDGES", "How far from the set the glow reaches.", 0.2, 20.0, 1.5, 1, "px", true, false, &C::edgeWidth, 0},

        {"hue", "Hue Shift", "EFFECTS", "Turns every color around the color wheel.", -180.0, 180.0, 0.0, 0, "°", false, false, &C::hue, 0},
        {"saturation", "Saturation", "EFFECTS", "0 is gray, 1 unchanged, 2 twice as vivid.", 0.0, 2.0, 1.0, 2, "", false, false, &C::saturation, 0},
        {"brightness", "Brightness", "EFFECTS", "Lightens or darkens everything.", -0.5, 0.5, 0.0, 2, "", false, false, &C::brightness, 0},
        {"contrast", "Contrast", "EFFECTS", "Spreads the colors apart from middle gray, or pulls them together.", 0.2, 3.0, 1.0, 2, "", false, false, &C::contrast, 0},
        {"gamma", "Gamma", "EFFECTS", "Below 1 lifts the dark colors; above 1 deepens them.", 0.3, 3.0, 1.0, 2, "", true, false, &C::gamma, 0},
        {"vignette", "Vignette", "EFFECTS", "Darkens the corners.", 0.0, 1.0, 0.0, 2, "", false, false, &C::vignette, 0},
        {"grain", "Film Grain", "EFFECTS", "Adds fine noise, like a photograph.", 0.0, 1.0, 0.0, 2, "", false, false, &C::grain, 0},
    };
    return list;
}

const SliderSpec* sliderSpec(const QString& key)
{
    for (const SliderSpec& s : sliderSpecs())
        if (key == QLatin1String(s.key)) return &s;
    return nullptr;
}

QStringList styleNames()
{
    return {QStringLiteral("Smooth Gradient"), QStringLiteral("Sine Waves"), QStringLiteral("Moiré Fabric"),
            QStringLiteral("Distance Bands"), QStringLiteral("Stripes"), QStringLiteral("Angle")};
}

QStringList interiorStyleNames()
{
    return {QStringLiteral("Solid"), QStringLiteral("Diagonal Gradient"), QStringLiteral("Depth")};
}

QStringList colorScaleNames()
{
    return {QStringLiteral("Linear"), QStringLiteral("Square Root"), QStringLiteral("Logarithmic")};
}

static QString colorText(const QColor& c) { return c.name(QColor::HexRgb).toUpper(); }

QJsonObject ColorParams::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("style"), style);
    o.insert(QStringLiteral("palette"), palette);
    o.insert(QStringLiteral("color_scale"), colorScale);
    for (const SliderSpec& s : sliderSpecs()) o.insert(QLatin1String(s.key), this->*(s.member));
    o.insert(QStringLiteral("moire_tint"), colorText(moireTint));
    o.insert(QStringLiteral("edge_color"), colorText(edgeColor));
    o.insert(QStringLiteral("interior_style"), interiorStyle);
    o.insert(QStringLiteral("interior_color"), colorText(interiorColor));
    o.insert(QStringLiteral("interior_color_2"), colorText(interiorColor2));
    return o;
}

void ColorParams::fromJson(const QJsonObject& o)
{
    const ColorParams d;
    style = std::clamp(o.value(QStringLiteral("style")).toInt(d.style), 0, int(styleNames().size()) - 1);
    palette = paletteByName(o.value(QStringLiteral("palette")).toString(d.palette)).name;
    colorScale = std::clamp(o.value(QStringLiteral("color_scale")).toInt(d.colorScale), 0, 2);
    for (const SliderSpec& s : sliderSpecs()) {
        const double v = o.value(QLatin1String(s.key)).toDouble(d.*(s.member));
        this->*(s.member) = std::isfinite(v) ? std::clamp(v, s.min, s.max) : d.*(s.member);
    }
    auto color = [&](const char* key, const QColor& def) {
        const QColor c(o.value(QLatin1String(key)).toString());
        return c.isValid() ? c : def;
    };
    moireTint = color("moire_tint", d.moireTint);
    edgeColor = color("edge_color", d.edgeColor);
    interiorStyle = std::clamp(o.value(QStringLiteral("interior_style")).toInt(d.interiorStyle), 0,
                               int(interiorStyleNames().size()) - 1);
    interiorColor = color("interior_color", d.interiorColor);
    interiorColor2 = color("interior_color_2", d.interiorColor2);
}

const QList<Look>& looks()
{
    static const QList<Look> list = [] {
        QList<Look> l;
        {
            ColorParams p;
            l.append({QStringLiteral("Seahorse Classic"), p});
        }
        {
            // The original Python renders: a lavender moiré outside, a dark-to-magenta diagonal inside.
            ColorParams p;
            p.style = int(ColorStyle::Moire);
            p.lightStrength = 0.0;
            p.interiorStyle = int(InteriorStyle::Diagonal);
            p.interiorColor = QColor(0, 0, 0);
            p.interiorColor2 = QColor(0xFF, 0x20, 0xFF);
            l.append({QStringLiteral("Lavender Moiré"), p});
        }
        {
            ColorParams p;
            p.palette = QStringLiteral("Nebula");
            p.density = 0.6;
            p.edgeGlow = 0.65;
            p.edgeWidth = 3.0;
            p.edgeColor = QColor(0xFF, 0xE9, 0xA8);
            p.lightStrength = 0.3;
            p.vignette = 0.45;
            p.grain = 0.15;
            l.append({QStringLiteral("Nebula Glow"), p});
        }
        {
            ColorParams p;
            p.palette = QStringLiteral("Electric");
            p.density = 1.4;
            p.lightStrength = 0.7;
            p.specular = 0.6;
            p.lightAngle = 60;
            l.append({QStringLiteral("Electric"), p});
        }
        {
            ColorParams p;
            p.style = int(ColorStyle::SineWaves);
            p.sineFreqR = 1.0;
            p.sineFreqG = 0.7;
            p.sineFreqB = 0.4;
            p.sinePhaseR = 0.0;
            p.sinePhaseG = 0.15;
            p.sinePhaseB = 0.20;
            p.density = 1.5;
            p.lightStrength = 0.4;
            l.append({QStringLiteral("Sine Waves"), p});
        }
        {
            ColorParams p;
            p.palette = QStringLiteral("Grayscale");
            p.density = 0.25;
            p.lightStrength = 1.0;
            p.relief = 0.5;
            p.specular = 0.5;
            p.saturation = 0.0;
            p.interiorColor = QColor(0x10, 0x10, 0x10);
            l.append({QStringLiteral("Embossed Stone"), p});
        }
        {
            ColorParams p;
            p.style = int(ColorStyle::Stripes);
            p.palette = QStringLiteral("Fire");
            p.density = 1.0;
            p.lightStrength = 0.35;
            l.append({QStringLiteral("Fire Stripes"), p});
        }
        {
            ColorParams p;
            p.style = int(ColorStyle::Angle);
            p.palette = QStringLiteral("Ice");
            p.density = 1.0;
            p.lightStrength = 0.5;
            l.append({QStringLiteral("Field Lines"), p});
        }
        {
            ColorParams p;
            p.style = int(ColorStyle::Distance);
            p.palette = QStringLiteral("Midnight");
            p.density = 1.0;
            p.edgeGlow = 1.0;
            p.edgeWidth = 1.0;
            p.edgeColor = QColor(0xF4, 0xF1, 0xDE);
            p.lightStrength = 0.2;
            l.append({QStringLiteral("Ink Lines"), p});
        }
        {
            ColorParams p;
            p.palette = QStringLiteral("Twilight");
            p.density = 0.8;
            p.lightStrength = 0.6;
            p.interiorStyle = int(InteriorStyle::Depth);
            p.interiorColor = QColor(0x0B, 0x06, 0x1A);
            p.interiorColor2 = QColor(0xF2, 0x8A, 0x54);
            l.append({QStringLiteral("Twilight Glass"), p});
        }
        return l;
    }();
    return list;
}

}  // namespace core
