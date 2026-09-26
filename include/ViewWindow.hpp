#ifndef VIEW_WINDOW_HPP
#define VIEW_WINDOW_HPP

#include "Placement.hpp"
#include "river-window-management-v1-client-protocol.h"

#include <cstdint>

class View;
class Decoration;

// One managed window. This was View::Window until the decoration feature needed
// room: View.hpp sits at the 500-line limit (INSTRUCTIONS.md:44) and this
// struct is the largest self-contained piece of it. It is still View's nested
// type -- the definition below is `struct View::Window`, which is legal C++ and
// keeps every existing `View::Window` and in-class `Window` reference working
// with no other change.
//
// It must be included at the very END of View.hpp, after the class body: an
// out-of-class definition of a nested type needs the enclosing class complete.
struct View::Window {
	struct river_window_v1 *window;
	struct river_node_v1 *node;
	bool managed;
	// What river believes: true when the last show/hide request
	// for this window was show(). River considers a new window
	// shown until told otherwise, so a new entry starts true.
	// The visibility pass in window_manager_render_start() is
	// the only writer.
	bool shown;
	bool rendered;
	bool placed;
	bool close_requested;
	bool close_sent;

	// Geometry as the window manager last set or heard it: x/y from
	// the placement pass, width/height from
	// river_window_v1.dimensions. Directional focus needs both, so
	// a window that never answered with dimensions is skipped.
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;

	// Where the user put the window. While has_user_geometry is
	// false the cascade owns the position; once an interactive
	// move or resize sets it, place_windows() leaves the position
	// alone instead of re-cascading the window. View::add_window()
	// zeroes the whole entry with memset before filling it in, so
	// false is the correct default.
	bool has_user_geometry;
	Rectangle user_geometry;

	// The geometry a window had before it was maximized, saved
	// when maximize turns on so un-maximize can restore it
	// exactly, the way labwc restores natural_geometry.
	// has_saved_geometry is false outside a maximize cycle.
	Rectangle saved_geometry;
	bool has_saved_geometry;

	// A resize the user asked for, waiting for the next manage
	// sequence: river_window_v1.propose_dimensions is
	// manage-sequence-only and a node has no dimension setter, so
	// the size half of a resize has to go through a proposal.
	bool propose_pending;
	Rectangle proposed_geometry;

	// Window state, as the window manager believes it. The
	// matching protocol requests are manage-sequence-only, so a
	// change is recorded here and sent by
	// window_manager_manage_start(); sent_* tracks whether river
	// has already been told, so a state that does not change does
	// not re-send.
	bool maximized;
	bool maximized_sent;
	bool fullscreen;
	bool fullscreen_sent;
	bool always_on_top;
	bool always_on_top_sent;
	// Minimizing hides the window through the visibility pass, like
	// any other visibility change, so there is no minimized_sent.
	//
	// minimize_sequence records when this entry was minimized, so
	// "restore the most recent one" means the most recent minimize
	// and not the highest array index. 0 = not minimized by the
	// action.
	bool minimized;
	uint64_t minimize_sequence;

	// A raise is pending: the render pass turns this into one
	// place_top and clears it. Clicking a window, unminimizing
	// it, and mapping it all raise it, and place_top is
	// render-sequence-only in v5, so the request waits for the
	// next render sequence.
	bool raise_pending;

	// Stacking order as yarfwm tracks it: the highest z_order
	// among the visible windows is the topmost one. River has no
	// stacking query, so this is the window manager's own record,
	// advanced every time a window is raised or mapped.
	uint64_t z_order;

	// Which virtual desktop this window belongs to. Desktops are a
	// window manager invention here, not a protocol feature.
	int desktop;

	// Window metadata river reports. None of it drives placement
	// on its own, but all of it is worth keeping: the title and
	// app_id are what a task list or window menu would show, the
	// identifier is the stable name river guarantees is unique and
	// never reused, and the pid is the creator's (explicitly
	// unreliable, so it must never gate anything security
	// sensitive).
	char title[256];
	char app_id[256];
	char identifier[64];
	int unreliable_pid;

	// The window's preferred size bounds, from dimensions_hint.
	// Zero means "no preference". They are a hint: the XML says
	// the window manager "is free to propose dimensions outside
	// of these bounds", but a user resize has no reason to
	// ignore them.
	int32_t min_width;
	int32_t min_height;
	int32_t max_width;
	int32_t max_height;

	// The parent window river reported, if any. A dialog should
	// sit directly above its parent.
	struct river_window_v1 *parent;
	// place_above has been sent for this window's parent, so the
	// request is not repeated every manage sequence.
	bool parent_placed;

	// Number of active screen capture sessions, from
	// capture_sessions. River sends it once at creation and again
	// whenever it changes.
	uint32_t capture_count;

	// The output a fullscreen window was sent to, or null for "no
	// preference".
	struct river_output_v1 *fullscreen_output;

	// The decoration hint river reported, as
	// RIVER_WINDOW_V1_DECORATION_HINT_*. It is stored rather than
	// answered: use_ssd/use_csd are manage-sequence-only and are sent
	// from window_manager_manage_start(), which is also where the
	// once-guard lives. Zero is ONLY_SUPPORTS_CSD, which is the safe
	// default: a window whose hint never arrives gets no use_ssd and no
	// titlebar.
	uint32_t decoration_hint;
	// use_ssd has been sent for this window. Cleared when the hint
	// changes, because the XML allows a window to re-send its
	// preference at any time.
	bool ssd_requested;

	// This window's titlebar, or null when decorations are off, the
	// window is not placed yet, or the client only supports CSD.
	// View::remove_window() destroys it before the swap-delete.
	Decoration *decoration;
};

#endif // VIEW_WINDOW_HPP
