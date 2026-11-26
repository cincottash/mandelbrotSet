// Randomize: a random fractal, a random interesting spot in it, and random colors, lighting and effects.
#pragma once

#include "ColorParams.h"
#include "Fractal.h"
#include "View.h"

#include <QRandomGenerator>

namespace core {

struct Scene {
    FractalParams fractal;
    View view;
    ColorParams colors;
};

// Random colors only (every style, palette, light, edge, inside and effect setting).
ColorParams randomColors(QRandomGenerator& rnd, const ColorParams& base);
// The same, made calm enough for one view: where the escape time changes fast from pixel to pixel, a lower
// density or the log scale keeps the colors from turning into static (views up to 1e12x; deeper ones as drawn).
ColorParams randomColorsFor(QRandomGenerator& rnd, const ColorParams& base, const FractalParams& f, const View& v);
// Everything at random. The spot is found by a quick dive toward the set's edge (where the detail is), and spots
// that would show one flat color are skipped.
Scene randomScene(QRandomGenerator& rnd);

}  // namespace core
