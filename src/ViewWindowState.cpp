#include "Output.hpp"
#include "Seat.hpp"
#include "View.hpp"
#include "river-window-management-v1-client-protocol.h"

#include <cstdio>

// The window state and geometry actions, and the virtual desktops.
//
// Every action here is reachable from two places: a key binding in
// KeybindActions.cpp, and the matching river event in ViewEvents.cpp (a
// client-side decoration button sends maximize_requested, fullscreen_requested,
// minimize_requested and friends). Both paths call these functions so the two
// cannot drift apart.
//
// Sequence rules, from river-window-management-v1.xml:
//   manage only   inform_maximized, inform_unmaximized, fullscreen,
//                 exit_fullscreen
//   either        hide, show, place_top, place_bottom
//   render only   set_position (through place_windows())
//
// So a state change is recorded on the Window and sent by
// View::window_manager_manage_start(); the position half is applied by
// View::place_windows() on the render sequence that follows.

// The smallest a window may be resized to, so a percentage press can never
// invert or collapse it.
static const int32_t minimum_extent = 64;

// Clamp a proposed size to the bounds the window stated in its
// dimensions_hint event. Zero means "no preference" for that value, which is
// how the protocol spells it: "A value of 0 indicates that the window has no
// preference for that value." The bounds are passed as values because
// View::Window is private to View and this stays a free function.
static void apply_dimension_hints(int32_t min_width, int32_t min_height,
				  int32_t max_width, int32_t max_height,
				  Rectangle &geometry)
{
	if (min_width > 0 && geometry.width < min_width) {
		geometry.width = min_width;
	}
	if (min_height > 0 && geometry.height < min_height) {
		geometry.height = min_height;
	}
	if (max_width > 0 && geometry.width > max_width) {
		geometry.width = max_width;
	}
	if (max_height > 0 && geometry.height > max_height) {
		geometry.height = max_height;
	}
}

// Pick the output a fullscreen window should fill.
//
// River's fullscreen REQUEST does not allow a null output ("<arg name="output"
// type="object" interface="river_output_v1">" — no allow-null), while the
// fullscreen_requested EVENT does. So a null from the window means "you
// choose", and the window manager must resolve it to a real output before
// sending the request; passing null makes libwayland fail to marshal the
// request and the connection breaks.
//
// Preference: the output the window mostly sits on, else the first output that
// has reported dimensions, else null (no outputs, nothing to go fullscreen on).
static struct river_output_v1 *
resolve_fullscreen_output(Output **outputs, int output_count,
			  const Rectangle &window)
{
	struct river_output_v1 *best = nullptr;
	int64_t best_overlap = 0;

	for (int i = 0; i < output_count; i++) {
		Output *output = outputs[i];
		if (!output || !output->has_dimensions) {
			continue;
		}
		// Overlap area between the window and the output.
		const int32_t left =
		    window.x > output->x ? window.x : output->x;
		const int32_t top = window.y > output->y ? window.y : output->y;
		const int32_t right =
		    (window.x + window.width) < (output->x + output->width)
			? (window.x + window.width)
			: (output->x + output->width);
		const int32_t bottom =
		    (window.y + window.height) < (output->y + output->height)
			? (window.y + window.height)
			: (output->y + output->height);
		const int64_t overlap =
		    (right > left && bottom > top)
			? static_cast<int64_t>(right - left) *
			      static_cast<int64_t>(bottom - top)
			: 0;

		if (best == nullptr || overlap > best_overlap) {
			best = output->river_output;
			best_overlap = overlap;
		}
	}

	return best;
}

// Mark a window's state as changed so the next manage sequence sends the
// matching request. Only a real change asks for a sequence, which keeps this
// from looping the way an unconditional request would (a manage sequence
// re-sends the state river already had).
static void request_state_update(View *view, bool changed)
{
	if (changed) {
		view->request_manage();
	}
}

void View::toggle_maximize(struct river_window_v1 *window)
{
	Window *window_entry = find_window(window);
	if (!window_entry) {
		return;
	}

	window_entry->maximized = !window_entry->maximized;
	std::fprintf(stderr, "Yarfwm: toggle_maximize -> %s\n",
		     window_entry->maximized ? "maximized" : "restored");

	if (window_entry->maximized) {
		// A maximized window fills the placement area, which is the
		// output minus the exclusive zones of bars and docks. River
		// keeps the window manager responsible for the geometry of a
		// maximized window, so this is a position and a proposal like
		// any other.
		Rectangle area{0, 0, 0, 0};
		placement_area(&area.x, &area.y, &area.width, &area.height);
		set_user_geometry(window, area);
		propose_user_dimensions(window, area);
	} else {
		// Restore the floating default: half the placement area,
		// centred. The window keeps where it was put.
		Rectangle area{0, 0, 0, 0};
		placement_area(&area.x, &area.y, &area.width, &area.height);
		Rectangle restored =
		    window_entry->has_user_geometry
			? window_entry->user_geometry
			: Rectangle{window_entry->x, window_entry->y,
				    window_entry->width, window_entry->height};
		restored.width = area.width / 2;
		restored.height = area.height / 2;
		propose_user_dimensions(window, restored);
	}

	request_state_update(this, true);
}

void View::set_fullscreen(struct river_window_v1 *window, bool fullscreen,
			  struct river_output_v1 *output)
{
	Window *window_entry = find_window(window);
	if (!window_entry) {
		return;
	}
	if (window_entry->fullscreen == fullscreen) {
		return;
	}

	window_entry->fullscreen = fullscreen;
	std::fprintf(stderr, "Yarfwm: fullscreen -> %s\n",
		     fullscreen ? "on" : "off");

	if (fullscreen) {
		// The compositor handles the position and dimensions while a
		// window is fullscreen: the XML says set_position and
		// propose_dimensions "shall not affect the current position
		// and dimensions of a fullscreen window". The output argument
		// is the one to fill and must not be null, so a null
		// preference (the window's event may send one) is resolved
		// here.
		Rectangle geometry =
		    window_entry->has_user_geometry
			? window_entry->user_geometry
			: Rectangle{window_entry->x, window_entry->y,
				    window_entry->width, window_entry->height};
		if (output == nullptr) {
			output = resolve_fullscreen_output(
			    outputs, output_count, geometry);
		}
		if (output == nullptr) {
			std::fprintf(stderr,
				     "Yarfwm: fullscreen: no output to fill, "
				     "ignoring\n");
			window_entry->fullscreen = false;
			return;
		}
		window_entry->fullscreen_output = output;
	} else {
		// The XML is explicit that position and dimensions are
		// undefined after exit_fullscreen "until a manage sequence in
		// which the window manager makes the propose_dimensions and
		// set_position requests is completed". So re-propose the
		// size here and let the render sequence that follows place
		// it, rather than leaving the window unplaced.
		Rectangle area{0, 0, 0, 0};
		placement_area(&area.x, &area.y, &area.width, &area.height);
		Rectangle restored =
		    window_entry->has_user_geometry
			? window_entry->user_geometry
			: Rectangle{area.x, area.y, area.width / 2,
				    area.height / 2};
		window_entry->fullscreen_output = nullptr;
		set_user_geometry(window, restored);
		propose_user_dimensions(window, restored);
	}

	request_state_update(this, true);
}

void View::toggle_fullscreen(struct river_window_v1 *window)
{
	Window *window_entry = find_window(window);
	if (!window_entry) {
		return;
	}
	// A null output means "no preference"; the compositor picks.
	set_fullscreen(window, !window_entry->fullscreen, nullptr);
}

void View::toggle_always_on_top(struct river_window_v1 *window)
{
	Window *window_entry = find_window(window);
	if (!window_entry) {
		return;
	}

	window_entry->always_on_top = !window_entry->always_on_top;
	std::fprintf(stderr, "Yarfwm: toggle_always_on_top -> %s\n",
		     window_entry->always_on_top ? "on top" : "normal");
	request_state_update(this, true);
}

void View::minimize_window(struct river_window_v1 *window)
{
	Window *window_entry = find_window(window);
	if (!window_entry || window_entry->minimized) {
		return;
	}

	// River's XML on minimize_requested: "The window manager is free to
	// ignore this request, hide the window, or do whatever else it
	// chooses." Hiding is the protocol's own answer to minimize, and there
	// is no inform_minimized request to pair with it.
	window_entry->minimized = true;
	std::fprintf(stderr, "Yarfwm: minimize_window\n");
	request_state_update(this, true);
}

void View::restore_minimized_window()
{
	// The most recently minimized window comes back, which is what the
	// taskbar or a "restore" binding means. Every window hidden for a
	// desktop other than the active one stays hidden.
	for (int i = window_count - 1; i >= 0; i--) {
		Window *window_entry = &windows[i];
		if (!window_entry->window || !window_entry->minimized) {
			continue;
		}
		window_entry->minimized = false;
		std::fprintf(stderr, "Yarfwm: restore_minimized_window\n");
		// Give the keyboard back if the seat still has nothing
		// focused; a minimize dropped the focus on the floor.
		if (seat && window_entry->focus_before_minimize) {
			seat->focus(window_entry->focus_before_minimize,
				    window_entry->window);
		}
		request_manage();
		return;
	}

	std::fprintf(stderr, "Yarfwm: restore_minimized_window: none\n");
}

void View::center_window(struct river_window_v1 *window)
{
	Window *window_entry = find_window(window);
	if (!window_entry) {
		return;
	}

	Rectangle area{0, 0, 0, 0};
	placement_area(&area.x, &area.y, &area.width, &area.height);

	// Keep the current size; only the position changes. The size comes
	// from the tracked geometry, which the dimensions event keeps current.
	int32_t width =
	    window_entry->width > 0 ? window_entry->width : area.width / 2;
	int32_t height =
	    window_entry->height > 0 ? window_entry->height : area.height / 2;

	Rectangle centred{area.x + (area.width - width) / 2,
			  area.y + (area.height - height) / 2, width, height};
	std::fprintf(stderr, "Yarfwm: center_window -> %d,%d\n", centred.x,
		     centred.y);
	set_user_geometry(window, centred);
}

void View::center_all_windows()
{
	Rectangle area{0, 0, 0, 0};
	placement_area(&area.x, &area.y, &area.width, &area.height);

	int centred = 0;
	for (int i = 0; i < window_count; i++) {
		Window *window_entry = &windows[i];
		if (!window_entry->window || !window_entry->placed) {
			continue;
		}
		int32_t width = window_entry->width > 0 ? window_entry->width
							: area.width / 2;
		int32_t height = window_entry->height > 0 ? window_entry->height
							  : area.height / 2;
		Rectangle target{area.x + (area.width - width) / 2,
				 area.y + (area.height - height) / 2, width,
				 height};
		set_user_geometry(window_entry->window, target);
		centred++;
	}

	std::fprintf(stderr, "Yarfwm: center_all_windows -> %d window(s)\n",
		     centred);
}

void View::fit_to_output(struct river_window_v1 *window)
{
	Window *window_entry = find_window(window);
	if (!window_entry) {
		return;
	}

	// The placement area rather than the raw output: fitting a window to
	// the full output would put it underneath the bars and docks.
	Rectangle area{0, 0, 0, 0};
	placement_area(&area.x, &area.y, &area.width, &area.height);
	std::fprintf(stderr, "Yarfwm: fit_to_output -> %dx%d\n", area.width,
		     area.height);
	set_user_geometry(window, area);
	propose_user_dimensions(window, area);
}

void View::resize_window(struct river_window_v1 *window, bool resize_width,
			 int percent_delta)
{
	Window *window_entry = find_window(window);
	if (!window_entry) {
		return;
	}

	Rectangle geometry =
	    window_entry->has_user_geometry
		? window_entry->user_geometry
		: Rectangle{window_entry->x, window_entry->y,
			    window_entry->width, window_entry->height};
	if (geometry.width <= 0 || geometry.height <= 0) {
		return;
	}

	// Percentage of the current size, so repeated presses compound the way
	// a user expects from a resize key.
	if (resize_width) {
		int32_t delta = geometry.width * percent_delta / 100;
		// A percentage of a small window can round to nothing; keep
		// the step meaningful so the key never feels dead.
		if (delta == 0) {
			delta = percent_delta > 0 ? 1 : -1;
		}
		geometry.width += delta;
		if (geometry.width < minimum_extent) {
			geometry.width = minimum_extent;
		}
	} else {
		int32_t delta = geometry.height * percent_delta / 100;
		if (delta == 0) {
			delta = percent_delta > 0 ? 1 : -1;
		}
		geometry.height += delta;
		if (geometry.height < minimum_extent) {
			geometry.height = minimum_extent;
		}
	}

	// Respect the window's own preferred bounds when it stated any. The
	// protocol calls them a hint ("the window manager is free to propose
	// dimensions outside of these bounds"), but a resize the user asked
	// for has no reason to fight the window's minimum or maximum.
	apply_dimension_hints(window_entry->min_width, window_entry->min_height,
			      window_entry->max_width, window_entry->max_height,
			      geometry);

	std::fprintf(stderr, "Yarfwm: set_window_%s %+d%% -> %dx%d\n",
		     resize_width ? "width" : "height", percent_delta,
		     geometry.width, geometry.height);
	// The size is a proposal and the position is unchanged, so only the
	// proposal needs to go out.
	window_entry->user_geometry = geometry;
	window_entry->has_user_geometry = true;
	propose_user_dimensions(window, geometry);
}

void View::apply_decoration_hint(struct river_window_v1 *window, uint32_t hint)
{
	Window *window_entry = find_window(window);
	if (!window_entry || window_entry->decoration_sent) {
		return;
	}
	window_entry->decoration_hint = hint;
	window_entry->decoration_sent = true;

	// The hint is what the window says it would like. River's own default
	// when the window manager sends neither request is client-side
	// decorations, and prefer_no_csd in the config only affects windows
	// that express no preference at all. The requests are allowed in
	// either sequence, so they go out in the next manage sequence.
	switch (hint) {
	case RIVER_WINDOW_V1_DECORATION_HINT_ONLY_SUPPORTS_CSD:
	case RIVER_WINDOW_V1_DECORATION_HINT_PREFERS_CSD:
		std::fprintf(stderr,
			     "Yarfwm: decoration hint -> client-side\n");
		break;
	case RIVER_WINDOW_V1_DECORATION_HINT_PREFERS_SSD:
		std::fprintf(stderr,
			     "Yarfwm: decoration hint -> server-side\n");
		break;
	case RIVER_WINDOW_V1_DECORATION_HINT_NO_PREFERENCE:
	default:
		std::fprintf(stderr,
			     "Yarfwm: decoration hint -> no preference\n");
		break;
	}
	request_manage();
}
