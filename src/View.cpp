#include "View.hpp"
#include "Display.hpp"
#include "Seat.hpp"
#include "Output.hpp"
#include "Config.hpp"
#include "river-window-management-v1-client-protocol.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <new>

// Window management policy and render state.
//
// The river manage/render handshake (river-window-management-v1; v5 rules are
// what river 0.4.8 enforces):
//
//   manage_start -> (manage-only requests) -> manage_finish     [mandatory]
//   render_start -> (render-only requests) -> render_finish     [mandatory]
//
// Request classification used here:
//   manage-only      propose_dimensions, set_capabilities, close, use_csd/use_ssd
//   render-only      show, set_borders, set_position, place_*
//   either           manage_finish, render_finish
//
// Layer shell (river_layer_shell_v1) state is created per output and per seat
// when river hands us those objects; the per-output non-exclusive area is the
// placement hint for windows and set_default (manage sequence only) points new
// layer surfaces at an output.
//
// The listener trampolines live in ViewEvents.cpp; the two sequence events
// carry the policy and are implemented below.

View::View()
    : seat(nullptr),
      display(nullptr),
      manager(nullptr),
      windows(nullptr),
      window_count(0),
      window_capacity(0),
      outputs(nullptr),
      output_count(0),
      output_capacity(0),
      default_layer_output(nullptr),
      pending_manage_count(0),
      pending_render_count(0),
      shutdown_requested(false) {
}

View::~View() {
    terminate();
}

bool View::initialize(Server *server, Seat *seat, Display *display, Config &config) {
    // Dynamic memory: window and output tracking arrays grow on demand.
    (void)server;
    (void)config;
    this->seat = seat;
    this->display = display;

    if (!display->window_manager) {
        std::fprintf(stderr, "Yarfwm: no river_window_manager_v1 available\n");
        return false;
    }

    manager = display->window_manager;

    static const struct river_window_manager_v1_listener manager_listener = {
        window_manager_unavailable,
        window_manager_finished,
        window_manager_manage_start,
        window_manager_render_start,
        window_manager_session_locked,
        window_manager_session_unlocked,
        window_manager_window,
        window_manager_output,
        window_manager_seat,
    };

    if (river_window_manager_v1_add_listener(manager, &manager_listener, this) != 0) {
        std::fprintf(stderr, "Yarfwm: failed to add window manager listener\n");
        return false;
    }

    return true;
}

void View::terminate() {
    // Dynamic memory: free tracked windows and outputs.
    for (int i = 0; i < window_count; i++) {
        Window *window_entry = &windows[i];
        if (window_entry->node) {
            river_node_v1_destroy(window_entry->node);
            window_entry->node = nullptr;
        }
        if (window_entry->window) {
            river_window_v1_destroy(window_entry->window);
            window_entry->window = nullptr;
        }
    }

    if (windows) {
        std::free(windows);
        windows = nullptr;
    }

    // Output objects are heap-allocated and own their protocol objects; delete
    // runs the Output destructor, which destroys the layer shell output state
    // and the river_output_v1 proxy.
    for (int i = 0; i < output_count; i++) {
        delete outputs[i];
        outputs[i] = nullptr;
    }

    if (outputs) {
        std::free(outputs);
        outputs = nullptr;
    }

    window_count = 0;
    window_capacity = 0;
    output_count = 0;
    output_capacity = 0;
    default_layer_output = nullptr;
    pending_manage_count = 0;
    pending_render_count = 0;
    manager = nullptr;
}

bool View::should_shutdown() const {
    return shutdown_requested;
}

View::Window *View::find_window(struct river_window_v1 *window) const {
    for (int i = 0; i < window_count; i++) {
        if (windows[i].window == window) {
            return &windows[i];
        }
    }
    return nullptr;
}

void View::add_window(struct river_window_v1 *window) {
    // Dynamic memory: grow the window array when needed.
    if (window_count >= window_capacity) {
        int new_capacity = window_capacity == 0 ? 8 : window_capacity * 2;
        Window *new_windows = static_cast<Window *>(
            std::realloc(windows, sizeof(Window) * static_cast<size_t>(new_capacity)));
        if (!new_windows) {
            std::fprintf(stderr, "Yarfwm: failed to allocate window storage\n");
            return;
        }
        windows = new_windows;
        window_capacity = new_capacity;
    }

    Window *window_entry = &windows[window_count];
    std::memset(window_entry, 0, sizeof(*window_entry));
    window_entry->window = window;

    // Single get_node call: the returned proxy is owned by this window entry.
    window_entry->node = river_window_v1_get_node(window);
    if (window_entry->node == nullptr) {
        std::fprintf(stderr, "Yarfwm: failed to get window node\n");
        return;
    }

    static const struct river_window_v1_listener window_listener = []{
        struct river_window_v1_listener listener{};
        listener.closed = window_closed;
        listener.dimensions_hint = window_dimensions_hint;
        listener.dimensions = window_dimensions;
        listener.app_id = window_app_id;
        listener.title = window_title;
        listener.parent = window_parent;
        listener.decoration_hint = window_decoration_hint;
        listener.pointer_move_requested = window_pointer_move_requested;
        listener.pointer_resize_requested = window_pointer_resize_requested;
        listener.show_window_menu_requested = window_show_window_menu_requested;
        listener.maximize_requested = window_maximize_requested;
        listener.unmaximize_requested = window_unmaximize_requested;
        listener.fullscreen_requested = window_fullscreen_requested;
        listener.exit_fullscreen_requested = window_exit_fullscreen_requested;
        listener.minimize_requested = window_minimize_requested;
        listener.unreliable_pid = window_unreliable_pid;
        listener.presentation_hint = window_presentation_hint;
        listener.identifier = window_identifier;
        listener.capture_sessions = window_capture_sessions;
        listener.touch_move_requested = window_touch_move_requested;
        listener.touch_resize_requested = window_touch_resize_requested;
        return listener;
    }();

    if (river_window_v1_add_listener(window, &window_listener, this) != 0) {
        std::fprintf(stderr, "Yarfwm: failed to add window listener\n");
    }

    // Count the window only once it is fully registered.
    window_count++;
}

void View::remove_window(struct river_window_v1 *window) {
    for (int i = 0; i < window_count; i++) {
        if (windows[i].window == window) {
            if (windows[i].node) {
                river_node_v1_destroy(windows[i].node);
                windows[i].node = nullptr;
            }
            river_window_v1_destroy(windows[i].window);
            windows[i].window = nullptr;

            if (i < window_count - 1) {
                windows[i] = windows[window_count - 1];
            }
            window_count--;
            return;
        }
    }
}

void View::add_output(struct river_output_v1 *output) {
    // Dynamic memory: grow the output pointer array when needed.
    if (output_count >= output_capacity) {
        int new_capacity = output_capacity == 0 ? 4 : output_capacity * 2;
        Output **new_outputs = static_cast<Output **>(
            std::realloc(outputs, sizeof(Output *) * static_cast<size_t>(new_capacity)));
        if (!new_outputs) {
            std::fprintf(stderr, "Yarfwm: failed to allocate output storage\n");
            return;
        }
        outputs = new_outputs;
        output_capacity = new_capacity;
    }

    // Dynamic memory: one Output object per river output, freed in
    // remove_output() or terminate().
    Output *output_entry = new (std::nothrow) Output();
    if (!output_entry) {
        std::fprintf(stderr, "Yarfwm: failed to allocate output entry\n");
        return;
    }

    if (!output_entry->initialize(display->layer_shell_state, output, this)) {
        delete output_entry;
        return;
    }

    outputs[output_count++] = output_entry;
}

void View::remove_output(Output *output_entry) {
    for (int i = 0; i < output_count; i++) {
        if (outputs[i] == output_entry) {
            if (default_layer_output == output_entry) {
                default_layer_output = nullptr;
            }
            // Dynamic memory: destroys the protocol objects the entry owns and
            // frees the entry itself.
            output_entry->terminate();
            delete output_entry;
            if (i < output_count - 1) {
                outputs[i] = outputs[output_count - 1];
            }
            output_count--;
            return;
        }
    }
}

void View::placement_area(int32_t *x, int32_t *y, int32_t *width, int32_t *height) const {
    // Prefer the first output's non-exclusive area: the rectangle left after
    // subtracting the exclusive zones of layer surfaces such as bars. Fall
    // back to the plain output rectangle, then to a conservative default until
    // the compositor tells us more.
    for (int i = 0; i < output_count; i++) {
        Output *output_entry = outputs[i];
        if (!output_entry) {
            continue;
        }

        if (output_entry->has_non_exclusive_area &&
            output_entry->non_exclusive_area_width > 0 &&
            output_entry->non_exclusive_area_height > 0) {
            *x = output_entry->non_exclusive_area_x;
            *y = output_entry->non_exclusive_area_y;
            *width = output_entry->non_exclusive_area_width;
            *height = output_entry->non_exclusive_area_height;
            return;
        }

        if (output_entry->has_dimensions && output_entry->width > 0 &&
            output_entry->height > 0) {
            *x = output_entry->x;
            *y = output_entry->y;
            *width = output_entry->width;
            *height = output_entry->height;
            return;
        }
    }

    *x = 0;
    *y = 0;
    *width = 800;
    *height = 600;
}

void View::propose_default_dimensions(Window *window) const {
    if (!window || !window->window) {
        return;
    }

    int32_t area_x = 0;
    int32_t area_y = 0;
    int32_t area_width = 0;
    int32_t area_height = 0;
    placement_area(&area_x, &area_y, &area_width, &area_height);
    (void)area_x;
    (void)area_y;

    // A sane floating default: half the output in each direction, leaving room
    // to move the window around. The window may pick different dimensions.
    river_window_v1_propose_dimensions(window->window, area_width / 2, area_height / 2);
    river_window_v1_set_capabilities(window->window,
                                     RIVER_WINDOW_V1_CAPABILITIES_WINDOW_MENU |
                                     RIVER_WINDOW_V1_CAPABILITIES_MAXIMIZE |
                                     RIVER_WINDOW_V1_CAPABILITIES_FULLSCREEN |
                                     RIVER_WINDOW_V1_CAPABILITIES_MINIMIZE);
}

void View::place_windows() {
    int32_t area_x = 0;
    int32_t area_y = 0;
    int32_t area_width = 0;
    int32_t area_height = 0;
    placement_area(&area_x, &area_y, &area_width, &area_height);

    int x = area_x;
    int y = area_y;

    for (int i = 0; i < window_count; i++) {
        Window *window_entry = &windows[i];
        if (!window_entry->window || !window_entry->node) {
            continue;
        }

        // Cascade floating placement, wrapping inside the placement area.
        river_node_v1_set_position(window_entry->node, x, y);
        window_entry->placed = true;

        x += 32;
        y += 32;
        if (x > area_x + area_width - 64) {
            x = area_x;
        }
        if (y > area_y + area_height - 64) {
            y = area_y;
        }
    }
}

void View::window_manager_manage_start(void *data, struct river_window_manager_v1 *manager) {
    View *view = static_cast<View *>(data);
    view->pending_manage_count = view->window_count;

    // Manage-only requests: propose dimensions and capabilities for windows we
    // have not managed yet. show() and set_position() are render-only in v5 and
    // are deferred to the render sequence.
    for (int i = 0; i < view->window_count; i++) {
        Window *window_entry = &view->windows[i];
        if (!window_entry->window) {
            continue;
        }
        if (!window_entry->managed) {
            view->propose_default_dimensions(window_entry);
            window_entry->managed = true;
        }
    }

    // Point layer shell at an output once, so layer surfaces that do not name
    // an output land somewhere sensible. Manage sequence only.
    if (!view->default_layer_output) {
        for (int i = 0; i < view->output_count; i++) {
            if (view->outputs[i] && view->outputs[i]->set_default()) {
                view->default_layer_output = view->outputs[i];
                break;
            }
        }
    }

    // Mandatory: end the manage sequence.
    river_window_manager_v1_manage_finish(manager);
}

void View::window_manager_render_start(void *data, struct river_window_manager_v1 *manager) {
    View *view = static_cast<View *>(data);
    view->pending_render_count = view->window_count;

    // Render-only requests: make sure every window is shown and positioned.
    for (int i = 0; i < view->window_count; i++) {
        Window *window_entry = &view->windows[i];
        if (!window_entry->window) {
            continue;
        }
        if (!window_entry->shown) {
            river_window_v1_show(window_entry->window);
            window_entry->shown = true;
        }
    }

    view->place_windows();

    // Mandatory: end the render sequence.
    river_window_manager_v1_render_finish(manager);
}
