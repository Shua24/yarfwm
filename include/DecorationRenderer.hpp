#ifndef DECORATION_RENDERER_HPP
#define DECORATION_RENDERER_HPP

#include "DecorationSettings.hpp"
#include <cstdint>

// Paints one titlebar into an ARGB8888 buffer of stride `stride` bytes. No
// Wayland object is touched here, so the painting is unit-testable with a
// plain malloc'ed buffer (tests/DecorationRendererTest.cpp).
//
// The glyphs are drawn as simple rectangles and lines, not as images: v1 has
// no icon assets. Minimize is a horizontal bar, maximize a square outline,
// close a cross.
//
// `visible_button_count` is clamped to the button enum's range here, because a
// stale count from a caller would otherwise index past the three-button
// layout and paint a square off the right edge of the buffer.
void render_titlebar(uint32_t *pixels, int32_t width, int32_t height,
		     int32_t stride, const TitlebarMetrics &metrics,
		     const TitlebarColors &colors, const char *title,
		     bool active, int visible_button_count,
		     const char *font_description);

// The colour a window's border takes from its focus state, in the 32-bit RGBA
// the river protocol wants (0xffffffff = 100%). Reversed byte order from
// TitlebarColors on purpose: river takes r,g,b,a as separate arguments and
// this helper only exists so the config's 0xAARRGGBB spelling has one
// conversion site.
void border_color_components(uint32_t argb, uint32_t *r, uint32_t *g,
			     uint32_t *b, uint32_t *a);

#endif // DECORATION_RENDERER_HPP
