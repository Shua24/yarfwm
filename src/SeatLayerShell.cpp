#include "Seat.hpp"
#include "View.hpp"

#include <cstdio>

// The river_layer_shell_seat_v1 keyboard-focus events.
//
// River tells the window manager which layer surface holds the keyboard, and
// the window manager is expected to give it up or hand it back accordingly.
// The three handlers record the state and, when focus returns to nothing,
// restore the window the user was last using.
//
// These live apart from Seat.cpp, which holds the focus state and the seat
// events, so both files stay inside the project's line budget.

void Seat::layer_shell_seat_focus_exclusive(
    void *data, struct river_layer_shell_seat_v1 *layer_shell_seat)
{
	SeatEntry *entry = static_cast<SeatEntry *>(data);
	(void)layer_shell_seat;
	entry->layer_surface_focus = layer_focus_exclusive;
	std::fprintf(stderr,
		     "Yarfwm: layer surface took exclusive keyboard focus\n");
}

// A bar or dock asking for the keyboard without taking it away from
// windows: recorded so the focus state is complete, and deliberately not
// acted on. River's XML (river-layer-shell-v1 focus_non_exclusive): "The
// window manager continues to control focus and may choose to focus a
// different window/shell surface at any time." att_wm records it the same
// way and acts only on focus_none (Seat.zig:407-408).
void Seat::layer_shell_seat_focus_non_exclusive(
    void *data, struct river_layer_shell_seat_v1 *layer_shell_seat)
{
	SeatEntry *entry = static_cast<SeatEntry *>(data);
	(void)layer_shell_seat;
	entry->layer_surface_focus = layer_focus_non_exclusive;
	std::fprintf(
	    stderr,
	    "Yarfwm: layer surface wants non-exclusive keyboard focus\n");
}

void Seat::layer_shell_seat_focus_none(
    void *data, struct river_layer_shell_seat_v1 *layer_shell_seat)
{
	SeatEntry *entry = static_cast<SeatEntry *>(data);
	(void)layer_shell_seat;
	entry->layer_surface_focus = layer_focus_none;

	// Hand the keyboard back to whatever the user was using. Recorded as
	// intent and applied by Seat::apply_manage(); ask for a manage
	// sequence so the window is refocused even if river does not send one
	// of its own after this event (att_wm does the same for its lock
	// restore).
	if (entry->focused_window) {
		entry->pending_focus_window = entry->focused_window;
		entry->pending_clear_focus = false;
	}
	if (entry->owner && entry->owner->view) {
		entry->owner->view->request_manage();
	}
	std::fprintf(stderr, "Yarfwm: no layer surface holds keyboard focus\n");
}
