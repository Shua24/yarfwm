#ifndef SEAT_HPP
#define SEAT_HPP

#include "Placement.hpp"
#include "Server.hpp"

struct river_seat_v1;
struct river_layer_shell_seat_v1;
struct river_shell_surface_v1;
struct river_window_v1;

class Display;
class Config;
class LayerShell;
class View;

// An interactive pointer move or resize in progress.
//
// River delivers the request as an event
// (river_window_v1.pointer_move_requested / pointer_resize_requested), the
// window manager answers with river_seat_v1.op_start_pointer inside a manage
// sequence, and river then sends river_seat_v1.op_delta with the TOTAL motion
// since the grab. The operation ends with river_seat_v1.op_end, again inside a
// manage sequence.
//
// Because op_delta is cumulative, every delta is applied to start_geometry and
// never accumulated on top of the previous one.
struct PointerOperation {
	enum Kind {
		kind_none = 0,
		kind_move,
		kind_resize,
	};
	Kind kind;
	// True when the operation came from a touch point rather than a
	// pointer: the same handshake, but op_start_touch/op_end_touch take
	// the touch point ID and the delta events are op_delta_touch.
	bool is_touch;
	// The transient touch point ID, for touch operations only.
	int32_t touch_point;
	// The window being moved or resized.
	struct river_window_v1 *window;
	// The seat the operation belongs to.
	struct river_seat_v1 *river_seat;
	// Geometry when the grab started.
	Rectangle start_geometry;
	// Which edges a resize is pulling, from the resize request. Bit flags
	// from the river edges enum, so a corner carries two bits.
	uint32_t resize_edges;
	// The last delta river sent, kept so the end-of-operation log can
	// report both the geometry and the motion that produced it. River
	// measures a delta from the pointer position when it processed
	// op_start_pointer, which can be a moment after the press, so the
	// delta is the authority on where the window should end up.
	int32_t last_delta_x;
	int32_t last_delta_y;
	// op_start_pointer has been sent for this operation. River ignores a
	// repeat, but tracking it keeps the request count honest.
	bool started;
	// op_release arrived: op_end must be sent in the next manage sequence.
	bool end_requested;
	// op_cancel_touch arrived: the operation must be undone, not ended.
	// River's XML: "The client should ideally behave as if this operation
	// was never started." The geometry is restored from
	// start_geometry.
	bool cancel_requested;
};

// Represents river seats.
//
// The seat owns the keyboard focus state. The river window management protocol
// keeps keyboard focus on the seat (river_seat_v1.focus_window and
// river_seat_v1.clear_focus), so the focused window is tracked here and
// nowhere else: the render path reads it from the seat instead of keeping a
// second copy of it.
//
// Focus changes are recorded as intent and applied during the next manage
// sequence, because river only accepts focus_window/clear_focus inside one.
// River follows every input event it sends us with a manage_start of its own,
// so recorded intent is never stranded. The shape of this follows att_wm
// (~/Sources/att_wm, Seat.zig: focus() records, applyManage() applies).
//
// Dynamic memory: per-seat state lives in a fixed-size array (a machine with
// more than max_seats seats is not a real configuration), so no allocation
// happens here; the layer shell seat object and the river_seat_v1 object are
// protocol objects created on demand and destroyed in terminate().
class Seat
{
      public:
	Seat();
	~Seat();

	bool initialize(Server *server, Display *display, Config &config);
	void terminate();

	// Called when river hands the window manager a new river_seat_v1. The
	// View is passed so that a recorded focus change can ask for a manage
	// sequence.
	void attach_river_seat(struct river_seat_v1 *river_seat,
			       LayerShell &layer_shell, View *view);

	// Focus intent: recorded now, sent as river_seat_v1.focus_window during
	// the next manage sequence. A null window clears the focus instead.
	void focus(struct river_seat_v1 *river_seat,
		   struct river_window_v1 *window);
	void focus_none(struct river_seat_v1 *river_seat);

	// A window river destroyed is gone for good: drop every reference to it
	// before its proxy is destroyed, and record the focus the seat falls
	// back to. Recorded as intent like focus(), because keyboard focus is
	// window management state and river only accepts
	// focus_window/clear_focus inside a manage sequence.
	void forget_window(struct river_window_v1 *window);

	// Put the keyboard back where it was before the session locked. River
	// drops the focus when the lock surface goes away, but the window we
	// remember as focused never changed, so nothing looks stale and no
	// request would otherwise be made.
	void restore_focus();

	struct river_window_v1 *
	focused_window(struct river_seat_v1 *river_seat) const;
	struct river_window_v1 *
	previous_focused_window(struct river_seat_v1 *river_seat) const;

	// The window the seat is on or about to be on: the recorded intent
	// when one is pending, otherwise the applied focus. A visibility
	// fixup reads this instead of the applied focus so a click whose
	// manage sequence has not run yet cannot be overwritten by a stale
	// applied focus.
	struct river_window_v1 *
	focus_intent(struct river_seat_v1 *river_seat) const;

	// The first seat river handed us. Bindings and events that are not tied
	// to a particular seat act on it.
	struct river_seat_v1 *primary_river_seat() const;

	// Every attached seat, so the Keybind engine can create one binding
	// object per seat.
	int river_seat_count() const;
	struct river_seat_v1 *river_seat_at(int index) const;

	// Whether river told us this seat is gone. The proxy is destroyed at
	// the next manage sequence, so anything pointing at it (binding
	// objects, an armed key repeat) has to be dropped first.
	bool is_removed(struct river_seat_v1 *river_seat) const;

	// Send the requests recorded by the event handlers. Manage sequence
	// only; called from View::window_manager_manage_start().
	void apply_manage();

	// Keyboard-driven pointer movement: move the pointer by a step in a
	// direction. river_seat_v1.pointer_warp is manage-sequence-only, so
	// the offset is recorded and sent by apply_manage().
	void move_pointer(struct river_seat_v1 *river_seat, int32_t delta_x,
			  int32_t delta_y);

	// Start an interactive pointer move or resize. Called from the
	// window's pointer_move_requested / pointer_resize_requested handlers,
	// which pass the window and the seat river named. is_resize picks the
	// kind; edges is the resize direction bit mask and is ignored for a
	// move. Returns without doing anything if an operation is already in
	// progress, because river ignores a second op_start_pointer.
	void start_pointer_operation(struct river_window_v1 *window,
				     struct river_seat_v1 *river_seat,
				     bool is_resize, uint32_t edges);

	// The same handshake for a touch point. River's
	// touch_move_requested / touch_resize_requested carry the transient
	// touch point ID that op_start_touch and op_delta_touch are keyed by.
	void start_touch_operation(struct river_window_v1 *window,
				   struct river_seat_v1 *river_seat,
				   bool is_resize, uint32_t edges,
				   int32_t touch_point);

	// Whether an interactive move or resize is in progress.
	bool pointer_operation_active() const;

	// The wl_pointer for a seat, used for exactly one thing: the window
	// manager's own surfaces. River routes a press on a window to
	// river_seat_v1.window_interaction, but a press on a decoration surface
	// -- which has no scene-node data for river to resolve -- arrives here
	// as an ordinary wl_pointer event on our own surface. That is the only
	// channel that can tell a titlebar button from a titlebar bar.
	//
	// Every slot is populated: libwayland aborts the process (SIGABRT) when
	// an event arrives for a NULL listener slot, and wl_pointer is a child
	// of wl_seat, so it takes the seat's advertised version (v9 here) and
	// the v8/v9 slots are reachable in practice.
	static void pointer_enter(void *data, struct wl_pointer *pointer,
				  uint32_t serial, struct wl_surface *surface,
				  wl_fixed_t surface_x, wl_fixed_t surface_y);
	static void pointer_leave(void *data, struct wl_pointer *pointer,
				  uint32_t serial, struct wl_surface *surface);
	static void pointer_motion(void *data, struct wl_pointer *pointer,
				   uint32_t time, wl_fixed_t surface_x,
				   wl_fixed_t surface_y);
	static void pointer_button(void *data, struct wl_pointer *pointer,
				   uint32_t serial, uint32_t time,
				   uint32_t button, uint32_t state);
	static void pointer_axis(void *data, struct wl_pointer *pointer,
				 uint32_t time, uint32_t axis,
				 wl_fixed_t value);
	static void pointer_frame(void *data, struct wl_pointer *pointer);
	static void pointer_axis_source(void *data, struct wl_pointer *pointer,
					uint32_t source);
	static void pointer_axis_stop(void *data, struct wl_pointer *pointer,
				      uint32_t time, uint32_t axis);
	static void pointer_axis_discrete(void *data,
					  struct wl_pointer *pointer,
					  uint32_t axis, int32_t discrete);
	static void pointer_axis_value120(void *data,
					  struct wl_pointer *pointer,
					  uint32_t axis, int32_t value120);
	static void pointer_axis_relative_direction(void *data,
						    struct wl_pointer *pointer,
						    uint32_t axis,
						    uint32_t direction);
	static void pointer_warp(void *data, struct wl_pointer *pointer,
				 wl_fixed_t surface_x, wl_fixed_t surface_y);

	// Bind or release the wl_pointer to match the seat's advertised
	// pointer capability. Called from Display::seat_capabilities, which is
	// where the capability event lands: wl_seat.get_pointer is a PROTOCOL
	// ERROR unless the seat has advertised the pointer capability at some
	// point, and a headless session with no input devices
	// (WLR_LIBINPUT_NO_DEVICES=1) starts with NO capabilities at all. The
	// pointer therefore cannot be bound when river names the seat -- it
	// has to wait for this. Getting it wrong kills the window manager at
	// startup with "wl_seat.get_pointer called when no pointer capability
	// has existed".
	void update_pointer_capability(struct wl_seat *wl_seat,
				       bool has_pointer);

      private:
	// Shared body of start_pointer_operation and start_touch_operation;
	// the two differ only in which op_start request is sent and whether a
	// touch point ID is carried.
	void start_operation(struct river_window_v1 *window,
			     struct river_seat_v1 *river_seat, bool is_resize,
			     uint32_t edges, bool is_touch,
			     int32_t touch_point);

	// Reset the operation state to idle. Called when an operation ends or
	// is cancelled, so the next grab starts from a clean record.
	void clear_pointer_operation();

	// The geometry half shared by the pointer and touch delta handlers:
	// both events carry the total motion since the grab, so both feed
	// this. Static because it is a pure function of the seat's recorded
	// operation state.
	static void apply_operation_delta(Seat *seat, int32_t dx, int32_t dy);
	// Send the recorded pointer-operation requests. Manage sequence only;
	// called from apply_manage().
	void apply_pointer_operation();

	// Send the recorded keyboard pointer warp, if any. Manage sequence
	// only; called from apply_manage().
	void apply_pointer_warp();

	static const int max_seats = 4;

	enum LayerSurfaceFocus {
		layer_focus_none = 0,
		layer_focus_exclusive = 1,
		layer_focus_non_exclusive = 2,
	};

	struct SeatEntry {
		// The Seat that owns this entry: the layer shell seat handlers
		// receive the entry as their listener data, and need a way back
		// to the Seat (and through it the View) to ask for a manage
		// sequence.
		Seat *owner;

		struct river_seat_v1 *river_seat;
		struct river_layer_shell_seat_v1 *layer_shell_seat;
		LayerSurfaceFocus layer_surface_focus;
		bool removed;

		// The wl_seat this entry corresponds to, from the name river
		// reports in river_seat_v1.wl_seat, and the wl_pointer bound
		// from it. The pointer exists for the window manager's own
		// decoration surfaces only.
		struct wl_seat *wl_seat;
		struct wl_pointer *pointer;

		// Focus state. previous_focused_window is what
		// focus_window_previous returns to;
		// pending_focus_window/pending_clear_focus is the intent
		// recorded since the last manage sequence.
		struct river_window_v1 *focused_window;
		struct river_window_v1 *previous_focused_window;
		struct river_window_v1 *pending_focus_window;
		bool pending_clear_focus;

		// The pointer position river last reported (the
		// pointer_position event). The keyboard pointer warp moves
		// relative to it.
		int32_t pointer_x;
		int32_t pointer_y;
		// A keyboard pointer warp waiting for the next manage
		// sequence: the offset accumulated since the last reported
		// position.
		int32_t pointer_warp_delta_x;
		int32_t pointer_warp_delta_y;
		bool pointer_warp_pending;
	};

	SeatEntry *find_entry(struct river_seat_v1 *river_seat);
	const SeatEntry *find_entry(struct river_seat_v1 *river_seat) const;
	// The entry owning this wl_seat, or null. Declared here, after
	// SeatEntry is complete.
	SeatEntry *find_entry_by_wl_seat(struct wl_seat *wl_seat);
	void record_focus(SeatEntry *entry, struct river_window_v1 *window);

	// river_seat_v1 events. Every slot must be non-NULL: libwayland aborts
	// the process when an event arrives for a NULL listener slot.
	static void river_seat_removed(void *data,
				       struct river_seat_v1 *river_seat);
	static void river_seat_wl_seat(void *data,
				       struct river_seat_v1 *river_seat,
				       uint32_t name);
	static void river_seat_pointer_enter(void *data,
					     struct river_seat_v1 *river_seat,
					     struct river_window_v1 *window);
	static void river_seat_pointer_leave(void *data,
					     struct river_seat_v1 *river_seat);
	static void
	river_seat_window_interaction(void *data,
				      struct river_seat_v1 *river_seat,
				      struct river_window_v1 *window);
	static void river_seat_shell_surface_interaction(
	    void *data, struct river_seat_v1 *river_seat,
	    struct river_shell_surface_v1 *shell_surface);
	static void river_seat_pointer_position(
	    void *data, struct river_seat_v1 *river_seat, int32_t x, int32_t y);
	static void river_seat_op_delta(void *data,
					struct river_seat_v1 *river_seat,
					int32_t dx, int32_t dy);
	static void river_seat_op_release(void *data,
					  struct river_seat_v1 *river_seat);
	static void river_seat_op_delta_touch(void *data,
					      struct river_seat_v1 *river_seat,
					      int32_t touch_point, int32_t dx,
					      int32_t dy);
	static void river_seat_op_release_touch(
	    void *data, struct river_seat_v1 *river_seat, int32_t touch_point);
	static void river_seat_op_cancel_touch(void *data,
					       struct river_seat_v1 *river_seat,
					       int32_t touch_point);

	// Layer shell seat events. A bar (non-exclusive) does not take the
	// keyboard away from windows; a lock screen (exclusive) does.
	static void layer_shell_seat_focus_exclusive(
	    void *data, struct river_layer_shell_seat_v1 *layer_shell_seat);
	static void layer_shell_seat_focus_non_exclusive(
	    void *data, struct river_layer_shell_seat_v1 *layer_shell_seat);
	static void layer_shell_seat_focus_none(
	    void *data, struct river_layer_shell_seat_v1 *layer_shell_seat);

	SeatEntry seat_entries[max_seats];
	int seat_count;
	bool focus_follows_mouse;
	View *view;

	// Needed to resolve the wl_seat river names in river_seat_v1.wl_seat
	// into the object a wl_pointer is bound from.
	Display *display;

	// The surface the pointer is currently on, and the last surface-local
	// position river reported for it. A press carries NO coordinates of its
	// own (only enter and motion do), so this cache is the only source for
	// where a click landed.
	//
	// Per seat in principle, but a single pointer drives one surface at a
	// time and yarfwm acts on the seat the press arrived on, so one cache
	// is enough -- the same reasoning pointer_operation uses.
	struct wl_surface *hovered_surface;
	int32_t pointer_surface_x;
	int32_t pointer_surface_y;

	// The interactive pointer move/resize in progress, if any. Per seat in
	// principle, but yarfwm drives one operation at a time: a second
	// request while one is running is ignored (river would ignore the
	// op_start_pointer anyway).
	PointerOperation pointer_operation;
};

#endif // SEAT_HPP
