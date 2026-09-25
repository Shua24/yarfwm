#ifndef PLACEMENT_HPP
#define PLACEMENT_HPP

#include <cstdint>

// Which way a directional focus move looks, used by
// View::window_in_direction(). This lives here rather than in View.hpp so the
// placement arithmetic below can be unit tested without pulling in the whole
// View header (and with it the generated protocol headers).
enum FocusDirection {
	focus_direction_left = 0,
	focus_direction_right = 1,
	focus_direction_up = 2,
	focus_direction_down = 3,
};

// A rectangle in output coordinates. Same field order and meaning as the
// window geometry the View tracks, so it can be passed straight through.
struct Rectangle {
	int32_t x;
	int32_t y;
	int32_t width;
	int32_t height;
};

// One cascade step, exactly as View::place_windows() performs it: advance by
// the cascade step, then wrap back to the area origin while keeping a margin.
// The result is written through the pointers, matching the output-parameter
// style of the other placement helpers and avoiding -Waggregate-return.
void cascade_next(const Rectangle &area, int32_t x, int32_t y, int32_t *next_x,
		  int32_t *next_y);

// The directional-focus score used by View::window_in_direction(). A negative
// return means the candidate is not in the requested direction at all;
// otherwise the score is the distance along the asked-for axis plus half the
// sideways offset, and the lowest score wins.
double direction_score(const Rectangle &source, const Rectangle &candidate,
		       enum FocusDirection direction);

// Wrap a desktop index into [0, count): positive modulo, so a switch left
// from desktop 0 lands on the last desktop, not on -1. A non-positive count
// has no valid index and yields 0.
int wrap_desktop(int desktop, int count);

// Clamp a proposed size to the bounds the window stated in its
// dimensions_hint event. Zero means "no preference" for that value.
void apply_dimension_hints(int32_t min_width, int32_t min_height,
			   int32_t max_width, int32_t max_height,
			   Rectangle &geometry);

// Follow parent links up to the root of a window's hierarchy and return its
// index. parent_index holds, for each entry, the index of its parent entry, or
// -1 when the entry has no parent. A window with no parent is its own root.
//
// The protocol guarantees "there are no loops in the window tree", but a bug
// upstream could still hand us one, so the walk is bounded by the entry count
// and returns the entry it would revisit rather than spinning.
//
// labwc minimizes a whole view hierarchy from any member of it (view.c:800-802
// minimizes the root, then every sub-view), which is why the root has to be
// findable from any member.
int minimize_root_index(const int *parent_index, int count, int index);

// The index of the root of the most recently minimized hierarchy, or -1 when
// nothing is minimized.
//
// Selection is by minimize_sequence, never by array position: yarfwm's restore
// action means "bring back the last thing I hid", and array order is not
// minimize order. View::remove_window() swaps the last entry into a freed slot,
// so array order changes under the user's feet, and a window minimized later
// can easily sit at a lower index than one minimized earlier.
int most_recently_minimized_root(const int *parent_index, const bool *minimized,
				 const uint64_t *minimize_sequence, int count);

#endif // PLACEMENT_HPP
