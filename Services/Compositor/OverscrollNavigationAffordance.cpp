/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <Compositor/OverscrollNavigationAffordance.h>
#include <LibGfx/Color.h>
#include <LibGfx/PaintingSurface.h>
#include <LibGfx/SkiaUtils.h>
#include <core/SkBlurTypes.h>
#include <core/SkCanvas.h>
#include <core/SkMaskFilter.h>
#include <core/SkPaint.h>
#include <core/SkPathBuilder.h>

namespace Compositor {

void paint_overscroll_navigation_affordance(Gfx::PaintingSurface& surface, Gfx::IntSize viewport_size, double device_pixel_ratio, Compositing::OverscrollNavigationAffordancePaintState const& state)
{
    // These match Chromium's gesture navigation affordance.
    static constexpr auto accent_color = Color(0x1a, 0x73, 0xe8);
    static constexpr auto ripple_color = Color(0x1a, 0x73, 0xe8, 0x4c);
    static constexpr auto shadow_color = Color(0, 0, 0, 0x4d);
    static constexpr float shadow_offset_y = 2;
    static constexpr float shadow_blur_radius = 8;
    static constexpr float arrow_size = 20;
    // The affordance stays this far from the top edge of a short viewport.
    static constexpr float minimum_center_y = 48;

    auto const background_radius = Compositing::OverscrollNavigationAffordancePaintState::background_radius;
    auto scale = static_cast<float>(device_pixel_ratio);
    auto viewport_width = static_cast<float>(viewport_size.width()) / scale;
    auto viewport_height = static_cast<float>(viewport_size.height()) / scale;

    // The background circle starts just outside the edge that the overscroll pulls it in from.
    auto center_x = state.points_back ? state.offset - background_radius : viewport_width + background_radius - state.offset;
    auto center_y = max(minimum_center_y, viewport_height / 2);

    auto& canvas = surface.canvas();
    auto save_count = canvas.save();
    canvas.clipRect(SkRect::MakeWH(viewport_size.width(), viewport_size.height()));
    canvas.scale(scale, scale);
    canvas.translate(center_x, center_y);
    if (state.opacity < 1)
        canvas.saveLayerAlphaf(nullptr, state.opacity);

    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(Gfx::to_skia_color(ripple_color));
    canvas.drawCircle(0, 0, state.ripple_radius, paint);

    SkPaint shadow_paint;
    shadow_paint.setAntiAlias(true);
    shadow_paint.setColor(Gfx::to_skia_color(shadow_color));
    shadow_paint.setMaskFilter(SkMaskFilter::MakeBlur(kNormal_SkBlurStyle, shadow_blur_radius / 2));
    canvas.drawCircle(0, shadow_offset_y, background_radius, shadow_paint);

    paint.setColor(Gfx::to_skia_color(state.activated ? accent_color : Color::White));
    canvas.drawCircle(0, 0, background_radius, paint);

    // The arrow is the Material Design back arrow, drawn in its 24 by 24 icon grid and mirrored to point forward.
    SkPathBuilder arrow;
    arrow.moveTo(20, 11);
    arrow.lineTo(7.83f, 11);
    arrow.lineTo(13.42f, 5.41f);
    arrow.lineTo(12, 4);
    arrow.lineTo(4, 12);
    arrow.lineTo(12, 20);
    arrow.lineTo(13.41f, 18.59f);
    arrow.lineTo(7.83f, 13);
    arrow.lineTo(20, 13);
    arrow.close();

    auto arrow_scale = arrow_size / 24;
    canvas.scale(state.points_back ? arrow_scale : -arrow_scale, arrow_scale);
    canvas.translate(-12, -12);
    paint.setColor(Gfx::to_skia_color(state.activated ? Color::White : accent_color));
    canvas.drawPath(arrow.detach(), paint);

    canvas.restoreToCount(save_count);
}

}
