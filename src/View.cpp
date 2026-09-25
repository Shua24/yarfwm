#include "View.hpp"
#include "Config.hpp"
#include "Display.hpp"
#include "Keybind.hpp"
#include "Output.hpp"
#include "Seat.hpp"
#include "river-window-management-v1-client-protocol.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

// Window management policy and render state.
//
// The manage/render handshake (river-window-management-v1; v5 rules are what
// river 0.4.8 enforces) is implemented in ViewSequences.cpp:
//
//   manage_start -> (manage-only requests) -> manage_finish     [mandatory]
//   render_start -> (render-only requests) -> render_finish     [mandatory]
//
// This file holds the tracking state (windows, outputs) and the placement
// policy those sequences apply.
//
// Layer shell (river_layer_shell_v1) state is created per output and per seat
// when river hands us those objects; the per-output non-exclusive area is the
// placement hint for windows and set_default (manage sequence only) points new
// layer surfaces at an output.
//
// The listener trampolines live in ViewEvents.cpp.

View::View()
    : seat(nullptr), display(nullptr), manager(nullptr), windows(nullptr),
      window_count(0), window_capacity(0), outputs(nullptr), output_count(0),
      output_capacity(0), default_layer_output(nullptr),
      pending_manage_count(0), pending_render_count(0),
      shutdown_requested(false), manage_requested(false), keybind(nullptr),
      session_locked(false), active_desktop(0)
{
}

View::~View() { terminate(); }

bool View::initialize(Server *server, Seat *seat, Display *display,
		      Config &config)
{
	// Dynamic memory: window and output tracking arrays grow on demand.
	(void)server;
	(void)config;
	this->seat = seat;
	this->display = display;

	if (!display->window_manager) {
		std::fprintf(stderr,
			     "Yarfwm: no river_window_manager_v1 available\n");
		return false;
	}

	manager = display->window_manager;

	static const struct river_window_manager_v1_listener manager_listener =
	    {
		window_manager_unavailable,    window_manager_finished,
		window_manager_manage_start,   window_manager_render_start,
		window_manager_session_locked, window_manager_session_unlocked,
		window_manager_window,	       window_manager_output,
		window_manager_seat,
	    };

	if (river_window_manager_v1_add_listener(manager, &manager_listener,
						 this) != 0) {
		std::fprintf(stderr,
			     "Yarfwm: failed to add window manager listener\n");
		return false;
	}

	return true;
}

void View::terminate()
{
	// Dynamic memory: free tracked windows and outputs.
	for (int i = 0; i < window_count; i++) {
		Window *window_entry = &windows[i];
		if (window_entry->node) {
			river_node_v1_destroy(window_entry->node);
			window_entry->node = nullptr;
		}
		if (window_entry->window) {
			river_window_v1_destroy(window_entry->window);
			window_entry->window = nullptr;
		}
	}

	if (windows) {
		std::free(windows);
		windows = nullptr;
	}

	// Output objects are heap-allocated and own their protocol objects;
	// delete runs the Output destructor, which destroys the layer shell
	// output state and the river_output_v1 proxy.
	for (int i = 0; i < output_count; i++) {
		delete outputs[i];
		outputs[i] = nullptr;
	}

	if (outputs) {
		std::free(outputs);
		outputs = nullptr;
	}

	window_count = 0;
	window_capacity = 0;
	output_count = 0;
	output_capacity = 0;
	default_layer_output = nullptr;
	pending_manage_count = 0;
	pending_render_count = 0;
	manager = nullptr;
}

bool View::should_shutdown() const { return shutdown_requested; }

View::Window *View::find_window(struct river_window_v1 *window) const
{
	for (int i = 0; i < window_count; i++) {
		if (windows[i].window == window) {
			return &windows[i];
		}
	}
	return nullptr;
}

bool View::window_entry_is_visible(const Window *window_entry,
				   int active_desktop)
{
	if (!window_entry->window) {
		return false;
	}
	return !window_entry->minimized &&
	       window_entry->desktop == active_desktop;
}

bool View::window_is_visible(struct river_window_v1 *window) const
{
	const Window *window_entry = find_window(window);
	return window_entry != nullptr &&
	       window_entry_is_visible(window_entry, active_desktop);
}

struct river_window_v1 *View::first_visible_window() const
{
	for (int i = 0; i < window_count; i++) {
		if (window_entry_is_visible(&windows[i], active_desktop)) {
			return windows[i].window;
		}
	}
	return nullptr;
}

void View::hand_focus_to_visible_window(struct river_seat_v1 *river_seat)
{
	if (!seat || !river_seat) {
		return;
	}

	// A focused window that is still visible keeps the keyboard: a
	// desktop switch that did not touch it must not move the focus. The
	// seat's recorded intent counts as focused here, so a click whose
	// manage sequence has not run yet cannot be overwritten by a stale
	// applied focus.
	struct river_window_v1 *focused = seat->focus_intent(river_seat);
	if (focused && window_is_visible(focused)) {
		return;
	}

	// Prefer the window the user was on before this one when it is
	// still visible: it is where the focus came from, and where
	// focus_window_previous would return to.
	struct river_window_v1 *next =
	    seat->previous_focused_window(river_seat);
	if (!next || !window_is_visible(next)) {
		next = first_visible_window();
	}
	if (next) {
		seat->focus(river_seat, next);
	} else {
		// The active desktop is empty: the keyboard has nowhere to
		// go, and leaving it on a hidden window is the bug this
		// whole path exists to avoid.
		seat->focus_none(river_seat);
	}
}

void View::add_window(struct river_window_v1 *window)
{
	// Dynamic memory: grow the window array when needed.
	if (window_count >= window_capacity) {
		int new_capacity =
		    window_capacity == 0 ? 8 : window_capacity * 2;
		Window *new_windows = static_cast<Window *>(std::realloc(
		    windows,
		    sizeof(Window) * static_cast<size_t>(new_capacity)));
		if (!new_windows) {
			std::fprintf(
			    stderr,
			    "Yarfwm: failed to allocate window storage\n");
			return;
		}
		windows = new_windows;
		window_capacity = new_capacity;
	}

	Window *window_entry = &windows[window_count];
	std::memset(window_entry, 0, sizeof(*window_entry));
	window_entry->window = window;
	// A new window joins the desktop the user is looking at, and river
	// considers a new window shown until told otherwise, so the cached
	// visibility starts true and the first render pass sends nothing.
	window_entry->desktop = active_desktop;
	window_entry->shown = true;

	// Single get_node call: the returned proxy is owned by this window
	// entry.
	window_entry->node = river_window_v1_get_node(window);
	if (window_entry->node == nullptr) {
		std::fprintf(stderr, "Yarfwm: failed to get window node\n");
		return;
	}

	static const struct river_window_v1_listener window_listener = [] {
		struct river_window_v1_listener listener{};
		listener.closed = window_closed;
		listener.dimensions_hint = window_dimensions_hint;
		listener.dimensions = window_dimensions;
		listener.app_id = window_app_id;
		listener.title = window_title;
		listener.parent = window_parent;
		listener.decoration_hint = window_decoration_hint;
		listener.pointer_move_requested = window_pointer_move_requested;
		listener.pointer_resize_requested =
		    window_pointer_resize_requested;
		listener.show_window_menu_requested =
		    window_show_window_menu_requested;
		listener.maximize_requested = window_maximize_requested;
		listener.unmaximize_requested = window_unmaximize_requested;
		listener.fullscreen_requested = window_fullscreen_requested;
		listener.exit_fullscreen_requested =
		    window_exit_fullscreen_requested;
		listener.minimize_requested = window_minimize_requested;
		listener.unreliable_pid = window_unreliable_pid;
		listener.presentation_hint = window_presentation_hint;
		listener.identifier = window_identifier;
		listener.capture_sessions = window_capture_sessions;
		listener.touch_move_requested = window_touch_move_requested;
		listener.touch_resize_requested = window_touch_resize_requested;
		return listener;
	}();

	if (river_window_v1_add_listener(window, &window_listener, this) != 0) {
		std::fprintf(stderr, "Yarfwm: failed to add window listener\n");
	}

	// Count the window only once it is fully registered.
	window_count++;

	// A newly mapped window takes the keyboard, as in att_wm.
	if (seat) {
		struct river_seat_v1 *river_seat = seat->primary_river_seat();
		if (river_seat) {
			seat->focus(river_seat, window);
		}
	}
}

void View::remove_window(struct river_window_v1 *window)
{
	for (int i = 0; i < window_count; i++) {
		if (windows[i].window == window) {
			if (windows[i].node) {
				river_node_v1_destroy(windows[i].node);
				windows[i].node = nullptr;
			}
			river_window_v1_destroy(windows[i].window);
			windows[i].window = nullptr;

			if (i < window_count - 1) {
				windows[i] = windows[window_count - 1];
			}
			window_count--;

			// Tell the seat the window is gone only now that it is
			// out of the tracked array: the seat asks the View for
			// a fallback window, and while the dying window is
			// still tracked the fallback can be the dying window
			// itself — focusing a destroyed proxy segfaults. This
			// is att_wm's reap order (Wm.zig:559-585: drop the
			// window from the list, then fix up focus).
			if (seat) {
				seat->forget_window(window);
			}
			return;
		}
	}
}

void View::add_output(struct river_output_v1 *output)
{
	// Dynamic memory: grow the output pointer array when needed.
	if (output_count >= output_capacity) {
		int new_capacity =
		    output_capacity == 0 ? 4 : output_capacity * 2;
		Output **new_outputs = static_cast<Output **>(std::realloc(
		    outputs,
		    sizeof(Output *) * static_cast<size_t>(new_capacity)));
		if (!new_outputs) {
			std::fprintf(
			    stderr,
			    "Yarfwm: failed to allocate output storage\n");
			return;
		}
		outputs = new_outputs;
		output_capacity = new_capacity;
	}

	// Dynamic memory: one Output object per river output, freed in
	// remove_output() or terminate().
	Output *output_entry = new (std::nothrow) Output();
	if (!output_entry) {
		std::fprintf(stderr,
			     "Yarfwm: failed to allocate output entry\n");
		return;
	}

	if (!output_entry->initialize(display->layer_shell_state, output,
				      this)) {
		delete output_entry;
		return;
	}

	outputs[output_count++] = output_entry;
}

void View::remove_output(Output *output_entry)
{
	for (int i = 0; i < output_count; i++) {
		if (outputs[i] == output_entry) {
			if (default_layer_output == output_entry) {
				default_layer_output = nullptr;
			}
			// Dynamic memory: destroys the protocol objects the
			// entry owns and frees the entry itself.
			output_entry->terminate();
			delete output_entry;
			if (i < output_count - 1) {
				outputs[i] = outputs[output_count - 1];
			}
			output_count--;
			return;
		}
	}
}

void View::placement_area(int32_t *x, int32_t *y, int32_t *width,
			  int32_t *height) const
{
	// Prefer the first output's non-exclusive area: the rectangle left
	// after subtracting the exclusive zones of layer surfaces such as bars.
	// Fall back to the plain output rectangle, then to a conservative
	// default until the compositor tells us more.
	for (int i = 0; i < output_count; i++) {
		Output *output_entry = outputs[i];
		if (!output_entry) {
			continue;
		}

		if (output_entry->has_non_exclusive_area &&
		    output_entry->non_exclusive_area_width > 0 &&
		    output_entry->non_exclusive_area_height > 0) {
			*x = output_entry->non_exclusive_area_x;
			*y = output_entry->non_exclusive_area_y;
			*width = output_entry->non_exclusive_area_width;
			*height = output_entry->non_exclusive_area_height;
			return;
		}

		if (output_entry->has_dimensions && output_entry->width > 0 &&
		    output_entry->height > 0) {
			*x = output_entry->x;
			*y = output_entry->y;
			*width = output_entry->width;
			*height = output_entry->height;
			return;
		}
	}

	*x = 0;
	*y = 0;
	*width = 800;
	*height = 600;
}

void View::propose_default_dimensions(Window *window) const
{
	if (!window || !window->window) {
		return;
	}

	int32_t area_x = 0;
	int32_t area_y = 0;
	int32_t area_width = 0;
	int32_t area_height = 0;
	placement_area(&area_x, &area_y, &area_width, &area_height);
	(void)area_x;
	(void)area_y;

	// A sane floating default: half the output in each direction, leaving
	// room to move the window around. The window may pick different
	// dimensions.
	river_window_v1_propose_dimensions(window->window, area_width / 2,
					   area_height / 2);
	river_window_v1_set_capabilities(
	    window->window, RIVER_WINDOW_V1_CAPABILITIES_MAXIMIZE |
				RIVER_WINDOW_V1_CAPABILITIES_FULLSCREEN |
				RIVER_WINDOW_V1_CAPABILITIES_MINIMIZE);
}

void View::place_windows()
{
	int32_t area_x = 0;
	int32_t area_y = 0;
	int32_t area_width = 0;
	int32_t area_height = 0;
	placement_area(&area_x, &area_y, &area_width, &area_height);
	const Rectangle area{area_x, area_y, area_width, area_height};

	int x = area_x;
	int y = area_y;

	for (int i = 0; i < window_count; i++) {
		Window *window_entry = &windows[i];
		if (!window_entry->window || !window_entry->node) {
			continue;
		}

		// A window the user moved keeps its position: re-cascading it
		// here would snap it back on the next render sequence. The
		// cascade counter is not advanced for it either, so a cascade
		// and a manually placed window coexist.
		//
		// set_position is render-sequence-only in v5, which is exactly
		// where this runs (View::window_manager_render_start).
		if (window_entry->has_user_geometry) {
			river_node_v1_set_position(
			    window_entry->node, window_entry->user_geometry.x,
			    window_entry->user_geometry.y);
			window_entry->placed = true;
			window_entry->x = window_entry->user_geometry.x;
			window_entry->y = window_entry->user_geometry.y;
			continue;
		}

		// Cascade floating placement, wrapping inside the placement
		// area. The step and the wrap are the tested arithmetic in
		// src/Placement.cpp. The window is placed at the current
		// position and the cascade advances afterwards, which is the
		// order the original placement used: the first window lands at
		// the area origin, the second one step in.
		river_node_v1_set_position(window_entry->node, x, y);
		window_entry->placed = true;
		// Remember where the window was put: directional focus works on
		// it.
		window_entry->x = x;
		window_entry->y = y;

		// Written through pointers: returning a Rectangle by value
		// trips -Waggregate-return, and this codebase's placement
		// helpers all take output parameters.
		cascade_next(area, x, y, &x, &y);
	}
}
