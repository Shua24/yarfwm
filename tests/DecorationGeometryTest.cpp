#include "DecorationGeometry.hpp"
#include <gtest/gtest.h>

// labwc's defaults (src/theme.c:534,558-560; titlebar_height derived at
// theme.c:1639-1649): 26px buttons, no spacing, border_width 1, and a 26px
// titlebar when the font is shorter than the buttons. They are written out at
// each call site rather than returned from a helper: the project builds with
// -Waggregate-return, so a function returning TitlebarMetrics by value would
// add a warning per call.
//
//   TitlebarMetrics{width, height, button_width, button_height,
//                   button_spacing, padding}

// Fullscreen always loses the titlebar. Maximized is a config choice, and the
// default is to KEEP it so the buttons stay reachable.
TEST(TitlebarVisibility, FullscreenNeverHasATitlebar)
{
	EXPECT_FALSE(decoration_titlebar_visible(false, true, true));
	EXPECT_FALSE(decoration_titlebar_visible(true, true, true));
	EXPECT_FALSE(decoration_titlebar_visible(true, true, false));
}

TEST(TitlebarVisibility, MaximizedKeepsTheTitlebarByDefault)
{
	EXPECT_TRUE(decoration_titlebar_visible(true, false, true));
}

TEST(TitlebarVisibility, MaximizedCanHideTheTitlebarWhenConfigured)
{
	EXPECT_FALSE(decoration_titlebar_visible(true, false, false));
}

TEST(TitlebarVisibility, AnUnmaximizedWindowAlwaysHasATitlebar)
{
	EXPECT_TRUE(decoration_titlebar_visible(false, false, true));
	EXPECT_TRUE(decoration_titlebar_visible(false, false, false));
}

TEST(TitlebarLayout, AllThreeButtonsFitInAWideWindow)
{
	const TitlebarMetrics metrics{800, 26, 26, 26, 0, 0};
	EXPECT_EQ(titlebar_visible_button_count(metrics), 3);
}

TEST(TitlebarLayout, ButtonsAreLaidOutBackwardsFromTheRightEdge)
{
	// 800 wide, 26px buttons, no spacing: close is rightmost at 774,
	// maximize at 748, minimize at 722.
	const TitlebarMetrics metrics{800, 26, 26, 26, 0, 0};
	EXPECT_EQ(titlebar_button_left(metrics, 3, 2), 800 - 26); // close
	EXPECT_EQ(titlebar_button_left(metrics, 3, 1), 800 - 52); // maximize
	EXPECT_EQ(titlebar_button_left(metrics, 3, 0), 800 - 78); // minimize
}

TEST(TitlebarLayout, ANarrowWindowDropsButtonsFromTheRight)
{
	// 60px fits two 26px buttons (52) but not three (78).
	const TitlebarMetrics metrics{60, 26, 26, 26, 0, 0};
	EXPECT_EQ(titlebar_visible_button_count(metrics), 2);
	// The remaining two are the left-most two, so close is gone.
	EXPECT_EQ(titlebar_button_left(metrics, 2, 1), 60 - 26);
	EXPECT_EQ(titlebar_button_left(metrics, 2, 0), 60 - 52);
}

TEST(TitlebarLayout, ZeroVisibleWhenNothingFits)
{
	const TitlebarMetrics metrics{20, 26, 26, 26, 0, 0};
	EXPECT_EQ(titlebar_visible_button_count(metrics), 0);
}

TEST(TitlebarLayout, PaddingOnBothEndsIsCountedTwice)
{
	// 60px wide, 26px buttons, 8px padding at BOTH ends: three buttons
	// need 78 + 16 = 94, two need 52 + 16 = 68, one needs 26 + 16 = 42.
	// So exactly one fits.
	//
	// This case exists because the plan's own control-check ("change
	// padding * 2 to padding and watch this test fail") is a NO-OP with
	// the default padding of 0 -- both spellings agree at zero. Only a
	// non-zero padding exercises the term, and here the two spellings
	// disagree (correct 1, missing-doubling 2).
	const TitlebarMetrics metrics{60, 26, 26, 26, 0, 8};
	EXPECT_EQ(titlebar_visible_button_count(metrics), 1);
}

TEST(TitlebarLayout, TextBandIsTheSpaceTheButtonsLeave)
{
	const TitlebarMetrics metrics{800, 26, 26, 26, 0, 0};
	int32_t left = 0;
	int32_t right = 0;
	ASSERT_TRUE(titlebar_text_band(metrics, 3, &left, &right));
	EXPECT_EQ(left, 0);
	EXPECT_EQ(right, 800 - 78);
}

TEST(TitlebarLayout, TextBandIsEmptyWhenTheButtonsFillTheBar)
{
	const TitlebarMetrics metrics{52, 26, 26, 26, 0, 0};
	int32_t left = 0;
	int32_t right = 0;
	EXPECT_FALSE(titlebar_text_band(metrics, 2, &left, &right));
}

TEST(TitlebarHitTest, ButtonsWinOverTheBar)
{
	const TitlebarMetrics metrics{800, 26, 26, 26, 0, 0};
	// Inside the close button's square.
	EXPECT_EQ(titlebar_part_at(metrics, 3, TitlebarPoint{780, 13}),
		  titlebar_part_button_close);
	// Inside the maximize button.
	EXPECT_EQ(titlebar_part_at(metrics, 3, TitlebarPoint{750, 13}),
		  titlebar_part_button_maximize);
	// Inside the minimize button.
	EXPECT_EQ(titlebar_part_at(metrics, 3, TitlebarPoint{724, 13}),
		  titlebar_part_button_minimize);
}

TEST(TitlebarHitTest, TheGapBetweenButtonsIsTheBar)
{
	// No spacing configured, but the 1px boundary must not fall inside a
	// button: the right edge of the minimize button belongs to the bar.
	const TitlebarMetrics metrics{800, 26, 26, 26, 0, 0};
	EXPECT_EQ(titlebar_part_at(metrics, 3, TitlebarPoint{721, 13}),
		  titlebar_part_bar);
}

TEST(TitlebarHitTest, OutsideTheSurfaceIsNone)
{
	const TitlebarMetrics metrics{800, 26, 26, 26, 0, 0};
	EXPECT_EQ(titlebar_part_at(metrics, 3, TitlebarPoint{-1, 13}),
		  titlebar_part_none);
	EXPECT_EQ(titlebar_part_at(metrics, 3, TitlebarPoint{800, 13}),
		  titlebar_part_none);
	EXPECT_EQ(titlebar_part_at(metrics, 3, TitlebarPoint{400, 26}),
		  titlebar_part_none);
	EXPECT_EQ(titlebar_part_at(metrics, 3, TitlebarPoint{400, -1}),
		  titlebar_part_none);
}

// The titlebar must never cover the window it decorates. The bar is placed
// outside the content -- above it, or below it when there is no room above --
// and it is not painted at all when it fits nowhere. An earlier version drew
// the bar inside the content's top edge whenever the area left no room above,
// which covered the top titlebar_height rows of every window placed at the area
// origin. That was measured on a real window: the content's top 26 rows were
// titlebar pixels.
//
// The one exception is a MAXIMIZED window, which opts into the overlap
// placement: it fills the whole area, so it would otherwise lose its bar
// entirely. Every other caller passes allow_overlap = false and keeps the
// strict no-cover guarantee.

// The bar clears the compositor's border as well as the content. river draws
// the border OUTSIDE the content -- the top border occupies the border_width
// rows immediately above the content's first row (river/Window.zig:1011-1016)
// -- and it draws above-decorations LAST, so the bar wins every row it covers
// (river-window-management-v1.xml:1199-1202). Measured before this was
// handled: with the bar at -26 and a 1px border, the focused window's top
// border row had 0 of 640 border pixels.
static const int32_t border = 1;

TEST(TitlebarOffset, SitsAboveTheContentAndSpansItsWidth)
{
	// A cascade-placed window with room above it: the ordinary case.
	const Rectangle content{64, 64, 636, 352};
	const Rectangle area{0, 0, 1280, 720};
	int32_t x = 123;
	int32_t y = 123;
	const TitlebarPlacement where =
	    titlebar_offset(26, border, content, area, false, &x, &y);
	EXPECT_EQ(where, titlebar_placement_above);
	EXPECT_EQ(x, 0);
	// 26 for the bar plus 1 for the border row the bar must clear.
	EXPECT_EQ(y, -27);
	// The bar's bottom row is at content.y - border - 1 and the border's
	// top row is at content.y - border, so neither the content nor the
	// border row is covered.
	EXPECT_EQ(content.y + y, content.y - border - 26 + 0);
	EXPECT_LE(content.y + y + 26, content.y - border);
}

TEST(TitlebarOffset, AWindowFlushWithTheAreaTopGetsTheBarBelowItNotOverIt)
{
	// The maximized-into-the-area-top case. The bar must not be drawn over
	// the content's top edge -- that was the bug -- and it must not be put
	// at y = -26 either, where the output's top edge would clip it away
	// entirely. With room below the content it goes there.
	const Rectangle content{0, 0, 1280, 660};
	const Rectangle area{0, 0, 1280, 720};
	int32_t x = 123;
	int32_t y = 123;
	const TitlebarPlacement where =
	    titlebar_offset(26, border, content, area, false, &x, &y);
	EXPECT_EQ(where, titlebar_placement_below);
	EXPECT_EQ(x, 0);
	// The bar's top edge is past the content's bottom border row: the
	// bottom border occupies [height, height + border_width).
	EXPECT_EQ(y, 660 + border);
	EXPECT_GE(content.y + y, content.y + content.height + border);
}

TEST(TitlebarOffset, NoRoomOnEitherSideMeansNoBarRatherThanACoveredWindow)
{
	// A window filling the whole area: no room above, no room below. The
	// bar is not painted, so the window keeps every pixel of its content.
	const Rectangle content{0, 0, 1280, 720};
	const Rectangle area{0, 0, 1280, 720};
	int32_t x = 123;
	int32_t y = 123;
	EXPECT_EQ(titlebar_offset(26, border, content, area, false, &x, &y),
		  titlebar_placement_hidden);
	// The offset is still defined even though nothing is painted: river
	// keeps the last set_offset it saw, so a stale one would put the
	// surface back over the window.
	EXPECT_EQ(x, 0);
	EXPECT_EQ(y, 0);
}

TEST(TitlebarOffset, AMaximizedWindowMayKeepItsBarByCoveringTheContentTop)
{
	// The same nowhere-to-go geometry as the test above, but with the
	// overlap exception a MAXIMIZED window opts into. The bar is kept and
	// drawn at the very top of the content, covering exactly the top
	// titlebar_height rows.
	//
	// Why a maximized window is allowed this when nothing else is: it
	// fills the whole placement area, so there is no outside left to put a
	// bar in, and losing the bar costs its buttons and its identity. The
	// covered rows are also not the user's content in the way a floating
	// window's would be -- the window asked to be maximized.
	const Rectangle content{0, 0, 1280, 720};
	const Rectangle area{0, 0, 1280, 720};
	int32_t x = 123;
	int32_t y = 123;
	EXPECT_EQ(titlebar_offset(26, border, content, area, true, &x, &y),
		  titlebar_placement_overlap);
	// Offset 0 puts the surface's first row on the content's first row, so
	// it covers [content.y, content.y + 26).
	EXPECT_EQ(x, 0);
	EXPECT_EQ(y, 0);
	// The covered band is inside the content, not above it.
	EXPECT_GE(content.y + y, content.y);
	EXPECT_LE(content.y + y + 26, content.y + content.height);
}

TEST(TitlebarOffset, OverlapIsNotUsedWhenTheBarFitsOutside)
{
	// allow_overlap must not change the ordinary cases: a window that
	// still has room outside gets its bar outside like anything else. The
	// exception is a last resort, not a mode.
	const Rectangle area{0, 0, 1280, 720};
	int32_t x = 0;
	int32_t y = 0;

	// Room above: still above, and the bar covers nothing.
	EXPECT_EQ(titlebar_offset(26, border, Rectangle{0, 64, 640, 300}, area,
				  true, &x, &y),
		  titlebar_placement_above);
	EXPECT_EQ(y, -27);

	// No room above but room below: still below, not overlapping.
	EXPECT_EQ(titlebar_offset(26, border, Rectangle{0, 0, 640, 600}, area,
				  true, &x, &y),
		  titlebar_placement_below);
	EXPECT_EQ(y, 600 + border);
}

TEST(TitlebarOffset, NeedsTheFullHeightOfRoomAboveNotJustOnePixel)
{
	const Rectangle area{0, 0, 1280, 720};
	int32_t x = 0;
	int32_t y = 0;

	// Exactly a bar plus a border row of room: the bar fits, so it goes
	// above, clearing both the content and the border row.
	EXPECT_EQ(titlebar_offset(26, border, Rectangle{0, 27, 640, 300}, area,
				  false, &x, &y),
		  titlebar_placement_above);
	EXPECT_EQ(y, -27);

	// One pixel less room above: the bar may not be clipped at the area's
	// top edge, so it goes below the content instead of over it. The
	// content is 26..325 and the area ends at 719, so there is room below.
	EXPECT_EQ(titlebar_offset(26, border, Rectangle{0, 26, 640, 300}, area,
				  false, &x, &y),
		  titlebar_placement_below);
	EXPECT_EQ(y, 300 + border);
}

TEST(TitlebarOffset, AZeroHeightBarIsNeverPainted)
{
	const Rectangle content{64, 64, 636, 352};
	const Rectangle area{0, 0, 1280, 720};
	int32_t x = 1;
	int32_t y = 1;
	EXPECT_EQ(titlebar_offset(0, border, content, area, false, &x, &y),
		  titlebar_placement_hidden);
	EXPECT_EQ(x, 0);
	EXPECT_EQ(y, 0);
}

TEST(TitlebarOffset, AnAreaWithABarAboveItKeepsTheTitlebarOnScreen)
{
	// A top panel: the placement area starts at y=37. A window flush with
	// the area top has no room above it, so the bar goes below the content
	// -- still on screen, and still not covering the window.
	const Rectangle area{0, 37, 1280, 683};
	const Rectangle maximized{0, 37, 1280, 683};
	int32_t x = 0;
	int32_t y = 0;
	// The content is 37..719 and the area ends at 719, so there is no room
	// below either: no bar.
	EXPECT_EQ(titlebar_offset(26, border, maximized, area, false, &x, &y),
		  titlebar_placement_hidden);

	// A floating window lower down has room, so it keeps the normal offset.
	EXPECT_EQ(titlebar_offset(26, border, Rectangle{0, 200, 640, 300}, area,
				  false, &x, &y),
		  titlebar_placement_above);
	EXPECT_EQ(y, -27);
}

TEST(TitlebarOffset, AThickBorderPushesTheBarFurtherUp)
{
	// The bar clears the whole border, so a 4px border moves it 4px up. The
	// rows the bar must not cover are the border's, and river draws it
	// outside the content.
	const Rectangle content{64, 64, 636, 352};
	const Rectangle area{0, 0, 1280, 720};
	int32_t x = 0;
	int32_t y = 0;
	EXPECT_EQ(titlebar_offset(26, 4, content, area, false, &x, &y),
		  titlebar_placement_above);
	EXPECT_EQ(y, -30);
}

TEST(TitlebarOffset, NoBorderLeavesTheBarDirectlyAboveTheContent)
{
	// With borders off the bar only has to clear the content, which is the
	// plain -height offset.
	const Rectangle content{64, 64, 636, 352};
	const Rectangle area{0, 0, 1280, 720};
	int32_t x = 0;
	int32_t y = 0;
	EXPECT_EQ(titlebar_offset(26, 0, content, area, false, &x, &y),
		  titlebar_placement_above);
	EXPECT_EQ(y, -26);
}

// The reserved strip is what gives the bar its room: placement puts windows in
// the reserved area, so a window at its origin has exactly titlebar_height of
// space above it and the bar covers nothing.

TEST(ReservedStrip, ReservesTheWholeFrameTopAtTheTopOfTheArea)
{
	// The caller passes titlebar_height + border_width: the bar needs the
	// border rows above the content as well.
	const Rectangle area{0, 0, 1280, 720};
	Rectangle content{0, 0, 0, 0};
	reserve_titlebar_strip(area, 26 + border, &content);
	EXPECT_EQ(content.x, 0);
	EXPECT_EQ(content.y, 27);
	EXPECT_EQ(content.width, 1280);
	EXPECT_EQ(content.height, 720 - 27);

	// A window placed at the reserved origin has exactly the room
	// titlebar_offset() requires, so the bar clears content and border.
	int32_t x = 0;
	int32_t y = 0;
	EXPECT_EQ(titlebar_offset(26, border, content, area, false, &x, &y),
		  titlebar_placement_above);
	EXPECT_EQ(y, -27);
	EXPECT_EQ(content.y + y + 26, content.y - border);
}

TEST(ReservedStrip, AnAreaWithABarAboveItReservesBelowThePanel)
{
	// The placement area starts at y=37 because a panel owns the top of the
	// output. The strip goes at the top of THAT area, not of the output.
	const Rectangle area{0, 37, 1280, 683};
	Rectangle content{0, 0, 0, 0};
	reserve_titlebar_strip(area, 26 + border, &content);
	EXPECT_EQ(content.y, 37 + 26 + border);
	EXPECT_EQ(content.height, 683 - 26 - border);
}

TEST(ReservedStrip, ReservesNothingWhenTheBarIsOff)
{
	const Rectangle area{0, 0, 1280, 720};
	Rectangle content{0, 0, 0, 0};
	reserve_titlebar_strip(area, 0, &content);
	EXPECT_EQ(content.y, 0);
	EXPECT_EQ(content.height, 720);
}

TEST(ReservedStrip, NeverTakesMoreThanHalfOfATinyArea)
{
	// An area only 40px tall cannot give the frame top its own strip and
	// still leave a usable window, so the reservation is clamped to half.
	// Windows are never pushed outside the area.
	const Rectangle area{0, 0, 640, 40};
	Rectangle content{0, 0, 0, 0};
	reserve_titlebar_strip(area, 26 + border, &content);
	EXPECT_EQ(content.y, 20);
	EXPECT_EQ(content.height, 20);
}

TEST(ReservedStrip, ReservesNothingForAnEmptyArea)
{
	const Rectangle area{0, 0, 640, 0};
	Rectangle content{0, 0, 0, 0};
	reserve_titlebar_strip(area, 26, &content);
	EXPECT_EQ(content.height, 0);
}
