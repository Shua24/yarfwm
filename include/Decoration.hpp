#ifndef DECORATION_HPP
#define DECORATION_HPP

#include "DecorationGeometry.hpp"
#include "DecorationSettings.hpp"
#include "Placement.hpp"
#include "ShmBuffer.hpp"

struct wl_surface;
struct wl_compositor;
struct wl_shm;
struct river_window_v1;
struct river_decoration_v1;

// The window-manager-drawn decoration for one window: a titlebar surface above
// the window content, plus the metrics the renderer needs.
//
// Lifetime: created on the render sequence that first sees a placed, mapped
// window; destroyed when the window goes away or when decorations are turned
// off. It owns its wl_surface, its river_decoration_v1 and its ShmBuffer.
//
// This object holds NO pointer into View::windows and no index into that
// array: View::remove_window() swap-deletes, so any index stored here would
// dangle. The only window reference kept is the river proxy, which the View
// destroys first.
class Decoration
{
      public:
	Decoration();
	~Decoration();

	// Create the surface and the river decoration object. get_decoration_*
	// is legal in either sequence class, so this runs where it is called
	// (the render sequence, which is where the placement is known).
	// Returns false on failure; the caller then leaves the window
	// undecorated rather than crashing.
	bool create(struct wl_compositor *compositor,
		    struct wl_shm *shared_memory,
		    struct river_window_v1 *window,
		    const DecorationSettings &settings);
	void destroy();

	// True once create() succeeded and destroy() has not run.
	bool is_created() const { return created; }

	// Record the window's content geometry and the area the window was
	// fitted into, and repaint if anything the titlebar shows actually
	// changed. The bar's offset clears the compositor's border rows, which
	// the settings carry. Returns true when the surface was repainted, so
	// the caller can count real work. The content geometry is what
	// river_window_v1.set_position/propose_dimensions describe: the
	// titlebar sits OUTSIDE it -- above it when the placement area leaves
	// room, below it otherwise -- and is never drawn over it.
	//
	// The placement area is a parameter because the paint decision depends
	// on it: the bar only fits above the content when the area has room
	// above the content's top edge, and a bar with nowhere legal to sit is
	// not painted at all (TitlebarPlacement::titlebar_placement_hidden).
	//
	// The metrics, colours and font are NOT parameters: they were fixed at
	// create() time and live in `settings`. Only per-window state is passed
	// here, so the caller cannot accidentally hand one window another's
	// colours. The one per-window quantity is the content WIDTH, which is
	// taken from content_geometry.width.
	bool update(const Rectangle &content_geometry,
		    const Rectangle &placement_area, const char *title,
		    bool active, bool maximized, bool fullscreen);

	// Set the decoration's offset for this render sequence. set_offset is
	// render-sequence-only under v5, so the caller invokes this from the
	// render pass, every sequence (river stores the last value, so a repeat
	// is harmless and a missed one would misplace the bar).
	//
	// Returns where the bar ended up. titlebar_placement_hidden means it is
	// not painted at all -- there was nowhere outside the content for it to
	// go -- and no buffer is attached, so the decoration covers nothing.
	TitlebarPlacement apply_offset(const Rectangle &placement_area);

	// Hit test a pointer position in surface-local coordinates.
	TitlebarPart part_at(TitlebarPoint point) const;

	// Whether the titlebar is currently drawn (not fullscreen, decorations
	// are on, and it found somewhere legal to sit). False means no buffer
	// is attached to the decoration surface, so nothing of the window is
	// covered by it.
	bool is_visible() const
	{
		return visible && placement != titlebar_placement_hidden;
	}

	struct wl_surface *surface() const { return titlebar_surface; }
	struct river_decoration_v1 *river_decoration() const
	{
		return decoration;
	}

      private:
	// Repaint the buffer from the current state. Called by update() only
	// when something changed.
	void repaint();

	struct wl_surface *titlebar_surface;
	struct river_decoration_v1 *decoration;
	struct wl_shm *shared_memory;
	ShmBuffer buffer;

	Rectangle content_geometry;
	// The area the window was fitted into. Kept because the paint decision
	// and apply_offset() both need to know whether the bar has room above
	// the content inside this area.
	Rectangle placement_area;
	// Where titlebar_offset() last put the bar. titlebar_placement_hidden
	// means no buffer is attached and the window is fully uncovered.
	TitlebarPlacement placement;
	// A buffer is currently attached to the titlebar surface. Tracked so
	// the detach on losing the room to paint happens once, not every
	// sequence.
	bool painted;
	// The settings as resolved at create() time. `metrics` is the live copy
	// the renderer reads; it is seeded from `settings` and only its width
	// ever changes, because that is the one per-window quantity.
	DecorationSettings settings;
	TitlebarMetrics metrics;
	TitlebarColors colors;
	int visible_button_count;
	bool visible;
	bool active;
	bool maximized;
	bool fullscreen;
	// The title as last painted, so an unchanged title does not repaint.
	// 256 bytes: the same size View::Window::title uses, so the copy can
	// never truncate further.
	char painted_title[256];
	bool created;
};

#endif // DECORATION_HPP
