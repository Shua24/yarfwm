#ifndef VIEW_HPP
#define VIEW_HPP

#include "Placement.hpp"
#include "Server.hpp"
#include "river-window-management-v1-client-protocol.h"

class Display;
class Seat;
class Config;
class Output;
class Keybind;

// Represents the window management policy and render state.
class View
{
      public:
	View();
	~View();

	bool initialize(Server *server, Seat *seat, Display *display,
			Config &config);
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

	// The window to hand the keyboard to when the focused window is
	// destroyed or stops being visible and the seat has no previous
	// window left to return to. Only windows on the active desktop are
	// candidates: a hidden window must never take the keyboard.
	struct river_window_v1 *first_visible_window() const;

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

	// Handle a decoration hint from river. Nothing is sent back, on
	// purpose: river's default when neither use_csd nor use_ssd is sent
	// is client-side decorations, and honouring a server-side hint would
	// mean drawing the decoration here, which yarfwm does not do.
	void apply_decoration_hint(struct river_window_v1 *window,
				   uint32_t hint);

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
	struct Window {
		struct river_window_v1 *window;
		struct river_node_v1 *node;
		bool managed;
		// What river believes: true when the last show/hide request
		// for this window was show(). River considers a new window
		// shown until told otherwise, so a new entry starts true.
		// The visibility pass in window_manager_render_start() is
		// the only writer.
		bool shown;
		bool rendered;
		bool placed;
		bool close_requested;
		bool close_sent;

		// Geometry as the window manager last set or heard it: x/y from
		// the placement pass, width/height from
		// river_window_v1.dimensions. Directional focus needs both, so
		// a window that never answered with dimensions is skipped.
		int32_t x;
		int32_t y;
		int32_t width;
		int32_t height;

		// Where the user put the window. While has_user_geometry is
		// false the cascade owns the position; once an interactive
		// move or resize sets it, place_windows() leaves the position
		// alone instead of re-cascading the window. View::add_window()
		// zeroes the whole entry with memset before filling it in, so
		// false is the correct default.
		bool has_user_geometry;
		Rectangle user_geometry;

		// A resize the user asked for, waiting for the next manage
		// sequence: river_window_v1.propose_dimensions is
		// manage-sequence-only and a node has no dimension setter, so
		// the size half of a resize has to go through a proposal.
		bool propose_pending;
		Rectangle proposed_geometry;

		// Window state, as the window manager believes it. The
		// matching protocol requests are manage-sequence-only, so a
		// change is recorded here and sent by
		// window_manager_manage_start(); sent_* tracks whether river
		// has already been told, so a state that does not change does
		// not re-send.
		bool maximized;
		bool maximized_sent;
		bool fullscreen;
		bool fullscreen_sent;
		bool always_on_top;
		bool always_on_top_sent;
		// Minimizing hides the window through the visibility pass,
		// like any other visibility change, so there is no
		// minimized_sent to track.
		bool minimized;

		// Which virtual desktop this window belongs to. Desktops are a
		// window manager invention here, not a protocol feature.
		int desktop;

		// Window metadata river reports. None of it drives placement
		// on its own, but all of it is worth keeping: the title and
		// app_id are what a task list or window menu would show, the
		// identifier is the stable name river guarantees is unique and
		// never reused, and the pid is the creator's (explicitly
		// unreliable, so it must never gate anything security
		// sensitive).
		char title[256];
		char app_id[256];
		char identifier[64];
		int unreliable_pid;

		// The window's preferred size bounds, from dimensions_hint.
		// Zero means "no preference". They are a hint: the XML says
		// the window manager "is free to propose dimensions outside
		// of these bounds", but a user resize has no reason to
		// ignore them.
		int32_t min_width;
		int32_t min_height;
		int32_t max_width;
		int32_t max_height;

		// The parent window river reported, if any. A dialog should
		// sit directly above its parent.
		struct river_window_v1 *parent;
		// place_above has been sent for this window's parent, so the
		// request is not repeated every manage sequence.
		bool parent_placed;

		// Number of active screen capture sessions, from
		// capture_sessions. River sends it once at creation and again
		// whenever it changes.
		uint32_t capture_count;

		// The output a fullscreen window was sent to, or null for "no
		// preference".
		struct river_output_v1 *fullscreen_output;
	};

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

	// Give the keyboard to the first visible window, or clear it when
	// the active desktop is empty. Called whenever the focused window
	// stops being visible — a desktop switch, a move to another desktop,
	// a minimize — so focus never sits on a window the user cannot see.
	void hand_focus_to_visible_window(struct river_seat_v1 *river_seat);
	void add_output(struct river_output_v1 *output);
	void propose_default_dimensions(Window *window) const;
	void place_windows();
	void placement_area(int32_t *x, int32_t *y, int32_t *width,
			    int32_t *height) const;

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

	// How many virtual desktops exist. River has no desktop concept, so
	// this is yarfwm's own fixed count.
	static const int desktop_count = 5;
};

#endif // VIEW_HPP
