/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Math.h>
#include <AK/StdLibExtras.h>
#include <LibCompositing/Easing.h>
#include <LibCompositing/Scrolling/SmoothScrollAnimation.h>

namespace Compositing {

static constexpr double scroll_speed_in_pixels_per_second = 1000.0;
static constexpr double maximum_scroll_duration_in_seconds = 0.2;

// https://drafts.csswg.org/css-easing/#cubic-bezier-algo
// The ease-in-out curve WebKit uses for programmatic smooth scrolling, cubic-bezier(0.42, 0, 0.58, 1).
static double ease_in_out(double progress)
{
    return cubic_bezier_easing(0.42, 0, 0.58, 1, progress);
}

static double momentum_frames_for_distance(double distance)
{
    auto frames = AK::ceil(-AK::log(1 - distance * (1 - 1 / momentum_distance_share_per_frame)) / AK::log(momentum_distance_share_per_frame));
    return min(frames, maximum_momentum_duration_in_seconds / momentum_frame_duration_in_seconds);
}

SmoothScrollAnimation::SmoothScrollAnimation(Gfx::FloatPoint start_offset, Gfx::FloatPoint destination_offset, double pixels_per_css_pixel, ScrollAnimationKind kind)
    : m_start_offset(start_offset)
    , m_destination_offset(destination_offset)
    , m_kind(kind)
{
    VERIFY(pixels_per_css_pixel > 0);
    auto horizontal_distance = static_cast<double>(destination_offset.x() - start_offset.x()) / pixels_per_css_pixel;
    auto vertical_distance = static_cast<double>(destination_offset.y() - start_offset.y()) / pixels_per_css_pixel;
    auto distance = AK::sqrt(horizontal_distance * horizontal_distance + vertical_distance * vertical_distance);

    auto duration_in_seconds = kind == ScrollAnimationKind::Momentum
        ? momentum_frames_for_distance(distance) * momentum_frame_duration_in_seconds
        : min(distance / scroll_speed_in_pixels_per_second, maximum_scroll_duration_in_seconds);
    m_duration = AK::Duration::from_seconds_f64(duration_in_seconds);
}

SmoothScrollAnimation::Sample SmoothScrollAnimation::sample(AK::Duration elapsed) const
{
    if (m_duration.is_zero() || elapsed >= m_duration)
        return { m_destination_offset, true };

    auto progress = clamp(elapsed.to_seconds_f64() / m_duration.to_seconds_f64(), 0.0, 1.0);

    double eased_progress = 0;
    if (m_kind == ScrollAnimationKind::Momentum) {
        auto frames = m_duration.to_seconds_f64() / momentum_frame_duration_in_seconds;
        auto frames_elapsed = progress * frames;
        eased_progress = (1 - AK::pow(momentum_distance_share_per_frame, frames_elapsed)) / (1 - AK::pow(momentum_distance_share_per_frame, frames));
    } else {
        // https://drafts.csswg.org/cssom-view/#smooth-scroll
        // A smooth scroll follows a user-agent-defined timing function.
        eased_progress = ease_in_out(progress);
    }

    return {
        {
            static_cast<float>(m_start_offset.x() + (m_destination_offset.x() - m_start_offset.x()) * eased_progress),
            static_cast<float>(m_start_offset.y() + (m_destination_offset.y() - m_start_offset.y()) * eased_progress),
        },
        false,
    };
}

}
