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

// The window-hierarchy walk that minimize uses. labwc minimizes the root of
// the tree and then every sub-view (src/view.c:784-816), so "which entry is
// the root" has to be right from any member of the hierarchy.
TEST(MinimizeRoot, AWindowWithNoParentIsItsOwnRoot)
{
	const int parent_index[] = {-1, -1, -1};
	EXPECT_EQ(minimize_root_index(parent_index, 3, 0), 0);
	EXPECT_EQ(minimize_root_index(parent_index, 3, 2), 2);
}

TEST(MinimizeRoot, ADialogResolvesToItsToplevel)
{
	// 2 is a dialog whose parent is 1, which is a dialog of 0. The root is
	// 0.
	const int parent_index[] = {-1, 0, 1};
	EXPECT_EQ(minimize_root_index(parent_index, 3, 2), 0);
	EXPECT_EQ(minimize_root_index(parent_index, 3, 1), 0);
	EXPECT_EQ(minimize_root_index(parent_index, 3, 0), 0);
}

TEST(MinimizeRoot, OutOfRangeIndexYieldsNone)
{
	const int parent_index[] = {-1, 0};
	EXPECT_EQ(minimize_root_index(parent_index, 2, -1), -1);
	EXPECT_EQ(minimize_root_index(parent_index, 2, 2), -1);
	EXPECT_EQ(minimize_root_index(nullptr, 2, 0), -1);
}

TEST(MinimizeRoot, ACycleTerminatesInsteadOfHanging)
{
	// The XML promises no loops in the window tree, but a malformed tree
	// must not hang the event loop: the walk is bounded by the entry count.
	const int parent_index[] = {1, 0};
	EXPECT_GE(minimize_root_index(parent_index, 2, 0), 0);
	EXPECT_GE(minimize_root_index(parent_index, 2, 1), 0);
}

// "Restore the most recently minimized hierarchy" must select by minimize
// order, not by array position: View::remove_window() swaps the last entry
// into a freed slot, so array order is not minimize order.
TEST(MinimizeOrder, PicksTheHighestSequence)
{
	const int parent_index[] = {-1, -1, -1};
	const bool minimized[] = {true, true, false};
	const uint64_t sequence[] = {7, 9, 0};
	// Index 1 was minimized last (9), so it comes back even though it is
	// not the highest index.
	EXPECT_EQ(
	    most_recently_minimized_root(parent_index, minimized, sequence, 3),
	    1);
}

TEST(MinimizeOrder, IgnoresWindowsMinimizedOutsideTheAction)
{
	// A sequence of 0 means "not minimized through the action", so it
	// cannot be the most recent one.
	const int parent_index[] = {-1, -1};
	const bool minimized[] = {true, true};
	const uint64_t sequence[] = {0, 0};
	EXPECT_EQ(
	    most_recently_minimized_root(parent_index, minimized, sequence, 2),
	    -1);
}

TEST(MinimizeOrder, NothingMinimizedYieldsNone)
{
	const int parent_index[] = {-1, -1};
	const bool minimized[] = {false, false};
	const uint64_t sequence[] = {3, 5};
	EXPECT_EQ(
	    most_recently_minimized_root(parent_index, minimized, sequence, 2),
	    -1);
}

TEST(MinimizeOrder, ReturnsTheRootOfTheChosenHierarchy)
{
	// 2 is a dialog of 1; 1 is the root and was minimized last. The root
	// comes back, not the dialog, so the whole hierarchy is restored.
	const int parent_index[] = {-1, -1, 1};
	const bool minimized[] = {false, true, true};
	const uint64_t sequence[] = {0, 4, 4};
	EXPECT_EQ(
	    most_recently_minimized_root(parent_index, minimized, sequence, 3),
	    1);
}

TEST(MinimizeOrder, EmptyViewYieldsNone)
{
	EXPECT_EQ(most_recently_minimized_root(nullptr, nullptr, nullptr, 0),
		  -1);
}
