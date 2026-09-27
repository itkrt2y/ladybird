/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/StdLibExtras.h>
#include <LibCompositing/Easing.h>
#include <LibWebView/OverscrollNavigationAffordance.h>

namespace WebView {

// The fast-out-slow-in curve Chromium eases the affordance with.
static float fast_out_slow_in(float progress)
{
    return static_cast<float>(Compositing::cubic_bezier_easing(0.4, 0, 0.2, 1, progress));
}

void OverscrollNavigationAffordance::drag(int direction, float progress, float maximum_progress)
{
    // An overscroll that starts while the previous affordance is still animating replaces it.
    m_state = State::Dragging;
    m_direction = direction;
    m_drag_progress = max(0.f, progress);
    m_maximum_drag_progress = max(1.f, maximum_progress);
}

void OverscrollNavigationAffordance::abort(MonotonicTime now)
{
    if (m_state != State::Dragging)
        return;
    m_state = State::Aborting;
    m_animation_start = now;
    if (animation_duration() <= AK::Duration::zero())
        m_state = State::Hidden;
}

void OverscrollNavigationAffordance::complete(MonotonicTime now)
{
    if (m_state != State::Dragging)
        return;
    m_state = State::Completing;
    m_animation_start = now;
}

float OverscrollNavigationAffordance::drag_affordance_progress() const
{
    if (m_drag_progress < 1)
        return m_drag_progress;

    // Past the activation threshold, the affordance slides a limited extra distance.
    auto extra_progress = m_maximum_drag_progress == 1 ? 1.f : min(1.f, (m_drag_progress - 1) / (m_maximum_drag_progress - 1));
    return 1 + fast_out_slow_in(extra_progress) * (extra_offset / activation_offset);
}

AK::Duration OverscrollNavigationAffordance::animation_duration() const
{
    if (m_state == State::Completing)
        return burst_duration;
    // An affordance retracts from where it is, so the closer to its hidden position it is, the sooner it is hidden.
    return AK::Duration::from_nanoseconds(static_cast<i64>(static_cast<double>(abort_duration.to_nanoseconds()) * drag_affordance_progress()));
}

float OverscrollNavigationAffordance::animation_progress(MonotonicTime now) const
{
    auto duration = animation_duration();
    if (duration <= AK::Duration::zero())
        return 1;
    auto elapsed = now - m_animation_start;
    return clamp(static_cast<float>(static_cast<double>(elapsed.to_nanoseconds()) / static_cast<double>(duration.to_nanoseconds())), 0.f, 1.f);
}

bool OverscrollNavigationAffordance::is_animating(MonotonicTime now) const
{
    if (m_state != State::Aborting && m_state != State::Completing)
        return false;
    return animation_progress(now) < 1;
}

Optional<OverscrollNavigationAffordance::PaintState> OverscrollNavigationAffordance::paint_state(MonotonicTime now) const
{
    if (m_state == State::Hidden || (m_state != State::Dragging && !is_animating(now)))
        return {};

    auto affordance_progress = drag_affordance_progress();
    if (m_state == State::Aborting)
        affordance_progress *= 1 - fast_out_slow_in(animation_progress(now));
    auto clamped_progress = min(1.f, affordance_progress);

    PaintState state;
    state.points_back = m_direction < 0;
    state.offset = affordance_progress * activation_offset;
    state.activated = clamped_progress >= 1;
    if (m_state == State::Completing) {
        auto burst_progress = animation_progress(now);
        state.ripple_radius = maximum_ripple_radius + fast_out_slow_in(burst_progress) * (maximum_ripple_burst_radius - maximum_ripple_radius);
        state.opacity = clamp(fast_out_slow_in(1 - burst_progress), 0.f, 1.f);
    } else {
        state.ripple_radius = PaintState::background_radius + clamped_progress * (maximum_ripple_radius - PaintState::background_radius);
    }
    return state;
}

}
