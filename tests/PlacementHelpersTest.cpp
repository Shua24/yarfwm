// Unit tests for the two placement helpers extracted from the View: the
// desktop wrap-around (refactor R2) and the dimension-hint clamp (R3).
#include <gtest/gtest.h>

#include "Placement.hpp"

TEST(WrapDesktop, KeepsIndicesInRange)
{
	EXPECT_EQ(wrap_desktop(0, 5), 0);
	EXPECT_EQ(wrap_desktop(4, 5), 4);
	EXPECT_EQ(wrap_desktop(5, 5), 0);
}

TEST(WrapDesktop, WrapsLeftFromZeroToTheLastDesktop)
{
	// A switch left from desktop 0 must land on the last desktop, not on
	// -1.
	EXPECT_EQ(wrap_desktop(-1, 5), 4);
	EXPECT_EQ(wrap_desktop(-5, 5), 0);
	EXPECT_EQ(wrap_desktop(-6, 5), 4);
}

TEST(WrapDesktop, NonPositiveCountYieldsZero)
{
	EXPECT_EQ(wrap_desktop(3, 0), 0);
	EXPECT_EQ(wrap_desktop(3, -1), 0);
}

TEST(DimensionHints, MinimumClampsUp)
{
	Rectangle geometry{0, 0, 10, 10};
	apply_dimension_hints(200, 200, 0, 0, geometry);
	EXPECT_EQ(geometry.width, 200);
	EXPECT_EQ(geometry.height, 200);
}

TEST(DimensionHints, MaximumClampsDown)
{
	Rectangle geometry{0, 0, 5000, 5000};
	apply_dimension_hints(0, 0, 1000, 1000, geometry);
	EXPECT_EQ(geometry.width, 1000);
	EXPECT_EQ(geometry.height, 1000);
}

TEST(DimensionHints, InBoundsIsUntouched)
{
	Rectangle geometry{0, 0, 300, 300};
	apply_dimension_hints(100, 100, 500, 500, geometry);
	EXPECT_EQ(geometry.width, 300);
	EXPECT_EQ(geometry.height, 300);
}

TEST(DimensionHints, ZeroMeansNoPreference)
{
	Rectangle geometry{0, 0, 50, 50};
	apply_dimension_hints(0, 0, 0, 0, geometry);
	EXPECT_EQ(geometry.width, 50);
	EXPECT_EQ(geometry.height, 50);
}

TEST(DimensionHints, ContradictoryBoundsResolveMaxLast)
{
	// The minimum is applied first, so a maximum below it wins.
	Rectangle geometry{0, 0, 50, 50};
	apply_dimension_hints(300, 300, 100, 100, geometry);
	EXPECT_EQ(geometry.width, 100);
	EXPECT_EQ(geometry.height, 100);
}
