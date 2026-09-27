/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/Math.h>
#include <LibWebView/OverscrollHistoryNavigation.h>

namespace WebView {

Optional<int> OverscrollHistoryNavigation::did_finish_handling_wheel_event(WheelEvent const& event, Web::EventResult event_result, Gfx::FloatSize viewport_size, HistoryAvailability history_availability, MonotonicTime now)
{
    // Only touchpad scrolling navigates, which arrives with precise deltas. A modifier gives the gesture another
    // meaning, e.g. zooming or scrolling along the other axis.
    if (event.precision != Web::WheelDeltaPrecision::Precise || event.modifiers != 0)
        return {};

    if (m_gesture.has_value() && m_gesture->is_continued_by(event.position, event.phase, event.modifiers, now, Compositing::wheel_gesture_position_slop_in_css_pixels)) {
        m_gesture->advance_to(event.phase, now);
    } else {
        // What remains of a gesture that has already ended does not start another one.
        if (event.phase == Web::ScrollGesturePhase::Momentum || event.phase == Web::ScrollGesturePhase::Ended) {
            m_gesture.clear();
            reset_overscroll();
            return {};
        }
        start_gesture(event, now);
    }

    if (event.phase == Web::ScrollGesturePhase::Ended)
        return complete_gesture(viewport_size);

    if (m_ignores_rest_of_gesture)
        return {};
    if (event.delta.x() == 0 && event.delta.y() == 0)
        return {};
    // The event reached no active document, e.g. because a navigation this gesture completed is still underway.
    if (event_result == Web::EventResult::Dropped)
        return {};

    if (event_result == Web::EventResult::Handled || event_result == Web::EventResult::Cancelled) {
        // Once the page has scrolled or cancelled a step of the gesture, the rest of the gesture is the page's too.
        if (!m_direction.has_value()) {
            m_ignores_rest_of_gesture = true;
            reset_overscroll();
        }
        return {};
    }

    // Momentum carries on an overscroll the fingers started, but does not start one.
    if (event.phase == Web::ScrollGesturePhase::Momentum && !m_direction.has_value())
        return {};

    m_overscroll_delta.translate_by(event.delta);
    auto horizontal_delta = AK::fabs(m_overscroll_delta.x());
    auto vertical_delta = AK::fabs(m_overscroll_delta.y());
    if (horizontal_delta <= start_threshold && vertical_delta <= start_threshold) {
        if (m_direction.has_value())
            reset_overscroll();
        return {};
    }

    // Scrolling past the left edge goes back, and scrolling past the right edge goes forward.
    Optional<int> new_direction;
    if (horizontal_delta > start_threshold && horizontal_delta > vertical_delta * minimum_horizontal_to_vertical_ratio) {
        if (m_overscroll_delta.x() < 0 && history_availability.can_go_back)
            new_direction = -1;
        else if (m_overscroll_delta.x() > 0 && history_availability.can_go_forward)
            new_direction = 1;
    }

    if (!m_direction.has_value()) {
        // A gesture overscrolls in the direction it first overscrolled in, or not at all.
        if (new_direction.has_value() && (!m_locked_direction.has_value() || m_locked_direction == new_direction)) {
            m_direction = new_direction;
            m_locked_direction = new_direction;
        }
    } else if (new_direction != m_direction) {
        reset_overscroll();
    }

    return {};
}

bool OverscrollHistoryNavigation::is_overscrolling_phase_less_gesture() const
{
    return m_gesture_is_phase_less && m_direction.has_value();
}

Optional<int> OverscrollHistoryNavigation::did_end_phase_less_gesture(Gfx::FloatSize viewport_size)
{
    if (!is_overscrolling_phase_less_gesture())
        return {};
    auto history_delta = complete_gesture(viewport_size);
    m_gesture.clear();
    return history_delta;
}

float OverscrollHistoryNavigation::completion_distance(Gfx::FloatSize viewport_size)
{
    return max(viewport_size.width(), viewport_size.height()) * complete_threshold_fraction_of_viewport - start_threshold;
}

float OverscrollHistoryNavigation::overscroll_progress(Gfx::FloatSize viewport_size) const
{
    auto distance = completion_distance(viewport_size);
    if (!m_direction.has_value() || distance <= 0)
        return 0;
    return max(0.f, AK::fabs(m_overscroll_delta.x()) - start_threshold) / distance;
}

float OverscrollHistoryNavigation::maximum_overscroll_progress(Gfx::FloatSize viewport_size)
{
    auto distance = completion_distance(viewport_size);
    if (distance <= 0)
        return 1;
    return max(1.f, (max(viewport_size.width(), viewport_size.height()) - start_threshold) / distance);
}

void OverscrollHistoryNavigation::start_gesture(WheelEvent const& event, MonotonicTime now)
{
    m_gesture = Compositing::WheelGestureIdentity::started_by(event.position, event.phase, event.modifiers, now);
    m_gesture_is_phase_less = event.phase == Web::ScrollGesturePhase::None;
    m_ignores_rest_of_gesture = false;
    m_locked_direction.clear();
    reset_overscroll();
}

void OverscrollHistoryNavigation::reset_overscroll()
{
    m_overscroll_delta = {};
    m_direction.clear();
}

Optional<int> OverscrollHistoryNavigation::complete_gesture(Gfx::FloatSize viewport_size)
{
    auto direction = m_direction;
    auto horizontal_delta = AK::fabs(m_overscroll_delta.x());
    reset_overscroll();
    m_ignores_rest_of_gesture = true;

    auto viewport_extent = max(viewport_size.width(), viewport_size.height());
    if (!direction.has_value() || viewport_extent <= 0)
        return {};
    if (horizontal_delta / viewport_extent < complete_threshold_fraction_of_viewport)
        return {};
    return direction;
}

}
