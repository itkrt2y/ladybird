/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibTest/TestCase.h>
#include <LibWebCommon/UIEvents/KeyCode.h>
#include <LibWebView/OverscrollHistoryNavigation.h>

using Web::ScrollGesturePhase;
using WebView::OverscrollHistoryNavigation;

static constexpr Gfx::FloatSize viewport_size { 1000, 800 };
static constexpr OverscrollHistoryNavigation::HistoryAvailability both_directions { true, true };

struct Swipe {
    OverscrollHistoryNavigation navigation;
    MonotonicTime now { MonotonicTime::now() };

    Optional<int> step(Gfx::FloatPoint delta, Web::EventResult result = Web::EventResult::Accepted, ScrollGesturePhase phase = ScrollGesturePhase::Ongoing, OverscrollHistoryNavigation::HistoryAvailability availability = both_directions)
    {
        now = now + AK::Duration::from_milliseconds(10);
        return navigation.did_finish_handling_wheel_event({ { 100, 100 }, delta, Web::WheelDeltaPrecision::Precise, phase, 0 }, result, viewport_size, availability, now);
    }

    Optional<int> end()
    {
        return step({}, Web::EventResult::Accepted, ScrollGesturePhase::Ended);
    }
};

TEST_CASE(unconsumed_horizontal_swipe_past_the_threshold_navigates)
{
    Swipe swipe;
    for (int i = 0; i < 10; ++i)
        EXPECT(!swipe.step({ -40, 0 }).has_value());
    EXPECT_EQ(swipe.navigation.overscroll_direction(), -1);
    EXPECT_EQ(swipe.end(), -1);

    Swipe forward_swipe;
    for (int i = 0; i < 10; ++i)
        (void)forward_swipe.step({ 40, 0 });
    EXPECT_EQ(forward_swipe.end(), 1);
}

TEST_CASE(swipe_short_of_the_completion_threshold_does_not_navigate)
{
    Swipe swipe;
    // 290 pixels, less than 30% of the larger viewport dimension.
    for (int i = 0; i < 29; ++i)
        (void)swipe.step({ -10, 0 });
    EXPECT_EQ(swipe.navigation.overscroll_direction(), -1);
    EXPECT(!swipe.end().has_value());
}

TEST_CASE(swipe_within_the_start_threshold_does_not_overscroll)
{
    Swipe swipe;
    for (int i = 0; i < 6; ++i)
        (void)swipe.step({ -10, 0 });
    EXPECT(!swipe.navigation.overscroll_direction().has_value());
}

TEST_CASE(diagonal_swipe_does_not_overscroll)
{
    Swipe swipe;
    for (int i = 0; i < 10; ++i)
        (void)swipe.step({ -40, -20 });
    EXPECT(!swipe.navigation.overscroll_direction().has_value());
    EXPECT(!swipe.end().has_value());
}

TEST_CASE(gesture_the_page_consumed_does_not_navigate)
{
    Swipe swipe;
    (void)swipe.step({ -40, 0 }, Web::EventResult::Handled);
    for (int i = 0; i < 10; ++i)
        (void)swipe.step({ -40, 0 });
    EXPECT(!swipe.end().has_value());

    Swipe cancelled_swipe;
    (void)cancelled_swipe.step({ -40, 0 }, Web::EventResult::Cancelled);
    for (int i = 0; i < 10; ++i)
        (void)cancelled_swipe.step({ -40, 0 });
    EXPECT(!cancelled_swipe.end().has_value());
}

TEST_CASE(dropped_events_neither_consume_nor_overscroll)
{
    Swipe swipe;
    for (int i = 0; i < 10; ++i)
        (void)swipe.step({ -40, 0 }, Web::EventResult::Dropped);
    EXPECT(!swipe.navigation.overscroll_direction().has_value());
    for (int i = 0; i < 10; ++i)
        (void)swipe.step({ -40, 0 });
    EXPECT_EQ(swipe.end(), -1);
}

TEST_CASE(swipe_does_not_overscroll_towards_unavailable_history)
{
    Swipe swipe;
    for (int i = 0; i < 10; ++i)
        (void)swipe.step({ -40, 0 }, Web::EventResult::Accepted, ScrollGesturePhase::Ongoing, { false, true });
    EXPECT(!swipe.navigation.overscroll_direction().has_value());
    EXPECT(!swipe.end().has_value());
}

TEST_CASE(swipe_that_reverses_does_not_navigate_the_other_way)
{
    Swipe swipe;
    for (int i = 0; i < 10; ++i)
        (void)swipe.step({ -40, 0 });
    for (int i = 0; i < 20; ++i)
        (void)swipe.step({ 40, 0 });
    EXPECT(!swipe.navigation.overscroll_direction().has_value());
    EXPECT(!swipe.end().has_value());
}

TEST_CASE(momentum_after_a_completed_gesture_does_not_navigate_again)
{
    Swipe swipe;
    for (int i = 0; i < 10; ++i)
        (void)swipe.step({ -40, 0 });
    EXPECT_EQ(swipe.end(), -1);
    for (int i = 0; i < 20; ++i)
        EXPECT(!swipe.step({ -40, 0 }, Web::EventResult::Accepted, ScrollGesturePhase::Momentum).has_value());
    EXPECT(!swipe.end().has_value());
}

TEST_CASE(momentum_does_not_start_an_overscroll)
{
    Swipe swipe;
    (void)swipe.step({ -1, 0 });
    for (int i = 0; i < 20; ++i)
        (void)swipe.step({ -40, 0 }, Web::EventResult::Accepted, ScrollGesturePhase::Momentum);
    EXPECT(!swipe.navigation.overscroll_direction().has_value());
}

TEST_CASE(discrete_and_modified_wheel_events_are_ignored)
{
    OverscrollHistoryNavigation navigation;
    auto now = MonotonicTime::now();
    for (int i = 0; i < 10; ++i) {
        now = now + AK::Duration::from_milliseconds(10);
        (void)navigation.did_finish_handling_wheel_event({ { 100, 100 }, { -40, 0 }, Web::WheelDeltaPrecision::Discrete, ScrollGesturePhase::None, 0 }, Web::EventResult::Accepted, viewport_size, both_directions, now);
        (void)navigation.did_finish_handling_wheel_event({ { 100, 100 }, { -40, 0 }, Web::WheelDeltaPrecision::Precise, ScrollGesturePhase::Ongoing, static_cast<u32>(Web::UIEvents::KeyModifier::Mod_Shift) }, Web::EventResult::Accepted, viewport_size, both_directions, now);
    }
    EXPECT(!navigation.overscroll_direction().has_value());
}

TEST_CASE(phase_less_gesture_navigates_when_it_ends)
{
    Swipe swipe;
    for (int i = 0; i < 10; ++i)
        (void)swipe.step({ -40, 0 }, Web::EventResult::Accepted, ScrollGesturePhase::None);
    EXPECT(swipe.navigation.is_overscrolling_phase_less_gesture());
    EXPECT_EQ(swipe.navigation.did_end_phase_less_gesture(viewport_size), -1);
    EXPECT(!swipe.navigation.is_overscrolling_phase_less_gesture());
    EXPECT(!swipe.navigation.did_end_phase_less_gesture(viewport_size).has_value());
}

TEST_CASE(gesture_end_the_page_handled_still_navigates)
{
    Swipe swipe;
    for (int i = 0; i < 10; ++i)
        (void)swipe.step({ -40, 0 });
    EXPECT_EQ(swipe.step({}, Web::EventResult::Handled, ScrollGesturePhase::Ended), -1);
}

TEST_CASE(handled_step_during_an_overscroll_does_not_cancel_it)
{
    Swipe swipe;
    for (int i = 0; i < 10; ++i)
        (void)swipe.step({ -40, 0 });
    (void)swipe.step({ -40, 0 }, Web::EventResult::Handled);
    EXPECT_EQ(swipe.navigation.overscroll_direction(), -1);
    EXPECT_EQ(swipe.end(), -1);
}
