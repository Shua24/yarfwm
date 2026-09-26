#include "Display.hpp"
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
	// River hands us the wl_seat's registry name here; the wl_seat itself
	// was bound during the registry roundtrip and is looked up by that
	// name. This is the first moment the two can be matched up, and it is
	// what lets the window manager bind a wl_pointer for its own surfaces.
	//
	// Nothing else about the seat is used: keyboard focus is river's own
	// business (river_seat_v1.focus_window), and wl_keyboard is never
	// bound.
	//
	// The wl_pointer is NOT bound here. get_pointer is a protocol error
	// unless the seat has advertised the pointer capability, and a headless
	// seat starts with none -- the binding belongs in the capabilities
	// handler, seat_capabilities().
	Seat *seat = static_cast<Seat *>(data);
	SeatEntry *entry = seat->find_entry(river_seat);
	if (!entry || !seat->display) {
		return;
	}

	struct wl_seat *wl_seat = seat->display->seat_by_registry_name(name);
	if (!wl_seat) {
		std::fprintf(stderr,
			     "Yarfwm: river named a wl_seat we did not bind "
			     "(name %u), titlebar clicks will not work\n",
			     name);
		return;
	}

	entry->wl_seat = wl_seat;

	// A pointer may only be bound once the capability has been advertised.
	// The capability event may have arrived before this one, so ask.
	if (seat->display->seat_has_pointer(name)) {
		seat->update_pointer_capability(wl_seat, true);
	}
}

void Seat::update_pointer_capability(struct wl_seat *wl_seat, bool has_pointer)
{
	SeatEntry *entry = find_entry_by_wl_seat(wl_seat);
	if (!entry) {
		return;
	}

	if (!has_pointer) {
		// The device went away: drop the pointer, and the hover cache
		// with it, since it names a surface that pointer could reach.
		if (entry->pointer) {
			wl_pointer_destroy(entry->pointer);
			entry->pointer = nullptr;
		}
		entry->owner->hovered_surface = nullptr;
		return;
	}

	if (entry->pointer) {
		return;
	}

	entry->pointer = wl_seat_get_pointer(wl_seat);
	if (!entry->pointer) {
		return;
	}

	static const struct wl_pointer_listener pointer_listener = [] {
		struct wl_pointer_listener listener{};
		// Every slot is populated: libwayland aborts the process
		// when an event arrives for a NULL slot, and wl_pointer
		// follows the seat's version (v9 here), so the v8 and v9
		// slots are reachable.
		listener.enter = pointer_enter;
		listener.leave = pointer_leave;
		listener.motion = pointer_motion;
		listener.button = pointer_button;
		listener.axis = pointer_axis;
		listener.frame = pointer_frame;
		listener.axis_source = pointer_axis_source;
		listener.axis_stop = pointer_axis_stop;
		listener.axis_discrete = pointer_axis_discrete;
		listener.axis_value120 = pointer_axis_value120;
		listener.axis_relative_direction =
		    pointer_axis_relative_direction;
		listener.warp = pointer_warp;
		return listener;
	}();
	wl_pointer_add_listener(entry->pointer, &pointer_listener,
				entry->owner);
}

Seat::SeatEntry *Seat::find_entry_by_wl_seat(struct wl_seat *wl_seat)
{
	for (int i = 0; i < seat_count; i++) {
		if (seat_entries[i].wl_seat == wl_seat) {
			return &seat_entries[i];
		}
	}
	return nullptr;
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

	// A titlebar press produces BOTH a wl_pointer.button on our decoration
	// surface and this event, in that order (measured -- see the SSD
	// notes). The button handler may have already acted on the window:
	// minimizing it, closing it, or starting an interactive move. Queueing
	// focus for it here would overwrite the focus handoff those actions
	// performed, so a minimized window would be refocused and pop back up.
	//
	// The interaction is claimed only for a decoration press. A decoration
	// press is still a focus+raise as far as the user is concerned, but
	// the wl_pointer handler does that itself for the titlebar-bar case,
	// and the three buttons must NOT focus the window they minimize or
	// close.
	if (seat->view && seat->view->consume_decoration_click(window)) {
		return;
	}

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
