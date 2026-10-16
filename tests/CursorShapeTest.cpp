// The cursor shape an interactive pointer operation shows.
//
// Pure mapping from the operation kind and the river resize-edge bit flags to
// a cursor-shape-v1 shape value, so it is testable without a Wayland
// connection. The thing that matters: the arrow must point at the edge the
// user is pulling, and a corner (two bits set) must win over either single
// edge.
#include <gtest/gtest.h>

#include "CursorShape.hpp"

TEST(CursorShape, MoveShowsTheMoveCursor)
{
	// A move ignores the edges argument entirely.
	EXPECT_EQ(cursor_shape_for_operation(false, 0), cursor_shape_move);
	EXPECT_EQ(cursor_shape_for_operation(false, cursor_edge_left),
		  cursor_shape_move);
}

TEST(CursorShape, SingleEdgesPointAtTheirOwnEdge)
{
	EXPECT_EQ(cursor_shape_for_operation(true, cursor_edge_left),
		  cursor_shape_w_resize);
	EXPECT_EQ(cursor_shape_for_operation(true, cursor_edge_right),
		  cursor_shape_e_resize);
	EXPECT_EQ(cursor_shape_for_operation(true, cursor_edge_top),
		  cursor_shape_n_resize);
	EXPECT_EQ(cursor_shape_for_operation(true, cursor_edge_bottom),
		  cursor_shape_s_resize);
}

TEST(CursorShape, CornersWinOverSingleEdges)
{
	// A corner carries two bits. Each of these would match a single-edge
	// case too, so the corner cases must be checked first.
	EXPECT_EQ(cursor_shape_for_operation(true, cursor_edge_top |
						       cursor_edge_left),
		  cursor_shape_nw_resize);
	EXPECT_EQ(cursor_shape_for_operation(true, cursor_edge_top |
						       cursor_edge_right),
		  cursor_shape_ne_resize);
	EXPECT_EQ(cursor_shape_for_operation(true, cursor_edge_bottom |
						       cursor_edge_left),
		  cursor_shape_sw_resize);
	EXPECT_EQ(cursor_shape_for_operation(true, cursor_edge_bottom |
						       cursor_edge_right),
		  cursor_shape_se_resize);
}

TEST(CursorShape, NoEdgesFallsBackToDefault)
{
	// river's XML says the edges argument "will never be none" for a
	// resize, so this is unreachable in practice -- but it must not
	// return an invalid shape value.
	EXPECT_EQ(cursor_shape_for_operation(true, 0), cursor_shape_default);
}

TEST(CursorShape, EveryEdgeCombinationIsAValidShape)
{
	// Sweep all 16 combinations of the four edge bits and assert the
	// result is one of the shape values cursor-shape-v1 defines, so no
	// combination can ever produce a shape the compositor would reject.
	for (uint32_t edges = 0; edges < 16; edges++) {
		const CursorShape shape =
		    cursor_shape_for_operation(true, edges);
		const bool known = shape == cursor_shape_default ||
				   shape == cursor_shape_move ||
				   shape == cursor_shape_grab ||
				   shape == cursor_shape_grabbing ||
				   shape == cursor_shape_e_resize ||
				   shape == cursor_shape_n_resize ||
				   shape == cursor_shape_ne_resize ||
				   shape == cursor_shape_nw_resize ||
				   shape == cursor_shape_s_resize ||
				   shape == cursor_shape_se_resize ||
				   shape == cursor_shape_sw_resize ||
				   shape == cursor_shape_w_resize;
		EXPECT_TRUE(known) << "edges=" << edges;
	}
}
