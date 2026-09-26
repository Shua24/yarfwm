#ifndef VIEW_HPP
#define VIEW_HPP

#include "DecorationGeometry.hpp"
#include "DecorationSettings.hpp"
#include "Placement.hpp"
#include "Server.hpp"
#include "river-window-management-v1-client-protocol.h"

class Display;
class Seat;
class Config;
class Output;
class Keybind;
class Decoration;
struct wl_surface;
// Only ever used through a reference here; the definition lives in
// WindowDecorationsConfig.hpp, which View.cpp includes.
struct WindowDecorationsConfig;

// Represents the window management policy and render state.
class View
{
      public:
	View();
	~View();

	bool initialize(Server *server, Seat *seat, Display *display,
			Config &config,
			const WindowDecorationsConfig &decoration_config);
	void terminate();

	// Set when the server tells us to stop (unavailable/finished). The main
	// loop polls this; nothing is destroyed from inside an event callback.
	bool should_shutdown() const;

	// Internal API used by Output's event handlers (an output is removed
	// when river sends river_output_v1.removed, not when the View decides).
	void remove_output(Output *output_entry);

	// The Keybind engine registers itself so that the manage sequence can
	// create and enable its binding objects, which river only accepts
	// inside one.
	void set_keybind(Keybind *keybind);

	// Ask river for a manage sequence. Key bindings and focus intent are
	// state river cannot see for itself, so they need one.
	void request_manage();

	// Ask for a window to be closed: recorded now, sent as
	// river_window_v1.close during the next manage sequence (the request is
	// manage-sequence-only).
	void request_close(struct river_window_v1 *window);

	// End the whole Wayland session: ask river to exit the compositor,
	// which disconnects every client including this one. River does NOT
	// exit when its window manager disconnects, so this is the only way to
	// leave a session from inside the window manager. River's XML asks that
	// this be sent only when the user explicitly wants the session to end,
	// so it is never sent on ordinary window manager termination.
	void request_exit_session();

	// Directional focus: the nearest window whose centre lies in the given
	// direction from the given window's centre.
	struct river_window_v1 *
	window_in_direction(struct river_window_v1 *from,
			    enum FocusDirection direction) const;

	// Record where the user put a window: an interactive move, or the
	// keyboard move action. Once set, place_windows() stops re-cascading
	// that window, so the position survives the next render sequence. The
	// position is applied by place_windows(); the size half of a resize
	// goes through propose_user_dimensions() instead, because a node has no
	// dimension setter.
	void set_user_geometry(struct river_window_v1 *window,
			       const Rectangle &geometry);

	// Ask the window to take a new size. river_window_v1.propose_dimensions
	// is manage-sequence-only, so the request is recorded here and sent by
	// View::window_manager_manage_start(). This is a proposal: a window may
	// answer with different dimensions (a minimum size, for instance), and
	// the recorded size follows whatever river reports back.
	void propose_user_dimensions(struct river_window_v1 *window,
				     const Rectangle &geometry);

	// Move the focused window by one step in the given direction, for the
	// move_window_* keybinds. A window with no user geometry yet starts
	// from where the cascade put it.
	void move_window(struct river_window_v1 *window,
			 enum FocusDirection direction);

	// The geometry the View currently has for a window, as placement and
	// the dimensions events last set it. Returns false when the window is
	// not tracked or has not been placed yet, in which case there is
	// nothing to start an interactive operation from.
	bool window_geometry(struct river_window_v1 *window,
			     Rectangle *geometry) const;

	// Raise a window to the front of the stacking order. place_top is
	// render-sequence-only in v5, so the request is recorded and sent by
	// the next render sequence; the stacking record advances immediately
	// so a focus fallback that runs before that sequence already sees
	// the new order.
	void raise_window(struct river_window_v1 *window);

	// The topmost visible window: the highest stacking order among the
	// windows on the active desktop. This is the focus fallback for a
	// close, a minimize, a desktop switch and a lock restore — the
	// choice labwc's desktop_focus_topmost_view() makes. Only windows on
	// the active desktop are candidates: a hidden window must never take
	// the keyboard.
	struct river_window_v1 *topmost_visible_window() const;

	// Whether a window is on screen right now: it is not minimized and
	// its desktop is the active one. Every focus hand-off filters its
	// target through this, so the keyboard never lands on a hidden
	// window.
	bool window_is_visible(struct river_window_v1 *window) const;

	// Window state actions. Each is reachable two ways: from a key binding,
	// and from the window's own request event (a client-side decoration
	// button sends maximize_requested and friends). Both paths land here so
	// the behaviour cannot drift between them.
	//
	// The requests they lead to are manage-sequence-only
	// (inform_maximized, inform_unmaximized, fullscreen, exit_fullscreen),
	// so the state is recorded and applied by
	// window_manager_manage_start(); the position half is applied by
	// place_windows() on the render sequence that follows.
	void toggle_maximize(struct river_window_v1 *window);
	void set_fullscreen(struct river_window_v1 *window, bool fullscreen,
			    struct river_output_v1 *output);

	// Flip a window's fullscreen state. The window's own request event
	// always states which way it wants, so only the key binding needs to
	// decide; it comes through here.
	void toggle_fullscreen(struct river_window_v1 *window);
	void toggle_always_on_top(struct river_window_v1 *window);

	// Minimize and restore: labwc's Iconify semantics (src/view.c:784-816).
	//
	// Minimize works on the whole hierarchy — a dialog and its toplevel go
	// together, whichever asked — and hands the keyboard to the topmost
	// visible window, labwc's desktop_focus_topmost_view().
	//
	// Restore brings back the most recently minimized hierarchy (by
	// minimize order, not array position), brings its desktop forward and
	// hands it the keyboard. It is the only way back: river drops a panel's
	// activate request (panel-taskbar.md).
	void minimize_window(struct river_window_v1 *window);
	void restore_minimized_window();

	// Geometry actions, all computed inside the placement area (the output
	// rectangle minus the exclusive zones of bars and docks).
	void center_window(struct river_window_v1 *window);
	void center_all_windows();
	void fit_to_output(struct river_window_v1 *window);
	// Grow or shrink a window by a percentage of its current size. A
	// negative delta shrinks. The size is a proposal: the window may keep
	// its own minimum.
	void resize_window(struct river_window_v1 *window, bool resize_width,
			   int percent_delta);

	// Virtual desktops. River has no desktop concept at all — there is no
	// workspace, tag or desktop interface in the protocol — so yarfwm
	// implements them the only way the protocol allows: the windows that
	// are not on the active desktop are hidden with river_window_v1.hide.
	//
	// Both actions only change the desktop state and ask for a manage
	// sequence; the visibility pass in window_manager_render_start() is
	// the single place that turns the state into show/hide requests. If
	// the focused window ends up on a desktop that is not visible, the
	// keyboard is handed to a visible window (or cleared).
	void focus_desktop(struct river_seat_v1 *river_seat, int delta);
	void move_window_to_desktop(struct river_seat_v1 *river_seat,
				    struct river_window_v1 *window, int delta);

	// Bring the active desktop to a window's desktop, so the window becomes
	// visible. labwc switches workspace to make a view visible before
	// focusing it (src/desktop.c:142-148); restore needs the same, or a
	// window minimized on another desktop would restore invisibly.
	void switch_to_window_desktop(struct river_window_v1 *window);

	// Handle a decoration hint from river. Nothing is sent back, on
	// purpose: river's default when neither use_csd nor use_ssd is sent
	// is client-side decorations, and honouring a server-side hint would
	// mean drawing the decoration here, which yarfwm does not do.
	void apply_decoration_hint(struct river_window_v1 *window,
				   uint32_t hint);

	// Turn decorations on or off. Off destroys every decoration object; on
	// recreates them lazily on the next render sequence. The request is
	// recorded and applied by the render pass, so it is safe to call from a
	// key binding.
	void set_decorations_enabled(bool enabled);
	bool decorations_enabled() const { return decorations_on; }

	// The window a titlebar click belongs to, for Seat to start a move on.
	// Null when the surface belongs to no tracked window.
	struct river_window_v1 *
	window_for_decoration_surface(struct wl_surface *surface) const;

	// What part of a window's titlebar a surface-local point falls in. Used
	// by the Seat's pointer handler to decide between a move and a button.
	TitlebarPart decoration_part_at(struct river_window_v1 *window,
					int32_t x, int32_t y) const;

	// One press on our own titlebar arrives TWICE: as a wl_pointer.button
	// on the decoration surface, and then as a river_seat_v1
	// window_interaction for the same window. The button handler acts
	// first and claims the press here; river_seat_window_interaction
	// consumes the claim and returns early instead of re-queueing focus
	// for a window the handler may have just minimized or closed -- which
	// would make a minimized window pop straight back up, because
	// Seat::record_focus only queues intent and Seat::apply_manage drains
	// it later.
	//
	// Called from Seat::pointer_button when a press lands on a decoration
	// surface, before acting on the titlebar part.
	void claim_decoration_click(struct river_window_v1 *window)
	{
		decoration_click_claimed = window;
	}

	// Called from Seat::river_seat_window_interaction. True exactly once
	// per claimed press.
	bool consume_decoration_click(struct river_window_v1 *window)
	{
		if (decoration_click_claimed == window) {
			decoration_click_claimed = nullptr;
			return true;
		}
		return false;
	}

	// Retire a claim that will never be consumed. Called from
	// Seat::pointer_enter when the pointer enters a surface that is not one
	// of our decorations -- the only place a stale claim can be dropped
	// safely, because window_interaction always follows its own press, so
	// by then any claim has either been consumed or belonged to a press
	// that produced no interaction at all. Without this, a claim from a
	// press river ignored would swallow the next genuine content click.
	void clear_decoration_click_claim()
	{
		decoration_click_claimed = nullptr;
	}

	// How many virtual desktops exist. Public because the wrap helper
	// needs it and the key binding layer reports it.
	static int desktop_total();

	// Whether a lock screen currently holds the keyboard. The key binding
	// layer refuses everything except exit_session while true, so a
	// binding cannot fight the lock screen for focus.
	bool session_is_locked() const { return session_locked; }

	// The session lock events, public so the unit tests can drive the
	// guard lifecycle without a Wayland connection. The listener wiring
	// still happens in initialize().
	static void
	window_manager_session_locked(void *data,
				      struct river_window_manager_v1 *manager);
	static void window_manager_session_unlocked(
	    void *data, struct river_window_manager_v1 *manager);

      private:
	// Forward declaration: the definition is the `struct View::Window` in
	// ViewWindow.hpp, included at the very end of this file. The class body
	// below only ever uses Window through a pointer, so an incomplete type
	// is enough here; the definition must follow the complete class.
	struct Window;

	// Manager events.
	static void
	window_manager_manage_start(void *data,
				    struct river_window_manager_v1 *manager);
	static void
	window_manager_render_start(void *data,
				    struct river_window_manager_v1 *manager);
	static void
	window_manager_window(void *data,
			      struct river_window_manager_v1 *manager,
			      struct river_window_v1 *window);
	static void
	window_manager_output(void *data,
			      struct river_window_manager_v1 *manager,
			      struct river_output_v1 *output);
	static void window_manager_seat(void *data,
					struct river_window_manager_v1 *manager,
					struct river_seat_v1 *seat);
	static void
	window_manager_unavailable(void *data,
				   struct river_window_manager_v1 *manager);
	static void
	window_manager_finished(void *data,
				struct river_window_manager_v1 *manager);
	// Window events. Every slot must be non-NULL: libwayland aborts the
	// process when an event arrives for a NULL listener slot.
	static void window_closed(void *data, struct river_window_v1 *window);
	static void
	window_dimensions_hint(void *data, struct river_window_v1 *window,
			       int32_t min_width, int32_t min_height,
			       int32_t max_width, int32_t max_height);
	static void window_dimensions(void *data,
				      struct river_window_v1 *window,
				      int32_t width, int32_t height);
	static void window_app_id(void *data, struct river_window_v1 *window,
				  const char *app_id);
	static void window_title(void *data, struct river_window_v1 *window,
				 const char *title);
	static void window_parent(void *data, struct river_window_v1 *window,
				  struct river_window_v1 *parent);
	static void window_decoration_hint(void *data,
					   struct river_window_v1 *window,
					   uint32_t hint);
	static void
	window_pointer_move_requested(void *data,
				      struct river_window_v1 *window,
				      struct river_seat_v1 *seat);
	static void window_pointer_resize_requested(
	    void *data, struct river_window_v1 *window,
	    struct river_seat_v1 *seat, uint32_t edges);
	static void window_show_window_menu_requested(
	    void *data, struct river_window_v1 *window, int32_t x, int32_t y);
	static void window_maximize_requested(void *data,
					      struct river_window_v1 *window);
	static void window_unmaximize_requested(void *data,
						struct river_window_v1 *window);
	static void window_fullscreen_requested(void *data,
						struct river_window_v1 *window,
						struct river_output_v1 *output);
	static void
	window_exit_fullscreen_requested(void *data,
					 struct river_window_v1 *window);
	static void window_minimize_requested(void *data,
					      struct river_window_v1 *window);
	static void window_unreliable_pid(void *data,
					  struct river_window_v1 *window,
					  int32_t pid);
	static void window_presentation_hint(void *data,
					     struct river_window_v1 *window,
					     uint32_t hint);
	static void window_identifier(void *data,
				      struct river_window_v1 *window,
				      const char *identifier);
	static void window_capture_sessions(void *data,
					    struct river_window_v1 *window,
					    uint32_t count);
	static void window_touch_move_requested(void *data,
						struct river_window_v1 *window,
						struct river_seat_v1 *seat,
						int32_t touch_point);
	static void window_touch_resize_requested(
	    void *data, struct river_window_v1 *window,
	    struct river_seat_v1 *seat, int32_t touch_point, uint32_t edges);

	Window *find_window(struct river_window_v1 *window) const;
	void add_window(struct river_window_v1 *window);
	void remove_window(struct river_window_v1 *window);

	// The visibility predicate for the virtual desktops: a window is
	// visible when it is not minimized and its desktop is the active
	// one. The render pass is the single place that turns the difference
	// between this and Window::shown into show/hide requests, and every
	// focus hand-off filters its target through it as well.
	static bool window_entry_is_visible(const Window *window_entry,
					    int active_desktop);

	// Fill parent_index with, for each tracked entry, the index of its
	// parent entry or -1 when it has none. This is the tree shape the pure
	// helpers in src/Placement.cpp walk, which is how the walking logic
	// stays unit testable without a Wayland connection. Returns false when
	// the array is unusable.
	bool build_parent_index(int *parent_index) const;

	// How many tracked windows a hierarchy holds, for the log line.
	int hierarchy_size(const int *parent_index, int root_index) const;

	// Set or clear the minimized flag across a whole hierarchy, from the
	// root down. Every member carries the same minimize_sequence so a later
	// restore can find the hierarchy again from any of its members.
	void set_hierarchy_minimized(const int *parent_index, int root_index,
				     bool minimized,
				     uint64_t minimize_sequence);
	// Give the keyboard to the topmost visible window, or clear it
	// when the active desktop is empty. Called whenever the focused
	// window stops being visible — a desktop switch, a move to another
	// desktop, a minimize — so focus never sits on a window the user
	// cannot see.
	void hand_focus_to_visible_window(struct river_seat_v1 *river_seat);
	void add_output(struct river_output_v1 *output);
	void propose_default_dimensions(Window *window) const;
	void place_windows();
	void placement_area(int32_t *x, int32_t *y, int32_t *width,
			    int32_t *height) const;

	// The rectangle windows are actually placed in: the placement area with
	// the titlebar's strip reserved at its top, so a window put here has
	// room for its own bar above it and the bar never has to cover the
	// window. Equal to placement_area() when decorations are off, because
	// then there is no bar to make room for.
	//
	// Every geometry action that fills the area (maximize, fit_to_output,
	// center) uses this rather than placement_area(), or the window would
	// end up flush with the area's top edge and its bar would have to go
	// below it or not be painted at all.
	void content_area(int32_t *x, int32_t *y, int32_t *width,
			  int32_t *height) const;

	// The decoration pass: one titlebar per window plus the focus border,
	// run at the END of the render sequence because it needs the placement
	// place_windows() just wrote. Defined in src/ViewDecoration.cpp.
	void apply_decorations(struct river_seat_v1 *river_seat);

	// Destroy a window entry's decoration, if it has one. The single
	// teardown path for a decoration: terminate(), remove_window() and
	// the decorations toggle all go through here, so the surface, buffer
	// and protocol object are released exactly once and in one order.
	void destroy_decoration(Window *window_entry);

	Seat *seat;
	Display *display;
	struct river_window_manager_v1 *manager;
	Window *windows;
	int window_count;
	int window_capacity;
	Output **outputs;
	int output_count;
	int output_capacity;
	Output *default_layer_output;
	int pending_manage_count;
	int pending_render_count;
	bool shutdown_requested;

	// A manage_dirty request is outstanding; set by request_manage() and
	// cleared when the manage sequence it asked for starts.
	bool manage_requested;
	Keybind *keybind;

	// The session is locked (a lock screen holds the keyboard). While
	// locked, every action except exit_session is refused: acting
	// on a key binding would fight the lock screen for focus. River sends
	// session_locked at startup too if the session is already locked.
	// window_manager_session_locked sets this and
	// window_manager_session_unlocked clears it; both must stay in step
	// with river's events or the key binding guard sticks.
	bool session_locked;

	// The virtual desktop shown right now. Windows whose desktop differs
	// are hidden with river_window_v1.hide.
	int active_desktop;

	// The stacking-order clock: every raise hands out the next value, so
	// the highest z_order among the visible windows is the topmost one.
	// It starts at 1 so a zero-initialized entry is never topmost.
	uint64_t next_z_order;

	// The minimize-order clock, the same shape as next_z_order: every
	// minimize hands out the next value, so the highest minimize_sequence
	// among the minimized hierarchies is the most recently minimized one.
	// It starts at 1 so a zero-initialized entry is never "most recent".
	uint64_t next_minimize_sequence;

	// Decoration appearance, resolved once from window-decorations.json by
	// View::initialize. Held by value here, but the cairo-using headers
	// stay in Decoration.hpp: View.hpp only sees the struct's layout.
	DecorationSettings decoration_settings;

	// Whether decorations are painted at all. Seeded from the config's
	// "enabled" key and flipped by the toggle_decorations binding.
	bool decorations_on;

	// The window whose decoration was pressed, waiting for the
	// window_interaction that press will also produce. See
	// claim_decoration_click().
	struct river_window_v1 *decoration_click_claimed;

	// How many virtual desktops exist. River has no desktop concept, so
	// this is yarfwm's own fixed count.
	static const int desktop_count = 5;
};

// View::Window lives in its own header, included at the very END of this file:
// `struct View::Window` is an out-of-class definition of a nested type, so View
// must already be complete. Moving this include to the top of the file fails to
// compile with "error: qualified name does not name a class before '{' token".
#include "ViewWindow.hpp"
#endif // VIEW_HPP
