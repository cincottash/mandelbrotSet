// The reference orbit (one point iterated with arbitrary precision) and its bilinear approximation table.
//
// Every pixel is computed as a small offset from the reference ("perturbation"), in fast single-precision math
// on the graphics card. The table lets a pixel skip many iterations at once while its offset is small
// ("bilinear approximation", BLA). Together they make zooms far past double precision fast enough to explore.
#pragma once

#include "BigFloat.h"
#include "FloatExp.h"

#include <atomic>
#include <cstdint>
#include <vector>

namespace core {

// Layouts shared with the compute shader (std430).
// Each reference value as a high and a low single-precision part (hi + lo carries about 48 bits): the low part
// restores the digits a "rebase" (z = Z + dz, near zero) would otherwise cancel away.
struct GpuRefF { float hx, hy, lx, ly; };
struct GpuRefX { float hx, hy, lx, ly; int32_t e, pad0, pad1, pad2; };
struct GpuBlaF { float ax, ay, bx, by, rInf, pad0, pad1, pad2; };   // valid while max(|dz.x|, |dz.y|) < rInf
struct GpuBlaX { float ax, ay, bx, by; int32_t ae, be; float log2R, pad; };

struct OrbitRequest {
    BigFloat x{0.0, 128}, y{0.0, 128};   // the reference point: c for Mandelbrot, the starting z for Julia
    BigFloat seedX{0.0, 128}, seedY{0.0, 128};   // Julia's c
    bool julia = false;
    int power = 2;
    int formula = 0;                     // core::Formula
    int maxIterations = 1000;
    double escapeRadius = 256.0;
    long precision = 128;
};

struct RefOrbit {
    OrbitRequest request;
    int length = 0;          // Z_0 .. Z_length are stored
    bool escaped = false;    // Z_length is outside the escape radius (else length == maxIterations)
    std::vector<ComplexExp> z;
    std::vector<GpuRefF> gpuF;
    std::vector<GpuRefX> gpuX;
};

// Iterates the reference point. False if cancelled. `progress` (optional) receives the iteration count.
bool computeOrbit(const OrbitRequest& rq, RefOrbit* out, const std::atomic<bool>* cancel = nullptr,
                  std::atomic<int>* progress = nullptr);

struct BlaTable {
    FloatExp dcMax;          // the largest pixel offset from the reference the table was built for
    double epsilon = 0;
    // Level k (k >= 1) skips 2^k iterations from m = j * 2^k + 1. GPU level index i holds BLA level i + 1.
    int levels = 0;
    std::vector<int32_t> offset, count;
    std::vector<GpuBlaF> gpuF;
    std::vector<GpuBlaX> gpuX;

    struct Entry { ComplexExp a, b; FloatExp r; };
    std::vector<std::vector<Entry>> cpu;   // kept only when asked (tests); index = BLA level (0 = single steps)
    bool valid() const { return levels > 0; }
};

// Only for power 2. dcMax: the largest |pixel - reference| in the views the table will be used for.
bool buildBla(const RefOrbit& orbit, const FloatExp& dcMax, double epsilon, BlaTable* out, bool keepCpu = false,
              const std::atomic<bool>* cancel = nullptr);

}  // namespace core
