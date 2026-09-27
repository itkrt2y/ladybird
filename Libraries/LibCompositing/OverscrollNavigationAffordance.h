/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

namespace Compositing {

// How the compositor paints the arrow that slides in from the edge of the viewport while an overscroll navigates the
// history. Lengths are in device-independent pixels.
struct OverscrollNavigationAffordancePaintState {
    static constexpr float background_radius = 20;

    bool points_back { true };
    // How far the affordance has slid in from its hidden position outside the viewport.
    float offset { 0 };
    float ripple_radius { 0 };
    bool activated { false };
    float opacity { 1 };

    bool operator==(OverscrollNavigationAffordancePaintState const&) const = default;
};

}
