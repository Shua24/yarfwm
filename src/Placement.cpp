#include "Placement.hpp"

// The pure placement arithmetic from the View, kept separate so it can be unit
// tested without a Wayland connection. The behaviour is copied verbatim from
// View::place_windows() and View::window_in_direction(); if you change it here
// you are changing the window manager's placement policy.

// The cascade constants as View::place_windows() uses them: a 32px step and a
// 64px margin kept free at the far edge before wrapping.
static const int32_t cascade_step = 32;
static const int32_t cascade_margin = 64;

void cascade_next(const Rectangle &area, int32_t x, int32_t y, int32_t *next_x,
		  int32_t *next_y)
{
	x += cascade_step;
	y += cascade_step;
	if (x > area.x + area.width - cascade_margin) {
		x = area.x;
	}
	if (y > area.y + area.height - cascade_margin) {
		y = area.y;
	}
	*next_x = x;
	*next_y = y;
}

double direction_score(const Rectangle &source, const Rectangle &candidate,
		       enum FocusDirection direction)
{
	const double source_center_x = source.x + source.width / 2.0;
	const double source_center_y = source.y + source.height / 2.0;
	const double delta_x =
	    candidate.x + candidate.width / 2.0 - source_center_x;
	const double delta_y =
	    candidate.y + candidate.height / 2.0 - source_center_y;

	// The asked-for axis picks the direction; the distance along it, plus
	// half the sideways offset, picks the winner among the candidates.
	double along = 0.0;
	double sideways = 0.0;
	switch (direction) {
	case focus_direction_left:
		if (delta_x >= 0.0) {
			return -1.0;
		}
		along = -delta_x;
		sideways = delta_y < 0.0 ? -delta_y : delta_y;
		break;
	case focus_direction_right:
		if (delta_x <= 0.0) {
			return -1.0;
		}
		along = delta_x;
		sideways = delta_y < 0.0 ? -delta_y : delta_y;
		break;
	case focus_direction_up:
		if (delta_y >= 0.0) {
			return -1.0;
		}
		along = -delta_y;
		sideways = delta_x < 0.0 ? -delta_x : delta_x;
		break;
	case focus_direction_down:
		if (delta_y <= 0.0) {
			return -1.0;
		}
		along = delta_y;
		sideways = delta_x < 0.0 ? -delta_x : delta_x;
		break;
	default:
		return -1.0;
	}

	return along + sideways / 2.0;
}

// The desktop wrap, moved verbatim from ViewDesktops.cpp so the wrap-around
// rule can be unit tested. Positive modulo: a switch left from desktop 0 must
// land on the last desktop, not on -1.
int wrap_desktop(int desktop, int count)
{
	if (count <= 0) {
		return 0;
	}
	desktop %= count;
	if (desktop < 0) {
		desktop += count;
	}
	return desktop;
}

// The dimension-hint clamp, moved verbatim from ViewWindowState.cpp. Zero
// means "no preference" for that value, which is how the protocol spells it:
// "A value of 0 indicates that the window has no preference for that value."
// The bounds are passed as values so this stays a free function.
void apply_dimension_hints(int32_t min_width, int32_t min_height,
			   int32_t max_width, int32_t max_height,
			   Rectangle &geometry)
{
	if (min_width > 0 && geometry.width < min_width) {
		geometry.width = min_width;
	}
	if (min_height > 0 && geometry.height < min_height) {
		geometry.height = min_height;
	}
	if (max_width > 0 && geometry.width > max_width) {
		geometry.width = max_width;
	}
	if (max_height > 0 && geometry.height > max_height) {
		geometry.height = max_height;
	}
}

// The root of a window hierarchy, found by walking parent links. labwc does
// this through view_get_root() (view.c:257-264) and then minimizes the root
// before its sub-views, so that a dialog's toplevel is minimized along with it
// and vice versa.
//
// A cycle would be a protocol violation ("The compositor must guarantee that
// there are no loops in the window tree"), but this runs inside the event loop
// on state river supplied, so it must not be able to hang: the walk is capped
// at the entry count and gives up on the entry it would revisit.
int minimize_root_index(const int *parent_index, int count, int index)
{
	if (!parent_index || index < 0 || index >= count) {
		return -1;
	}

	int root = index;
	for (int step = 0; step < count; step++) {
		const int parent = parent_index[root];
		if (parent < 0 || parent >= count || parent == root) {
			break;
		}
		root = parent;
	}
	return root;
}

// The root of the most recently minimized hierarchy.
//
// "Most recently minimized" is tracked with a counter rather than inferred
// from array position. Array order is not minimize order: remove_window()
// swaps the last entry into the freed slot, and a hierarchy minimized later
// can sit at a lower index than one minimized earlier, so an index scan would
// restore the wrong window.
int most_recently_minimized_root(const int *parent_index, const bool *minimized,
				 const uint64_t *minimize_sequence, int count)
{
	int best = -1;
	uint64_t best_sequence = 0;

	for (int index = 0; index < count; index++) {
		if (!minimized[index]) {
			continue;
		}
		const uint64_t sequence = minimize_sequence[index];
		if (sequence == 0) {
			// Not minimized through the action (no sequence was
			// handed out); it cannot be the most recent one.
			continue;
		}
		if (best < 0 || sequence > best_sequence) {
			best = index;
			best_sequence = sequence;
		}
	}

	if (best < 0) {
		return -1;
	}
	return minimize_root_index(parent_index, count, best);
}
