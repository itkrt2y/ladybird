/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibCompositing/OverscrollNavigationAffordance.h>
#include <LibGfx/Forward.h>
#include <LibGfx/Size.h>

namespace Compositor {

void paint_overscroll_navigation_affordance(Gfx::PaintingSurface&, Gfx::IntSize viewport_size, double device_pixel_ratio, Compositing::OverscrollNavigationAffordancePaintState const&);

}
