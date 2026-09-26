// yarfwm -- one window's server-side decoration: a titlebar surface above the
// window content.
//
// The lifecycle is the whole point of this file:
//
//   - create() is called from a render sequence, because that is where the
//     placement is known. It creates a wl_surface and hands it to river with
//     get_decoration_above, which assigns the surface its role.
//   - update() runs every render sequence but repaints ONLY when something the
//     titlebar shows changed. A per-sequence repaint would be a
//     frame-rate-dependent CPU burn for a bar that is almost always static.
//   - apply_offset() must run every render sequence: set_offset is
//     render-sequence-only under v5, river keeps the last value, and a missed
//     one leaves the titlebar at the previous window position.
//
// There is no listener on the decoration object. river_decoration_v1 has no
// events, and registering a listener with a NULL slot aborts the process --
// a trap this project has already been bitten by.
#include "Decoration.hpp"
#include "DecorationRenderer.hpp"
#include "river-window-management-v1-client-protocol.h"

#include <cstdio>
#include <cstring>
#include <wayland-client.h>

Decoration::Decoration()
    : titlebar_surface(nullptr), decoration(nullptr), shared_memory(nullptr),
      content_geometry{0, 0, 0, 0}, placement_area{0, 0, 0, 0},
      placement(titlebar_placement_hidden), painted(false),
      metrics{0, 0, 0, 0, 0, 0}, colors{0, 0, 0, 0, 0}, visible_button_count(0),
      visible(false), active(false), maximized(false), fullscreen(false),
      painted_title{}, created(false)
{
	std::memset(&settings, 0, sizeof(settings));
}

Decoration::~Decoration() { destroy(); }

bool Decoration::create(struct wl_compositor *compositor, struct wl_shm *shm,
			struct river_window_v1 *window,
			const DecorationSettings &new_settings)
{
	if (created) {
		return true;
	}
	if (!compositor || !shm || !window) {
		return false;
	}
	settings = new_settings;
	metrics = settings.metrics;
	colors = settings.colors;
	shared_memory = shm;
	titlebar_surface = wl_compositor_create_surface(compositor);
	if (!titlebar_surface) {
		shared_memory = nullptr;
		return false;
	}
	// A wl_surface has NO role until get_decoration_above assigns one;
	// river's XML calls giving an already-roled or already-committed
	// surface a protocol error, so nothing may be attached or committed
	// before this call.
	decoration =
	    river_window_v1_get_decoration_above(window, titlebar_surface);
	if (!decoration) {
		wl_surface_destroy(titlebar_surface);
		titlebar_surface = nullptr;
		shared_memory = nullptr;
		return false;
	}
	// A fresh wl_surface is fully input-opaque by default, which is what a
	// clickable titlebar wants. Do NOT call set_input_region here: an empty
	// input region kills both the wl_pointer events and river's
	// window_interaction for this area.
	visible = true;
	created = true;
	return true;
}

void Decoration::destroy()
{
	if (decoration) {
		river_decoration_v1_destroy(decoration);
		decoration = nullptr;
	}
	if (titlebar_surface) {
		wl_surface_destroy(titlebar_surface);
		titlebar_surface = nullptr;
	}
	buffer.destroy();
	shared_memory = nullptr;
	visible = false;
	visible_button_count = 0;
	painted = false;
	placement = titlebar_placement_hidden;
	created = false;
}

bool Decoration::update(const Rectangle &new_content_geometry,
			const Rectangle &new_placement_area, const char *title,
			bool new_active, bool new_maximized,
			bool new_fullscreen)
{
	if (!created) {
		return false;
	}
	const bool title_changed =
	    std::strncmp(painted_title, title ? title : "",
			 sizeof(painted_title)) != 0;
	const bool width_changed =
	    new_content_geometry.width != content_geometry.width;
	const bool state_changed = new_active != active ||
				   new_maximized != maximized ||
				   new_fullscreen != fullscreen;
	// Where the bar has room to sit is part of what the surface shows, so a
	// move that takes the room away has to repaint.
	const bool placement_changed =
	    new_content_geometry.x != content_geometry.x ||
	    new_content_geometry.y != content_geometry.y ||
	    new_content_geometry.height != content_geometry.height ||
	    new_placement_area.y != placement_area.y ||
	    new_placement_area.height != placement_area.height;
	// The colours and font come from `settings` and never change for the
	// life of the object: a settings change means a new Decoration, not an
	// update. Only the WIDTH is per-window.

	content_geometry = new_content_geometry;
	placement_area = new_placement_area;
	active = new_active;
	maximized = new_maximized;
	fullscreen = new_fullscreen;
	visible = decoration_titlebar_visible(maximized, fullscreen,
					      settings.titlebar_when_maximized);
	if (title) {
		std::snprintf(painted_title, sizeof(painted_title), "%s",
			      title);
	} else {
		painted_title[0] = '\0';
	}

	// A fullscreen window has no titlebar (labwc destroys the whole SSD on
	// fullscreen, src/view.c:1670-1682); everything else keeps it, except
	// a maximized window when the config asks for it to be hidden.
	if (!visible || new_content_geometry.width <= 0) {
		visible_button_count = 0;
		// A bar that was painted and is now not wanted has to be taken
		// off the surface. view.c:1670-1682 destroys the whole SSD when
		// a view goes fullscreen; simply not repainting would leave the
		// old buffer attached and the bar on screen over a fullscreen
		// window, which is the same class of bug as covering content.
		if (painted) {
			buffer.detach(titlebar_surface);
			painted = false;
			placement = titlebar_placement_hidden;
			return true;
		}
		return false;
	}

	// The bar spans the content width. This is the one per-window quantity
	// the layout depends on, so it is folded in before anything is laid
	// out.
	metrics.width = new_content_geometry.width;
	visible_button_count = titlebar_visible_button_count(metrics);

	// The paint decision is placement-dependent: a bar with nowhere to sit
	// is not painted at all, so the window underneath stays whole. The
	// border width is part of it because river draws the border outside the
	// content and the bar must clear those rows.
	int32_t offset_x = 0;
	int32_t offset_y = 0;
	// A MAXIMIZED window is the one caller allowed to keep its bar by
	// covering the content's top rows: it fills the whole placement area,
	// so there is nowhere outside it for the bar to go. Everything else
	// keeps the strict no-cover rule.
	const TitlebarPlacement new_placement = titlebar_offset(
	    metrics.height, settings.border_width, content_geometry,
	    placement_area, maximized, &offset_x, &offset_y);
	const bool placement_moved = new_placement != placement;
	placement = new_placement;
	if (placement == titlebar_placement_hidden) {
		// Nothing to draw. Drop any buffer that is still attached so a
		// stale titlebar cannot keep covering the window after the bar
		// lost its room.
		if (painted) {
			buffer.detach(titlebar_surface);
			painted = false;
			return true;
		}
		return placement_moved;
	}

	if (!title_changed && !width_changed && !state_changed &&
	    !placement_changed && !placement_moved &&
	    buffer.width() == metrics.width &&
	    buffer.height() == metrics.height) {
		return false;
	}
	repaint();
	return true;
}

void Decoration::repaint()
{
	if (!created || !shared_memory || metrics.width <= 0 ||
	    metrics.height <= 0) {
		return;
	}
	// A hidden bar has no buffer by construction; never paint one, or the
	// surface would start covering the window again.
	if (placement == titlebar_placement_hidden) {
		return;
	}
	// The buffer is content-width x titlebar-height, and create() resizes
	// it when the width changes. If the buffer cannot be had, an
	// undecorated window is a better outcome than a dead window manager.
	if (!buffer.create(shared_memory, metrics.width, metrics.height)) {
		visible = false;
		std::fprintf(stderr,
			     "Yarfwm: could not allocate a titlebar buffer\n");
		return;
	}
	render_titlebar(buffer.pixels(), metrics.width, metrics.height,
			buffer.stride(), metrics, colors, painted_title, active,
			visible_button_count, settings.font);
	buffer.attach_and_commit(titlebar_surface);
	painted = true;
}

TitlebarPlacement Decoration::apply_offset(const Rectangle &area)
{
	if (!created || !decoration) {
		return titlebar_placement_hidden;
	}
	int32_t offset_x = 0;
	int32_t offset_y = 0;
	// The offset has to be recomputed here rather than read back from
	// update(): set_offset is render-sequence-only under v5, so it is sent
	// from this pass every sequence, and river keeps the last value it saw.
	// Leaving a stale offset behind after the bar lost its room would put
	// the surface back over the window.
	//
	// `maximized` is passed so the two passes agree: a maximized window
	// keeps its bar by overlapping the content's top rows (see
	// titlebar_offset's allow_overlap), and both the paint decision and the
	// offset have to make the same choice.
	const TitlebarPlacement where = titlebar_offset(
	    metrics.height, settings.border_width, content_geometry, area,
	    maximized, &offset_x, &offset_y);
	placement = where;
	river_decoration_v1_set_offset(decoration, offset_x, offset_y);
	return where;
}

TitlebarPart Decoration::part_at(TitlebarPoint point) const
{
	// is_visible(), not the raw flag: a bar that lost its room is not
	// painted, so a press there belongs to the window underneath, not to
	// us.
	if (!created || !is_visible()) {
		return titlebar_part_none;
	}
	return titlebar_part_at(metrics, visible_button_count, point);
}
