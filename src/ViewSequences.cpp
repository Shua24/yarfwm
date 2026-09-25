#include "Keybind.hpp"
#include "Output.hpp"
#include "Seat.hpp"
#include "View.hpp"
#include "river-window-management-v1-client-protocol.h"

#include <cstdio>

// The manage/render handshake, one function per sequence event.
//
//   manage_start -> (manage-only requests) -> manage_finish     [mandatory]
//   render_start -> (render-only requests) -> render_finish     [mandatory]
//
// River stalls its event loop until manage_finish() and render_finish() arrive,
// so every path through these two functions has to reach its finish call.
//
// Request classification used here (v5 rules, which is what river 0.4.8
// enforces):
//   manage-only   propose_dimensions, set_capabilities, close, set_default
//   render-only   show, set_position, place_*, set_borders
//
// These live apart from View.cpp, which holds the tracking state and the
// placement policy, so both files stay inside the project's line budget.

void View::window_manager_manage_start(void *data,
				       struct river_window_manager_v1 *manager)
{
	View *view = static_cast<View *>(data);
	view->pending_manage_count = view->window_count;
	// The manage sequence this flag was waiting for has arrived.
	view->manage_requested = false;

	// Manage-only requests: propose dimensions and capabilities for windows
	// we have not managed yet. show() and set_position() are render-only in
	// v5 and are deferred to the render sequence.
	for (int i = 0; i < view->window_count; i++) {
		Window *window_entry = &view->windows[i];
		if (!window_entry->window) {
			continue;
		}
		if (!window_entry->managed) {
			view->propose_default_dimensions(window_entry);
			window_entry->managed = true;
		}
	}

	// Point layer shell at an output once, so layer surfaces that do not
	// name an output land somewhere sensible. Manage sequence only.
	if (!view->default_layer_output) {
		for (int i = 0; i < view->output_count; i++) {
			if (view->outputs[i] &&
			    view->outputs[i]->set_default()) {
				view->default_layer_output = view->outputs[i];
				break;
			}
		}
	}

	// A resize the user asked for: propose_dimensions is
	// manage-sequence-only, and a node has no dimension setter, so the size
	// half of a resize goes through here. It is a proposal — the window may
	// answer with different dimensions and the recorded size follows the
	// answer.
	for (int i = 0; i < view->window_count; i++) {
		Window *window_entry = &view->windows[i];
		if (!window_entry->window || !window_entry->propose_pending) {
			continue;
		}
		river_window_v1_propose_dimensions(
		    window_entry->window, window_entry->proposed_geometry.width,
		    window_entry->proposed_geometry.height);
		window_entry->propose_pending = false;
	}

	// Window state the user asked for. Every request here is
	// manage-sequence-only, and each is sent only when the state actually
	// changed, so a manage sequence that river starts on its own does not
	// re-send what it already knows.
	for (int i = 0; i < view->window_count; i++) {
		Window *window_entry = &view->windows[i];
		if (!window_entry->window) {
			continue;
		}

		if (window_entry->maximized != window_entry->maximized_sent) {
			if (window_entry->maximized) {
				river_window_v1_inform_maximized(
				    window_entry->window);
			} else {
				river_window_v1_inform_unmaximized(
				    window_entry->window);
			}
			window_entry->maximized_sent = window_entry->maximized;
		}

		if (window_entry->fullscreen != window_entry->fullscreen_sent) {
			if (window_entry->fullscreen) {
				// The compositor owns the geometry from here.
				river_window_v1_fullscreen(
				    window_entry->window,
				    window_entry->fullscreen_output);
				river_window_v1_inform_fullscreen(
				    window_entry->window);
			} else {
				// exit_fullscreen leaves the position and
				// dimensions undefined until a manage
				// sequence containing propose_dimensions and
				// set_position completes. The proposal went
				// out above (propose_pending) and
				// place_windows() positions it on the render
				// sequence that follows.
				river_window_v1_exit_fullscreen(
				    window_entry->window);
				river_window_v1_inform_not_fullscreen(
				    window_entry->window);
			}
			window_entry->fullscreen_sent =
			    window_entry->fullscreen;
		}

		// A minimize needs no request of its own: it only flips
		// Window::minimized, and the visibility pass in the render
		// sequence hides the window (restoring shows it again).
	}

	// Close requests are manage-sequence-only; send one per window, once.
	for (int i = 0; i < view->window_count; i++) {
		Window *window_entry = &view->windows[i];
		if (!window_entry->window) {
			continue;
		}
		if (window_entry->close_requested &&
		    !window_entry->close_sent) {
			std::fprintf(
			    stderr,
			    "Yarfwm: manage: sending close request to river\n");
			river_window_v1_close(window_entry->window);
			window_entry->close_sent = true;
		}
	}

	// Keyboard state: create and enable the binding objects first (river
	// scopes a binding to a seat and only accepts enable inside a manage
	// sequence), then apply the focus intent the seat recorded since the
	// last sequence.
	if (view->keybind) {
		view->keybind->apply_manage();
	}
	if (view->seat) {
		// A seat river removed has to lose its binding objects and any
		// repeat armed on it before Seat::apply_manage() destroys the
		// proxy underneath them.
		if (view->keybind) {
			view->keybind->forget_removed_seats();
		}
		view->seat->apply_manage();
	}

	// Mandatory: end the manage sequence.
	river_window_manager_v1_manage_finish(manager);
}

void View::window_manager_render_start(void *data,
				       struct river_window_manager_v1 *manager)
{
	View *view = static_cast<View *>(data);
	view->pending_render_count = view->window_count;

	// Render-only requests: make sure every window is shown or hidden to
	// match its desktop and minimized state. River 0.4.8 (v5) documents
	// show() and hide() as render-sequence requests, while v6 allows
	// either sequence; sending them here is legal under both. Doing it
	// in one pass, next to the placement that depends on it, keeps every
	// visibility change in a single place.
	for (int i = 0; i < view->window_count; i++) {
		Window *window_entry = &view->windows[i];
		if (!window_entry->window) {
			continue;
		}

		// A window is visible when it is not minimized and its desktop
		// is the active one. River has no desktop concept, so this
		// filter is the whole of yarfwm's virtual desktops.
		const bool visible =
		    !window_entry->minimized &&
		    window_entry->desktop == view->active_desktop;
		if (visible == window_entry->shown) {
			continue;
		}
		if (visible) {
			river_window_v1_show(window_entry->window);
		} else {
			river_window_v1_hide(window_entry->window);
		}
		window_entry->shown = visible;
	}

	// Stacking requests. place_top/place_bottom/place_above are
	// render-sequence-only in v5, so every one of them goes out here:
	// a pending raise (a click, a fresh window, an un-minimize), the
	// always-on-top state when it changed, and a child window's
	// place_above its parent once the parent is known.
	for (int i = 0; i < view->window_count; i++) {
		Window *window_entry = &view->windows[i];
		if (!window_entry->window || !window_entry->node) {
			continue;
		}

		if (window_entry->raise_pending) {
			river_node_v1_place_top(window_entry->node);
			window_entry->raise_pending = false;
		}

		if (window_entry->always_on_top !=
		    window_entry->always_on_top_sent) {
			if (window_entry->always_on_top) {
				river_node_v1_place_top(window_entry->node);
				// The render list now has this window on
				// top; keep the stacking record in step or a
				// later raise would be skipped as redundant.
				window_entry->z_order = view->next_z_order++;
			} else {
				river_node_v1_place_bottom(window_entry->node);
				// ... and on the bottom now, so a focus
				// fallback does not pick it believing it is
				// still the topmost window.
				window_entry->z_order = 0;
			}
			window_entry->always_on_top_sent =
			    window_entry->always_on_top;
		}

		// A child window (a dialog, file picker, or similar) sits
		// directly above its parent, applied once as soon as the
		// parent is known and managed.
		if (window_entry->parent && !window_entry->parent_placed) {
			Window *parent_entry =
			    view->find_window(window_entry->parent);
			if (parent_entry && parent_entry->node) {
				river_node_v1_place_above(window_entry->node,
							  parent_entry->node);
				window_entry->parent_placed = true;
			}
		}
	}

	view->place_windows();

	// Mandatory: end the render sequence.
	river_window_manager_v1_render_finish(manager);
}
