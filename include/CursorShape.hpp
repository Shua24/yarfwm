#ifndef CURSOR_SHAPE_HPP
#define CURSOR_SHAPE_HPP

#include <cstdint>

// Which cursor shape an interactive pointer operation should show.
//
// This is a pure mapping from a pointer operation to a cursor-shape-v1 shape
// enum value, kept in its own translation unit so it can be unit tested
// without a Wayland connection. The values are deliberately the
// wp_cursor_shape_device_v1 shape numbers, spelled out here rather than
// included, so this header stays free of generated protocol headers.
enum CursorShape {
	// wp_cursor_shape_device_v1 shape values.
	cursor_shape_default = 1,
	cursor_shape_move = 13,
	cursor_shape_grab = 16,
	cursor_shape_grabbing = 17,
	cursor_shape_e_resize = 18,
	cursor_shape_n_resize = 19,
	cursor_shape_ne_resize = 20,
	cursor_shape_nw_resize = 21,
	cursor_shape_s_resize = 22,
	cursor_shape_se_resize = 23,
	cursor_shape_sw_resize = 24,
	cursor_shape_w_resize = 25,
};

// The resize edges, mirroring the river edges bit flags. Repeated here so the
// mapping does not drag a generated protocol header into its consumers.
enum CursorResizeEdge {
	cursor_edge_top = 1,
	cursor_edge_bottom = 2,
	cursor_edge_left = 4,
	cursor_edge_right = 8,
};

// The cursor for an operation that is in progress.
//
// is_resize picks between the move cursor and a resize arrow; edges is the
// river edges bit mask and is ignored for a move. A corner carries two bits,
// which is why the eight cases are spelled out: a resize arrow points at the
// edge being pulled, and a corner points diagonally.
//
// A resize with no edge set cannot come from river (the XML says the edges
// argument "will never be none"), but the mapping still returns something
// sensible rather than an invalid shape value.
CursorShape cursor_shape_for_operation(bool is_resize, uint32_t edges);

#endif // CURSOR_SHAPE_HPP
