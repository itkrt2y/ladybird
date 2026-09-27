/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/Time.h>
#include <AK/Types.h>
#include <LibCompositing/Scrolling/WheelGestureIdentity.h>
#include <LibGfx/Point.h>
#include <LibGfx/Size.h>
#include <LibWebCommon/Page/EventResult.h>
#include <LibWebCommon/Page/InputEvent.h>
#include <LibWebView/Export.h>

namespace WebView {

// Traverses the session history when a touchpad gesture keeps scrolling horizontally past what the page consumes,
// the way Chromium's overscroll history navigation does. The gesture is followed by the results the page reports for
// its wheel events, so that a page which scrolls or cancels any of them keeps the whole gesture to itself.
class WEBVIEW_API OverscrollHistoryNavigation {
public:
    // Positions, deltas and sizes are in device-independent pixels.
    struct WheelEvent {
        Gfx::FloatPoint position;
        Gfx::FloatPoint delta;
        Web::WheelDeltaPrecision precision { Web::WheelDeltaPrecision::Discrete };
        Web::ScrollGesturePhase phase { Web::ScrollGesturePhase::None };
        u32 modifiers { 0 };
    };

    struct HistoryAvailability {
        bool can_go_back { false };
        bool can_go_forward { false };
    };

    static constexpr float start_threshold = 60;
    static constexpr float complete_threshold_fraction_of_viewport = 0.3f;
    static constexpr float minimum_horizontal_to_vertical_ratio = 2.5f;

    // Returns the delta to traverse the history by once the gesture completes a navigation.
    Optional<int> did_finish_handling_wheel_event(WheelEvent const&, Web::EventResult, Gfx::FloatSize viewport_size, HistoryAvailability, MonotonicTime now);

    // A gesture without scroll phases has no event to end it, so it ends once no event has continued it for a while.
    bool is_overscrolling_phase_less_gesture() const;
    Optional<int> did_end_phase_less_gesture(Gfx::FloatSize viewport_size);

    Optional<int> overscroll_direction() const { return m_direction; }

    // How far the overscroll has gone past the start threshold, where 1 is far enough to complete the navigation.
    float overscroll_progress(Gfx::FloatSize viewport_size) const;
    // The progress at which the overscroll would cover the whole viewport.
    static float maximum_overscroll_progress(Gfx::FloatSize viewport_size);

private:
    static float completion_distance(Gfx::FloatSize viewport_size);

    void start_gesture(WheelEvent const&, MonotonicTime now);
    void reset_overscroll();
    Optional<int> complete_gesture(Gfx::FloatSize viewport_size);

    Optional<Compositing::WheelGestureIdentity> m_gesture;
    bool m_gesture_is_phase_less { false };
    // The page has consumed the gesture, or the gesture has completed.
    bool m_ignores_rest_of_gesture { false };
    Gfx::FloatPoint m_overscroll_delta;
    Optional<int> m_direction;
    Optional<int> m_locked_direction;
};

}
