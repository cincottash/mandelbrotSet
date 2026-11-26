// Sphere: the flat picture wrapped onto a lit ball that floats over a background, with a soft shadow below it.
// The graphics card does it for the picture on screen (shaders/sphere.frag); this does the same for saved pictures
// and movie frames, once their pieces are put together. The front half of the ball shows the whole height of the
// flat picture: a point at longitude lon and latitude lat shows the flat picture at
//   center + (lon, -lat) / (pi / 2) * (height / 2).
#pragma once

#include <QColor>
#include <QImage>

namespace core {

struct SphereLook {
    double size = 0;             // the ball's radius as a share of half the picture's height; 0 = off
    double shade = 0.6;          // how much the light shapes the ball
    double shine = 0.5;          // glossy highlight
    QColor background{0x0B, 0x0D, 0x12};
    double lightX = -0.5, lightY = 0.5, lightZ = 0.7;   // toward the light (y up), normalized by the caller
};

// The ball over the background, from a flat RGB32 picture of the same size.
QImage applySphere(const QImage& flat, const SphereLook& look);

}  // namespace core
