#include "Placement.hpp"
#include "Seat.hpp"
#include "View.hpp"
#include "river-window-management-v1-client-protocol.h"

#include <cstdio>
#include <new>

// Minimize and restore: labwc's Iconify, expressed in what the protocol can
// carry.
//
// River's XML on minimize_requested says the window manager "is free to
// ignore this request, hide the window, or do whatever else it chooses."
// Hiding is the protocol's own answer to minimize, and there is no
// inform_minimized request to pair with it: the visibility pass in the render
// sequence hides the window once Window::minimized is set. A minimized window
// stays tracked and mapped, which is exactly labwc's shape too — there,
// view_update_visibility() disables the scene node and the surface stays
// mapped (src/view.c:2374-2410).
//
// Two labwc behaviours are reproduced here because they are what makes
// minimize feel right, and both are reachable through river's protocol:
//
//   1. The whole hierarchy minimizes together. labwc's view_minimize() takes
//      the root of the window tree and then every sub-view
//      (src/view.c:784-816): "if an 'About' or 'Open File' dialog is minimized,
//      its toplevel is minimized also. And vice versa." yarfwm has the parent
//      link river reports (river_window_v1.parent), so the same walk is
//      possible.
//
//   2. Focus falls to the topmost visible window, not to the previously
//      focused one: labwc calls desktop_focus_topmost_view() on minimize
//      (src/view.c:809-810), which is desktop_topmost_focusable_view() — the
//      first non-minimized view from the front of the stacking list
//      (src/desktop.c:199-219).
//
// What is NOT reproduced, and why: labwc also updates its foreign-toplevel
// handle so a taskbar can show the minimized state and restore the window
// (src/foreign-toplevel/wlr-foreign.c:11-19, :148-157). That path does not
// exist for a river window manager — river subscribes to no listener on the
// wlroots foreign-toplevel handle, so a panel's click never reaches us and
// there is no protocol event to receive it through. See
// docs/features/panel-taskbar.md. The restore binding is therefore the only
// way back, which is why it ships bound by default.

// Fill parent_index with, for each tracked entry, the index of its parent
// entry or -1 when it has none. Returns false when the array is unusable.
//
// This is the tree shape the two pure helpers in src/Placement.cpp walk, so
// the walking logic itself is unit tested without a Wayland connection.
bool View::build_parent_index(int *parent_index) const
{
	if (!parent_index || window_count <= 0) {
		return false;
	}
	for (int i = 0; i < window_count; i++) {
		const Window *parent_entry =
		    windows[i].parent ? find_window(windows[i].parent)
				      : nullptr;
		parent_index[i] = parent_entry
				      ? static_cast<int>(parent_entry - windows)
				      : -1;
	}
	return true;
}

// How many tracked windows a hierarchy holds, for the log line.
int View::hierarchy_size(const int *parent_index, int root_index) const
{
	int count = 0;
	for (int i = 0; i < window_count; i++) {
		if (minimize_root_index(parent_index, window_count, i) ==
		    root_index) {
			count++;
		}
	}
	return count;
}

// Set or clear the minimized flag across a whole hierarchy.
//
// Every member carries the same minimize_sequence, so a later restore can find
// the hierarchy again from any of its members.
void View::set_hierarchy_minimized(const int *parent_index, int root_index,
				   bool minimized, uint64_t minimize_sequence)
{
	if (root_index < 0 || root_index >= window_count) {
		return;
	}

	// The root first, then the rest: labwc minimizes the root before its
	// sub-views (src/view.c:800-802). The resulting state is the same
	// either way — these are flags, not requests — so the order is kept
	// only so the code reads the way the reference does.
	for (int pass = 0; pass < 2; pass++) {
		for (int i = 0; i < window_count; i++) {
			const bool is_root = i == root_index;
			const bool wanted_pass = is_root ? 0 : 1;
			if (pass != wanted_pass) {
				continue;
			}
			if (minimize_root_index(parent_index, window_count,
						i) != root_index) {
				continue;
			}
			windows[i].minimized = minimized;
			windows[i].minimize_sequence =
			    minimized ? minimize_sequence : 0;
		}
	}
}

void View::minimize_window(struct river_window_v1 *window)
{
	const Window *window_entry = find_window(window);
	if (!window_entry) {
		return;
	}

	// Dynamic memory: one int per tracked window, freed on every path.
	int *parent_index =
	    new (std::nothrow) int[window_count > 0 ? window_count : 1];
	if (!parent_index) {
		return;
	}
	build_parent_index(parent_index);

	const int root_index =
	    minimize_root_index(parent_index, window_count,
				static_cast<int>(window_entry - windows));
	if (root_index < 0 || windows[root_index].minimized) {
		// Nothing to do: an already-minimized hierarchy must not take a
		// second sequence number, or restore order would drift.
		delete[] parent_index;
		return;
	}

	// Hand out the sequence before hiding anything, so the most recent
	// minimize always carries the highest value.
	const uint64_t sequence = next_minimize_sequence++;
	set_hierarchy_minimized(parent_index, root_index, true, sequence);
	const int hidden = hierarchy_size(parent_index, root_index);
	delete[] parent_index;

	// A minimized hierarchy stops being visible, so if the keyboard was on
	// any member of it the focus has to move to something still on screen.
	// labwc: desktop_focus_topmost_view() (src/view.c:809-810).
	std::fprintf(stderr, "Yarfwm: minimize_window -> %d window(s)\n",
		     hidden);
	if (seat) {
		hand_focus_to_visible_window(seat->primary_river_seat());
	}
	request_manage();
}

void View::restore_minimized_window()
{
	if (window_count <= 0) {
		std::fprintf(stderr,
			     "Yarfwm: restore_minimized_window: none\n");
		return;
	}

	// Dynamic memory: three arrays of one element per tracked window, all
	// freed on every path below.
	int *parent_index = new (std::nothrow) int[window_count];
	bool *minimized = new (std::nothrow) bool[window_count];
	uint64_t *sequence = new (std::nothrow) uint64_t[window_count];
	if (!parent_index || !minimized || !sequence) {
		delete[] parent_index;
		delete[] minimized;
		delete[] sequence;
		return;
	}

	build_parent_index(parent_index);
	for (int i = 0; i < window_count; i++) {
		minimized[i] = windows[i].minimized;
		sequence[i] = windows[i].minimize_sequence;
	}

	const int root_index = most_recently_minimized_root(
	    parent_index, minimized, sequence, window_count);
	delete[] minimized;
	delete[] sequence;

	if (root_index < 0) {
		delete[] parent_index;
		std::fprintf(stderr,
			     "Yarfwm: restore_minimized_window: none\n");
		return;
	}

	set_hierarchy_minimized(parent_index, root_index, false, 0);
	const int shown = hierarchy_size(parent_index, root_index);
	delete[] parent_index;

	struct river_window_v1 *restored = windows[root_index].window;
	std::fprintf(stderr,
		     "Yarfwm: restore_minimized_window -> %d window(s)\n",
		     shown);

	// labwc's unminimize path: desktop_focus_view(view, raise=true)
	// (src/view.c:811-812). The window goes to the front of the stacking
	// order and takes the keyboard back. If it was minimized on another
	// virtual desktop, that desktop is brought forward first — labwc
	// switches workspace to make the view visible (src/desktop.c:142-148),
	// and without it the restore would be a no-op on screen.
	switch_to_window_desktop(restored);
	raise_window(restored);
	if (seat) {
		struct river_seat_v1 *river_seat = seat->primary_river_seat();
		if (river_seat) {
			if (window_is_visible(restored)) {
				seat->focus(river_seat, restored);
			} else {
				hand_focus_to_visible_window(river_seat);
			}
		}
	}
	request_manage();
}
