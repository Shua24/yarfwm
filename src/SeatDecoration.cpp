// yarfwm -- the wl_pointer handlers, for the window manager's own surfaces.
//
// River routes a press on a *window* to river_seat_v1.window_interaction, but a
// press on a decoration surface has no scene-node data for river to resolve, so
// it arrives here as an ordinary wl_pointer event on our own surface. That is
// the whole reason this file exists: it is the only channel that can tell a
// titlebar button from a titlebar bar.
//
// MEASURED (not inferred) -- see .hermes/notes/2026-09-25-ssd/ and the SSD
// section of the yarfwm-scaffold skill: a single press on a decoration surface
// fires BOTH channels, in this order:
//
//   1. wl_pointer.button (PRESSED, then RELEASED) on our own pointer, with
//      SURFACE-LOCAL coordinates -- and a press carries NO coordinates of its
//      own, so the position has to be cached from the preceding enter/motion.
//   2. river_seat_v1.window_interaction naming the PARENT window, in the manage
//      sequence that follows.
//
// Neither suppresses the other, and the second one is dangerous: it is answered
// by record_focus + raise_window, and record_focus only queues intent that
// apply_manage drains later. A titlebar press that minimizes a window would
// therefore queue the focus handoff, then have it overwritten by the
// interaction re-queueing the now-minimized window -- and the window pops back
// up. The claim/consume pair is what prevents that
// (View::claim_decoration_click).
#include "Seat.hpp"
#include "View.hpp"
#include "river-window-management-v1-client-protocol.h"

#include <linux/input-event-codes.h>

void Seat::pointer_enter(void *data, struct wl_pointer *pointer,
			 uint32_t serial, struct wl_surface *surface,
			 wl_fixed_t surface_x, wl_fixed_t surface_y)
{
	(void)pointer;
	(void)serial;
	Seat *seat = static_cast<Seat *>(data);

	seat->hovered_surface = surface;
	seat->pointer_surface_x = wl_fixed_to_int(surface_x);
	seat->pointer_surface_y = wl_fixed_to_int(surface_y);

	// A stale claim must never swallow a later genuine content click, and
	// this is the only place it can be retired safely: window_interaction
	// always follows its own press, so by the time the pointer has moved to
	// a surface that is not a decoration, any claim from the last press has
	// either been consumed or belonged to a press that produced no
	// interaction at all.
	if (seat->view && !seat->view->window_for_decoration_surface(surface)) {
		seat->view->clear_decoration_click_claim();
	}
}

void Seat::pointer_leave(void *data, struct wl_pointer *pointer,
			 uint32_t serial, struct wl_surface *surface)
{
	(void)pointer;
	(void)serial;
	Seat *seat = static_cast<Seat *>(data);
	if (seat->hovered_surface == surface) {
		seat->hovered_surface = nullptr;
	}
}

void Seat::pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time,
			  wl_fixed_t surface_x, wl_fixed_t surface_y)
{
	(void)pointer;
	(void)time;
	Seat *seat = static_cast<Seat *>(data);
	seat->pointer_surface_x = wl_fixed_to_int(surface_x);
	seat->pointer_surface_y = wl_fixed_to_int(surface_y);
}

void Seat::pointer_button(void *data, struct wl_pointer *pointer,
			  uint32_t serial, uint32_t time, uint32_t button,
			  uint32_t state)
{
	(void)pointer;
	(void)serial;
	(void)time;
	Seat *seat = static_cast<Seat *>(data);
	if (state != WL_POINTER_BUTTON_STATE_PRESSED || button != BTN_LEFT ||
	    !seat->view) {
		return;
	}

	// A decoration surface is the only window-manager surface with a role
	// today, so a hit here means a titlebar.
	struct river_window_v1 *window =
	    seat->view->window_for_decoration_surface(seat->hovered_surface);
	if (!window) {
		return;
	}

	const TitlebarPart part = seat->view->decoration_part_at(
	    window, seat->pointer_surface_x, seat->pointer_surface_y);

	// This press will ALSO arrive as a window_interaction for the same
	// window, a moment later. Claim it so river_seat_window_interaction
	// does not overwrite the focus handoff the buttons below perform.
	seat->view->claim_decoration_click(window);

	switch (part) {
	case titlebar_part_button_minimize:
		seat->view->minimize_window(window);
		return;
	case titlebar_part_button_maximize:
		seat->view->toggle_maximize(window);
		return;
	case titlebar_part_button_close:
		seat->view->request_close(window);
		return;
	case titlebar_part_bar:
		// Clicking our own titlebar is a focus+raise, exactly like
		// clicking the client area. The interaction is consumed for
		// this press (see river_seat_window_interaction), so do it
		// here. Dragging the bar then becomes an interactive move:
		// the same handshake a client-side titlebar triggers through
		// river_window_v1.pointer_move_requested. River measures the
		// op_delta from the pointer position at op_start_pointer, so
		// no grab offset is needed here.
		seat->record_focus(seat->find_entry(seat->primary_river_seat()),
				   window);
		seat->view->raise_window(window);
		seat->start_pointer_operation(window,
					      seat->primary_river_seat(),
					      /*is_resize=*/false, /*edges=*/0);
		return;
	case titlebar_part_none:
	default:
		return;
	}
}

// The remaining wl_pointer slots have no policy today. They are all populated
// because libwayland aborts the process (SIGABRT) when an event arrives for a
// NULL listener slot, and the slots are reachable: wl_pointer is a child of
// wl_seat and takes the seat's version, which river advertises as v9 here, so
// axis_value120 (v8) and axis_relative_direction (v9) can arrive in practice.
void Seat::pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time,
			uint32_t axis, wl_fixed_t value)
{
	(void)data;
	(void)pointer;
	(void)time;
	(void)axis;
	(void)value;
}

void Seat::pointer_frame(void *data, struct wl_pointer *pointer)
{
	(void)data;
	(void)pointer;
}

void Seat::pointer_axis_source(void *data, struct wl_pointer *pointer,
			       uint32_t source)
{
	(void)data;
	(void)pointer;
	(void)source;
}

void Seat::pointer_axis_stop(void *data, struct wl_pointer *pointer,
			     uint32_t time, uint32_t axis)
{
	(void)data;
	(void)pointer;
	(void)time;
	(void)axis;
}

void Seat::pointer_axis_discrete(void *data, struct wl_pointer *pointer,
				 uint32_t axis, int32_t discrete)
{
	(void)data;
	(void)pointer;
	(void)axis;
	(void)discrete;
}

void Seat::pointer_axis_value120(void *data, struct wl_pointer *pointer,
				 uint32_t axis, int32_t value120)
{
	(void)data;
	(void)pointer;
	(void)axis;
	(void)value120;
}

void Seat::pointer_axis_relative_direction(void *data,
					   struct wl_pointer *pointer,
					   uint32_t axis, uint32_t direction)
{
	(void)data;
	(void)pointer;
	(void)axis;
	(void)direction;
}

void Seat::pointer_warp(void *data, struct wl_pointer *pointer,
			wl_fixed_t surface_x, wl_fixed_t surface_y)
{
	(void)data;
	(void)pointer;
	(void)surface_x;
	(void)surface_y;
}
