#include "Display.hpp"
#include "View.hpp"
#include "river-window-management-v1-client-protocol.h"

#include <cstdio>

// How far one move_window_* keypress shifts a window. It matches the cascade
// step, so a keyboard move keeps the visual rhythm of the placement pass.
static const int32_t move_step = 32;

// Requests the event handlers and the Keybind engine make on the View. They
// live together because they share one rule: river only accepts the requests
// they lead to (manage_dirty is the exception) inside a manage or render
// sequence, so the state is recorded here and applied by
// window_manager_manage_start().

void View::set_keybind(Keybind *keybind) { this->keybind = keybind; }

int View::desktop_total() { return View::desktop_count; }

void View::request_manage()
{
	if (!manager || manage_requested) {
		return;
	}
	manage_requested = true;
	river_window_manager_v1_manage_dirty(manager);
}

void View::request_close(struct river_window_v1 *window)
{
	Window *window_entry = find_window(window);
	if (!window_entry) {
		std::fprintf(
		    stderr,
		    "Yarfwm: close request for an unknown window, ignoring\n");
		return;
	}
	std::fprintf(
	    stderr,
	    "Yarfwm: close request recorded, waiting for a manage sequence\n");
	window_entry->close_requested = true;
	// Re-arm the send. close_sent stops the same request from being sent
	// again in later manage sequences; a fresh press clears it so a window
	// that ignored the first request can be asked again.
	window_entry->close_sent = false;
}

void View::set_user_geometry(struct river_window_v1 *window,
			     const Rectangle &geometry)
{
	Window *window_entry = find_window(window);
	if (!window_entry) {
		return;
	}
	// Idempotence matters here, it is not an optimisation. During an
	// interactive operation river re-sends the same cumulative op_delta
	// after every manage sequence, so requesting a manage sequence for an
	// unchanged geometry would start a new sequence, which re-sends the
	// delta, which requests another sequence: an endless request storm that
	// starves the event loop. Only a real change asks for a sequence.
	if (window_entry->has_user_geometry &&
	    window_entry->user_geometry.x == geometry.x &&
	    window_entry->user_geometry.y == geometry.y &&
	    window_entry->user_geometry.width == geometry.width &&
	    window_entry->user_geometry.height == geometry.height) {
		return;
	}
	window_entry->user_geometry = geometry;
	window_entry->has_user_geometry = true;
	// The position is applied by place_windows() on the next render
	// sequence (set_position is render-sequence-only in v5), so ask for
	// one.
	request_manage();
}

void View::propose_user_dimensions(struct river_window_v1 *window,
				   const Rectangle &geometry)
{
	Window *window_entry = find_window(window);
	if (!window_entry) {
		return;
	}
	// Same idempotence rule as set_user_geometry: a manage sequence
	// re-sends the cumulative op_delta, so a redundant request here would
	// loop.
	if (window_entry->propose_pending &&
	    window_entry->proposed_geometry.width == geometry.width &&
	    window_entry->proposed_geometry.height == geometry.height) {
		return;
	}
	window_entry->proposed_geometry = geometry;
	window_entry->propose_pending = true;
	// propose_dimensions is manage-sequence-only; the manage sequence
	// handler sends it.
	request_manage();
}

void View::move_window(struct river_window_v1 *window,
		       enum FocusDirection direction)
{
	Window *window_entry = find_window(window);
	if (!window_entry || !window_entry->placed) {
		std::fprintf(stderr,
			     "Yarfwm: move_window: window is not placed yet\n");
		return;
	}

	// Start from the user's geometry if there is one, otherwise from
	// wherever the cascade put the window: the first move of a window
	// takes over from the cascade instead of jumping.
	Rectangle geometry =
	    window_entry->has_user_geometry
		? window_entry->user_geometry
		: Rectangle{window_entry->x, window_entry->y,
			    window_entry->width, window_entry->height};

	switch (direction) {
	case focus_direction_left:
		geometry.x -= move_step;
		break;
	case focus_direction_right:
		geometry.x += move_step;
		break;
	case focus_direction_up:
		geometry.y -= move_step;
		break;
	case focus_direction_down:
		geometry.y += move_step;
		break;
	default:
		// Not a direction the placement maths knows about. The
		// FocusDirection enum has no other members, so this is
		// unreachable; it keeps -Wswitch-default quiet.
		return;
	}

	std::fprintf(stderr, "Yarfwm: move_window -> %d,%d\n", geometry.x,
		     geometry.y);
	set_user_geometry(window, geometry);
}

bool View::window_geometry(struct river_window_v1 *window,
			   Rectangle *geometry) const
{
	const Window *window_entry = find_window(window);
	if (!window_entry || !window_entry->placed) {
		return false;
	}
	*geometry = Rectangle{window_entry->x, window_entry->y,
			      window_entry->width, window_entry->height};
	return true;
}

void View::request_exit_session()
{
	// river_window_manager_v1.exit_session: end the Wayland session and
	// exit the compositor, disconnecting every client including this window
	// manager. It is not restricted to a manage or render sequence, so it
	// can be sent straight from a key binding, which is what att_wm does
	// (Wm.zig:1308).
	if (!manager) {
		std::fprintf(stderr, "Yarfwm: exit_session: no window manager "
				     "object, ignoring\n");
		return;
	}
	std::fprintf(stderr, "Yarfwm: exit_session\n");
	river_window_manager_v1_exit_session(manager);
}

struct river_window_v1 *
View::window_in_direction(struct river_window_v1 *from,
			  enum FocusDirection direction) const
{
	const Window *source = find_window(from);
	if (!source || !source->placed || source->width <= 0 ||
	    source->height <= 0) {
		return nullptr;
	}

	const Rectangle source_rectangle{source->x, source->y, source->width,
					 source->height};

	struct river_window_v1 *best = nullptr;
	double best_score = 0.0;

	for (int i = 0; i < window_count; i++) {
		const Window *candidate = &windows[i];
		if (!candidate->window || candidate->window == from) {
			continue;
		}
		if (!candidate->placed || candidate->width <= 0 ||
		    candidate->height <= 0) {
			continue;
		}
		// Directional focus is a keyboard action, so it must only
		// land on a window the user can see: a window on another
		// desktop (or minimized) is not a candidate.
		if (!window_entry_is_visible(candidate, active_desktop)) {
			continue;
		}

		const Rectangle candidate_rectangle{candidate->x, candidate->y,
						    candidate->width,
						    candidate->height};

		// The scoring lives in src/Placement.cpp so it can be unit
		// tested without a Wayland connection: a negative score means
		// "not in that direction", otherwise the lowest score wins.
		const double score = direction_score(
		    source_rectangle, candidate_rectangle, direction);
		if (score < 0.0) {
			continue;
		}
		if (!best || score < best_score) {
			best = candidate->window;
			best_score = score;
		}
	}

	return best;
}

void View::raise_window(struct river_window_v1 *window)
{
	Window *window_entry = find_window(window);
	if (!window_entry) {
		return;
	}
	if (window_entry->z_order == next_z_order - 1) {
		// Already the front-most window: a repeat click on the
		// focused window must not churn the stacking order.
		return;
	}
	window_entry->z_order = next_z_order++;
	window_entry->raise_pending = true;
	// place_top is render-sequence-only in v5, so ask for the sequence
	// that sends it.
	request_manage();
}

struct river_window_v1 *View::topmost_visible_window() const
{
	struct river_window_v1 *topmost = nullptr;
	uint64_t best_z_order = 0;
	for (int i = 0; i < window_count; i++) {
		const Window *window_entry = &windows[i];
		if (!window_entry_is_visible(window_entry, active_desktop)) {
			continue;
		}
		// The comparison is >= so a tie (possible only if the
		// clock was reset) keeps the later entry, matching the
		// "most recently raised" reading of the order.
		if (!topmost || window_entry->z_order >= best_z_order) {
			topmost = window_entry->window;
			best_z_order = window_entry->z_order;
		}
	}
	return topmost;
}
