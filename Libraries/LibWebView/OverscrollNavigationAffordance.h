/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/Time.h>
#include <LibCompositing/OverscrollNavigationAffordance.h>
#include <LibWebView/Export.h>

namespace WebView {

// The arrow that slides in from the edge of the viewport while an overscroll navigates the history, animated the way
// Chromium's gesture navigation affordance is. It moves with the overscroll, retracts when the overscroll is abandoned,
// and bursts when the navigation happens.
class WEBVIEW_API OverscrollNavigationAffordance {
public:
    using PaintState = Compositing::OverscrollNavigationAffordancePaintState;

    static constexpr float maximum_ripple_radius = 40;
    static constexpr float maximum_ripple_burst_radius = 48;
    static constexpr float activation_offset = 146;
    static constexpr float extra_offset = 72;
    static constexpr AK::Duration abort_duration = AK::Duration::from_milliseconds(300);
    static constexpr AK::Duration burst_duration = AK::Duration::from_milliseconds(200);

    // The direction is -1 for going back and 1 for going forward. A progress of 1 completes the navigation.
    void drag(int direction, float progress, float maximum_progress);
    void abort(MonotonicTime now);
    void complete(MonotonicTime now);

    Optional<PaintState> paint_state(MonotonicTime now) const;
    bool is_animating(MonotonicTime now) const;

private:
    enum class State : u8 {
        Hidden,
        Dragging,
        Aborting,
        Completing,
    };

    float drag_affordance_progress() const;
    float animation_progress(MonotonicTime now) const;
    AK::Duration animation_duration() const;

    State m_state { State::Hidden };
    int m_direction { -1 };
    float m_drag_progress { 0 };
    float m_maximum_drag_progress { 1 };
    MonotonicTime m_animation_start { MonotonicTime::now_coarse() };
};

}
