#include "Seat.hpp"
#include "View.hpp"
#include "river-window-management-v1-client-protocol.h"

#include <cstdio>

// Interactive pointer move/resize.
//
// River's side of the handshake, from river-window-management-v1.xml:
//
//   window -> WM   river_window_v1.pointer_move_requested / _resize_requested
//   WM -> river    river_seat_v1.op_start_pointer       [manage sequence only]
//   river -> WM    river_seat_v1.op_delta(dx, dy)       [cumulative since grab]
//   river -> WM    river_seat_v1.op_release             [buttons released]
//   WM -> river    river_seat_v1.op_end                 [manage sequence only]
//
// op_delta carries the TOTAL motion since the grab, so every delta is applied
// to the geometry recorded at grab time and never accumulated on top of the
// previous delta.
//
// This lives apart from Seat.cpp (which holds the focus state and the seat
// events) so both files stay inside the project's line budget.

// A resize must never invert a window: this is the smallest size a drag can
// produce. It is a floor for the proposal only — a window is free to refuse and
// keep its own minimum, which river then reports back in a dimensions event.
static const int32_t minimum_resize_extent = 64;

bool Seat::pointer_operation_active() const
{
	return pointer_operation.kind != PointerOperation::kind_none;
}

void Seat::start_pointer_operation(struct river_window_v1 *window,
				   struct river_seat_v1 *river_seat,
				   bool is_resize, uint32_t edges)
{
	start_operation(window, river_seat, is_resize, edges, false, 0);
}

void Seat::start_touch_operation(struct river_window_v1 *window,
				 struct river_seat_v1 *river_seat,
				 bool is_resize, uint32_t edges,
				 int32_t touch_point)
{
	start_operation(window, river_seat, is_resize, edges, true,
			touch_point);
}

void Seat::start_operation(struct river_window_v1 *window,
			   struct river_seat_v1 *river_seat, bool is_resize,
			   uint32_t edges, bool is_touch, int32_t touch_point)
{
	// River ignores a second op_start while one is in progress, so refuse
	// it here rather than recording state river will not act on.
	if (pointer_operation_active()) {
		std::fprintf(stderr,
			     "Yarfwm: pointer operation already in progress, "
			     "ignoring the new request\n");
		return;
	}
	if (!view || !river_seat || !window) {
		return;
	}

	Rectangle geometry{0, 0, 0, 0};
	if (!view->window_geometry(window, &geometry)) {
		// Without a placement there is no geometry to move from. A
		// window is placed on its first render sequence, so this only
		// happens if the request races the first render.
		std::fprintf(stderr,
			     "Yarfwm: pointer request before the window was "
			     "placed, ignoring\n");
		return;
	}

	pointer_operation.kind = is_resize ? PointerOperation::kind_resize
					   : PointerOperation::kind_move;
	pointer_operation.is_touch = is_touch;
	pointer_operation.touch_point = touch_point;
	pointer_operation.window = window;
	pointer_operation.river_seat = river_seat;
	pointer_operation.start_geometry = geometry;
	pointer_operation.resize_edges = is_resize ? edges : 0;
	pointer_operation.last_delta_x = 0;
	pointer_operation.last_delta_y = 0;
	pointer_operation.started = false;
	pointer_operation.end_requested = false;
	pointer_operation.cancel_requested = false;

	std::fprintf(stderr, "Yarfwm: %s%s requested from %d,%d %dx%d\n",
		     is_touch ? "touch " : "", is_resize ? "resize" : "move",
		     geometry.x, geometry.y, geometry.width, geometry.height);

	// op_start_pointer / op_start_touch are manage-sequence-only, so ask
	// for the sequence that will send it.
	view->request_manage();
}

void Seat::apply_pointer_operation()
{
	// Manage sequence only: op_start_pointer, op_end and
	// inform_resize_start/_end are all manage-sequence requests.
	if (!pointer_operation_active() || !pointer_operation.river_seat) {
		return;
	}

	if (!pointer_operation.started) {
		if (pointer_operation.is_touch) {
			river_seat_v1_op_start_touch(
			    pointer_operation.river_seat,
			    pointer_operation.touch_point);
		} else {
			river_seat_v1_op_start_pointer(
			    pointer_operation.river_seat);
		}
		pointer_operation.started = true;
		if (pointer_operation.kind == PointerOperation::kind_resize &&
		    pointer_operation.window) {
			river_window_v1_inform_resize_start(
			    pointer_operation.window);
		}
	}

	// A cancelled touch never happened as far as the window is concerned:
	// put the geometry back the way it was and end the operation without
	// the finished-at log, which would report a move that was undone.
	if (pointer_operation.cancel_requested) {
		if (pointer_operation.window && view) {
			view->set_user_geometry(
			    pointer_operation.window,
			    pointer_operation.start_geometry);
			if (pointer_operation.kind ==
			    PointerOperation::kind_resize) {
				view->propose_user_dimensions(
				    pointer_operation.window,
				    pointer_operation.start_geometry);
			}
		}
		if (pointer_operation.is_touch) {
			river_seat_v1_op_end_touch(
			    pointer_operation.river_seat,
			    pointer_operation.touch_point);
		} else {
			river_seat_v1_op_end(pointer_operation.river_seat);
		}
		std::fprintf(stderr,
			     "Yarfwm: touch operation cancelled, geometry "
			     "restored to %d,%d %dx%d\n",
			     pointer_operation.start_geometry.x,
			     pointer_operation.start_geometry.y,
			     pointer_operation.start_geometry.width,
			     pointer_operation.start_geometry.height);
		clear_pointer_operation();
		return;
	}

	if (pointer_operation.end_requested) {
		if (pointer_operation.is_touch) {
			river_seat_v1_op_end_touch(
			    pointer_operation.river_seat,
			    pointer_operation.touch_point);
		} else {
			river_seat_v1_op_end(pointer_operation.river_seat);
		}
		if (pointer_operation.kind == PointerOperation::kind_resize &&
		    pointer_operation.window) {
			river_window_v1_inform_resize_end(
			    pointer_operation.window);
		}
		// One line per completed operation, not per delta: the deltas
		// arrive on every pointer motion and logging them all buries
		// the rest of the log.
		Rectangle final_geometry{0, 0, 0, 0};
		if (pointer_operation.window &&
		    view->window_geometry(pointer_operation.window,
					  &final_geometry)) {
			// The delta is printed alongside the geometry: river
			// measures it from the pointer position when it handled
			// op_start_pointer, so when the two disagree by a step
			// the delta is what the compositor asked for.
			std::fprintf(stderr,
				     "Yarfwm: %s finished at %d,%d %dx%d "
				     "(last delta %d,%d)\n",
				     pointer_operation.kind ==
					     PointerOperation::kind_resize
					 ? "resize"
					 : "move",
				     final_geometry.x, final_geometry.y,
				     final_geometry.width,
				     final_geometry.height,
				     pointer_operation.last_delta_x,
				     pointer_operation.last_delta_y);
		}
		clear_pointer_operation();
	}
}

void Seat::clear_pointer_operation()
{
	pointer_operation.kind = PointerOperation::kind_none;
	pointer_operation.is_touch = false;
	pointer_operation.touch_point = 0;
	pointer_operation.window = nullptr;
	pointer_operation.river_seat = nullptr;
	pointer_operation.started = false;
	pointer_operation.end_requested = false;
	pointer_operation.cancel_requested = false;
	pointer_operation.resize_edges = 0;
}

void Seat::river_seat_op_delta(void *data, struct river_seat_v1 *river_seat,
			       int32_t dx, int32_t dy)
{
	(void)river_seat;
	Seat *seat = static_cast<Seat *>(data);
	if (!seat->pointer_operation_active() || !seat->view) {
		return;
	}
	if (seat->pointer_operation.is_touch) {
		return;
	}

	apply_operation_delta(seat, dx, dy);
}

// The body shared by op_delta and op_delta_touch: both carry the total motion
// since the grab, and only the event that delivers it differs.
void Seat::apply_operation_delta(Seat *seat, int32_t dx, int32_t dy)
{
	seat->pointer_operation.last_delta_x = dx;
	seat->pointer_operation.last_delta_y = dy;

	// Always start from the grab geometry: the delta is cumulative.
	Rectangle geometry = seat->pointer_operation.start_geometry;
	const uint32_t edges = seat->pointer_operation.resize_edges;

	if (seat->pointer_operation.kind == PointerOperation::kind_move) {
		geometry.x += dx;
		geometry.y += dy;
	} else {
		// A resize pulls the grabbed edges; the opposite edge stays
		// anchored. The edge values are bit flags, so a corner arrives
		// with two bits set and both branches below run.
		if (edges & RIVER_WINDOW_V1_EDGES_LEFT) {
			geometry.x += dx;
			geometry.width -= dx;
		} else if (edges & RIVER_WINDOW_V1_EDGES_RIGHT) {
			geometry.width += dx;
		}
		if (edges & RIVER_WINDOW_V1_EDGES_TOP) {
			geometry.y += dy;
			geometry.height -= dy;
		} else if (edges & RIVER_WINDOW_V1_EDGES_BOTTOM) {
			geometry.height += dy;
		}

		if (geometry.width < minimum_resize_extent) {
			geometry.width = minimum_resize_extent;
		}
		if (geometry.height < minimum_resize_extent) {
			geometry.height = minimum_resize_extent;
		}
	}

	seat->view->set_user_geometry(seat->pointer_operation.window, geometry);
	if (seat->pointer_operation.kind == PointerOperation::kind_resize) {
		// The size half cannot be applied by set_position: a node has
		// no dimension setter, so it goes through a proposal.
		seat->view->propose_user_dimensions(
		    seat->pointer_operation.window, geometry);
	}
}

void Seat::river_seat_op_release(void *data, struct river_seat_v1 *river_seat)
{
	(void)river_seat;
	Seat *seat = static_cast<Seat *>(data);
	if (!seat->pointer_operation_active()) {
		return;
	}
	if (seat->pointer_operation.is_touch) {
		return;
	}
	seat->pointer_operation.end_requested = true;
	if (seat->view) {
		// op_end is manage-sequence-only. River usually follows
		// op_delta with a manage_start of its own, but the release can
		// arrive with no motion at all, so ask explicitly.
		seat->view->request_manage();
	}
}

// The touch half of the same handshake. River keys a touch operation by the
// transient touch point ID, so every handler checks that the event belongs to
// the operation in progress before acting on it.

void Seat::river_seat_op_delta_touch(void *data,
				     struct river_seat_v1 *river_seat,
				     int32_t touch_point, int32_t dx,
				     int32_t dy)
{
	(void)river_seat;
	Seat *seat = static_cast<Seat *>(data);
	if (!seat->pointer_operation_active() || !seat->view) {
		return;
	}
	if (!seat->pointer_operation.is_touch ||
	    seat->pointer_operation.touch_point != touch_point) {
		return;
	}

	apply_operation_delta(seat, dx, dy);
}

void Seat::river_seat_op_release_touch(void *data,
				       struct river_seat_v1 *river_seat,
				       int32_t touch_point)
{
	(void)river_seat;
	Seat *seat = static_cast<Seat *>(data);
	if (!seat->pointer_operation_active() ||
	    !seat->pointer_operation.is_touch ||
	    seat->pointer_operation.touch_point != touch_point) {
		return;
	}
	seat->pointer_operation.end_requested = true;
	if (seat->view) {
		// op_end_touch is manage-sequence-only, and a release can
		// arrive with no motion at all, so ask explicitly.
		seat->view->request_manage();
	}
}

void Seat::river_seat_op_cancel_touch(void *data,
				      struct river_seat_v1 *river_seat,
				      int32_t touch_point)
{
	(void)river_seat;
	Seat *seat = static_cast<Seat *>(data);
	if (!seat->pointer_operation_active() ||
	    !seat->pointer_operation.is_touch ||
	    seat->pointer_operation.touch_point != touch_point) {
		return;
	}
	// River's XML: "The client should ideally behave as if this operation
	// was never started." apply_pointer_operation() puts the grab
	// geometry back and ends the operation.
	seat->pointer_operation.cancel_requested = true;
	if (seat->view) {
		seat->view->request_manage();
	}
}

// Keyboard-driven pointer warping.
//
// river_seat_v1.pointer_warp is manage-sequence-only, so a key press records
// an offset from the last position river reported (the pointer_position
// event, stored per seat in Seat.cpp) and apply_pointer_warp() sends the
// warp during the next manage sequence. Offsets accumulate while a press is
// waiting for its sequence, so a held key (the repeat timer) walks the
// pointer instead of stalling. River clamps the target itself: "If the given
// position is outside the bounds of all outputs, the pointer will be warped
// to the closest point inside an output instead."

void Seat::move_pointer(struct river_seat_v1 *river_seat, int32_t delta_x,
			int32_t delta_y)
{
	SeatEntry *entry = find_entry(river_seat);
	if (!entry || entry->removed || !view) {
		return;
	}
	if (delta_x == 0 && delta_y == 0) {
		return;
	}

	entry->pointer_warp_delta_x += delta_x;
	entry->pointer_warp_delta_y += delta_y;
	entry->pointer_warp_pending = true;

	// pointer_warp is manage-sequence-only, so ask for the sequence that
	// will send it.
	view->request_manage();
}

void Seat::apply_pointer_warp()
{
	for (int i = 0; i < seat_count; i++) {
		SeatEntry *entry = &seat_entries[i];
		if (!entry->river_seat || !entry->pointer_warp_pending) {
			continue;
		}

		const int32_t x =
		    entry->pointer_x + entry->pointer_warp_delta_x;
		const int32_t y =
		    entry->pointer_y + entry->pointer_warp_delta_y;
		river_seat_v1_pointer_warp(entry->river_seat, x, y);
		std::fprintf(stderr, "Yarfwm: pointer warp %d,%d -> %d,%d\n",
			     entry->pointer_x, entry->pointer_y, x, y);

		entry->pointer_warp_delta_x = 0;
		entry->pointer_warp_delta_y = 0;
		entry->pointer_warp_pending = false;
	}
}
