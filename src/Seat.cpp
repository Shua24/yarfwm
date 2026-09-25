#include "Seat.hpp"
#include "Config.hpp"
#include "LayerShell.hpp"
#include "View.hpp"
#include "river-layer-shell-v1-client-protocol.h"
#include "river-window-management-v1-client-protocol.h"
#include <cstdio>
#include <cstring>

Seat::Seat() : seat_count(0), focus_follows_mouse(true), view(nullptr)
{
	std::memset(seat_entries, 0, sizeof(seat_entries));
	pointer_operation.kind = PointerOperation::kind_none;
	pointer_operation.window = nullptr;
	pointer_operation.river_seat = nullptr;
	pointer_operation.start_geometry = Rectangle{0, 0, 0, 0};
	pointer_operation.resize_edges = 0;
	pointer_operation.started = false;
	pointer_operation.end_requested = false;
}

Seat::~Seat() { terminate(); }

bool Seat::initialize(Server *server, Display *display, Config &config)
{
	// Dynamic memory: none yet; per-seat state is created lazily in
	// attach_river_seat() when river hands the window manager a seat.
	(void)server;
	(void)display;

	focus_follows_mouse =
	    config.input.get("focus_follows_mouse", true).asBool();
	return true;
}

void Seat::attach_river_seat(struct river_seat_v1 *river_seat,
			     LayerShell &layer_shell, View *view)
{
	this->view = view;

	if (!river_seat) {
		return;
	}

	// get_seat may only be requested once per river_seat_v1: a repeat would
	// be a protocol error, so a repeated attach is ignored.
	for (int i = 0; i < seat_count; i++) {
		if (seat_entries[i].river_seat == river_seat) {
			return;
		}
	}

	if (seat_count >= max_seats) {
		std::fprintf(
		    stderr,
		    "Yarfwm: more seats than the first %d, ignoring the rest\n",
		    max_seats);
		return;
	}

	SeatEntry *entry = &seat_entries[seat_count];
	std::memset(entry, 0, sizeof(*entry));
	entry->owner = this;
	entry->river_seat = river_seat;
	entry->layer_surface_focus = layer_focus_none;

	// Named slots on purpose: the struct field order in the generated
	// header is an implementation detail and a positionally-initialized
	// listener that drifts out of order silently dispatches the wrong
	// event.
	static const struct river_seat_v1_listener seat_listener = [] {
		struct river_seat_v1_listener listener{};
		listener.removed = river_seat_removed;
		listener.wl_seat = river_seat_wl_seat;
		listener.pointer_enter = river_seat_pointer_enter;
		listener.pointer_leave = river_seat_pointer_leave;
		listener.window_interaction = river_seat_window_interaction;
		listener.shell_surface_interaction =
		    river_seat_shell_surface_interaction;
		listener.pointer_position = river_seat_pointer_position;
		listener.op_delta = river_seat_op_delta;
		listener.op_release = river_seat_op_release;
		listener.op_delta_touch = river_seat_op_delta_touch;
		listener.op_release_touch = river_seat_op_release_touch;
		listener.op_cancel_touch = river_seat_op_cancel_touch;
		return listener;
	}();

	// The listener data is the Seat itself, so the handlers can reach the
	// focus policy and the View; the per-seat entry is looked up from the
	// river_seat_v1 the event carries.
	if (river_seat_v1_add_listener(river_seat, &seat_listener, this) != 0) {
		std::fprintf(stderr,
			     "Yarfwm: failed to add river seat listener\n");
	}

	if (layer_shell.available()) {
		entry->layer_shell_seat =
		    layer_shell.create_seat_state(river_seat);
		if (entry->layer_shell_seat) {
			static const struct river_layer_shell_seat_v1_listener
			    layer_shell_seat_listener = {
				layer_shell_seat_focus_exclusive,
				layer_shell_seat_focus_non_exclusive,
				layer_shell_seat_focus_none,
			    };

			if (river_layer_shell_seat_v1_add_listener(
				entry->layer_shell_seat,
				&layer_shell_seat_listener, entry) != 0) {
				std::fprintf(stderr,
					     "Yarfwm: failed to add layer "
					     "shell seat listener\n");
				river_layer_shell_seat_v1_destroy(
				    entry->layer_shell_seat);
				entry->layer_shell_seat = nullptr;
			}
		}
	}

	seat_count++;
}

Seat::SeatEntry *Seat::find_entry(struct river_seat_v1 *river_seat)
{
	for (int i = 0; i < seat_count; i++) {
		if (seat_entries[i].river_seat == river_seat) {
			return &seat_entries[i];
		}
	}
	return nullptr;
}

const Seat::SeatEntry *Seat::find_entry(struct river_seat_v1 *river_seat) const
{
	for (int i = 0; i < seat_count; i++) {
		if (seat_entries[i].river_seat == river_seat) {
			return &seat_entries[i];
		}
	}
	return nullptr;
}

void Seat::record_focus(SeatEntry *entry, struct river_window_v1 *window)
{
	if (!entry || entry->removed) {
		return;
	}

	if (window) {
		entry->pending_focus_window = window;
		entry->pending_clear_focus = false;
	} else {
		entry->pending_focus_window = nullptr;
		entry->pending_clear_focus = true;
	}

	// This is the one state change river cannot see for itself, so ask for
	// a manage sequence; river already sends one after the input event,
	// which makes this a redundant second sequence (att_wm makes the same
	// request).
	if (view) {
		view->request_manage();
	}
}

void Seat::focus(struct river_seat_v1 *river_seat,
		 struct river_window_v1 *window)
{
	record_focus(find_entry(river_seat), window);
}

void Seat::focus_none(struct river_seat_v1 *river_seat)
{
	record_focus(find_entry(river_seat), nullptr);
}

void Seat::forget_window(struct river_window_v1 *window)
{
	if (!window) {
		return;
	}

	for (int i = 0; i < seat_count; i++) {
		SeatEntry *entry = &seat_entries[i];

		// A destroyed proxy must never be compared again, returned to
		// the Keybind engine, or handed to river_seat_v1.focus_window.
		if (entry->pending_focus_window == window) {
			entry->pending_focus_window = nullptr;
			entry->pending_clear_focus = false;
		}
		if (entry->previous_focused_window == window) {
			entry->previous_focused_window = nullptr;
		}

		if (entry->focused_window != window) {
			continue;
		}

		// The focused window is gone. Fall back to the most recently
		// focused survivor, then to any survivor, and only clear the
		// keyboard focus when no window is left at all.
		entry->focused_window = nullptr;

		struct river_window_v1 *fallback =
		    entry->previous_focused_window;
		if (!fallback && view) {
			fallback = view->first_window();
		}

		if (fallback) {
			entry->pending_focus_window = fallback;
			entry->pending_clear_focus = false;
			// The slot must never point at the window that is
			// becoming the focused one: focus_window_previous would
			// then be a silent no-op. It reports "no previous
			// window" instead.
			if (entry->previous_focused_window == fallback) {
				entry->previous_focused_window = nullptr;
			}
		} else {
			entry->pending_focus_window = nullptr;
			entry->pending_clear_focus = true;
		}
	}

	// Applied by Seat::apply_manage() inside the next manage sequence.
	if (view) {
		view->request_manage();
	}
}

void Seat::restore_focus()
{
	for (int i = 0; i < seat_count; i++) {
		SeatEntry *entry = &seat_entries[i];
		if (!entry->river_seat || entry->removed) {
			continue;
		}

		// A lock screen still holding the keyboard is left alone; its
		// own focus_none event restores the window when it lets go.
		if (entry->layer_surface_focus == layer_focus_exclusive) {
			continue;
		}

		struct river_window_v1 *target = entry->focused_window;
		if (!target && view) {
			// The focused window died while the screen was
			// locked: fall back the way forget_window() does.
			target = entry->previous_focused_window
				     ? entry->previous_focused_window
				     : view->first_window();
		}
		if (!target) {
			continue;
		}

		entry->pending_focus_window = target;
		entry->pending_clear_focus = false;
	}

	// Applied by Seat::apply_manage() inside the next manage sequence.
	if (view) {
		view->request_manage();
	}
}

struct river_window_v1 *
Seat::focused_window(struct river_seat_v1 *river_seat) const
{
	const SeatEntry *entry = find_entry(river_seat);
	return entry ? entry->focused_window : nullptr;
}

struct river_window_v1 *
Seat::previous_focused_window(struct river_seat_v1 *river_seat) const
{
	const SeatEntry *entry = find_entry(river_seat);
	return entry ? entry->previous_focused_window : nullptr;
}

struct river_seat_v1 *Seat::primary_river_seat() const
{
	for (int i = 0; i < seat_count; i++) {
		if (seat_entries[i].river_seat && !seat_entries[i].removed) {
			return seat_entries[i].river_seat;
		}
	}
	return nullptr;
}

int Seat::river_seat_count() const { return seat_count; }

struct river_seat_v1 *Seat::river_seat_at(int index) const
{
	if (index < 0 || index >= seat_count) {
		return nullptr;
	}
	return seat_entries[index].river_seat;
}

bool Seat::is_removed(struct river_seat_v1 *river_seat) const
{
	const SeatEntry *entry = find_entry(river_seat);
	return entry ? entry->removed : false;
}

void Seat::apply_manage()
{
	// Destroy seats river has told us are gone first, at the top of a
	// manage sequence, so no handler ever observes a half-removed object.
	for (int i = 0; i < seat_count;) {
		if (!seat_entries[i].removed) {
			i++;
			continue;
		}
		if (seat_entries[i].river_seat) {
			river_seat_v1_destroy(seat_entries[i].river_seat);
			seat_entries[i].river_seat = nullptr;
		}
		if (i < seat_count - 1) {
			seat_entries[i] = seat_entries[seat_count - 1];
		}
		seat_count--;
	}

	for (int i = 0; i < seat_count; i++) {
		SeatEntry *entry = &seat_entries[i];
		if (!entry->river_seat) {
			continue;
		}

		// A layer surface with exclusive focus (a lock screen) outranks
		// us entirely: never fight it for the keyboard. River ignores
		// focus requests until the surface lets go
		// (river-layer-shell-v1 focus_exclusive: "all window manager
		// requests to change focus are ignored"), so the recorded
		// intent is left alone and the first manage sequence after
		// focus_none applies it. Discarding it here would strand the
		// seat with nothing focused when the window it fell back to was
		// the one that died.
		if (entry->layer_surface_focus == layer_focus_exclusive) {
			continue;
		}

		if (entry->pending_focus_window) {
			// Remember where the focus came from so that
			// focus_window_previous can go back to it. Keep the old
			// value when the previous window has died (a close
			// fallback: it must not be wiped) or when the focus is
			// being re-applied to the same window (a lock screen
			// handing the keyboard back).
			if (entry->focused_window &&
			    entry->focused_window !=
				entry->pending_focus_window) {
				entry->previous_focused_window =
				    entry->focused_window;
			}
			river_seat_v1_focus_window(entry->river_seat,
						   entry->pending_focus_window);
			entry->focused_window = entry->pending_focus_window;
			entry->pending_focus_window = nullptr;
		} else if (entry->pending_clear_focus) {
			river_seat_v1_clear_focus(entry->river_seat);
			entry->previous_focused_window = entry->focused_window;
			entry->focused_window = nullptr;
			entry->pending_clear_focus = false;
			// Nothing is focused: the previous slot would hand
			// focus back to a window the user just left behind.
			entry->previous_focused_window = nullptr;
		}
	}

	apply_pointer_warp();
	apply_pointer_operation();
}

void Seat::terminate()
{
	for (int i = 0; i < seat_count; i++) {
		if (seat_entries[i].layer_shell_seat) {
			river_layer_shell_seat_v1_destroy(
			    seat_entries[i].layer_shell_seat);
			seat_entries[i].layer_shell_seat = nullptr;
		}
		if (seat_entries[i].river_seat) {
			river_seat_v1_destroy(seat_entries[i].river_seat);
			seat_entries[i].river_seat = nullptr;
		}
	}
	seat_count = 0;
	view = nullptr;
}

void Seat::river_seat_removed(void *data, struct river_seat_v1 *river_seat)
{
	Seat *seat = static_cast<Seat *>(data);
	(void)river_seat;
	// The object is destroyed during the next manage sequence: river
	// follows this event with a manage_start of its own.
	SeatEntry *entry = seat->find_entry(river_seat);
	if (entry) {
		entry->removed = true;
	}
	std::fprintf(stderr, "Yarfwm: seat removed\n");
}

void Seat::river_seat_wl_seat(void *data, struct river_seat_v1 *river_seat,
			      uint32_t name)
{
	// River owns the wl_seat plumbing; the window manager only needs the
	// name to correlate the two objects, which nothing does yet.
	(void)data;
	(void)river_seat;
	(void)name;
}

void Seat::river_seat_pointer_enter(void *data,
				    struct river_seat_v1 *river_seat,
				    struct river_window_v1 *window)
{
	Seat *seat = static_cast<Seat *>(data);
	if (!seat->focus_follows_mouse) {
		return;
	}
	seat->record_focus(seat->find_entry(river_seat), window);
}

void Seat::river_seat_pointer_leave(void *data,
				    struct river_seat_v1 *river_seat)
{
	(void)data;
	(void)river_seat;
}

void Seat::river_seat_window_interaction(void *data,
					 struct river_seat_v1 *river_seat,
					 struct river_window_v1 *window)
{
	// Clicking a window always focuses it, whether or not focus follows the
	// pointer.
	Seat *seat = static_cast<Seat *>(data);
	seat->record_focus(seat->find_entry(river_seat), window);
}

void Seat::river_seat_shell_surface_interaction(
    void *data, struct river_seat_v1 *river_seat,
    struct river_shell_surface_v1 *shell_surface)
{
	// A window manager shell surface (a bar drawn by the window manager)
	// took the click; there is no window to focus.
	(void)data;
	(void)river_seat;
	(void)shell_surface;
}

void Seat::river_seat_pointer_position(void *data,
				       struct river_seat_v1 *river_seat,
				       int32_t x, int32_t y)
{
	// River sends this in every manage sequence (unless the position is
	// unchanged). The keyboard pointer warp moves relative to the last
	// reported position, so it is kept per seat.
	Seat *seat = static_cast<Seat *>(data);
	SeatEntry *entry = seat->find_entry(river_seat);
	if (!entry) {
		return;
	}
	entry->pointer_x = x;
	entry->pointer_y = y;
}
