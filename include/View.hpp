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

	// Stop the window manager. The main loop polls should_shutdown() and
	// exits.
	void request_shutdown();

	// End the whole Wayland session: ask river to exit the compositor,
	// which disconnects every client including this one. River does NOT
	// exit when its window manager disconnects, so this is the only way to
	// leave a session from inside the window manager. River's XML asks that
	// this be sent only when the user explicitly wants the session to end,
	// so it is a separate action from request_shutdown().
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
	// destroyed and the seat has no previous window left to return to.
	// yarfwm is a single floating cascade with no per-output or per-tag
	// visibility, so every tracked window is a candidate.
	struct river_window_v1 *first_window() const;

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
	// are not on the active desktop are hidden with
	// river_window_v1.hide. See set_decoration_mode() for the related note
	// about what river does and does not provide.
	void focus_desktop(struct river_seat_v1 *river_seat, int delta);
	void move_window_to_desktop(struct river_window_v1 *window, int delta);

	// Apply a decoration hint from river by sending use_csd or use_ssd.
	// Both are either-sequence requests.
	void apply_decoration_hint(struct river_window_v1 *window,
				   uint32_t hint);

	// How many virtual desktops exist. Public because the wrap helper
	// needs it and the key binding layer reports it.
	static int desktop_total();

	// Whether a lock screen currently holds the keyboard. The key binding
	// layer refuses everything except quit and exit_session while true, so
	// a binding cannot fight the lock screen for focus.
	bool session_is_locked() const { return session_locked; }

      private:
	struct Window {
		struct river_window_v1 *window;
		struct river_node_v1 *node;
		bool managed;
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
		bool minimized;
		bool minimized_sent;

		// The decoration hint river last reported, and the request sent
		// for it. Values come from river_window_v1_decoration_hint.
		uint32_t decoration_hint;
		bool decoration_sent;

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

		// The seat whose keyboard focus this window held when it was
		// minimized, so it can be focused again on restore. May be
		// null.
		struct river_seat_v1 *focus_before_minimize;
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
	static void
	window_manager_session_locked(void *data,
				      struct river_window_manager_v1 *manager);
	static void window_manager_session_unlocked(
	    void *data, struct river_window_manager_v1 *manager);

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
	// locked, every action except quit and exit_session is refused: acting
	// on a key binding would fight the lock screen for focus. River sends
	// session_locked at startup too if the session is already locked, so
	// this is never assumed false for long.
	bool session_locked;

	// Whether a session_locked or session_unlocked event has been seen, so
	// the startup case is distinguishable from a genuine transition.
	bool session_lock_known;

	// The virtual desktop shown right now. Windows whose desktop differs
	// are hidden with river_window_v1.hide.
	int active_desktop;

	// How many virtual desktops exist. River has no desktop concept, so
	// this is yarfwm's own count and the config's "workspaces" list is NOT
	// used for it: that list holds xkb keyboard layout names
	// (["ID","JP","RU","US","DE/CH"]) and never described desktops.
	static const int desktop_count = 5;
};

#endif // VIEW_HPP
