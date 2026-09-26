#ifndef DECORATION_GEOMETRY_HPP
#define DECORATION_GEOMETRY_HPP

#include "Placement.hpp"
#include <cstdint>

// Which part of a titlebar a point falls in. The order matters for hit
// testing: buttons are checked before the bar, so a click on a button is
// never mistaken for a drag.
enum TitlebarPart {
	titlebar_part_none = 0,
	titlebar_part_button_minimize,
	titlebar_part_button_maximize,
	titlebar_part_button_close,
	titlebar_part_bar,
};

// The three buttons, in the order they are drawn from the LEFT edge of the
// titlebar. labwc's default layout is "menu:iconify,max,close" (its
// src/config/rcxml.c:1899): the left group holds the menu/icon button and the
// right group holds iconify, max, close in that order, laid out backwards from
// the right edge. yarfwm has no window menu, so the left group is empty and
// this list is the right group, left to right.
enum TitlebarButton {
	titlebar_button_minimize = 0,
	titlebar_button_maximize,
	titlebar_button_close,
	titlebar_button_count,
};

// A point in the titlebar surface's own coordinate space: (0,0) is the
// surface's top-left corner.
struct TitlebarPoint {
	int32_t x;
	int32_t y;
};

// Layout inputs. Every field is a positive pixel count; the caller resolves
// them from the config (Task 6) before calling anything here.
struct TitlebarMetrics {
	int32_t width;	// surface width, = the content width
	int32_t height; // titlebar height, the surface height
	int32_t button_width;
	int32_t button_height;
	int32_t button_spacing;
	int32_t padding; // horizontal padding at both ends
};

// How many buttons fit, and therefore which are drawn. A window too narrow for
// all three drops from the right group first (the rightmost, close, goes
// first) -- labwc's loop drops from the larger side and ties drop a right-hand
// button (src/ssd/ssd-titlebar.c:240-247).
int titlebar_visible_button_count(const TitlebarMetrics &metrics);

// The x of a button's left edge, given how many buttons are drawn. Buttons are
// laid out backwards from (width - padding), so button index 0 is the
// left-most drawn button.
int32_t titlebar_button_left(const TitlebarMetrics &metrics,
			     int visible_button_count, int button_index);

// The title text band: [left, right) inside the surface, i.e. the space the
// buttons leave over. Returns false when there is no room at all, in which
// case the title must not be drawn.
bool titlebar_text_band(const TitlebarMetrics &metrics,
			int visible_button_count, int32_t *left,
			int32_t *right);

// Hit test. Buttons win over the bar; anything outside the surface is none.
TitlebarPart titlebar_part_at(const TitlebarMetrics &metrics,
			      int visible_button_count, TitlebarPoint point);

// Where the titlebar surface ends up relative to the window's CONTENT
// rectangle. Outside the content by default: a decoration that covers the
// window it decorates is the bug this enum exists to prevent. The single
// exception is titlebar_placement_overlap, which a MAXIMIZED window may
// request because it has no outside left to put a bar in.
enum TitlebarPlacement {
	// Outside, above the content's top edge. This is the normal case, and
	// the only one a correctly placed window ever needs: the placement pass
	// reserves the bar's strip at the top of the placement area, so the bar
	// always has room.
	titlebar_placement_above = 0,
	// No room above the content inside the placement area, but there is
	// room
	// below it. The bar goes below the window rather than over it, so its
	// buttons stay reachable.
	titlebar_placement_below,
	// No room outside the content anywhere. The bar is NOT painted; the
	// window keeps every pixel of its content. This is what a window the
	// user dragged flush against the top edge with no room below it gets,
	// and it is deliberately a lost titlebar rather than a covered window.
	titlebar_placement_hidden,
	// No room outside the content anywhere, but the caller asked for the
	// overlap exception (allow_overlap) because the window is MAXIMIZED.
	// The bar is drawn INSIDE the content's top edge, covering the top
	// titlebar_height rows of the window.
	//
	// Why this exists: a maximized window fills the whole placement area,
	// so there is nowhere outside it to put a bar, and hiding the bar costs
	// the user its buttons and its identity. Maximized is also the one case
	// where the covered rows are not "the user's content": the window asked
	// to be maximized, and every other WM keeps some chrome there.
	//
	// It is opt-in per call and only ever set for a maximized window, so
	// the ordinary floating case keeps the strict no-cover guarantee that
	// titlebar_placement_hidden provides.
	titlebar_placement_overlap,
};

// Where the titlebar sits relative to the window's CONTENT rectangle, given
// the placement area the window is being fitted into. The surface spans the
// content width, so the x offset is always 0.
//
// This is the value river_decoration_v1.set_offset takes. The offset is
// measured from the window's top-left corner, which is the CONTENT's top-left
// corner: river's set_borders description says the window's
// "position/dimensions ... refer to the position/dimensions of the window
// content and are unaffected by the presence of borders or decoration
// surfaces".
//
// The placement area is required because the bar must land somewhere legal
// inside it. When the bar does not fit above the content it is placed below
// it, and when it fits nowhere outside the content it is not painted at all --
// see TitlebarPlacement. An earlier version of this function kept the bar on
// screen by drawing it INSIDE the content's top edge, which covered the top
// titlebar_height rows of the window: the bug this contract fixes.
//
// allow_overlap is the one sanctioned way back to covering the content, and
// the caller must only set it for a MAXIMIZED window (a window with nowhere
// outside itself to put a bar). With it false the strict behaviour is
// unchanged: nowhere outside means titlebar_placement_hidden and the window
// keeps every pixel.
//
// border_width is the compositor's border (river_window_v1.set_borders), which
// is drawn OUTSIDE the content: the top border occupies the rows
// [-border_width, 0) relative to the content's top edge (river/Window.zig:1011-
// 1016). The bar has to clear those rows or it hides them -- river draws
// above-decorations last, so the bar wins every row it covers
// (river-window-management-v1.xml:1199-1202). Clearing them is what labwc does
// too: its frame top margin is titlebar_height + border_width
// (labwc src/ssd/ssd.c:74-79), and it insets the maximized content by that
// whole margin (src/view.c:1283-1299).
TitlebarPlacement titlebar_offset(int32_t titlebar_height, int32_t border_width,
				  const Rectangle &content,
				  const Rectangle &placement_area,
				  bool allow_overlap, int32_t *offset_x,
				  int32_t *offset_y);

// The placement area with the frame's top margin reserved at its top, for a
// window that is going to be decorated. Placement puts windows in this
// rectangle rather than in the raw placement area, which is what gives the bar
// room above the content: a window placed at the returned origin has exactly
// top_margin of space above it, so titlebar_offset() returns
// titlebar_placement_above and the bar covers neither the content nor the
// border.
//
// top_margin is the whole frame top: titlebar_height + border_width, the same
// margin labwc insets its maximized windows by (labwc src/ssd/ssd.c:44-50
// returns titlebar_height for a maximized view, and src/view.c:1283-1299 adds
// border.top to y). A caller that passes only the titlebar height leaves the
// bar sitting on the top border row.
//
// A top_margin of 0 (or less) reserves nothing, so an undecorated window still
// gets the whole area.
void reserve_titlebar_strip(const Rectangle &placement_area, int32_t top_margin,
			    Rectangle *content_area);

// Whether a window's titlebar is painted at all. Fullscreen never gets one.
// A window maximized on both axes gets one only when the config asks for it
// (decorations.titlebar_when_maximized, default true).
//
// This MATCHES labwc, whose default is also to keep the titlebar when
// maximized: rc.hide_maximized_window_titlebar is initialised to false in the
// defaults block (labwc src/config/rcxml.c:1588, under `has_run = true`), and
// the rc.xml key that flips it is maximizedDecoration.theme, where only the
// literal "none" sets it true (labwc src/config/rcxml.c:1253-1258). The
// predicate that consumes it is labwc src/view.c:1614-1621.
//
// The border is a SEPARATE decision, and it is NOT hidden when maximized:
// yarfwm keeps river's border on a maximized window (src/ViewDecoration.cpp
// excludes only fullscreen), while labwc disables its own SSD border tree in
// that case (labwc src/ssd/ssd-border.c:58-60). The two differ here on purpose:
// river draws the border itself and only hides it for fullscreen
// (river/Window.zig:945-961), and the border is the focus indication a
// maximized window still needs.
bool decoration_titlebar_visible(bool maximized, bool fullscreen,
				 bool titlebar_when_maximized);

#endif // DECORATION_GEOMETRY_HPP
