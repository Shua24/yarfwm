#ifndef LAYER_SHELL_HPP
#define LAYER_SHELL_HPP

#include <cstdint>

struct wl_registry;
struct river_layer_shell_v1;
struct river_layer_shell_output_v1;
struct river_layer_shell_seat_v1;
struct river_output_v1;
struct river_seat_v1;

// river_layer_shell_v1 support: wallpaper, bar and launcher clients (swaybg,
// wbg, waybar) can only map layer surfaces while the window manager holds this
// global. Binding it is the gate; river closes every layer surface of a window
// manager that does not bind it.
//
// This object owns the global proxy and creates the per-object state handles:
// one river_layer_shell_output_v1 per output (non-exclusive-area hint and
// set_default) and one river_layer_shell_seat_v1 per seat (layer surface
// keyboard focus). The callers that own those outputs and seats destroy them.
class LayerShell {
public:
    LayerShell();
    ~LayerShell();

    // Called from the registry handler when river advertises the global.
    void bind_registry_global(struct wl_registry *registry, uint32_t name, uint32_t version);

    bool available() const;

    // Dynamic memory: each call returns a newly created protocol object; the
    // caller owns it and destroys it with the matching _destroy request.
    struct river_layer_shell_output_v1 *create_output_state(struct river_output_v1 *output);
    struct river_layer_shell_seat_v1 *create_seat_state(struct river_seat_v1 *seat);

    void terminate();

    struct river_layer_shell_v1 *layer_shell;
};

#endif // LAYER_SHELL_HPP
