#include "DecorationGeometry.hpp"

// The pitch of one button: labwc lays buttons out with
// `button_width + button_spacing` between their left edges
// (src/ssd/ssd-titlebar.c:101-109).
static int32_t button_pitch(const TitlebarMetrics &metrics)
{
	return metrics.button_width + metrics.button_spacing;
}

int titlebar_visible_button_count(const TitlebarMetrics &metrics)
{
	int count = titlebar_button_count;
	if (metrics.button_width <= 0) {
		return 0;
	}
	// labwc prunes from the larger side and ties drop a right-hand button
	// (src/ssd/ssd-titlebar.c:240-247); with an empty left group every
	// drop removes the right-most remaining button.
	while (count > 0 && metrics.width < button_pitch(metrics) * count +
						metrics.padding * 2) {
		count--;
	}
	return count;
}

int32_t titlebar_button_left(const TitlebarMetrics &metrics,
			     int visible_button_count, int button_index)
{
	// Laid out backwards from the right edge: the LAST button in the group
	// ends up rightmost, which is why close is rightmost and index 0 is the
	// left-most drawn button.
	return metrics.width - metrics.padding -
	       button_pitch(metrics) * (visible_button_count - button_index);
}

bool titlebar_text_band(const TitlebarMetrics &metrics,
			int visible_button_count, int32_t *left, int32_t *right)
{
	const int32_t band_left = metrics.padding;
	const int32_t band_right = metrics.width - metrics.padding -
				   button_pitch(metrics) * visible_button_count;
	if (band_right <= band_left) {
		return false;
	}
	if (left) {
		*left = band_left;
	}
	if (right) {
		*right = band_right;
	}
	return true;
}

TitlebarPart titlebar_part_at(const TitlebarMetrics &metrics,
			      int visible_button_count, TitlebarPoint point)
{
	if (point.x < 0 || point.y < 0 || point.x >= metrics.width ||
	    point.y >= metrics.height) {
		return titlebar_part_none;
	}
	const int32_t button_y = (metrics.height - metrics.button_height) / 2;
	const bool inside_button_row =
	    point.y >= button_y && point.y < button_y + metrics.button_height;
	if (inside_button_row) {
		for (int index = 0; index < visible_button_count; index++) {
			const int32_t button_left = titlebar_button_left(
			    metrics, visible_button_count, index);
			if (point.x >= button_left &&
			    point.x < button_left + metrics.button_width) {
				// Buttons win over the bar, so a click on a
				// button is never mistaken for a drag.
				switch (index) {
				case titlebar_button_minimize:
					return titlebar_part_button_minimize;
				case titlebar_button_maximize:
					return titlebar_part_button_maximize;
				case titlebar_button_close:
					return titlebar_part_button_close;
				default:
					break;
				}
			}
		}
	}
	return titlebar_part_bar;
}

TitlebarPlacement titlebar_offset(int32_t titlebar_height, int32_t border_width,
				  const Rectangle &content,
				  const Rectangle &placement_area,
				  int32_t *offset_x, int32_t *offset_y)
{
	// The surface is above or below the content and spans its width, so the
	// x offset is always zero. river_decoration_v1.set_offset takes an i32
	// and negative offsets are legal (river/Decoration.zig:112-126).
	if (offset_x) {
		*offset_x = 0;
	}
	if (offset_y) {
		*offset_y = 0;
	}
	if (titlebar_height <= 0) {
		// Nothing to paint.
		return titlebar_placement_hidden;
	}
	if (border_width < 0) {
		border_width = 0;
	}

	// The compositor's border is drawn outside the content: the top border
	// occupies the border_width rows immediately above the content's first
	// row (river/Window.zig:1011-1016). The bar has to clear those rows.
	// Measured before this was handled: the bar's bottom row landed exactly
	// on the top border row and, because river draws above-decorations last
	// (river-window-management-v1.xml:1199-1202), the focused window's top
	// border was invisible -- 0 of 640 pixels on the top row.
	const int32_t frame_top = titlebar_height + border_width;

	// The bar belongs above the content, and the whole bar plus the border
	// has to fit inside the placement area or it would be clipped by the
	// output.
	const int32_t room_above = content.y - placement_area.y;
	if (room_above >= frame_top) {
		if (offset_y) {
			*offset_y = -frame_top;
		}
		return titlebar_placement_above;
	}

	// Not enough room above. Rather than covering the top of the window --
	// which is what an offset of 0 would do, and what this function used to
	// do -- try below the content, so the bar stays on screen with its
	// buttons reachable. The bottom border is in the way there too.
	const int32_t content_bottom = content.y + content.height;
	const int32_t area_bottom = placement_area.y + placement_area.height;
	if (area_bottom - content_bottom >= titlebar_height + border_width) {
		if (offset_y) {
			*offset_y = content.height + border_width;
		}
		return titlebar_placement_below;
	}

	// Fits nowhere outside the content: paint nothing. A window with no
	// titlebar is a smaller loss than a window with its top rows hidden
	// behind one.
	return titlebar_placement_hidden;
}

void reserve_titlebar_strip(const Rectangle &placement_area, int32_t top_margin,
			    Rectangle *content_area)
{
	if (!content_area) {
		return;
	}
	*content_area = placement_area;
	if (top_margin <= 0) {
		return;
	}
	// Clamp rather than invert: an area shorter than two frame tops cannot
	// give the bar its own strip and still leave the window anything to
	// show, so it keeps half and the bar falls back to
	// titlebar_placement_below or titlebar_placement_hidden. Windows are
	// never moved outside the area.
	//
	// The comparison is written as a division rather than `height * 2 >`
	// because top_margin comes from the user's config file and doubling a
	// value near INT32_MAX would overflow.
	if (top_margin > placement_area.height / 2) {
		top_margin = placement_area.height / 2;
	}
	if (top_margin <= 0) {
		return;
	}
	content_area->y += top_margin;
	content_area->height -= top_margin;
}

bool decoration_titlebar_visible(bool maximized, bool fullscreen,
				 bool titlebar_when_maximized)
{
	if (fullscreen) {
		return false;
	}
	if (maximized && !titlebar_when_maximized) {
		return false;
	}
	return true;
}
