#include "Seat.hpp"
#include "View.hpp"
#include "river-window-management-v1-client-protocol.h"

#include <cstdio>

// The virtual desktop subsystem.
//
// River has no desktop, tag, or workspace interface at all: the eight
// interfaces it exposes model outputs, seats, windows, nodes, layer surfaces
// and capture, and none of them can hold a second set of windows. Desktops
// are therefore a window manager invention here, built out of the one
// mechanism the protocol does provide — river_window_v1.hide and .show, which
// the XML allows in either sequence and which genuinely stop a window from
// being rendered.
//
// The model: every window carries a desktop index, exactly one desktop is
// active, and a window is shown when its desktop is the active one and it is
// not minimized. Switching desktops is a hide/show sweep plus a focus fixup,
// because the focused window may be on the desktop that just went away.

// How many virtual desktops exist, and how a switch wraps around them.
// Positive modulo: a switch left from desktop 0 must land on the last desktop,
// not on -1.
static int wrap_desktop(int desktop)
{
	const int count = View::desktop_total();
	if (count <= 0) {
		return 0;
	}
	desktop %= count;
	if (desktop < 0) {
		desktop += count;
	}
	return desktop;
}

void View::focus_desktop(struct river_seat_v1 *river_seat, int delta)
{
	if (delta == 0) {
		return;
	}
	active_desktop = wrap_desktop(active_desktop + delta);
	std::fprintf(stderr, "Yarfwm: focus_desktop -> %d\n", active_desktop);

	// River has no desktop concept, so switching is hide and show. The
	// request is allowed in either sequence; recording the intent and
	// letting the manage sequence send it keeps every request in one
	// place.
	for (int i = 0; i < window_count; i++) {
		Window *window_entry = &windows[i];
		if (!window_entry->window) {
			continue;
		}
		// A window that is minimized stays hidden whatever the
		// desktop: its own flag decides that.
		window_entry->shown = window_entry->minimized ||
				      window_entry->desktop == active_desktop;
	}

	// The focused window may now be on a desktop that is not visible, so
	// hand the keyboard to something that is.
	if (seat && river_seat) {
		struct river_window_v1 *focused =
		    seat->focused_window(river_seat);
		Window *focused_entry = find_window(focused);
		if (!focused_entry ||
		    focused_entry->desktop != active_desktop) {
			struct river_window_v1 *next = nullptr;
			for (int i = 0; i < window_count; i++) {
				if (windows[i].window &&
				    windows[i].desktop == active_desktop &&
				    !windows[i].minimized) {
					next = windows[i].window;
					break;
				}
			}
			if (next) {
				seat->focus(river_seat, next);
			} else {
				seat->focus_none(river_seat);
			}
		}
	}

	request_manage();
}

void View::move_window_to_desktop(struct river_window_v1 *window, int delta)
{
	Window *window_entry = find_window(window);
	if (!window_entry || delta == 0) {
		return;
	}

	window_entry->desktop = wrap_desktop(window_entry->desktop + delta);
	std::fprintf(stderr, "Yarfwm: move_window_to_desktop -> %d\n",
		     window_entry->desktop);
	// Sending it to another desktop makes it disappear from this one.
	window_entry->shown =
	    window_entry->minimized || window_entry->desktop == active_desktop;
	request_manage();
}
