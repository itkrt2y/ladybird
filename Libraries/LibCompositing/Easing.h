/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <LibCompositing/Export.h>

namespace Compositing {

// https://drafts.csswg.org/css-easing/#cubic-bezier-algo
COMPOSITING_API double cubic_bezier_easing(double x1, double y1, double x2, double y2, double progress);

}
