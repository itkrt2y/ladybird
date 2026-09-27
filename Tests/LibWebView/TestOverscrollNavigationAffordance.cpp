/*
 * Copyright (c) 2026-present, the Ladybird developers.
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <LibTest/TestCase.h>
#include <LibWebView/OverscrollNavigationAffordance.h>

using WebView::OverscrollNavigationAffordance;

static AK::Duration milliseconds(i64 value)
{
    return AK::Duration::from_milliseconds(value);
}

TEST_CASE(hidden_until_dragged)
{
    OverscrollNavigationAffordance affordance;
    EXPECT(!affordance.paint_state(MonotonicTime::now()).has_value());
}

TEST_CASE(drag_slides_the_affordance_in_and_grows_the_ripple)
{
    OverscrollNavigationAffordance affordance;
    auto now = MonotonicTime::now();

    affordance.drag(-1, 0.5f, 4);
    auto state = affordance.paint_state(now);
    EXPECT(state.has_value());
    EXPECT(state->points_back);
    EXPECT_APPROXIMATE(state->offset, 0.5f * OverscrollNavigationAffordance::activation_offset);
    EXPECT_APPROXIMATE(state->ripple_radius, 30.f);
    EXPECT(!state->activated);

    affordance.drag(1, 1, 4);
    state = affordance.paint_state(now);
    EXPECT(!state->points_back);
    EXPECT_APPROXIMATE(state->offset, OverscrollNavigationAffordance::activation_offset);
    EXPECT_APPROXIMATE(state->ripple_radius, OverscrollNavigationAffordance::maximum_ripple_radius);
    EXPECT(state->activated);
}

TEST_CASE(drag_past_the_threshold_slides_a_limited_extra_distance)
{
    OverscrollNavigationAffordance affordance;
    affordance.drag(-1, 100, 4);
    auto state = affordance.paint_state(MonotonicTime::now());
    EXPECT_APPROXIMATE(state->offset, OverscrollNavigationAffordance::activation_offset + OverscrollNavigationAffordance::extra_offset);
}

TEST_CASE(abort_retracts_the_affordance_in_proportion_to_its_offset)
{
    OverscrollNavigationAffordance affordance;
    auto now = MonotonicTime::now();
    affordance.drag(-1, 0.5f, 4);
    affordance.abort(now);

    EXPECT(affordance.is_animating(now));
    auto state = affordance.paint_state(now + milliseconds(75));
    EXPECT(state.has_value());
    EXPECT(state->offset < 0.5f * OverscrollNavigationAffordance::activation_offset);
    EXPECT(!affordance.is_animating(now + milliseconds(150)));
    EXPECT(!affordance.paint_state(now + milliseconds(150)).has_value());
}

TEST_CASE(complete_bursts_and_fades_out)
{
    OverscrollNavigationAffordance affordance;
    auto now = MonotonicTime::now();
    affordance.drag(-1, 1.2f, 4);
    affordance.complete(now);

    auto state = affordance.paint_state(now + milliseconds(100));
    EXPECT(state.has_value());
    EXPECT(state->activated);
    EXPECT(state->ripple_radius > OverscrollNavigationAffordance::maximum_ripple_radius);
    EXPECT(state->opacity < 1);
    EXPECT(!affordance.paint_state(now + OverscrollNavigationAffordance::burst_duration).has_value());
}

TEST_CASE(drag_replaces_a_running_animation)
{
    OverscrollNavigationAffordance affordance;
    auto now = MonotonicTime::now();
    affordance.drag(-1, 1, 4);
    affordance.abort(now);
    affordance.drag(1, 0.25f, 4);

    EXPECT(!affordance.is_animating(now));
    auto state = affordance.paint_state(now + milliseconds(1000));
    EXPECT(state.has_value());
    EXPECT(!state->points_back);
}
