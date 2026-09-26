// yarfwm -- the decoration pass and the decoration settings reader.
//
// This is the only place that reads window-decorations.json, and the only
// place that turns the window state into decoration requests. It is a separate
// file for two reasons: View.cpp is already near the size budget, and keeping
// the cairo-facing settings in one translation unit means View.hpp never has
// to include the renderer.
//
// Sequence discipline (v5 is what river 0.4.8 enforces):
//   set_borders      render-only
//   set_offset       render-only
//   use_ssd          manage-only   (sent from window_manager_manage_start)
//   get_decoration_* neither       (done here, in the render pass)
// so apply_decorations() runs at the END of the render sequence, after
// place_windows() has written the geometry it needs.
#include "Config.hpp"
#include "Decoration.hpp"
#include "DecorationRenderer.hpp"
#include "Display.hpp"
#include "Seat.hpp"
#include "View.hpp"
#include "WindowDecorationsConfig.hpp"
#include "river-window-management-v1-client-protocol.h"

#include <cstdio>
#include <cstring>

void decoration_settings_from(const WindowDecorationsConfig &config,
			      DecorationSettings *settings)
{
	std::memset(settings, 0, sizeof(*settings));

	// metrics.width stays 0: it is the one per-window quantity, and
	// Decoration::update() sets it from the content width.
	settings->metrics.width = 0;
	settings->metrics.height = config.titlebar_height;
	settings->metrics.button_width = config.button_width;
	// labwc's window buttons are square (labwc src/theme.c:558-559).
	settings->metrics.button_height = config.button_width;
	settings->metrics.button_spacing = config.button_spacing;
	settings->metrics.padding = config.padding;

	settings->colors.background_active = config.titlebar_active_color;
	settings->colors.background_inactive = config.titlebar_inactive_color;
	settings->colors.text_active = config.text_active_color;
	settings->colors.text_inactive = config.text_inactive_color;
	settings->colors.button_glyph = config.button_glyph_color;

	// snprintf, never strcpy: the config string is user input and the
	// buffer is fixed.
	std::snprintf(settings->font, sizeof(settings->font), "%s",
		      config.font.c_str());

	settings->titlebar_when_maximized = config.titlebar_when_maximized;
	settings->border_width = config.border_width;
	settings->border_active_color = config.border_active_color;
	settings->border_inactive_color = config.border_inactive_color;
}

void View::content_area(int32_t *x, int32_t *y, int32_t *width,
			int32_t *height) const
{
	// This lives here rather than in View.cpp for two reasons: it is the
	// one place that turns the decoration settings into window placement
	// policy, and View.cpp is at the project's line budget
	// (INSTRUCTIONS.md:44).
	Rectangle area{0, 0, 0, 0};
	placement_area(&area.x, &area.y, &area.width, &area.height);

	// With decorations off there is no bar, so the whole area is usable.
	// With them on the reserved strip is the whole frame top -- the bar
	// plus the border rows above the content -- so that a window placed at
	// the reserved origin has room for both and the bar does not sit on the
	// border. This is labwc's margin: titlebar_height + border_width
	// (labwc src/ssd/ssd.c:74-79).
	int32_t strip = 0;
	if (decorations_on) {
		strip = decoration_settings.metrics.height +
			decoration_settings.border_width;
		if (strip < 0) {
			strip = 0;
		}
	}

	Rectangle content{0, 0, 0, 0};
	reserve_titlebar_strip(area, strip, &content);

	*x = content.x;
	*y = content.y;
	*width = content.width;
	*height = content.height;
}

void View::apply_decorations(struct river_seat_v1 *river_seat)
{
	for (int i = 0; i < window_count; i++) {
		Window *window_entry = &windows[i];
		if (!window_entry->window || !window_entry->node) {
			continue;
		}
		// A window that has never been placed or sized has no geometry
		// to hang a titlebar on; the next render sequence will have
		// both.
		if (!window_entry->placed || window_entry->width <= 0 ||
		    window_entry->height <= 0) {
			continue;
		}

		const bool focused =
		    river_seat && seat &&
		    seat->focused_window(river_seat) == window_entry->window;

		// The compositor-drawn border: the focus indication, and it
		// costs no renderer at all. A fullscreen window gets none (the
		// XML says borders are not drawn while fullscreen, but sending
		// none makes the state explicit and idempotent). A MAXIMIZED
		// window keeps its border, unlike labwc, which disables its own
		// SSD border tree when maximized (src/ssd/ssd-border.c:58-60):
		// river draws this one, it only hides it for fullscreen
		// (river/Window.zig:945-961), and the focus indication is worth
		// keeping. The bar is offset to clear the border's rows, so the
		// two never fight over the same pixels.
		if (decorations_on && !window_entry->fullscreen) {
			uint32_t border_r = 0;
			uint32_t border_g = 0;
			uint32_t border_b = 0;
			uint32_t border_a = 0;
			border_color_components(
			    focused ? decoration_settings.border_active_color
				    : decoration_settings.border_inactive_color,
			    &border_r, &border_g, &border_b, &border_a);
			river_window_v1_set_borders(
			    window_entry->window,
			    RIVER_WINDOW_V1_EDGES_TOP |
				RIVER_WINDOW_V1_EDGES_BOTTOM |
				RIVER_WINDOW_V1_EDGES_LEFT |
				RIVER_WINDOW_V1_EDGES_RIGHT,
			    decoration_settings.border_width, border_r,
			    border_g, border_b, border_a);
		} else {
			river_window_v1_set_borders(window_entry->window,
						    RIVER_WINDOW_V1_EDGES_NONE,
						    0, 0, 0, 0, 0);
		}

		// The titlebar. It is skipped for a CSD-only client, where
		// river's use_ssd is a documented no-op: drawing a second
		// titlebar over the client's own would be a bug, not a feature.
		if (!decorations_on || window_entry->fullscreen ||
		    window_entry->decoration_hint ==
			RIVER_WINDOW_V1_DECORATION_HINT_ONLY_SUPPORTS_CSD) {
			continue;
		}

		if (!window_entry->decoration) {
			// Dynamic memory: one Decoration per window, owned by
			// the Window entry. It is destroyed in remove_window()
			// and terminate().
			window_entry->decoration = new Decoration();
			if (!window_entry->decoration->create(
				display ? display->compositor : nullptr,
				display ? display->shared_memory : nullptr,
				window_entry->window, decoration_settings)) {
				std::fprintf(stderr,
					     "Yarfwm: could not create a "
					     "decoration for a window\n");
				delete window_entry->decoration;
				window_entry->decoration = nullptr;
				continue;
			}
		}

		const Rectangle content{window_entry->x, window_entry->y,
					window_entry->width,
					window_entry->height};
		// The area the window was fitted into. The titlebar has to land
		// outside the content but inside this area, so both the paint
		// decision and the offset need it.
		Rectangle area{0, 0, 0, 0};
		placement_area(&area.x, &area.y, &area.width, &area.height);
		// Only per-window state is passed: the colours and font were
		// fixed at create() time from decoration_settings.
		window_entry->decoration->update(
		    content, area, window_entry->title, focused,
		    window_entry->maximized, window_entry->fullscreen);
		// set_offset is render-sequence-only under v5: send it every
		// sequence so a window that moved is never left with a titlebar
		// at the old offset. A bar with nowhere legal to sit comes back
		// as titlebar_placement_hidden and has no buffer attached, so
		// it covers nothing.
		window_entry->decoration->apply_offset(area);
	}
}

void View::destroy_decoration(Window *window_entry)
{
	if (!window_entry->decoration) {
		return;
	}
	window_entry->decoration->destroy();
	delete window_entry->decoration;
	window_entry->decoration = nullptr;
}

void View::set_decorations_enabled(bool enabled)
{
	if (decorations_on == enabled) {
		return;
	}
	decorations_on = enabled;

	if (!enabled) {
		// Destroy every decoration now. The render pass recreates them
		// lazily when decorations come back on.
		for (int i = 0; i < window_count; i++) {
			destroy_decoration(&windows[i]);
		}
	}
	// The borders and the titlebars are (re)applied by the render pass, so
	// all this needs is a manage sequence to get there.
	request_manage();
}

struct river_window_v1 *
View::window_for_decoration_surface(struct wl_surface *surface) const
{
	if (!surface) {
		return nullptr;
	}
	for (int i = 0; i < window_count; i++) {
		if (windows[i].decoration &&
		    windows[i].decoration->surface() == surface) {
			return windows[i].window;
		}
	}
	return nullptr;
}

TitlebarPart View::decoration_part_at(struct river_window_v1 *window, int32_t x,
				      int32_t y) const
{
	const Window *window_entry = find_window(window);
	if (!window_entry || !window_entry->decoration) {
		return titlebar_part_none;
	}
	return window_entry->decoration->part_at(TitlebarPoint{x, y});
}
