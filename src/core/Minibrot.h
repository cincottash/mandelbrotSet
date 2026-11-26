// Finding the mini Mandelbrot sets ("minibrots") inside a view, for zooms that loop seamlessly: every minibrot is
// a small, turned copy of the whole set, so zooming from the whole set into one ends on a picture that looks like
// the start.
#pragma once

#include "BigFloat.h"
#include "FloatExp.h"
#include "View.h"

#include <atomic>

namespace core {

struct Minibrot {
    int period = 0;
    BigFloat x{0.0, 128}, y{0.0, 128};   // the nucleus (the center of its main cardioid)
    ComplexExp size;                     // its size and turn relative to the whole set (the whole set is 1)
    bool found = false;
};

// The lowest period of a minibrot whose nucleus lies within `radius` of (x, y) (the "ball period" method).
// 0 if none up to maxPeriod.
int findPeriod(const BigFloat& x, const BigFloat& y, const FloatExp& radius, int maxPeriod,
               const std::atomic<bool>* cancel = nullptr);
// Newton's method from (x, y) for the nucleus of the given period. False if it doesn't settle.
bool findNucleus(BigFloat& x, BigFloat& y, int period, long precision, const std::atomic<bool>* cancel = nullptr);
// The size estimate of the minibrot at a nucleus.
ComplexExp minibrotSize(const BigFloat& x, const BigFloat& y, int period);
// All three: the minibrot nearest the view's center (within the view).
Minibrot findMinibrot(const View& v, int maxPeriod = 200000, const std::atomic<bool>* cancel = nullptr);

// The view, framed like the whole set, that shows a minibrot.
View minibrotHome(const Minibrot& m);

// A zoom along the self-similar path from the whole set (t = 0) to a minibrot's own copy of it (t = 1). Each step
// is the same turn and shrink, so the picture flows evenly and the end matches the start.
class LoopPath {
public:
    explicit LoopPath(const Minibrot& m);
    View at(double t) const;
    double doublings() const;   // how many times the zoom halves the view from start to end
private:
    Minibrot m_m;
};

// A zoom from `from` to `to` (t = 0 to 1): the scale shrinks evenly, the turn follows evenly, and the center moves
// so that every step is the same shrink-and-turn (the target slides smoothly into the middle). t past 1 keeps going.
View zoomPathAt(const View& from, const View& to, double t);

}  // namespace core
