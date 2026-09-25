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

	// The interactive pointer move/resize in progress, if any. Per seat in
	// principle, but yarfwm drives one operation at a time: a second
	// request while one is running is ignored (river would ignore the
	// op_start_pointer anyway).
	PointerOperation pointer_operation;
};

#endif // SEAT_HPP
