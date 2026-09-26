#ifndef DECORATION_SETTINGS_HPP
#define DECORATION_SETTINGS_HPP

#include "DecorationGeometry.hpp"
#include <cstdint>

struct WindowDecorationsConfig;

// The colours a titlebar is painted with, resolved from the config by the
// caller. ARGB, 0xAARRGGBB.
struct TitlebarColors {
	uint32_t background_active;
	uint32_t background_inactive;
	uint32_t text_active;
	uint32_t text_inactive;
	uint32_t button_glyph;
};

// Everything the titlebar renderer and the border pass need, resolved from
// window-decorations.json once at startup.
//
// This header exists so that View.hpp can hold one of these BY VALUE without
// including DecorationRenderer.hpp: nothing here mentions cairo or pango, only
// the geometry structs and plain integers. DecorationRenderer.hpp includes it,
// not the other way round.
struct DecorationSettings {
	TitlebarMetrics metrics;
	TitlebarColors colors;
	// A pango font description, e.g. "JetBrains Mono 10".
	char font[128];
	// Paint a titlebar on a window maximized on both axes. From
	// window-decorations.json "titlebar_when_maximized", default true.
	bool titlebar_when_maximized;
	// The compositor-drawn border. It is NOT part of the titlebar: river
	// draws it (river_window_v1.set_borders), so it needs no renderer and
	// no ShmBuffer. It lives here because it comes from the same config
	// file and is consumed by the same pass (View::apply_decorations).
	int32_t border_width;
	uint32_t border_active_color;
	uint32_t border_inactive_color;
};

// Fills `settings` from a WindowDecorationsConfig. Defined in
// src/ViewDecoration.cpp, the only place that reads window-decorations.json.
//
// An out-parameter rather than a return value: returning the struct would
// trigger -Waggregate-return, which this project already fights elsewhere.
//
//   metrics.height        <- titlebar_height
//   metrics.button_width  <- button_width
//   metrics.button_height <- button_width (labwc's buttons are square)
//   metrics.width         <- 0; it is per-window and set by Decoration::update
//   metrics.button_spacing, metrics.padding <- the same config keys
//   colors.background_*   <- titlebar_active_color / titlebar_inactive_color
//   colors.text_*         <- text_active_color / text_inactive_color
//   colors.button_glyph   <- button_glyph_color
//   font                  <- font, truncated to sizeof(font) - 1, always
//                            NUL-terminated (snprintf, never strcpy)
//   border_*              <- border_width / border_active_color /
//                            border_inactive_color
void decoration_settings_from(const WindowDecorationsConfig &config,
			      DecorationSettings *settings);

#endif // DECORATION_SETTINGS_HPP
