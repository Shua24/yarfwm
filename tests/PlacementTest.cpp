// Unit tests for the pure placement arithmetic.
//
// These need no Wayland connection and no river session: the whole point of
// src/Placement.cpp is that the cascade step and the directional-focus scoring
// can be tested in isolation. See .hermes/plans for the interactive-move plan
// these back.
#include <gtest/gtest.h>

#include "Placement.hpp"

TEST(Cascade, AdvancesByThirtyTwo)
{
	const Rectangle area{0, 0, 1280, 720};
	int32_t next_x = 0;
	int32_t next_y = 0;
	cascade_next(area, 0, 0, &next_x, &next_y);
	EXPECT_EQ(next_x, 32);
	EXPECT_EQ(next_y, 32);
}

TEST(Cascade, WrapsInsideTheArea)
{
	const Rectangle area{0, 0, 1280, 720};
	// The wrap threshold is area.x + width - 64 = 1216, so a step from
	// x=1200 lands past it and wraps back to the area origin.
	int32_t next_x = 0;
	int32_t next_y = 0;
	cascade_next(area, 1200, 700, &next_x, &next_y);
	EXPECT_EQ(next_x, 0);
	EXPECT_EQ(next_y, 0);
}

TEST(Cascade, RespectsANonZeroAreaOrigin)
{
	// A top bar of 37px, as waybar produces on a 1280x720 output: the
	// placement area starts at y=37 and is 683 tall. This is the real
	// configuration on the development machine.
	const Rectangle area{0, 37, 1280, 683};
	int32_t next_x = 0;
	int32_t next_y = 0;
	cascade_next(area, 0, 37, &next_x, &next_y);
	EXPECT_EQ(next_x, 32);
	EXPECT_EQ(next_y, 69);
}

TEST(Cascade, DoesNotWrapWhileInsideTheArea)
{
	const Rectangle area{0, 0, 1280, 720};
	// The wrap thresholds are 1216 in x and 656 in y, so a step from
	// (1180, 600) lands at (1212, 632) without wrapping. Note the y
	// threshold bites much sooner than the x one: a start of y=640 would
	// step to 672 and wrap, which is what an earlier version of this test
	// got wrong.
	int32_t next_x = 0;
	int32_t next_y = 0;
	cascade_next(area, 1180, 600, &next_x, &next_y);
	EXPECT_EQ(next_x, 1212);
	EXPECT_EQ(next_y, 632);
}

TEST(Cascade, WrapsInYBeforeX)
{
	const Rectangle area{0, 0, 1280, 720};
	// x stays inside (1212 <= 1216) while y wraps (672 > 656). The two
	// axes are independent, which is easy to get wrong when reasoning
	// about the cascade.
	int32_t next_x = 0;
	int32_t next_y = 0;
	cascade_next(area, 1180, 640, &next_x, &next_y);
	EXPECT_EQ(next_x, 1212);
	EXPECT_EQ(next_y, 0);
}

TEST(DirectionScore, WindowToTheLeftScoresNegativeForRight)
{
	const Rectangle source{100, 100, 200, 200};
	const Rectangle candidate{0, 100, 50, 200};
	EXPECT_LT(direction_score(source, candidate, focus_direction_right),
		  0.0);
}

TEST(DirectionScore, StraightLeftScoresTheCentreDistance)
{
	const Rectangle source{100, 100, 100, 100};
	const Rectangle candidate{0, 100, 100, 100};
	// Centres are (150,150) and (50,150): 100 along, no sideways offset.
	EXPECT_DOUBLE_EQ(
	    direction_score(source, candidate, focus_direction_left), 100.0);
}

TEST(DirectionScore, SidewaysOffsetIsHalved)
{
	const Rectangle source{100, 100, 100, 100};
	const Rectangle candidate{0, 300, 100, 100};
	// Centres (150,150) and (50,350): 100 along, 200 sideways -> 200.
	EXPECT_DOUBLE_EQ(
	    direction_score(source, candidate, focus_direction_left), 200.0);
}

TEST(DirectionScore, StraightUpScoresTheCentreDistance)
{
	const Rectangle source{100, 300, 100, 100};
	const Rectangle candidate{100, 100, 100, 100};
	// Centres (150,350) and (150,150): 200 along, no sideways offset.
	EXPECT_DOUBLE_EQ(direction_score(source, candidate, focus_direction_up),
			 200.0);
}

TEST(DirectionScore, WindowBelowScoresNegativeForUp)
{
	const Rectangle source{100, 100, 100, 100};
	const Rectangle candidate{100, 400, 100, 100};
	EXPECT_LT(direction_score(source, candidate, focus_direction_up), 0.0);
}
