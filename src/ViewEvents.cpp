#include "Display.hpp"
#include "Seat.hpp"
#include "View.hpp"
#include "river-window-management-v1-client-protocol.h"
#include <cstdio>

// Listener trampolines for the river window-management objects.
//
// Every slot is populated on purpose: libwayland aborts the process when an
// event arrives for a NULL listener slot, and windows send app_id/title early.
//
// The two sequence events (manage_start, render_start) carry the actual policy
// and live in View.cpp. Output events live in Output.cpp, layer shell seat
// events in Seat.cpp. Everything here either records state or is a no-op that
// keeps the protocol happy until the corresponding feature is implemented.

void View::window_manager_unavailable(void *data,
				      struct river_window_manager_v1 *manager)
{
	View *view = static_cast<View *>(data);
	(void)manager;
	std::fprintf(stderr, "Yarfwm: window management unavailable\n");
	view->shutdown_requested = true;
}

void View::window_manager_finished(void *data,
				   struct river_window_manager_v1 *manager)
{
	View *view = static_cast<View *>(data);
	(void)manager;
	std::fprintf(stderr, "Yarfwm: window manager finished by server\n");
	view->shutdown_requested = true;
}

void View::window_manager_session_locked(
    void *data, struct river_window_manager_v1 *manager)
{
	// XML: "The window manager may wish to restrict which key bindings are
	// available while locked or otherwise use this information." A lock
	// screen owns the keyboard, so acting on a binding would fight it for
	// focus; the key binding layer refuses everything but exit_session
	// while this is set.
	View *view = static_cast<View *>(data);
	(void)manager;
	view->session_locked = true;
}

void View::window_manager_session_unlocked(
    void *data, struct river_window_manager_v1 *manager)
{
	// The session is usable again: clear the lock guard. The guard is
	// what the key binding layer checks before every action, so a flag
	// left set here silently refuses every binding for the rest of the
	// run (att_wm clears its own flag the same way, Wm.zig:450-451).
	View *view = static_cast<View *>(data);
	(void)manager;
	view->session_locked = false;

	// The lock surface held the keyboard and river dropped it again on
	// unlock, but our recorded focus never changed, so nothing looks
	// stale. Without re-issuing it the seat sits there with no window
	// focused until the user clicks one (att_wm's restoreFocus does the
	// same).
	if (view->seat) {
		view->seat->restore_focus();
	}
}

void View::window_manager_window(void *data,
				 struct river_window_manager_v1 *manager,
				 struct river_window_v1 *window)
{
	View *view = static_cast<View *>(data);
	(void)manager;
	view->add_window(window);
}

void View::window_manager_output(void *data,
				 struct river_window_manager_v1 *manager,
				 struct river_output_v1 *output)
{
	View *view = static_cast<View *>(data);
	(void)manager;
	view->add_output(output);
}

void View::window_manager_seat(void *data,
			       struct river_window_manager_v1 *manager,
			       struct river_seat_v1 *seat)
{
	View *view = static_cast<View *>(data);
	(void)manager;
	if (view->seat && view->display) {
		view->seat->attach_river_seat(
		    seat, view->display->layer_shell_state, view);
	}
}

void View::window_closed(void *data, struct river_window_v1 *window)
{
	View *view = static_cast<View *>(data);
	view->remove_window(window);
}

void View::window_dimensions_hint(void *data, struct river_window_v1 *window,
				  int32_t min_width, int32_t min_height,
				  int32_t max_width, int32_t max_height)
{
	// The window's preferred size bounds. River documents them as a
	// hint, and records the constraints the compositor guarantees: every
	// value is >= 0, 0 means no preference, and when a max is set the min
	// is <= it. Stored so a user resize can respect them; the
	// window manager is free to propose outside them.
	View *view = static_cast<View *>(data);
	Window *window_entry = view->find_window(window);
	if (!window_entry) {
		return;
	}
	window_entry->min_width = min_width;
	window_entry->min_height = min_height;
	window_entry->max_width = max_width;
	window_entry->max_height = max_height;
}

void View::window_dimensions(void *data, struct river_window_v1 *window,
			     int32_t width, int32_t height)
{
	View *view = static_cast<View *>(data);
	Window *window_entry = view->find_window(window);
	if (!window_entry) {
		return;
	}

	if (width > 0 && height > 0) {
		// Directional focus works on the negotiated size.
		window_entry->width = width;
		window_entry->height = height;
		window_entry->rendered = true;
	}
}

void View::window_app_id(void *data, struct river_window_v1 *window,
			 const char *app_id)
{
	// May be null: the XML says the argument is null when the window has
	// never set an app_id or has cleared it.
	View *view = static_cast<View *>(data);
	Window *window_entry = view->find_window(window);
	if (!window_entry) {
		return;
	}
	std::snprintf(window_entry->app_id, sizeof(window_entry->app_id), "%s",
		      app_id ? app_id : "");
}

void View::window_title(void *data, struct river_window_v1 *window,
			const char *title)
{
	// May be null, for the same reason as app_id.
	View *view = static_cast<View *>(data);
	Window *window_entry = view->find_window(window);
	if (!window_entry) {
		return;
	}
	std::snprintf(window_entry->title, sizeof(window_entry->title), "%s",
		      title ? title : "");
}

void View::window_parent(void *data, struct river_window_v1 *window,
			 struct river_window_v1 *parent)
{
	View *view = static_cast<View *>(data);
	Window *window_entry = view->find_window(window);
	if (!window_entry) {
		return;
	}
	window_entry->parent = parent;

	// XML: "Child windows should generally be rendered directly above
	// their parent." place_above is an either-sequence request; a manage
	// sequence is asked for because that is the only sequence the window
	// manager can request from river, and the parent may not be managed
	// yet (river can report a parent before the parent window itself), so
	// it is applied when the manage sequence runs.
	if (parent) {
		view->request_manage();
	}
}

void View::window_decoration_hint(void *data, struct river_window_v1 *window,
				  uint32_t hint)
{
	View *view = static_cast<View *>(data);
	view->apply_decoration_hint(window, hint);
}

void View::window_pointer_move_requested(void *data,
					 struct river_window_v1 *window,
					 struct river_seat_v1 *river_seat)
{
	// The window asked to be dragged, which is what a client-side titlebar
	// does. River's XML: the window manager "may use the
	// river_seat_v1.op_start_pointer request to interactively move the
	// window or ignore this event entirely".
	View *view = static_cast<View *>(data);
	if (view->seat) {
		view->seat->start_pointer_operation(window, river_seat, false,
						    0);
	}
}

void View::window_pointer_resize_requested(void *data,
					   struct river_window_v1 *window,
					   struct river_seat_v1 *river_seat,
					   uint32_t edges)
{
	View *view = static_cast<View *>(data);
	if (view->seat) {
		view->seat->start_pointer_operation(window, river_seat, true,
						    edges);
	}
}

void View::window_show_window_menu_requested(void *data,
					     struct river_window_v1 *window,
					     int32_t x, int32_t y)
{
	(void)data;
	(void)window;
	(void)x;
	(void)y;
}

void View::window_maximize_requested(void *data, struct river_window_v1 *window)
{
	// The window's own maximize button (or an xdg-shell maximize request).
	// Same path as the key binding, so the two cannot drift.
	View *view = static_cast<View *>(data);
	view->toggle_maximize(window);
}

void View::window_unmaximize_requested(void *data,
				       struct river_window_v1 *window)
{
	View *view = static_cast<View *>(data);
	view->toggle_maximize(window);
}

void View::window_fullscreen_requested(void *data,
				       struct river_window_v1 *window,
				       struct river_output_v1 *output)
{
	// output is the window's preference and may be null, meaning the
	// window manager picks.
	View *view = static_cast<View *>(data);
	view->set_fullscreen(window, true, output);
}

void View::window_exit_fullscreen_requested(void *data,
					    struct river_window_v1 *window)
{
	View *view = static_cast<View *>(data);
	view->set_fullscreen(window, false, nullptr);
}

void View::window_minimize_requested(void *data, struct river_window_v1 *window)
{
	// River's XML: "The window manager is free to ignore this request,
	// hide the window, or do whatever else it chooses."
	View *view = static_cast<View *>(data);
	view->minimize_window(window);
}

void View::window_unreliable_pid(void *data, struct river_window_v1 *window,
				 int32_t pid)
{
	// XML: "Obtaining this information is inherently racy due to PID
	// reuse. Therefore, this PID must not be used for anything security
	// sensitive." Recorded for diagnostics only; nothing keys off it.
	View *view = static_cast<View *>(data);
	Window *window_entry = view->find_window(window);
	if (window_entry) {
		window_entry->unreliable_pid = pid;
	}
}

void View::window_presentation_hint(void *data, struct river_window_v1 *window,
				    uint32_t hint)
{
	// The window's preferred presentation mode (vsync/async/tearing).
	// River owns the actual presentation: the mode enum lives on
	// river_output_v1 and this is only a preference the window stated,
	// with no request on river_window_v1 to act on it. Recorded in the
	// log so a debugging session can see it; nothing to send.
	View *view = static_cast<View *>(data);
	Window *window_entry = view->find_window(window);
	if (window_entry && window_entry->title[0] != '\0') {
		std::fprintf(stderr,
			     "Yarfwm: window '%s' prefers presentation mode "
			     "%u\n",
			     window_entry->title, hint);
	}
}

void View::window_identifier(void *data, struct river_window_v1 *window,
			     const char *identifier)
{
	// XML: sent once at creation, "up to 32 printable ASCII bytes", "must
	// not be an empty string", unique per window and never reused. It is
	// the stable name a task list or IPC layer should key on, unlike the
	// title.
	View *view = static_cast<View *>(data);
	Window *window_entry = view->find_window(window);
	if (!window_entry) {
		return;
	}
	std::snprintf(window_entry->identifier,
		      sizeof(window_entry->identifier), "%s",
		      identifier ? identifier : "");
}

void View::window_capture_sessions(void *data, struct river_window_v1 *window,
				   uint32_t count)
{
	// XML: "the number of active screen capture sessions for the window",
	// sent once at creation and again whenever it changes. There is no
	// per-session object to track; the count is all river reports.
	View *view = static_cast<View *>(data);
	Window *window_entry = view->find_window(window);
	if (window_entry) {
		window_entry->capture_count = count;
	}
}

void View::window_touch_move_requested(void *data,
				       struct river_window_v1 *window,
				       struct river_seat_v1 *seat,
				       int32_t touch_point)
{
	// The touch equivalent of pointer_move_requested: a window dragged by
	// a finger, and the window manager "may use the
	// river_seat_v1.op_start_touch request to interactively move the
	// window or ignore this event entirely".
	View *view = static_cast<View *>(data);
	if (view->seat) {
		view->seat->start_touch_operation(window, seat, false, 0,
						  touch_point);
	}
}

void View::window_touch_resize_requested(void *data,
					 struct river_window_v1 *window,
					 struct river_seat_v1 *seat,
					 int32_t touch_point, uint32_t edges)
{
	// XML: "The edges argument ... will never be none and will never have
	// both top and bottom or both left and right edges set."
	View *view = static_cast<View *>(data);
	if (view->seat) {
		view->seat->start_touch_operation(window, seat, true, edges,
						  touch_point);
	}
}
