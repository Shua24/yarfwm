// The cursor shape an interactive pointer operation shows.
//
// Pure arithmetic on bit flags, so it lives on its own and is tested without a
// Wayland connection. The eight resize directions matter because a resize
// arrow must point at the edge the user is pulling: a left-edge resize shows
// the left arrow, and a corner shows the diagonal.
#include "CursorShape.hpp"

CursorShape cursor_shape_for_operation(bool is_resize, uint32_t edges)
{
	if (!is_resize) {
		// A move: the closed hand, the grab cursor. The pointer is
		// holding the window.
		return cursor_shape_move;
	}

	const bool top = (edges & cursor_edge_top) != 0;
	const bool bottom = (edges & cursor_edge_bottom) != 0;
	const bool left = (edges & cursor_edge_left) != 0;
	const bool right = (edges & cursor_edge_right) != 0;

	// Corners first: a corner carries two edge bits, so it must be
	// matched before the single-edge cases below.
	if (top && left) {
		return cursor_shape_nw_resize;
	}
	if (top && right) {
		return cursor_shape_ne_resize;
	}
	if (bottom && left) {
		return cursor_shape_sw_resize;
	}
	if (bottom && right) {
		return cursor_shape_se_resize;
	}

	if (left) {
		return cursor_shape_w_resize;
	}
	if (right) {
		return cursor_shape_e_resize;
	}
	if (top) {
		return cursor_shape_n_resize;
	}
	if (bottom) {
		return cursor_shape_s_resize;
	}

	// River never sends a resize with no edges (the XML says the edges
	// argument "will never be none"), so this is unreachable in practice.
	// The default shape is returned rather than an invalid enum value.
	return cursor_shape_default;
}
