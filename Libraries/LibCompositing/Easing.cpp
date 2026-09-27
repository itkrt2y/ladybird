/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Math.h>
#include <LibCompositing/Easing.h>

namespace Compositing {

double cubic_bezier_easing(double x1, double y1, double x2, double y2, double progress)
{
    auto bezier = [](double t, double p1, double p2) {
        return 3 * (1 - t) * (1 - t) * t * p1 + 3 * (1 - t) * t * t * p2 + t * t * t;
    };
    auto bezier_derivative = [](double t, double p1, double p2) {
        return 3 * (1 - t) * (1 - t) * p1 + 6 * (1 - t) * t * (p2 - p1) + 3 * t * t * (1 - p2);
    };

    // Find the curve parameter whose horizontal position is the progress: a few Newton steps usually land on it, and
    // bisection finishes the job when the slope gets too flat for them.
    auto t = progress;
    for (int i = 0; i < 8; ++i) {
        auto x = bezier(t, x1, x2) - progress;
        if (AK::abs(x) < 1e-7)
            return bezier(t, y1, y2);
        auto slope = bezier_derivative(t, x1, x2);
        if (AK::abs(slope) < 1e-6)
            break;
        t -= x / slope;
    }
    double low = 0;
    double high = 1;
    while (high - low > 1e-7) {
        t = (low + high) / 2;
        if (bezier(t, x1, x2) < progress)
            low = t;
        else
            high = t;
    }
    return bezier(t, y1, y2);
}

}
