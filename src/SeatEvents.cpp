#include "Seat.hpp"
#include "View.hpp"
#include "river-layer-shell-v1-client-protocol.h"
#include "river-window-management-v1-client-protocol.h"

#include <cstdio>

// The river_seat_v1 event handlers.
//
// These live apart from Seat.cpp, which holds the focus state and the
// request-recording API, so both files stay inside the project's line budget
// (the same split SeatPointer.cpp and SeatLayerShell.cpp use).

void Seat::river_seat_removed(void *data, struct river_seat_v1 *river_seat)
{
	Seat *seat = static_cast<Seat *>(data);
	(void)river_seat;
	// The object is destroyed during the next manage sequence: river
	// follows this event with a manage_start of its own.
	SeatEntry *entry = seat->find_entry(river_seat);
	if (entry) {
		entry->removed = true;
	}
	std::fprintf(stderr, "Yarfwm: seat removed\n");
}

void Seat::river_seat_wl_seat(void *data, struct river_seat_v1 *river_seat,
			      uint32_t name)
{
	// River owns the wl_seat plumbing; the window manager only needs the
	// name to correlate the two objects, which nothing does yet.
	(void)data;
	(void)river_seat;
	(void)name;
}

void Seat::river_seat_pointer_enter(void *data,
				    struct river_seat_v1 *river_seat,
				    struct river_window_v1 *window)
{
	Seat *seat = static_cast<Seat *>(data);
	if (!seat->focus_follows_mouse) {
		return;
	}
	seat->record_focus(seat->find_entry(river_seat), window);
}

void Seat::river_seat_pointer_leave(void *data,
				    struct river_seat_v1 *river_seat)
{
	(void)data;
	(void)river_seat;
}

void Seat::river_seat_window_interaction(void *data,
					 struct river_seat_v1 *river_seat,
					 struct river_window_v1 *window)
{
	// Clicking a window focuses it and raises it, whether or not focus
	// follows the pointer. The XML's own rationale for this event names
	// raising: it gives window managers "necessary information to
	// determine when to send keyboard focus, raise a window that already
	// has keyboard focus, etc."
	Seat *seat = static_cast<Seat *>(data);
	seat->record_focus(seat->find_entry(river_seat), window);
	if (seat->view) {
		seat->view->raise_window(window);
	}
}

void Seat::river_seat_shell_surface_interaction(
    void *data, struct river_seat_v1 *river_seat,
    struct river_shell_surface_v1 *shell_surface)
{
	// A window manager shell surface (a bar drawn by the window manager)
	// took the click; there is no window to focus.
	(void)data;
	(void)river_seat;
	(void)shell_surface;
}

void Seat::river_seat_pointer_position(void *data,
				       struct river_seat_v1 *river_seat,
				       int32_t x, int32_t y)
{
	// River sends this in every manage sequence (unless the position is
	// unchanged). The keyboard pointer warp moves relative to the last
	// reported position, so it is kept per seat.
	Seat *seat = static_cast<Seat *>(data);
	SeatEntry *entry = seat->find_entry(river_seat);
	if (!entry) {
		return;
	}
	entry->pointer_x = x;
	entry->pointer_y = y;
}
