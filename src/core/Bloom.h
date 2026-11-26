// Bloom: bright parts of the picture glow into their surroundings. The same steps run on the graphics card for the
// picture on screen (GpuEngine) and here for saved pictures and movie frames, which are drawn in pieces and get
// their glow once whole, so no seams show:
//   1. keep only what is brighter than the threshold (with a soft knee),
//   2. shrink by a whole factor (box average), so the blur stays small,
//   3. blur with a Gaussian whose width is a share of the picture's height,
//   4. enlarge again (bilinear) and lay it over the picture with a "screen" blend.
#pragma once

#include <QImage>

namespace core {

struct BloomPlan {
    int factor = 1;        // the shrink factor
    int smallW = 1, smallH = 1;
    double sigma = 1;      // the blur's width in shrunk pixels
    int radius = 3;        // the blur's half width in shrunk pixels (3 sigma)
};
// sizePercent: the blur's width (sigma) as a percentage of the picture's height.
BloomPlan bloomPlan(int w, int h, double sizePercent);
// The soft-knee bright pass for one color (0..1 each), as the shader does it.
double bloomWeight(double luma, double threshold);
// Adds the glow to an RGB32 picture.
void applyBloom(QImage& img, double strength, double sizePercent, double threshold);

}  // namespace core
