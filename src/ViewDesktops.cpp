#include "View.hpp"
#include "river-window-management-v1-client-protocol.h"

#include <csignal>
#include <cstdio>
#include <sys/wait.h>
#include <unistd.h>

// The virtual desktop subsystem.
//
// River has no desktop, tag, or workspace interface at all: the interfaces it
// exposes model outputs, seats, windows, nodes, layer surfaces and capture,
// and none of them can hold a second set of windows. Desktops are therefore a
// window manager invention here, built out of the one mechanism the protocol
// does provide — river_window_v1.hide and .show, which the XML allows in
// either sequence and which genuinely stop a window from being rendered.
//
// The model: every window carries a desktop index, exactly one desktop is
// active, and a window is visible when its desktop is the active one and it is
// not minimized.
//
// These two functions only change that state and ask for a manage sequence;
// they send nothing themselves. The visibility pass in
// window_manager_render_start() is the single place that turns the difference
// between the state and the cached Window::shown into show/hide requests.
// Writing that cache from here as well would make the pass see no difference
// and skip every request. Switching desktops also revalidates the keyboard
// focus, because the focused window may be on the desktop that just went
// away.
//
// Each switch is also announced to the user, because nothing else can: river
// has no desktop concept and no status bar can read yarfwm's index, so
// without the announcement a switch is invisible and the only way to tell
// where you are is to cycle and count.

// Announce a desktop switch through the notification daemon.
//
// notify-send runs as a child process rather than the window manager talking
// D-Bus itself: the notification daemon is a user choice and may not be
// running, and a failed announcement must never disturb the event loop.
//
// The private-synchronous hint makes a daemon that understands it (mako,
// dunst, ...) replace the previous bubble instead of stacking a new one, so
// cycling through the desktops leaves a single bubble that follows the
// current index.
//
// The double fork is the same shape as a key binding's spawn: the window
// manager waits only for the intermediate child, which exits immediately
// after the inner fork; notify-send itself is reparented to init and is
// never waited on.
//
// The desktop is shown one-based, the way a user counts desktops; the stderr
// log stays zero-based, matching the stored index.
static void announce_desktop_switch(int desktop_index, int desktop_total,
				    bool window_moved)
{
	char summary[64];
	std::snprintf(summary, sizeof(summary),
		      window_moved ? "Window -> desktop %d/%d"
				   : "Desktop %d/%d",
		      desktop_index + 1, desktop_total);

	pid_t process_id = fork();
	if (process_id < 0) {
		std::fprintf(
		    stderr,
		    "Yarfwm: failed to fork for the desktop notification\n");
		return;
	}

	if (process_id == 0) {
		setsid();
		pid_t inner = fork();
		if (inner < 0) {
			_exit(1);
		}
		if (inner > 0) {
			_exit(0);
		}

		// Same reason as the spawn path: the child must not be born
		// ignoring SIGPIPE.
		signal(SIGPIPE, SIG_DFL);

		execlp("notify-send", "notify-send", "-a", "yarfwm", "-u",
		       "low", "-t", "1500", "-h",
		       "string:x-canonical-private-synchronous:yarfwm-desktop",
		       summary, static_cast<char *>(nullptr));
		std::fprintf(stderr, "Yarfwm: failed to exec notify-send\n");
		_exit(127);
	}

	int status = 0;
	waitpid(process_id, &status, 0);
}

void View::focus_desktop(struct river_seat_v1 *river_seat, int delta)
{
	if (delta == 0) {
		return;
	}
	active_desktop =
	    wrap_desktop(active_desktop + delta, View::desktop_total());
	std::fprintf(stderr, "Yarfwm: focus_desktop -> %d\n", active_desktop);
	announce_desktop_switch(active_desktop, View::desktop_total(), false);

	// The windows of the desktop that just went away are hidden and the
	// windows of the new desktop are shown by the visibility pass in the
	// render sequence this asks for.
	//
	// The focused window may now be on a desktop that is not visible, so
	// hand the keyboard to something that is.
	hand_focus_to_visible_window(river_seat);
	request_manage();
}

void View::switch_to_window_desktop(struct river_window_v1 *window)
{
	Window *window_entry = find_window(window);
	if (!window_entry || window_entry->desktop == active_desktop) {
		return;
	}

	// labwc's desktop_focus_view_internal() switches workspace to make the
	// view visible before focusing it ("Switch workspace if necessary to
	// make the view visible", src/desktop.c:142-148), so focusing a view on
	// another workspace brings that workspace forward. Restore does the
	// same here: without it, restoring a window that was minimized on
	// another desktop would succeed in the model and show the user
	// nothing, which is exactly the "restore did not work" symptom.
	active_desktop = window_entry->desktop;
	std::fprintf(stderr, "Yarfwm: switch_to_window_desktop -> %d\n",
		     active_desktop);
	// Announced like any other switch: the user did not press the desktop
	// key, so without the bubble the screen would change with no
	// explanation.
	announce_desktop_switch(active_desktop, View::desktop_total(), false);
}

void View::move_window_to_desktop(struct river_seat_v1 *river_seat,
				  struct river_window_v1 *window, int delta)
{
	Window *window_entry = find_window(window);
	if (!window_entry || delta == 0) {
		return;
	}

	window_entry->desktop =
	    wrap_desktop(window_entry->desktop + delta, View::desktop_total());
	std::fprintf(stderr, "Yarfwm: move_window_to_desktop -> %d\n",
		     window_entry->desktop);
	announce_desktop_switch(window_entry->desktop, View::desktop_total(),
				true);

	// The window disappears from this desktop, or appears on it when the
	// wrap lands back on the active one, in the visibility pass. If it
	// held the keyboard and just left the active desktop, the keyboard
	// has to follow something visible.
	hand_focus_to_visible_window(river_seat);
	request_manage();
}
