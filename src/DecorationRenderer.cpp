// yarfwm -- the titlebar painter.
//
// This is the only file in the tree that knows cairo exists. Everything it
// touches is the caller's plain ARGB8888 memory, so it is unit testable with
// no Wayland connection at all.
//
// Two things here are load-bearing and easy to get wrong:
//
//   - CAIRO_FORMAT_ARGB32 is PREMULTIPLIED. A colour's channels must be
//     scaled by its alpha before being handed to cairo, or a translucent
//     titlebar composites too brightly. On opaque colours the mistake is
//     invisible, which is what makes it a trap.
//   - The text source must be set explicitly before showing the layout. After
//     painting the background the source is still the bar colour, so a title
//     drawn without this call is painted in the background colour and is
//     invisible. With the shipped defaults it is invisible *twice over*:
//     button_glyph_color and text_active_color are both ffffffff, so the title
//     silently inherits the glyph colour instead. The unit tests cannot catch
//     it; the live probe can.
#include "DecorationSettings.hpp"

#include <cstring>

#include <cairo.h>
#include <pango/pangocairo.h>

namespace
{

// 0xAARRGGBB -> cairo's [0,1] doubles.
void set_source_argb(cairo_t *cr, uint32_t argb, bool premultiply)
{
	const double a = static_cast<double>((argb >> 24) & 0xffu) / 255.0;
	const double r = static_cast<double>((argb >> 16) & 0xffu) / 255.0;
	const double g = static_cast<double>((argb >> 8) & 0xffu) / 255.0;
	const double b = static_cast<double>(argb & 0xffu) / 255.0;
	if (premultiply) {
		cairo_set_source_rgba(cr, r * a, g * a, b * a, a);
	} else {
		cairo_set_source_rgba(cr, r, g, b, a);
	}
}

// labwc's icon padding: inset the glyph square by button_width / 10
// (labwc src/ssd/ssd-button.c:49). Integer division on purpose, which means a
// button narrower than 10px gets no inset at all.
void draw_button_glyph(cairo_t *cr, int button_index, int32_t left, int32_t top,
		       int32_t width, int32_t height, uint32_t glyph_argb)
{
	const double inset = static_cast<double>(width / 10);
	const double x0 = static_cast<double>(left) + inset;
	const double y0 = static_cast<double>(top) + inset;
	const double x1 = static_cast<double>(left + width) - inset;
	const double y1 = static_cast<double>(top + height) - inset;

	set_source_argb(cr, glyph_argb, true);
	cairo_set_line_width(cr, 1.0);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);

	switch (button_index) {
	case titlebar_button_minimize: // a horizontal bar
		cairo_move_to(cr, x0, (y0 + y1) / 2.0);
		cairo_line_to(cr, x1, (y0 + y1) / 2.0);
		cairo_stroke(cr);
		break;
	case titlebar_button_maximize: // a square outline
		cairo_rectangle(cr, x0, y0, x1 - x0, y1 - y0);
		cairo_stroke(cr);
		break;
	case titlebar_button_close: // a cross
		cairo_move_to(cr, x0, y0);
		cairo_line_to(cr, x1, y1);
		cairo_move_to(cr, x0, y1);
		cairo_line_to(cr, x1, y0);
		cairo_stroke(cr);
		break;
	default:
		break;
	}
}

} // namespace

void render_titlebar(uint32_t *pixels, int32_t width, int32_t height,
		     int32_t stride, const TitlebarMetrics &metrics,
		     const TitlebarColors &colors, const char *title,
		     bool active, int visible_button_count,
		     const char *font_description)
{
	if (pixels == nullptr || width <= 0 || height <= 0) {
		return;
	}

	// 1. Zero the buffer so a previous frame cannot show through.
	std::memset(pixels, 0,
		    static_cast<size_t>(stride) * static_cast<size_t>(height));

	// 2. The cairo surface over the caller's mapping. ARGB32 is
	//    premultiplied, which is what WL_SHM_FORMAT_ARGB8888 wants: on a
	//    little-endian machine both are B,G,R,A in memory.
	cairo_surface_t *surface = cairo_image_surface_create_for_data(
	    reinterpret_cast<unsigned char *>(pixels), CAIRO_FORMAT_ARGB32,
	    width, height, stride);
	cairo_t *cr = cairo_create(surface);

	// 3. Background, active or inactive.
	const uint32_t background =
	    active ? colors.background_active : colors.background_inactive;
	set_source_argb(cr, background, true);
	cairo_paint(cr);

	// The renderer trusts the caller's count, but never past the end of the
	// button enum: a stale count is the classic out-of-range glyph index.
	int button_count = visible_button_count;
	if (button_count < 0) {
		button_count = 0;
	}
	if (button_count > titlebar_button_count) {
		button_count = titlebar_button_count;
	}

	// 4. Button glyphs, in the squares the geometry helper hands out.
	for (int i = 0; i < button_count; i++) {
		const int32_t left =
		    titlebar_button_left(metrics, button_count, i);
		draw_button_glyph(cr, i, left, 0, metrics.button_width,
				  metrics.button_height, colors.button_glyph);
	}

	// 5. The title, inside the band the buttons leave over.
	int32_t band_left = 0;
	int32_t band_right = 0;
	if (title != nullptr && titlebar_text_band(metrics, button_count,
						   &band_left, &band_right)) {
		PangoLayout *layout = pango_cairo_create_layout(cr);
		PangoFontDescription *font =
		    pango_font_description_from_string(font_description);
		pango_layout_set_font_description(layout, font);
		pango_layout_set_width(layout,
				       (band_right - band_left) * PANGO_SCALE);
		pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
		pango_layout_set_text(layout, title, -1);

		// Required, and easy to omit: the source is still the
		// background colour from step 3.
		set_source_argb(
		    cr, active ? colors.text_active : colors.text_inactive,
		    true);

		int text_width = 0;
		int text_height = 0;
		pango_layout_get_pixel_size(layout, &text_width, &text_height);
		// Vertical centring, labwc's (titlebar_height - text height)/2
		// (src/ssd/ssd-titlebar.c:379).
		cairo_move_to(cr,
			      static_cast<double>(band_left + metrics.padding),
			      static_cast<double>((height - text_height) / 2));
		pango_cairo_show_layout(cr, layout);

		g_object_unref(layout);
		pango_font_description_free(font);
	}

	// 6. Flush before destroying, or the last writes may not be in the
	//    mapping.
	cairo_surface_flush(surface);

	// 7. Free everything.
	cairo_destroy(cr);
	cairo_surface_destroy(surface);
}

void border_color_components(uint32_t argb, uint32_t *r, uint32_t *g,
			     uint32_t *b, uint32_t *a)
{
	if (r == nullptr || g == nullptr || b == nullptr || a == nullptr) {
		return;
	}
	// river wants each component as a percentage in 32 bits, 0xffffffff =
	// 100%, so one byte is replicated across all four bytes.
	const uint32_t alpha = (argb >> 24) & 0xffu;
	*r = ((argb >> 16) & 0xffu) * 0x01010101u;
	*g = ((argb >> 8) & 0xffu) * 0x01010101u;
	*b = (argb & 0xffu) * 0x01010101u;
	*a = alpha * 0x01010101u;
}
